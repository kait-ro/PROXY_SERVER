# The Proxy Server — Every Detail Explained

This document walks through every file in the proxy server, explaining each concept as if you have never seen it before. It is intentionally long. Read it like a story.

---

# Part 0 — Things You Need to Know First

Before touching a single line of code, some foundational ideas.

## What is a string in C++?

A C++ `string` is a sequence of characters stored in memory. Think of it as a row of boxes, where each box holds one letter. The string `"Hello"` is five boxes: `H`, `e`, `l`, `l`, `o`. Each box has a position number starting from 0.

```
Position:  0   1   2   3   4
Content:   H   e   l   l   o
```

## What is `string::npos`?

`npos` means "no position". It is a special value that `string::find()` returns when it cannot find what you asked for.

`find()` scans through the string looking for a piece of text. If it finds it, it returns the position (a number like 0, 7, 42). If it does NOT find it, it returns `string::npos`.

`string::npos` is technically the number `18446744073709551615` — the largest possible number for a `size_t`. It was chosen because no real position in a string would ever be that large. It serves as a "not found" sentinel value.

So whenever you see:
```cpp
if (pos == string::npos)
```
That simply means: "if the thing was not found."

And:
```cpp
if (pos != string::npos)
```
Means: "if the thing WAS found."

## What is `size_t`?

`size_t` is a data type for sizes and positions. It is always a non-negative whole number (called "unsigned"). It is used for anything that represents a size, index, or position — because those things can never be negative. `string::find()` returns a `size_t`. The size of a string (`.size()`) returns a `size_t`.

## What is a socket?

When two programs want to talk to each other over a network, they each open a "socket". Think of a socket like a telephone handset — it is the endpoint of a communication channel. You get a socket by calling `socket()`, which returns a small integer (like 3, 4, 5) called a **file descriptor**. Every subsequent operation (send data, receive data, close) uses that integer to identify which socket you mean.

## What is a port?

A computer can run many network programs simultaneously. A **port** is a number (1–65535) that distinguishes them. When you connect to a web server, you connect to port 80 (for HTTP) or 443 (for HTTPS). The proxy server listens on port 8080. Port numbers are like apartment numbers in a building — the IP address is the building, the port is the specific apartment.

## What is a thread?

A thread is an independent path of execution inside a program. A program with one thread does one thing at a time. A program with 20 threads can do 20 things simultaneously. Each thread has its own local variables but shares the same memory as the other threads. This sharing is powerful (they can communicate) but dangerous (they can corrupt each other's data if not careful).

## What is a mutex?

A mutex (mutual exclusion) is a lock. Only one thread can hold it at a time. When thread A locks a mutex and thread B also tries to lock the same mutex, thread B is forced to wait (sleep) until thread A unlocks it. This prevents two threads from modifying the same data at the same time, which would cause corruption.

## What does `\r\n` mean?

In network protocols, lines end with `\r\n` — two special characters:
- `\r` is "carriage return" (ASCII 13)
- `\n` is "newline" (ASCII 10)

This combination is called CRLF. HTTP requires it. So every HTTP header line ends with `\r\n`, and the blank line that separates headers from the body is `\r\n\r\n` (two CRLFs in a row).

---

# Part 1 — The Entry Point: `main.cpp`

This is the very first code that runs when you type `./proxy`.

```cpp
#include <iostream>
#include "proxyserver.h"
#include "Authenticator.h"
using namespace std;
```

`#include` is a preprocessor directive. Before the compiler runs, a separate program called the "preprocessor" reads your source file. When it encounters `#include`, it literally copies and pastes the contents of that file at that position.

`<iostream>` is a standard library header that gives you `cout` (print to screen) and `cin` (read from keyboard).

`"proxyserver.h"` and `"Authenticator.h"` are your own header files — notice the double quotes instead of angle brackets. Angle brackets tell the compiler to look in system folders. Double quotes tell it to look in the current project folder first.

`using namespace std;` — everything in the C++ standard library lives in a "namespace" called `std`. Without this line, you would have to write `std::cout`, `std::string`, `std::cin` everywhere. This line lets you drop the `std::` prefix.

```cpp
int main()
{
```

`main` is the mandatory entry point. Every C++ program must have exactly one function named `main`. When you run the program, the OS calls this function. `int` means it returns an integer — by convention, returning 0 means "success", anything else means error.

```cpp
    Authenticator auth;
    auth.loadUsers("users.txt");
```

This creates an `Authenticator` object called `auth`. An object is an instance of a class — the class is the blueprint, the object is the actual thing. `loadUsers("users.txt")` reads the user database from a file on disk into memory.

```cpp
    string choice;
    cout << "1. Login\n2. Signup\nChoice: ";
    cin >> choice;
```

`cout <<` sends text to the terminal. The `<<` operator is called the "stream insertion operator" — think of it as "push this into the output stream." `\n` is the newline character (just starts a new line on screen). `cin >>` reads one word from the keyboard into `choice`.

```cpp
    if (choice == "2")
    {
        cout << "Enter username: ";
        cin >> username;
        cout << "Enter password: ";
        cin >> password;
        cout << "Enter role (admin/user): ";
        cin >> role;
        auth.signup(username, password, role);
        cout << "Signup successful!\n";
    }
```

If the user chose option 2, they are creating a new account. The program asks for a username, password, and role. `auth.signup(...)` hashes the password and saves the new user to `users.txt`.

```cpp
    while (true)
    {
        cout << "\nLogin\nUsername: ";
        cin >> username;
        cout << "Password: ";
        cin >> password;
        role = auth.login(username, password);
        if (role != "")
        {
            cout << "Login successful as " << role << endl;
            break;
        }
        else
        {
            cout << "Invalid credentials, try again.\n";
        }
    }
```

`while (true)` is an infinite loop — it runs forever until something inside calls `break`. Each iteration asks for credentials. `auth.login()` returns the user's role if credentials are correct, or an empty string `""` if wrong. The loop keeps asking until login succeeds.

`endl` flushes the output buffer and adds a newline. `"\n"` just adds the newline character without flushing — `endl` is slightly slower but ensures the text appears immediately.

```cpp
    ProxyServer proxy(8080);
    proxy.setUser(username, role);
    proxy.startServer();
    return 0;
}
```

Creates the proxy server object bound to port 8080, tells it which user is currently logged in (for logging purposes), and starts it. `startServer()` never returns — it contains an infinite loop accepting connections. So `return 0` at the bottom is technically unreachable but good practice to keep.

---

# Part 2 — The Authenticator: `Authenticator.cpp`

```cpp
#include <openssl/sha.h>
#include <iomanip>
#include <sstream>
```

`openssl/sha.h` provides the `SHA256()` function — a cryptographic hash function. `iomanip` provides `setw` and `setfill` for formatting numbers. `sstream` provides `stringstream` — a string you can write to like a stream.

## Password Hashing

```cpp
string hashPassword(const string& password)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)password.c_str(), password.size(), hash);
```

`unsigned char hash[SHA256_DIGEST_LENGTH]` — This declares an array of 32 bytes (SHA256 always produces exactly 32 bytes). `unsigned char` means a byte that can hold values 0–255 (regular `char` can hold -128 to 127).

`SHA256(...)` — This function takes three things: a pointer to the input data, the length of that data, and an output buffer. It computes the hash and writes 32 bytes into `hash`.

`password.c_str()` — The `string` class is a C++ class. But `SHA256` was written in C and expects a raw pointer to a character array (called a "C string"). `.c_str()` returns a pointer to the underlying character data of the string, so C functions can use it.

The `(unsigned char*)` before it is a **cast** — it forces the compiler to treat the pointer as pointing to unsigned characters instead of signed characters. SHA256 wants unsigned bytes.

```cpp
    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }
    return ss.str();
}
```

The raw SHA256 output is 32 bytes of binary data — not printable text. This loop converts each byte to its two-character hexadecimal representation.

- `hex` tells the stream to output numbers in base-16 (hexadecimal) instead of base-10
- `setw(2)` says "each number should take up at least 2 characters of width"
- `setfill('0')` says "if a number is only 1 character wide, pad it on the left with '0'"
- `(int)hash[i]` casts the byte to an integer so it prints as a number, not as a character

So byte value 255 becomes `"ff"`, byte value 10 becomes `"0a"`, etc. After the loop, `ss.str()` extracts the accumulated string — something like `"a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3"`.

## Loading Users

```cpp
void Authenticator::loadUsers(const string& filename)
{
    ifstream file(filename);
    string user, pass, role;
    while (file >> user >> pass >> role)
    {
        users[user] = {pass, role};
    }
    file.close();
}
```

`ifstream` is "input file stream" — it opens a file for reading. `file >> user >> pass >> role` reads three whitespace-separated words from the file. `users` is an `unordered_map<string, pair<string, string>>` — a dictionary where the key is the username and the value is a pair of (hashed_password, role).

`users[user] = {pass, role}` — The `[]` operator on a map either looks up an existing key or creates a new one. The `{pass, role}` syntax constructs a `pair` from the two values.

`users.txt` looks like:
```
alice a665a45920422f9d... admin
bob   4a44dc15364204a8... user
```

## Login

```cpp
string Authenticator::login(const string& user, const string& pass)
{
    string hashedPass = hashPassword(pass);
    if (users.find(user) != users.end() &&
        users[user].first == hashedPass)
    {
        return users[user].second;
    }
    return "";
}
```

`users.find(user)` searches the map for the key `user`. It returns an **iterator** — a pointer-like object that either points to the found entry or points to `users.end()` (a sentinel value meaning "not found"). So `!= users.end()` means "was found".

If the user exists AND the hashed version of the submitted password matches the stored hash, return the role. Otherwise return empty string.

`.first` accesses the first element of the pair (hashed password). `.second` accesses the second (role). This is because `pair` doesn't have named fields — it just has `first` and `second`.

## Signup

```cpp
void Authenticator::signup(const string& user, const string& pass, const string& role)
{
    ofstream file("users.txt", ios::app);
    string hashedPass = hashPassword(pass);
    file << "\n" << user << " " << hashedPass << " " << role << endl;
    file.close();
    users[user] = {hashedPass, role};
}
```

`ofstream` is "output file stream". `ios::app` means "append mode" — don't overwrite the file, add to the end. Then it writes the new user's hashed credentials and also adds them to the in-memory `users` map so they can log in immediately without restarting.

---

# Part 3 — The Website Filter: `WebsiteFilter.cpp`

## Domain Normalisation

```cpp
string normalizeDomain(string domain)
{
    transform(domain.begin(), domain.end(), domain.begin(), ::tolower);

    if (domain.find("http://") == 0)
        domain = domain.substr(7);
    else if (domain.find("https://") == 0)
        domain = domain.substr(8);

    if (domain.find("www.") == 0)
        domain = domain.substr(4);

    size_t slash = domain.find('/');
    if (slash != string::npos)
        domain = domain.substr(0, slash);

    return domain;
}
```

This function takes any form of a domain and reduces it to its bare canonical form. For example `"https://www.YouTube.com/watch?v=123"` becomes `"youtube.com"`.

`transform(domain.begin(), domain.end(), domain.begin(), ::tolower)` — This applies the function `tolower` to every character in the string. 
- `domain.begin()` is an iterator pointing to the first character
- `domain.end()` is an iterator pointing one past the last character
- The third argument is where to write the results (same place — in-place transformation)
- `::tolower` is the standard C function that converts a character to lowercase. The `::` prefix means "look in the global namespace" — it distinguishes it from any `tolower` you might have defined yourself

`domain.substr(7)` — `substr` extracts a portion of a string. Given just one argument (a starting position), it returns everything from that position to the end. Position 7 skips the 7 characters of `"http://"`.

`domain.substr(0, slash)` — With two arguments: start position and length. This takes characters from position 0 for `slash` characters — effectively cutting off everything from the `/` onwards.

## Loading Blocked Sites

```cpp
void WebsiteFilter::loadSites(const string& filename)
{
    ifstream file(filename);
    string site;

    if (!file.is_open())
    {
        cout << "Warning: Could not open " << filename << endl;
        return;
    }

    while (file >> site)
    {
        site = normalizeDomain(site);
        if (!isValidDomain(site))
            continue;
        blockedSites.insert(site);
    }
}
```

`file.is_open()` returns true if the file was successfully opened. `!` is the logical NOT operator, so `!file.is_open()` means "if the file is NOT open". This handles the case where the file doesn't exist.

`blockedSites.insert(site)` — `blockedSites` is an `unordered_set<string>`. A set is a collection with no duplicates. `insert` adds an element. Lookups in an `unordered_set` are O(1) average — meaning no matter how many items are in the set, finding an item takes roughly the same time. This is possible because it uses a hash table internally.

## The isBlockedForRole Function

```cpp
bool WebsiteFilter::isBlockedForRole(const string& domain, const string& role)
{
    string d = normalizeDomain(domain);

    if (role == "admin")
        return false;
```

Immediately return false for admins — they have unrestricted access, no need to check anything.

```cpp
    auto isMatch = [&](const unordered_set<string>& set) {
        if (set.find(d) != set.end())
            return true;

        size_t pos = d.find('.');
        while (pos != string::npos)
        {
            string sub = d.substr(pos + 1);
            if (set.find(sub) != set.end())
                return true;
            pos = d.find('.', pos + 1);
        }
        return false;
    };
```

This is a **lambda function** — an anonymous function defined inline and stored in `isMatch`. The `[&]` means "capture all local variables by reference" — the lambda can see and use the variable `d` from the surrounding function.

The logic: first check if `d` itself is in the set. Then walk up the domain hierarchy:
- For `sub.evil.com`: check `sub.evil.com`, then `evil.com`, then `com`

This means blocking `evil.com` automatically blocks `anything.evil.com`. The `d.find('.', pos + 1)` call starts searching for the next dot after position `pos + 1`, walking forward through the domain.

```cpp
    if (isMatch(blockedSites))
        return true;

    if (role == "user")
    {
        if (isMatch(gamingSites))
            return true;
        if (isMatch(socialSites))
            return true;
    }

    return false;
}
```

Students only get the `blockedSites` check (global block list). Users get that plus the category checks (gaming, social). If nothing matched, the site is allowed.

---

# Part 4 — The Logger: `Logger.cpp`

```cpp
Logger::Logger()
{
    logFile.open("proxy.log", std::ios::app);
}
```

The constructor opens `proxy.log` in append mode. This is a member variable `ofstream logFile` — it stays open for the entire life of the Logger object (which is the entire run of the proxy). Keeping the file open is more efficient than opening and closing it on every log write.

```cpp
void Logger::log(const std::string& user,
                 const std::string& host,
                 const std::string& type,
                 const std::string& status)
{
    std::lock_guard<std::mutex> lock(logMutex);
```

`lock_guard<mutex>` is a RAII wrapper around a mutex. RAII stands for "Resource Acquisition Is Initialisation" — when the `lock` object is created, it automatically locks `logMutex`. When `lock` goes out of scope (when the function returns), its destructor automatically unlocks the mutex. You don't have to remember to unlock — it happens automatically. This is important because if the function threw an exception midway, the mutex would still be correctly unlocked.

This is critical in a multi-threaded server: if two threads tried to write to the log file simultaneously, the output lines would interleave and become garbled. The mutex ensures only one thread writes at a time.

```cpp
    if (logFile.is_open())
    {
        time_t now = time(0);
        struct tm* tm_info = localtime(&now);
        char timeStr[9];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);
        logFile << "[" << timeStr << "] " << user << " | " << host << " | " << type << " | " << status << std::endl;
        logFile.flush();
    }
}
```

`time(0)` returns the current time as a `time_t` — which is just a large integer counting seconds since midnight January 1, 1970 (called "Unix epoch time"). `localtime(&now)` converts that integer into a `tm` struct — a structure with named fields like `tm_hour`, `tm_min`, `tm_sec`, `tm_year`, etc.

`strftime` ("string format time") formats the time into a string. `"%H:%M:%S"` means hours (24-hour), minutes, seconds. The result is written into `timeStr`. `sizeof(timeStr)` is 9 because HH:MM:SS is 8 characters plus the null terminator.

`logFile.flush()` forces the operating system to write the buffered data to disk immediately. Without this, the OS might hold the data in a buffer and write it later — if the proxy crashed, you could lose recent log entries.

---

# Part 5 — The LRU Cache: `LRUCache.h` and `LRUCache.cpp`

## The Data Structure (from the header)

```cpp
list<pair<string, string>> cacheList;
unordered_map<string, list<pair<string, string>>::iterator> cacheMap;
mutex cacheMutex;
```

Understanding this structure is key to understanding the cache.

`list<pair<string, string>>` — A doubly-linked list where each node holds a pair of strings: (cache_key, response_body). A doubly-linked list means each node knows the node before it and after it. The front of the list is the most recently used item. The back is the least recently used.

`unordered_map<string, list<...>::iterator>` — A hash map where the key is the cache key (string), and the value is an **iterator** (a pointer into the list) pointing directly to the corresponding node in `cacheList`.

Why both? Because each alone is insufficient:
- The list gives you O(1) move-to-front (just adjust pointers), but finding a specific key would require scanning the whole list — O(n) which is slow.
- The hash map gives you O(1) lookup by key, but you can't do O(1) reordering with it.
- Together: the map finds the list node instantly, and the list does the reordering instantly. Both operations are O(1).

## The `get` Function

```cpp
bool LRUCache::get(const string& key, string& value)
{
    lock_guard<mutex> lock(cacheMutex);
    cout << "Cache accessed by thread: " << threadNumber << endl;

    if (cacheMap.find(key) == cacheMap.end())
        return false;
```

Lock the mutex so only one thread accesses the cache at a time. Check if the key exists in the map. `cacheMap.end()` is the "not found" sentinel for maps. If not found, return false immediately.

```cpp
    auto it = cacheMap[key];
    cacheList.splice(cacheList.begin(), cacheList, it);
    value = it->second;
    return true;
}
```

`auto it = cacheMap[key]` — `auto` tells the compiler to figure out the type automatically. `it` is an iterator (pointer) into `cacheList` pointing to the node for this key.

`cacheList.splice(cacheList.begin(), cacheList, it)` — This moves the node pointed to by `it` to the front of the list (`cacheList.begin()`), without copying any data. "Splicing" in a linked list means rearranging pointers — it's O(1) regardless of where in the list the node is. After this, `it` still points to the same node; the node just lives at the front now.

`value = it->second` — The node holds a pair `{key, response_body}`. `->second` accesses the response body (the second element of the pair). We write it into the `value` parameter — which was passed by reference, so the caller's variable gets updated.

## The `put` Function

```cpp
void LRUCache::put(const string& key, const string& value)
{
    lock_guard<mutex> lock(cacheMutex);

    if (cacheMap.find(key) != cacheMap.end())
    {
        cacheList.erase(cacheMap[key]);
        cacheMap.erase(key);
    }
```

If the key already exists, remove the old entry first (from both the list and the map). This handles the case where we are updating a cached response.

```cpp
    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();
```

Add the new entry at the front of the list (most recently used position). Then update the map to point to this new front node.

```cpp
    if ((int)cacheList.size() > capacity)
    {
        auto last = cacheList.back();
        cacheMap.erase(last.first);
        cacheList.pop_back();
    }
}
```

If the list is now over capacity, evict the back node (least recently used). `cacheList.back()` returns a reference to the last element (the pair). `last.first` is the key of that element. We erase it from the map, then pop it off the list.

`(int)cacheList.size()` — The cast to `int` is needed because `size()` returns a `size_t` (unsigned), and `capacity` is an `int` (signed). Comparing signed and unsigned types directly causes a compiler warning.

---

# Part 6 — The Proxy Server Header: `proxyserver.h`

```cpp
#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H
// ... all the content ...
#endif
```

These three lines are called an **include guard**. The `#ifndef` means "if not defined". The first time this file is included, `PROXY_SERVER_H` is not defined, so the contents are included AND `PROXY_SERVER_H` is defined. If the file is somehow included a second time, `PROXY_SERVER_H` is already defined, so `#ifndef` is false and the whole file is skipped. This prevents the same declarations from appearing twice, which would cause compiler errors.

```cpp
class ProxyServer
{
private:
    int server_fd;
    int port;
    Authenticator auth;
    WebsiteFilter filter;
    Logger logger;
    std::string currentUser;
    std::string currentRole;
    std::queue<int> clientQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::mutex authMutex;
    LRUCache cache{200};
```

`private:` — These members can only be accessed from within the class. External code cannot touch `server_fd` or `clientQueue` directly. This is **encapsulation** — hiding implementation details.

`int server_fd` — The socket file descriptor for the listening socket. When a client wants to connect, they connect to this socket.

`queue<int> clientQueue` — A FIFO (first in, first out) queue of client socket file descriptors. The main thread pushes sockets in. Worker threads pop them out.

`mutex queueMutex` — Protects the queue from simultaneous access by multiple threads.

`condition_variable cv` — Used to make worker threads sleep when the queue is empty and wake up exactly one when a new client arrives.

`LRUCache cache{200}` — Creates a cache with capacity 200. The `{200}` is a member initialiser — it passes 200 to the LRUCache constructor right when the ProxyServer object is created.

---

# Part 7 — The Proxy Server Implementation: `proxyserver.cpp`

This is the main file. Walk through it from top to bottom.

## The Includes

```cpp
#include <unistd.h>      // close(), read(), write()
#include <netinet/in.h>  // sockaddr_in structure, INADDR_ANY
#include <sys/socket.h>  // socket(), bind(), listen(), accept(), connect()
#include <sys/types.h>   // various type definitions
#include <arpa/inet.h>   // inet_ntop(), inet_pton(), htons()
#include <netdb.h>       // getaddrinfo(), freeaddrinfo()
#include <cstring>       // memset(), memcpy()
#include <fcntl.h>       // fcntl(), O_NONBLOCK
#include <errno.h>       // errno, EINPROGRESS, EPIPE
#include <unordered_map> // std::unordered_map
#include <mutex>         // std::mutex, std::lock_guard
#include <ctime>         // time(), time_t
#include <csignal>       // signal(), SIGPIPE, SIG_IGN
```

Each `#include` brings in a set of function declarations and type definitions. You can think of them as "importing modules". Without `<sys/socket.h>`, the compiler doesn't know what `socket()` or `bind()` are.

## The Defines

```cpp
#define BUFFER_SIZE         65536
#define CONNECT_TIMEOUT_SEC 10
#define RECV_TIMEOUT_SEC    30
#define DNS_CACHE_TTL_SEC   30
```

`#define` creates a simple text substitution. Before compilation, the preprocessor replaces every occurrence of `BUFFER_SIZE` with `65536`. It is like a find-and-replace. This means you only change the number in one place and it applies everywhere.

65536 bytes = 64 KB. This is the size of the buffer used to read data from sockets. Each worker thread reads up to 64 KB of data at a time.

## The DNS Cache

```cpp
static mutex dnsCacheMutex;
static unordered_map<string, pair<string, time_t>> dnsCache;
```

`static` at file scope means these variables are **internal to this file**. Other .cpp files cannot see them. They are shared between all functions within this file.

`unordered_map<string, pair<string, time_t>>` — A dictionary where:
- Key: hostname string (e.g. `"example.com"`)
- Value: a pair of (IP address string, expiry timestamp)

`time_t` is just a large integer that holds a Unix timestamp. The expiry is `time(nullptr) + 30` — the current time plus 30 seconds.

## The `resolveHost` Function

```cpp
static string resolveHost(const string& host)
{
    {
        lock_guard<mutex> lock(dnsCacheMutex);
        auto it = dnsCache.find(host);
        if (it != dnsCache.end() && time(nullptr) < it->second.second)
            return it->second.first;
    }
```

The extra `{ }` braces create a new scope. The `lock_guard` will automatically unlock when the closing `}` is reached — before the potentially slow DNS lookup. This is important: we don't want to hold the lock during the DNS call, which could take 50ms and would block other threads from checking the cache.

`it->second.second` — `it` is an iterator to a map entry. `it->second` is the value (the pair). `.second` of that pair is the expiry timestamp. So this checks: is the current time less than the expiry?

`it->second.first` — The `.first` of the pair is the IP address string. Return it if the cache entry is still valid.

```cpp
    struct addrinfo hints{}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0)
        return "";
```

`struct addrinfo hints{}` — Declares a struct and zero-initialises it with `{}`. This ensures all fields start at 0/NULL instead of containing random garbage from the stack.

`hints.ai_family = AF_INET` — `AF_INET` means "IPv4" (as opposed to `AF_INET6` for IPv6). We want IPv4 addresses.

`hints.ai_socktype = SOCK_STREAM` — `SOCK_STREAM` means TCP (as opposed to `SOCK_DGRAM` for UDP). This hints to `getaddrinfo` that we want addresses suitable for TCP connections.

`getaddrinfo(host.c_str(), nullptr, &hints, &res)` — This is the DNS lookup. It takes the hostname, optional service/port (nullptr here), hints about what we want, and a pointer to a pointer where it will write the results. If successful it returns 0 and sets `res` to point to a linked list of address results.

```cpp
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr,
              ip, sizeof(ip));
    freeaddrinfo(res);
```

`INET_ADDRSTRLEN` is 16 — big enough for any IPv4 address string like `"255.255.255.255"` plus null terminator.

`inet_ntop` — "network to presentation". Converts a binary IPv4 address into a human-readable string. The `AF_INET` argument tells it this is an IPv4 address.

The complicated middle argument: `res->ai_addr` is a pointer to a generic `sockaddr` struct. `reinterpret_cast<struct sockaddr_in*>(...)` forces the compiler to treat it as the more specific `sockaddr_in` type (which has the `sin_addr` field). `->sin_addr` accesses the binary IPv4 address inside. `reinterpret_cast` is a very low-level cast that says "just interpret these bytes as if they were this type" — it does no conversion.

`freeaddrinfo(res)` — `getaddrinfo` allocates memory for the result. This releases it. Forgetting this would be a memory leak.

```cpp
    string ipStr(ip);
    {
        lock_guard<mutex> lock(dnsCacheMutex);
        dnsCache[host] = {ipStr, time(nullptr) + DNS_CACHE_TTL_SEC};
    }
    return ipStr;
}
```

Store the result in the cache with an expiry 30 seconds from now. Lock the mutex again because we are now writing to the shared cache. Another scope block so the lock is released immediately after writing.

## The `connectWithTimeout` Function

```cpp
static bool connectWithTimeout(int fd, const struct sockaddr* addr, socklen_t addrlen)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
```

`fcntl` stands for "file control" — it manipulates properties of file descriptors (sockets are file descriptors too).

`fcntl(fd, F_GETFL, 0)` — "Get file flags". Returns the current set of flags on the socket as a bitmask integer.

`flags | O_NONBLOCK` — The `|` is the bitwise OR operator. `O_NONBLOCK` is a specific bit. OR-ing it in sets that bit without changing any others. This puts the socket into non-blocking mode.

**Non-blocking mode**: Normally, `connect()` blocks — the thread stops and waits until the connection is established (up to minutes). In non-blocking mode, `connect()` returns immediately, and you must check back later to see if it succeeded.

```cpp
    int ret = connect(fd, addr, addrlen);
    if (ret == 0)
    {
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }
```

`connect()` returns 0 on immediate success (rare — usually for loopback connections). If it returns -1 and `errno == EINPROGRESS`, that means "connection is being attempted, not done yet" — this is the normal non-blocking case. If it returns -1 with any other `errno`, something genuinely failed (e.g. connection refused).

`errno` is a global variable set by system calls to indicate what kind of error occurred. Each error type has a constant: `EINPROGRESS` means "operation in progress", `ECONNREFUSED` means "connection refused", etc.

After either outcome, we call `fcntl(fd, F_SETFL, flags)` to restore the original flags (removing `O_NONBLOCK`), then return.

```cpp
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);
    struct timeval tv = {CONNECT_TIMEOUT_SEC, 0};

    ret = select(fd + 1, NULL, &writefds, NULL, &tv);
    if (ret <= 0)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }
```

`fd_set` is a bitmask data structure that represents a set of file descriptors. `FD_ZERO` clears all bits. `FD_SET(fd, &writefds)` sets the bit for our socket.

`select()` is a system call that blocks and waits for one or more file descriptors to become ready. Its five arguments:
1. `fd + 1` — the number of file descriptors to check (must be highest fd + 1)
2. `NULL` — set of fds to watch for readability (we don't care about read)
3. `&writefds` — set of fds to watch for writability (when connect completes, the socket becomes writable)
4. `NULL` — set of fds to watch for errors (we handle errors differently)
5. `&tv` — timeout: wait at most `CONNECT_TIMEOUT_SEC` seconds

`timeval` is a struct with `tv_sec` (seconds) and `tv_usec` (microseconds). `{CONNECT_TIMEOUT_SEC, 0}` means 10 seconds and 0 microseconds.

`select()` returns: the number of ready fds (>0 means something is ready), 0 if timed out, -1 on error. If `ret <= 0`, we timed out or errored, so return false.

```cpp
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    fcntl(fd, F_SETFL, flags);
    return err == 0;
}
```

When `select()` reports the socket is writable, the connection might have succeeded OR failed. The only way to know is to ask the socket for its error status. `getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen)` reads the socket-level error. If `err == 0`, no error, connection succeeded. Restore blocking mode and return the result.

`socklen_t` is the type for socket-related lengths — it is just an `unsigned int` but using the proper type makes the code portable.

## The Constructor

```cpp
ProxyServer::ProxyServer(int port)
{
    this->port = port;
    auth.loadUsers("users.txt");
    signal(SIGPIPE, SIG_IGN);
}
```

`this->port = port` — `this` is a pointer that every member function automatically has, pointing to the object itself. `this->port` refers to the member variable `port`. Without `this->`, both sides of the assignment would refer to the parameter `port`. The `this->` makes it explicit which `port` you mean.

`signal(SIGPIPE, SIG_IGN)` — A **signal** is an asynchronous notification sent to a process by the OS. `SIGPIPE` is sent when you try to write to a socket whose other end has been closed. The default behaviour of `SIGPIPE` is to terminate the process immediately. `SIG_IGN` tells the OS to ignore it. After this call, writing to a closed socket will make `send()` return -1 with `errno == EPIPE` instead of killing the process — an error you can handle gracefully.

## The `extractHost` Function

```cpp
string ProxyServer::extractHost(const string& request)
{
    if (request.find("CONNECT") == 0)
    {
        size_t start = request.find("CONNECT ") + 8;
        size_t end = request.find(":", start);
        if (end == string::npos) return "";
        return request.substr(start, end - start);
    }
```

`request.find("CONNECT") == 0` — Does the request start with "CONNECT"? `find` returns the position of the first match, so returning 0 means it starts at the very beginning of the string.

For a CONNECT request like `CONNECT example.com:443 HTTP/1.1\r\n...`, we want to extract `example.com`. Position 0 is `C`. `"CONNECT "` is 8 characters, so `start` = 8, pointing at `e` of `example.com`. `request.find(":", start)` finds the colon after the hostname. `substr(start, end - start)` extracts from `start` for `end - start` characters — exactly the hostname.

`if (end == string::npos) return ""` — If there is no colon (malformed request), return an empty string instead of crashing. Without this, `end - start` would be `npos - start`, which is a huge number due to unsigned wraparound, and `substr` would try to read millions of characters out of bounds.

```cpp
    size_t pos = request.find("Host:");
    if (pos == string::npos)
        return "";
    size_t start = pos + 6;
    while (start < request.size() && request[start] == ' ')
        start++;
    size_t end = request.find("\r\n", start);
    if (end == string::npos) return "";
    string host = request.substr(start, end - start);
    size_t colonPos = host.find(":");
    if (colonPos != string::npos)
        host = host.substr(0, colonPos);
    transform(host.begin(), host.end(), host.begin(), ::tolower);
    return host;
}
```

For a regular HTTP request like:
```
GET http://example.com/page HTTP/1.1\r\n
Host: example.com\r\n
...
```

`pos` is the position of `Host:`. Adding 6 skips `"Host: "` (5 chars + colon = 6). The while loop skips any extra spaces after the colon. Then `find("\r\n", start)` finds the end of the host header line. `substr` extracts everything between the colon and the `\r\n`.

The `colonPos` check handles cases where the Host header includes a port, like `Host: example.com:8080` — we strip the `:8080` part.

`transform(..., ::tolower)` lowercases the hostname so that `Example.COM` and `example.com` are treated as the same.

## The `base64Decode` Function

HTTP Proxy Authentication sends credentials as a Base64-encoded string. Base64 is an encoding scheme that converts any binary data (or text) into a string using only 64 printable characters: A-Z, a-z, 0-9, `+`, `/`.

```cpp
string base64Decode(const string& input)
{
    static const string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;
```

`vector<int> T(256, -1)` — Creates a vector (dynamic array) of 256 integers, all initialised to -1. This is a lookup table indexed by character code. `T[chars[i]] = i` populates it: `T['A'] = 0`, `T['B'] = 1`, ..., `T['Z'] = 25`, `T['a'] = 26`, etc. Any character not in the Base64 alphabet (like spaces or `=` padding) stays at -1.

`static const string chars` — The `static` keyword inside a function means this variable is created once and persists for the entire program run. Without `static`, it would be recreated every time the function was called.

```cpp
    string output;
    int val = 0, valb = -8;

    for (unsigned char c : input)
    {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0)
        {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return output;
}
```

Base64 works by packing groups of 3 bytes into 4 characters (each character represents 6 bits; 4 × 6 = 24 = 3 × 8). The decoding reverses this.

`val = (val << 6) + T[c]` — `<<` is the left-shift operator. `val << 6` shifts all bits in `val` left by 6 positions (multiply by 64). Then we OR in the 6-bit value of the current character. This accumulates bits.

`valb` tracks how many valid bits we have. Starting at -8 means we need 8 more bits before we can output anything. Each Base64 character adds 6 bits. When `valb >= 0`, we have at least 8 bits and can extract one byte.

`(val >> valb) & 0xFF` — `>>` is right-shift. Shift right by `valb` positions to get the byte we want to the lowest 8 bits, then `& 0xFF` masks to keep only the bottom 8 bits.

## The `handleClient` Function — The Heart of the Proxy

This function handles one complete client request from start to finish. Every single thing that happens to a client goes through here.

```cpp
void ProxyServer::handleClient(int client_socket)
{
    vector<char> buffer(BUFFER_SIZE);
    int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
    if (bytes <= 0) return;
```

`vector<char> buffer(BUFFER_SIZE)` — Allocates a 64 KB byte buffer on the heap. We use a `vector` instead of a plain array because it automatically manages its memory.

`buffer.data()` — Returns a raw pointer to the underlying array. `recv()` is a C function and needs a raw pointer, not a C++ vector object.

`recv(client_socket, buffer.data(), BUFFER_SIZE, 0)` — Receive up to BUFFER_SIZE bytes from the client. Arguments: socket fd, buffer to write into, maximum bytes to read, flags (0 = normal). Returns the number of bytes actually received, 0 if the connection closed cleanly, -1 on error.

`if (bytes <= 0) return` — If nothing was received or an error occurred, there is nothing to do. Return and let the worker thread pick up the next client.

```cpp
    string request(buffer.data(), bytes);
```

Constructs a `string` from exactly `bytes` characters starting at `buffer.data()`. We pass `bytes` explicitly because the buffer may not be null-terminated (it's raw network data, not a C string). This string now holds the entire HTTP request from the browser.

### Authentication Block

```cpp
    string username = "";
    string role = "";

    size_t authPos = request.find("Proxy-Authorization: Basic ");
```

Every HTTP request through a proxy can carry a `Proxy-Authorization` header. The browser attaches this automatically once you've entered credentials. It looks like:
```
Proxy-Authorization: Basic dXNlcjE6cGFzczE=
```
The part after `Basic ` is `username:password` encoded in Base64.

```cpp
    if (authPos != string::npos)
    {
        size_t start = request.find("Basic ") + 6;
        size_t end = request.find("\r\n", start);
        if (end == string::npos) end = request.size();

        string encoded = request.substr(start, end - start);
```

Find the position of `"Basic "` and add 6 to skip past those 6 characters, landing on the first character of the Base64 string. Find the `\r\n` that ends the header line. If there is no `\r\n` (malformed request), use the end of the string as a fallback — this prevents the crash described earlier.

```cpp
        encoded.erase(encoded.find_last_not_of(" \r\n\t") + 1);
        encoded.erase(0, encoded.find_first_not_of(" \r\n\t"));
```

These two lines trim whitespace from both ends of `encoded`. This is called "trimming" or "stripping".

`find_last_not_of(" \r\n\t")` finds the position of the last character that is NOT a space, carriage return, newline, or tab. Adding 1 gives you the position just after the last real character. `erase(pos, ...)` with one argument erases everything from that position to the end — removing trailing whitespace.

`find_first_not_of(" \r\n\t")` finds the first non-whitespace character. `erase(0, pos)` removes everything from position 0 to that position — removing leading whitespace.

```cpp
        string decoded = base64Decode(encoded);

        decoded.erase(decoded.find_last_not_of(" \r\n\t") + 1);
        decoded.erase(0, decoded.find_first_not_of(" \r\n\t"));

        size_t colon = decoded.find(':');

        if (colon != string::npos)
        {
            string user = decoded.substr(0, colon);
            string pass = decoded.substr(colon + 1);
            role = auth.login(user, pass);
            username = user;
        }
    }
```

After decoding, the string looks like `"alice:secretpassword"`. `colon` is the position of `:`. `substr(0, colon)` extracts everything before the colon (the username). `substr(colon + 1)` extracts everything after the colon (the password). Then `auth.login` checks the credentials and returns the role.

```cpp
    if (role == "")
    {
        string response =
        "HTTP/1.1 407 Proxy Authentication Required\r\n"
        "Proxy-Authenticate: Basic realm=\"Proxy\"\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";

        send(client_socket, response.c_str(), response.length(), 0);
        close(client_socket);
        return;
    }
```

If authentication failed (or no credentials were sent), respond with HTTP 407. In C++, adjacent string literals are automatically concatenated — all those quoted strings become one long string.

`"realm=\"Proxy\""` — The backslash escapes the double quote so it appears literally in the string without ending it.

`\r\n\r\n` at the very end is the blank line that marks the end of HTTP headers. Without it, the browser would keep waiting for more headers.

`send(client_socket, response.c_str(), response.length(), 0)` sends the response back to the browser. `c_str()` gives a raw char pointer. `length()` gives the number of characters.

`close(client_socket)` — Closes the socket. This triggers the TCP closing handshake. The OS sends a FIN packet to the browser, telling it the connection is ending. Without this, the socket would leak — the OS has a limited number of sockets available.

### HTTPS Handling (CONNECT Method)

```cpp
    if (request.find("CONNECT") == 0)
    {
        string host = extractHost(request);
```

HTTPS requests start with `CONNECT`. Normal HTTP requests start with `GET`, `POST`, etc. This `if` block handles all HTTPS traffic.

```cpp
        if (filter.isBlockedForRole(host, role))
        {
            string response =
            "HTTP/1.1 403 Forbidden\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 18\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Blocked by proxy";
            send(client_socket, response.c_str(), response.length(), 0);
            logger.log(..., "BLOCKED");
            close(client_socket);
            return;
        }
```

Check the filter. If blocked, send 403 and stop. Note `Content-Length: 18` — the body `"Blocked by proxy"` is exactly 18 characters. Getting this right matters: the browser reads exactly that many bytes for the body.

```cpp
        int connectPort = 443;
        {
            size_t portColon = request.find(":", request.find("CONNECT ") + 8);
            size_t portEnd   = request.find(" ", portColon);
            if (portColon != string::npos && portEnd != string::npos)
                connectPort = stoi(request.substr(portColon + 1, portEnd - portColon - 1));
        }
```

Parse the port from `CONNECT example.com:443 HTTP/1.1`. Find the colon after the hostname, find the space after the port number. `stoi` converts a string to an integer. Port defaults to 443 if parsing fails.

```cpp
        string httpsIP = resolveHost(host);
        if (httpsIP.empty()) { /* send 502, return */ }

        int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in httpsAddr{};
        httpsAddr.sin_family = AF_INET;
        httpsAddr.sin_port   = htons(connectPort);
        inet_pton(AF_INET, httpsIP.c_str(), &httpsAddr.sin_addr);
```

`socket(AF_INET, SOCK_STREAM, 0)` — Creates a new socket. `AF_INET` = IPv4, `SOCK_STREAM` = TCP, `0` = let the OS choose the protocol (TCP when using SOCK_STREAM). Returns a file descriptor integer.

`sockaddr_in` is a structure for IPv4 socket addresses. Its fields:
- `sin_family = AF_INET` — marks this as IPv4
- `sin_port = htons(connectPort)` — the port number in network byte order
- `sin_addr` — the IP address in binary form

`htons` — "Host to Network Short". Different computers store multi-byte numbers differently (some put the most significant byte first, some last). Network protocols standardise on "big-endian" (most significant byte first). `htons` converts from whatever your CPU uses to big-endian. Always use it for port numbers.

`inet_pton(AF_INET, httpsIP.c_str(), &httpsAddr.sin_addr)` — "presentation to network". Converts the IP string like `"93.184.216.34"` to 4 binary bytes and writes them into `sin_addr`.

```cpp
        if (!connectWithTimeout(remote_socket, (sockaddr*)&httpsAddr, sizeof(httpsAddr)))
        {
            // send 504, return
        }
```

The cast `(sockaddr*)&httpsAddr` — `connect()` takes a generic `sockaddr*` pointer. `httpsAddr` is a `sockaddr_in` (the IPv4 specific version). Since `sockaddr_in` starts with the same `sin_family` field as `sockaddr`, this cast is safe and standard practice.

```cpp
        string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
        send(client_socket, response.c_str(), response.length(), 0);
```

Tell the browser: "I've established the connection to the remote server, start sending your TLS data." After this, the proxy becomes a transparent pipe.

```cpp
        fd_set fds;
        while (true)
        {
            FD_ZERO(&fds);
            FD_SET(client_socket, &fds);
            FD_SET(remote_socket, &fds);
            int maxfd = max(client_socket, remote_socket) + 1;
            int activity = select(maxfd, &fds, NULL, NULL, NULL);
            if (activity <= 0)
                break;
```

The tunnel loop. `select()` waits indefinitely (no timeout — the last `NULL`) for either socket to have data to read. When either one does, `FD_ISSET` tells us which one.

`max(client_socket, remote_socket) + 1` — `select` needs to know the range of file descriptors to watch. It checks all fds from 0 up to (but not including) this number. By convention, you pass the highest fd + 1.

```cpp
            if (FD_ISSET(client_socket, &fds))
            {
                int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
                if (bytes <= 0) break;
                send(remote_socket, buffer.data(), bytes, 0);
            }
            if (FD_ISSET(remote_socket, &fds))
            {
                int bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0);
                if (bytes <= 0) break;
                send(client_socket, buffer.data(), bytes, 0);
            }
        }
        close(remote_socket);
        close(client_socket);
        return;
    }
```

Data from browser → forward to remote. Data from remote → forward to browser. The proxy never looks at the content — it's encrypted TLS data. When either end closes (recv returns 0), break the loop and close both sockets.

### HTTP Handling

```cpp
    string host = extractHost(request);
    string cacheKey;

    size_t getPos = request.find("GET ");
    size_t httpPos = request.find(" HTTP/");

    if (getPos != string::npos && httpPos != string::npos)
    {
        string url = request.substr(getPos + 4, httpPos - (getPos + 4));
```

A typical HTTP GET request looks like:
```
GET http://example.com/page?q=foo HTTP/1.1\r\n
Host: example.com\r\n
...
```

`getPos` is position of `"GET "`. `httpPos` is position of `" HTTP/"`. The URL is between them: from `getPos + 4` (skip `"GET "`) for length `httpPos - (getPos + 4)` characters.

```cpp
        if (url.find("http://") == 0)
            url = url.substr(7);

        size_t slashPos = url.find('/');
        string path = "/";
        if (slashPos != string::npos)
            path = url.substr(slashPos);

        string normalizedHost = host;
        transform(normalizedHost.begin(), normalizedHost.end(), normalizedHost.begin(), ::tolower);
        if (normalizedHost.find("www.") == 0)
            normalizedHost = normalizedHost.substr(4);

        cacheKey = "GET:" + normalizedHost + path;
    }
```

Strip `http://` to leave `example.com/page`. Find the first `/` — everything from there is the path. Build the cache key: `"GET:example.com/page?q=foo"`. Normalise the host (lowercase, strip www) so `www.Example.com` and `example.com` use the same cache entry.

The `+` operator on strings concatenates them. `"GET:" + normalizedHost + path` joins three strings into one.

```cpp
    bool isGet = (request.find("GET ") == 0);
```

True if the request starts with `"GET "`. We only cache GET requests (they are safe to replay). POST, PUT etc. have side effects — you wouldn't want to cache a bank transfer.

### Filter and Cache Check

```cpp
    if (filter.isBlockedForRole(host, role))
    {
        // send 403, log BLOCKED, return
    }

    string cachedResponse;
    if (isGet && cache.get(cacheKey, cachedResponse))
    {
        cout << "CACHE HIT\n";
        send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);
        logger.log(..., "CACHE HIT");
        close(client_socket);
        return;
    }
```

Filter runs first (security requirement). Then cache. `cache.get(cacheKey, cachedResponse)` passes `cachedResponse` by reference — if the cache hit is successful, it fills in the response. The function returns `true` or `false`. If it's a hit, send the cached response to the browser and we're done.

### DNS, Socket, Connect

```cpp
    string httpIP = resolveHost(host);
    // ... error handling ...

    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in httpAddr{};
    httpAddr.sin_family = AF_INET;
    httpAddr.sin_port   = htons(80);
    inet_pton(AF_INET, httpIP.c_str(), &httpAddr.sin_addr);

    if (!connectWithTimeout(remote_socket, (sockaddr*)&httpAddr, sizeof(httpAddr)))
    {
        // send 504, return
    }
```

Identical pattern to HTTPS, but port is always 80 (standard HTTP port).

### Modifying the Request

```cpp
    string modifiedRequest = request;

    size_t connPos = modifiedRequest.find("\r\nConnection:");
    if (connPos != string::npos)
    {
        connPos += 2;
        size_t end = modifiedRequest.find("\r\n", connPos);
        modifiedRequest.replace(connPos, end - connPos, "Connection: close");
    }
    else
    {
        size_t headerEnd = modifiedRequest.find("\r\n\r\n");
        if (headerEnd != string::npos)
            modifiedRequest.insert(headerEnd + 2, "Connection: close\r\n");
    }
```

We copy the request (not modify the original). We need `Connection: close` in the request to the remote server, so it closes the connection after responding (HTTP/1.1 defaults to keep-alive which would make our recv loop hang forever waiting for more data that never comes).

We search for `"\r\nConnection:"` with the leading `\r\n` to avoid accidentally matching `Proxy-Connection:`. If found, we use `replace(pos, len, newText)` to substitute the old value. If the header doesn't exist at all, we insert it just before the `\r\n\r\n` that ends the headers. `insert(pos, text)` inserts text at position `pos`, shifting everything after it right.

```cpp
    size_t start = modifiedRequest.find("GET ");
    size_t end = modifiedRequest.find(" HTTP/");

    if (start != string::npos && end != string::npos)
    {
        string url = modifiedRequest.substr(start + 4, end - (start + 4));

        if (url.find("http://") == 0)
        {
            string temp = url.substr(7);
            size_t slashPos = temp.find('/');
            string path = (slashPos != string::npos) ? temp.substr(slashPos) : "/";
            modifiedRequest.replace(start + 4, url.length(), path);
        }
    }
```

The browser sends a full URL: `GET http://example.com/page HTTP/1.1`. The origin server expects a relative path: `GET /page HTTP/1.1`. This block strips the `http://example.com` prefix.

`(slashPos != string::npos) ? temp.substr(slashPos) : "/"` — The ternary operator. It is a compressed if-else: if the condition before `?` is true, evaluate the expression before `:`, otherwise evaluate the expression after `:`.

`modifiedRequest.replace(start + 4, url.length(), path)` — Replace from position `start + 4` (where the URL began) for `url.length()` characters with the relative `path`.

### Stripping Hop-by-Hop Headers

```cpp
    for (const string& hdr : {"Proxy-Authorization", "Proxy-Connection"})
    {
        string search = "\r\n" + hdr + ":";
        size_t pos = modifiedRequest.find(search);
        while (pos != string::npos)
        {
            size_t lineEnd = modifiedRequest.find("\r\n", pos + 2);
            if (lineEnd == string::npos) break;
            modifiedRequest.erase(pos, lineEnd - pos);
            pos = modifiedRequest.find(search);
        }
    }
```

`for (const string& hdr : {"Proxy-Authorization", "Proxy-Connection"})` — A range-based for loop over an initialiser list. It runs twice: once with `hdr = "Proxy-Authorization"`, once with `hdr = "Proxy-Connection"`.

For each header, we search for `"\r\nProxy-Authorization:"`. The leading `\r\n` anchors the search to the start of a header line, so we don't accidentally match text inside a value.

`modifiedRequest.erase(pos, lineEnd - pos)` — Erase starting at `pos` for `lineEnd - pos` characters. This removes the entire header line (from the `\r\n` before the header name up to — but not including — the `\r\n` at the end). The next header line's `\r\n` becomes the new separator.

The `while` loop handles the (unlikely) case where the same header appears multiple times.

### Setting the Receive Timeout

```cpp
    struct timeval timeout;
    timeout.tv_sec = RECV_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

`setsockopt` — "set socket option". Configures behaviour of a socket.
- `remote_socket` — which socket to configure
- `SOL_SOCKET` — the option is at the socket level (as opposed to the TCP level, etc.)
- `SO_RCVTIMEO` — "receive timeout"
- `&timeout` — pointer to the timeout value
- `sizeof(timeout)` — size of the timeout value

After this, any `recv()` on `remote_socket` will return -1 with `errno == EAGAIN` if no data arrives within 30 seconds.

### Sending the Request and Reading the Response

```cpp
    send(remote_socket, modifiedRequest.c_str(), modifiedRequest.size(), 0);
```

This is the single most important line in the whole proxy. Send the modified HTTP request to the remote server so it knows what page we want. Before this line was added, nothing worked.

```cpp
    string fullResponse;
    bool capacityReserved = false;

    while ((bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0)) > 0)
    {
        fullResponse.append(buffer.data(), bytes);
        send(client_socket, buffer.data(), bytes, 0);
```

`while ((bytes = recv(...)) > 0)` — This is an assignment inside a condition. `recv()` is called, its return value is stored in `bytes`, and then the loop checks if `bytes > 0`. If 0 (connection closed) or -1 (error or timeout), the loop exits.

`fullResponse.append(buffer.data(), bytes)` — Accumulate the response in `fullResponse` for possible caching later.

`send(client_socket, buffer.data(), bytes, 0)` — Simultaneously forward each chunk to the browser. The user sees the page loading progressively rather than waiting for the entire response to be received first.

```cpp
        if (!capacityReserved)
        {
            size_t headerEnd = fullResponse.find("\r\n\r\n");
            if (headerEnd != string::npos)
            {
                size_t clPos = fullResponse.find("Content-Length:");
                if (clPos != string::npos && clPos < headerEnd)
                {
                    size_t valStart = clPos + 15;
                    while (valStart < fullResponse.size() && fullResponse[valStart] == ' ')
                        valStart++;
                    size_t valEnd = fullResponse.find("\r\n", valStart);
                    if (valEnd != string::npos)
                    {
                        long long contentLength = stoll(fullResponse.substr(valStart, valEnd - valStart));
                        fullResponse.reserve(headerEnd + 4 + (size_t)contentLength);
                    }
                }
                capacityReserved = true;
            }
        }
    }
```

When `fullResponse` grows beyond its current allocated capacity, C++ must allocate a new larger block, copy all the data, and free the old block. For a large response this can happen many times. By calling `reserve(total_expected_size)` once we have the headers, we pre-allocate the entire memory needed upfront — no further reallocations.

`clPos < headerEnd` — Ensures the `Content-Length` header is in the header section, not in the body (some responses might contain `Content-Length` in their body text).

`clPos + 15` — The string `"Content-Length:"` is 15 characters. This skips past it to point at the value.

`stoll` — "string to long long". Converts a string like `"12345"` to the integer 12345. We use `long long` to handle large file sizes.

`capacityReserved = true` once we've done this once — no need to re-parse headers on every loop iteration.

### Caching and Cleanup

```cpp
    if (!cacheKey.empty() && isGet && fullResponse.size() > 0 
        && fullResponse.size() < 100000 && fullResponse.find("HTTP/") == 0)
    {
        cache.put(cacheKey, fullResponse);
        logger.log(..., "CACHE STORED");
    }

    close(remote_socket);
    close(client_socket);
}
```

Cache if: we have a cache key, it was a GET request, the response is non-empty, under 100 KB, and starts with `HTTP/` (a real HTTP response). Always close both sockets when done.

## The `workerThread` Function

```cpp
void ProxyServer::workerThread()
{
    while (true)
    {
        int client_socket;

        {
            unique_lock<mutex> lock(queueMutex);

            cv.wait(lock, [this] {
                return !clientQueue.empty();
            });

            client_socket = clientQueue.front();
            clientQueue.pop();
        }

        handleClient(client_socket);
    }
}
```

Each worker thread loops forever, picking up and handling one client at a time.

`unique_lock<mutex> lock(queueMutex)` — Like `lock_guard`, this locks the mutex. But `unique_lock` is more flexible — it can be explicitly unlocked and re-locked. This is required by `cv.wait()`.

`cv.wait(lock, [this] { return !clientQueue.empty(); })` — This is the condition variable wait. It does three things atomically:
1. Unlocks `queueMutex` (so the main thread can push items)
2. Puts this thread to sleep
3. Wakes up when `cv.notify_one()` is called elsewhere

When it wakes up, it re-locks the mutex and checks the condition `!clientQueue.empty()`. If the queue is still empty (can happen due to "spurious wakeups"), it goes back to sleep. Only when the queue actually has something does it proceed.

The lambda `[this] { return !clientQueue.empty(); }` is the condition predicate. `[this]` captures the `this` pointer so the lambda can access `clientQueue`. Without `this`, the lambda would not know which `clientQueue` you mean.

`clientQueue.front()` — Peek at the first element. `clientQueue.pop()` — Remove the first element. Together they dequeue one socket.

The `{ }` scope block ends here. `unique_lock`'s destructor runs and releases `queueMutex`. Critically, we release the lock BEFORE calling `handleClient` — otherwise the mutex would be held for the entire duration of handling the client (up to 30 seconds), blocking all other threads from picking up new clients.

## The `startServer` Function

```cpp
void ProxyServer::startServer()
{
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { cout << "Socket creation failed\n"; return; }
```

Creates the listening socket. This is the "front door" of the proxy server — all clients connect to this socket.

```cpp
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

`SO_REUSEADDR` — "Reuse address". When a server shuts down, the port it was using enters a "TIME_WAIT" state for up to 60 seconds. The OS does this to handle any delayed packets still in the network. Without `SO_REUSEADDR`, if you restart the proxy within that 60 seconds, `bind()` will fail with "Address already in use". This option tells the OS to allow binding even if the port is in TIME_WAIT.

```cpp
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0)
    {
        cout << "Bind failed\n";
        return;
    }
```

`INADDR_ANY` is `0.0.0.0` — listen on all network interfaces. This means the proxy accepts connections from localhost, LAN, WiFi — anything connected to this machine.

`::bind(...)` — The `::` prefix means "look in the global namespace". There is a `bind` function in some C++ standard library headers; the `::` ensures we call the POSIX `bind()` syscall, not something else.

`bind()` assigns the address and port to `server_fd`. After this, the OS knows that incoming packets to port 8080 should go to our socket.

```cpp
    listen(server_fd, SOMAXCONN);
```

Marks the socket as a **passive socket** — it will not initiate connections, only accept them. `SOMAXCONN` is the system maximum for the backlog queue (typically 128+). The backlog is the number of completed TCP handshakes waiting in the OS queue that have not yet been `accept()`ed. If more connections come in than the backlog, the OS drops them.

```cpp
    const int THREAD_COUNT = 20;
    vector<thread> workers;
    for (int i = 0; i < THREAD_COUNT; i++)
    {
        workers.emplace_back([this, i]() {
            threadNumber = i + 1;
            cout << "Thread-" << threadNumber << " started\n";
            workerThread();
        });
        workers.back().detach();
    }
```

`workers.emplace_back(...)` — Constructs a new `thread` directly inside the vector. The argument is a **lambda function** that will run in the new thread. `[this, i]` captures `this` (pointer to the ProxyServer object) and `i` (the loop counter) by value.

`threadNumber = i + 1` — Sets the thread-local variable. Since `threadNumber` is `thread_local`, each thread has its own independent copy. Thread 1 sets its copy to 1, Thread 2 sets its copy to 2, etc. They never interfere.

`workers.back().detach()` — `back()` returns a reference to the last element (the thread we just added). `detach()` separates the thread from the `thread` object — the thread continues running independently. If we didn't detach, when the `thread` object's destructor ran (when the vector is destroyed — which never happens here since `startServer` loops forever), it would call `std::terminate` and crash the program.

```cpp
    while (true)
    {
        int client_socket = accept(server_fd, NULL, NULL);
        if (client_socket < 0)
            continue;

        {
            unique_lock<mutex> lock(queueMutex);
            clientQueue.push(client_socket);
        }

        cv.notify_one();
    }

    close(server_fd);
}
```

The main thread's infinite loop. `accept()` blocks — it sleeps until a new client connects. When one does, it returns a new socket file descriptor specifically for that client. `server_fd` stays open for future connections; `client_socket` is the connection to this specific browser.

The `NULL, NULL` arguments mean we don't care about the client's IP address or port (we could capture them for logging, but we use the Host header for that instead).

Push the client socket into the queue and call `cv.notify_one()` to wake exactly one sleeping worker thread. That worker will dequeue the socket and call `handleClient`.

`close(server_fd)` — Technically unreachable since the loop above never exits. But it is good practice to show the intended cleanup.

---

# Part 8 — Putting It All Together: The Life of a Request

Here is the complete journey of a single browser request through the proxy:

1. **Browser**: Wants to visit `http://example.com`. Has proxy configured as `127.0.0.1:8080`. Sends TCP connection to port 8080.

2. **`startServer` accept loop**: `accept()` returns a new socket file descriptor, say `5`. Pushes `5` onto `clientQueue`. Calls `cv.notify_one()`.

3. **Worker Thread 3**: Was sleeping in `cv.wait()`. Wakes up. Pops socket `5` from the queue. Calls `handleClient(5)`.

4. **`handleClient`**: Calls `recv(5, ...)`. The browser's request arrives:
   ```
   GET http://example.com/ HTTP/1.1\r\n
   Host: example.com\r\n
   Proxy-Authorization: Basic dGVzdDI6cGFzcw==\r\n
   ...
   ```

5. **Authentication**: Finds `Proxy-Authorization`, decodes Base64 → `"test2:pass"`, hashes password, looks up in `users` map → role = `"user"`.

6. **Filter check**: `isBlockedForRole("example.com", "user")` → not blocked, continue.

7. **Cache check**: `cache.get("GET:example.com/", cachedResponse)` → cache miss (first visit).

8. **DNS**: `resolveHost("example.com")` → checks DNS cache (empty first time) → calls `getaddrinfo` → gets `"93.184.216.34"` → stores in DNS cache for 30 seconds → returns IP.

9. **Connect**: Creates remote socket. Calls `connectWithTimeout` to `93.184.216.34:80`. Connection succeeds in ~50ms.

10. **Request modification**: Copies request. Replaces `Connection: keep-alive` with `Connection: close`. Rewrites `GET http://example.com/ HTTP/1.1` to `GET / HTTP/1.1`. Erases `Proxy-Authorization` and `Proxy-Connection` headers.

11. **Forward**: `send(remote_socket, modifiedRequest, ...)`. The remote server receives a clean request with no proxy credentials.

12. **Response loop**: Calls `recv(remote_socket, ...)`. Gets first 65536 bytes of the response. Immediately forwards with `send(client_socket, ...)`. Parses `Content-Length: 1256` from headers. Calls `reserve(1256 + headerSize)`. Continues receiving and forwarding until done.

13. **Cache store**: Response is non-empty, starts with `"HTTP/"`, under 100KB → `cache.put("GET:example.com/", fullResponse)`.

14. **Log**: `logger.log("Thread-3 test2(user)", "example.com", "HTTP", "ALLOWED")`.

15. **Cleanup**: `close(remote_socket)`. `close(client_socket)`. Worker thread loops back to `cv.wait()`, ready for the next client.

**Second visit** to the same URL: steps 1-7 are the same. Step 7 is now a cache hit — sends the response immediately, skips DNS, connect, send, recv entirely. Worker thread is free in milliseconds instead of hundreds of milliseconds.

---

# Quick Reference: Things That Confused You

| Term | Plain English |
|------|---------------|
| `npos` | "Not found" return value from `string::find()`. Equal to the largest possible number. |
| `size_t` | An unsigned integer type used for sizes and positions. Cannot be negative. |
| `auto` | "Figure out the type for me" — compiler deduces the type from context. |
| `nullptr` | The null pointer constant in C++. Means "this pointer points to nothing". |
| `this->` | Pointer to the current object. Disambiguates between member variables and parameters with the same name. |
| `&` in parameter | Pass by reference — function works on the caller's original, not a copy. |
| `const string&` | A reference to a string that you promise not to modify. Avoids copying. |
| `->` | Member access through a pointer. `ptr->field` is the same as `(*ptr).field`. |
| `.first` / `.second` | The two fields of a `std::pair`. |
| `cacheList.begin()` | An iterator pointing to the first element of the list. |
| `cacheList.end()` | A sentinel iterator pointing one past the last element. Used to detect "not found". |
| `\r\n` | The two-character sequence that ends every HTTP header line (CRLF). |
| `\r\n\r\n` | The blank line that separates HTTP headers from the body. |
| `htons(port)` | Convert port number from your CPU's byte order to network byte order (big-endian). |
| `inet_pton` | Convert IP address string `"93.184.216.34"` to 4 binary bytes. |
| `inet_ntop` | Convert 4 binary bytes to IP address string `"93.184.216.34"`. |
| `errno` | Global variable set by system calls to indicate which error occurred. |
| `EINPROGRESS` | Error code meaning "the operation is still in progress, not failed". |
| `fd_set` | A bitmask representing a set of file descriptors. Used with `select()`. |
| `select()` | Block until one or more sockets have data to read or are ready to write. |
| `fcntl` | Manipulate file descriptor flags. Used here to set non-blocking mode. |
| `O_NONBLOCK` | Flag that makes socket operations return immediately instead of blocking. |
| `SIGPIPE` | Signal sent when you write to a closed socket. We ignore it with `SIG_IGN`. |
| `SO_REUSEADDR` | Socket option that allows re-binding to a port in TIME_WAIT state. |
| `SO_RCVTIMEO` | Socket option that sets a timeout on `recv()` calls. |
| `SOMAXCONN` | System constant: maximum listen backlog the OS supports. |
| `thread_local` | Variable where each thread has its own independent copy. |
| `lock_guard` | RAII mutex lock. Automatically unlocks when it goes out of scope. |
| `unique_lock` | Like `lock_guard` but more flexible. Required for `condition_variable`. |
| `cv.wait(lock, pred)` | Sleep until notified AND the predicate returns true. |
| `cv.notify_one()` | Wake exactly one thread sleeping in `cv.wait()`. |
| `emplace_back` | Add an element to a vector, constructing it in-place (more efficient than push_back). |
| `detach()` | Let a thread run independently from its `thread` object. |
| `stoi` | String to int. Converts `"443"` to the integer 443. |
| `stoll` | String to long long. Like `stoi` but for larger numbers. |
| `reserve(n)` | Pre-allocate memory for a string/vector without changing its size. Avoids future reallocations. |
| `c_str()` | Get a raw C-style char pointer from a C++ string. Needed for C functions. |
| `reinterpret_cast` | Force the compiler to reinterpret memory as a different type. Very low-level. |
