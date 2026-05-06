# The Complete Journey of an HTTP Request Through the Proxy

This document traces a single HTTP request — visiting `http://example.com` — from the moment the browser decides to connect, through every function, every line of code, every decision the proxy makes, until the page is displayed and all sockets are closed. It then traces the same URL a second time to show the cache hit path.

---

## Why HTTP Is Different From HTTPS

HTTPS uses a CONNECT tunnel where the proxy acts as a blind pipe and cannot read anything. HTTP is the opposite — completely transparent. Every byte the browser sends is plain text that the proxy can read, modify, and make decisions about.

This means the proxy can:
- Read the URL being requested
- Read and modify headers
- Strip out credentials before forwarding to the website
- Cache the response so future requests skip the network entirely
- Rewrite the request format to match what the origin server expects

The trade-off is that HTTP has no privacy — anyone in the network path (including this proxy) can see exactly what you are requesting and what the server responds with. This is why the modern web has mostly moved to HTTPS.

---

## What the Raw HTTP Request Looks Like

When you visit `http://example.com/about` through a proxy, the browser sends a request that looks like this:

```
GET http://example.com/about HTTP/1.1\r\n
Host: example.com\r\n
Proxy-Authorization: Basic dGVzdDI6cGFzcw==\r\n
Proxy-Connection: keep-alive\r\n
Connection: keep-alive\r\n
Accept: text/html,application/xhtml+xml\r\n
Accept-Language: en-GB,en;q=0.9\r\n
User-Agent: Mozilla/5.0 (Macintosh; ...)\r\n
\r\n
```

Notice several things:
- The first line uses the full URL `http://example.com/about`, not just `/about`. This is specific to proxy requests — a normal browser-to-server request would just say `GET /about HTTP/1.1`. The full URL is needed so the proxy knows where to forward the request.
- `Proxy-Authorization` carries the credentials for the proxy itself.
- `Proxy-Connection` is a header only relevant between browser and proxy — it must not be forwarded to the website.
- `\r\n` ends every header line. The blank `\r\n` at the very end marks the end of all headers.

The proxy's job is to receive this, validate the user, check the filters, modify the request into the correct format, forward it to `example.com:80`, receive the response, send it back to the browser, and cache it.

---

## Phase 1 — The Browser Opens a TCP Connection to the Proxy

You type `http://example.com/about` in your browser. The browser is configured to use the proxy at `127.0.0.1:8080`.

The browser opens a TCP connection to port 8080. The operating system performs the three-way handshake on behalf of both programs:

1. Browser → OS/Proxy: **SYN** — "I want to connect"
2. OS/Proxy → Browser: **SYN-ACK** — "Accepted, ready"
3. Browser → OS/Proxy: **ACK** — "Confirmed"

After this, a TCP connection exists. Neither program has sent any HTTP data yet — TCP just sets up the transport channel. HTTP runs on top of TCP.

The proxy's `startServer()` function is sitting in an `accept()` call, which is blocked (sleeping) waiting for exactly this:

```cpp
int client_socket = accept(server_fd, NULL, NULL);
```

`server_fd` is the listening socket — the permanent "front door" that was opened when the proxy started. `accept()` wakes up and creates a brand new socket specifically for this browser connection. Let's say it returns file descriptor `7`. From now on, `7` is the handle the proxy uses to talk to this browser.

`NULL, NULL` — we do not capture the browser's IP address or port in these arguments. We get the hostname from the HTTP `Host` header later.

---

## Phase 2 — The Main Thread Hands Off to a Worker

The main thread's only job is to accept connections and immediately hand them off. It never handles clients itself.

```cpp
{
    unique_lock<mutex> lock(queueMutex);
    clientQueue.push(7);
}
cv.notify_one();
```

`unique_lock<mutex> lock(queueMutex)` — locks the queue's mutex. `clientQueue` is a shared resource accessed by 21 things simultaneously (the main thread and 20 workers), so we must lock before touching it. If two threads modified `clientQueue` at the same time without a lock, the internal data structure could be left in a corrupted state.

`clientQueue.push(7)` — pushes socket `7` onto the back of the FIFO queue.

The `{ }` block ends here. `unique_lock`'s destructor runs automatically and releases the mutex. This is intentional — we release the lock before calling `notify_one()` so that the worker thread we are about to wake up can immediately acquire the lock without waiting.

`cv.notify_one()` — wakes exactly one of the 20 worker threads that are currently sleeping inside `cv.wait()`.

---

## Phase 3 — A Worker Thread Picks Up the Socket

All 20 worker threads run this loop:

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

Say Thread 3 wakes up. It is already holding `queueMutex` at this point (that is part of how `cv.wait` works — it re-acquires the lock before returning). It checks `!clientQueue.empty()` — the queue has socket `7`, so the condition is true, and `cv.wait` returns.

`clientQueue.front()` peeks at the first element: `7`. `clientQueue.pop()` removes it. Now the `{ }` block ends, the mutex is released, and Thread 3 calls `handleClient(7)`.

Releasing the lock before `handleClient` is critical. `handleClient` can take 30+ seconds if the remote server is slow. If the lock were held the entire time, the main thread could not push new clients onto the queue, and 19 other workers would be stuck waiting to dequeue — the entire proxy would freeze.

`threadNumber` is a `thread_local` variable. Every thread has its own independent copy of it. Thread 3 set its copy to `3` when it was created in `startServer()`:

```cpp
workers.emplace_back([this, i]() {
    threadNumber = i + 1;
    workerThread();
});
```

So `threadNumber` is `3` for Thread 3's entire lifetime, without interfering with any other thread's copy.

---

## Phase 4 — Reading the Browser's Request

```cpp
void ProxyServer::handleClient(int client_socket)
{
    vector<char> buffer(BUFFER_SIZE);
    int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
    if (bytes <= 0) return;
    cout << "START handling on thread: " << threadNumber << endl;
    string request(buffer.data(), bytes);
```

`vector<char> buffer(BUFFER_SIZE)` — allocates a 65,536-byte (64 KB) buffer on the heap. We use `vector` rather than a plain array because `vector` manages its own memory and will be automatically freed when the function returns.

`recv(client_socket, buffer.data(), BUFFER_SIZE, 0)`:
- `client_socket` — socket `7`, the browser's connection
- `buffer.data()` — pointer to the raw byte array inside the vector
- `BUFFER_SIZE` — maximum bytes to read (65,536)
- `0` — no special flags (normal blocking read)

`recv()` waits until the browser sends data, then reads it into the buffer and returns the number of bytes received. For a typical HTTP request, `bytes` will be a few hundred.

`if (bytes <= 0) return` — if nothing was received (connection already closed) or an error occurred, there is nothing to process. The function returns immediately and Thread 3 loops back to wait for the next client.

`string request(buffer.data(), bytes)` — constructs a C++ string from exactly `bytes` characters of raw data. We pass `bytes` explicitly because network data is not null-terminated. Without the byte count, the string constructor would not know where to stop.

`request` now holds the full HTTP request as a readable string.

Since `request.find("CONNECT") != 0` is true (this starts with `GET`), the terminal prints:

```
REQUEST RECEIVED:
```

---

## Phase 5 — Authentication

Every single request — HTTP or HTTPS — must carry valid credentials. The proxy checks them before doing anything else.

```cpp
string username = "";
string role = "";

size_t authPos = request.find("Proxy-Authorization: Basic ");
```

`request.find("Proxy-Authorization: Basic ")` scans the entire request string from left to right, looking for that exact text. It returns the position where the text starts (a number like `48`), or `string::npos` if not found.

`string::npos` is the largest possible `size_t` value — `18446744073709551615` on a 64-bit system. It was chosen as the "not found" sentinel because no real position in a string could ever be that large.

The browser included the header, so `authPos != string::npos` is true.

```cpp
size_t start = request.find("Basic ") + 6;
size_t end = request.find("\r\n", start);
if (end == string::npos) end = request.size();

string encoded = request.substr(start, end - start);
```

`request.find("Basic ") + 6` — finds where `"Basic "` starts and adds 6 (the length of `"Basic "`) to skip past it. `start` now points at the first character of the Base64 string: `d` in `dGVzdDI6cGFzcw==`.

`request.find("\r\n", start)` — searches for `\r\n` starting from `start`. This finds the end of the header line. The second argument to `find` is the starting position for the search — without it, `find` would always start from position 0 and could match an earlier `\r\n` before our header.

`if (end == string::npos) end = request.size()` — safety guard. If the request is malformed with no `\r\n`, use the end of the string. Without this guard, `end - start` would be `npos - start` which is a colossal number due to unsigned integer arithmetic, and `substr` would attempt to read memory far beyond the string's bounds — a crash.

`request.substr(start, end - start)` — extracts from position `start` for `end - start` characters. This gives us the raw Base64 string: `"dGVzdDI6cGFzcw=="`.

```cpp
encoded.erase(encoded.find_last_not_of(" \r\n\t") + 1);
encoded.erase(0, encoded.find_first_not_of(" \r\n\t"));
```

Two-step whitespace trimming:

Line 1 — trims the right side: `find_last_not_of(" \r\n\t")` finds the position of the last character that is NOT a space, carriage return, newline, or tab. Adding 1 gives the position just after the last real character. `erase(pos)` with one argument erases everything from that position to the end of the string.

Line 2 — trims the left side: `find_first_not_of(" \r\n\t")` finds the first non-whitespace character. `erase(0, pos)` erases everything from position 0 up to (but not including) that position.

```cpp
string decoded = base64Decode(encoded);

decoded.erase(decoded.find_last_not_of(" \r\n\t") + 1);
decoded.erase(0, decoded.find_first_not_of(" \r\n\t"));
```

`base64Decode("dGVzdDI6cGFzcw==")` converts the Base64 encoding back to the original text: `"test2:pass"`. The trim is applied again to the decoded result, because the decoded string itself might have trailing whitespace.

```cpp
size_t colon = decoded.find(':');

if (colon != string::npos)
{
    string user = decoded.substr(0, colon);
    string pass = decoded.substr(colon + 1);
    role = auth.login(user, pass);
    username = user;
}
```

`decoded.find(':')` finds the colon that separates username from password. `substr(0, colon)` takes everything before it: `"test2"`. `substr(colon + 1)` takes everything after it: `"pass"`.

`auth.login("test2", "pass")`:
1. Hashes `"pass"` with SHA-256 → 64-character hex string
2. Looks up `"test2"` in the `users` map (loaded from `users.txt` at startup)
3. Compares the hash of the submitted password with the stored hash
4. If they match, returns the stored role string: `"user"`
5. If they do not match, returns `""`

`username = "test2"`, `role = "user"`.

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

Since `role == "user"` (not empty), this block is skipped. But if authentication had failed, we would send `407 Proxy Authentication Required`. The `Proxy-Authenticate` header tells the browser the authentication scheme (`Basic`) and realm (`"Proxy"` — a display name for the login dialog). The browser pops up a credentials dialog and retries.

`"Content-Length: 0\r\n"` — there is no body in this response. The Content-Length header tells the browser to expect 0 bytes of body. Without it, the browser might wait for body data that never comes.

The double `\r\n` at the very end: the first `\r\n` ends the last header (`Connection: close`), and the second `\r\n` is the mandatory blank line separating headers from body.

`send(client_socket, response.c_str(), response.length(), 0)` — sends the string over socket `7` to the browser. `c_str()` gets a raw char pointer from the C++ string. `length()` is the number of characters. The `0` flag means normal send with no special behaviour.

---

## Phase 6 — Identifying This as an HTTP (Not HTTPS) Request

```cpp
if (request.find("CONNECT") == 0)
{
    // HTTPS handling — entire block
    ...
    return;
}
// HTTP handling begins here
```

`request.find("CONNECT") == 0` would be true if the request started with `CONNECT` (HTTPS). Since our request starts with `GET`, this is false — we skip the entire HTTPS block and fall through into HTTP handling.

```cpp
string host = extractHost(request);
cout << "HTTP HOST: " << host << endl;
```

`extractHost` takes the non-CONNECT path:

```cpp
size_t pos = request.find("Host:");
if (pos == string::npos) return "";
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
```

`request.find("Host:")` finds the `Host:` header. Adding 6 skips `"Host: "` (5 chars + a space). The `while` loop skips any extra spaces after the colon — some browsers send `Host:  example.com` with two spaces.

`request.find("\r\n", start)` finds the end of the Host header line. `substr(start, end - start)` extracts just `"example.com"`.

The `colonPos` check handles `Host: example.com:8080` — strip the port number since we only want the hostname for filtering and cache key purposes.

`transform(..., ::tolower)` lowercases every character. This means `"Example.COM"` and `"example.com"` are treated as the same host.

`host = "example.com"`. The terminal prints `HTTP HOST: example.com`.

---

## Phase 7 — Building the Cache Key

The cache key is how the proxy identifies "have I seen this exact request before?" It must be precise enough that different URLs produce different keys, but normalised enough that trivially equivalent URLs (different capitalisation, with or without www) share the same cache entry.

```cpp
string cacheKey;

size_t getPos = request.find("GET ");
size_t httpPos = request.find(" HTTP/");

if (getPos != string::npos && httpPos != string::npos)
{
    string url = request.substr(getPos + 4, httpPos - (getPos + 4));
```

The request first line is `GET http://example.com/about HTTP/1.1`.

`getPos` = position of `"GET "` = 0.
`httpPos` = position of `" HTTP/"`. This appears after the URL, so it is something like 30.

`request.substr(getPos + 4, httpPos - (getPos + 4))`:
- Start: `getPos + 4` = 4 (skip `"GET "`)
- Length: `httpPos - (getPos + 4)` = `30 - 4` = 26

This extracts just the URL: `"http://example.com/about"`.

```cpp
    if (url.find("http://") == 0)
        url = url.substr(7);
```

`url.find("http://") == 0` — does the URL start with `"http://"`? Yes. `url.substr(7)` strips the first 7 characters, leaving `"example.com/about"`.

```cpp
    size_t slashPos = url.find('/');
    string path = "/";
    if (slashPos != string::npos)
        path = url.substr(slashPos);
```

`url.find('/')` finds the first `/` in `"example.com/about"`, which is at position 11. `url.substr(11)` = `"/about"`. `path = "/about"`.

If the URL had no path (just `"example.com"`), `find('/')` would return `npos` and `path` would stay as the default `"/"`.

```cpp
    string normalizedHost = host;
    transform(normalizedHost.begin(), normalizedHost.end(), normalizedHost.begin(), ::tolower);

    if (normalizedHost.find("www.") == 0)
        normalizedHost = normalizedHost.substr(4);

    cacheKey = "GET:" + normalizedHost + path;
}
```

The host was already lowercased by `extractHost`, but we normalise it again here to be safe. Then strip the `"www."` prefix if present — `"www.example.com"` and `"example.com"` should share the same cache entry.

`"GET:" + "example.com" + "/about"` = `"GET:example.com/about"`. This is the cache key.

The `"GET:"` prefix is intentional — if we ever cached POST responses (we don't, but defensively), their keys would start with `"POST:"` and never conflict with GET keys.

```cpp
else
{
    cacheKey = host;
}
```

If this was not a GET request (e.g. POST), `getPos` or `httpPos` would be `npos` and we fall here. The cache key becomes just the hostname. This is a fallback — POST responses are never actually cached (checked later with `isGet`), but the key is set anyway.

```cpp
bool isGet = (request.find("GET ") == 0);
```

`true` if the request starts with `"GET "`. This flag is used in two places: deciding whether to check the cache at all, and deciding whether to store the response in the cache. We only cache GET responses. POST, PUT, DELETE have side effects and must not be replayed from cache.

---

## Phase 8 — The Website Filter (Security Critical)

```cpp
if (filter.isBlockedForRole(host, role))
{
    cout << "Blocked HTTP site: " << host << endl;
    string response =
    "HTTP/1.1 403 Forbidden\r\n\r\nBlocked by proxy";
    send(client_socket, response.c_str(), response.length(), 0);
    logger.log("Thread-3 test2(user)", "example.com", "HTTP", "BLOCKED");
    close(client_socket);
    cout << "END handling on thread: " << threadNumber << endl;
    return;
}
```

`filter.isBlockedForRole("example.com", "user")` normalises the domain and checks it against:
1. `blockedSites` — the global block list (everyone except admin is blocked)
2. `gamingSites` — the gaming category (blocked for `user` role)
3. `socialSites` — the social media category (blocked for `user` role)

`example.com` is not in any list, so this returns `false` and we skip the block.

**Why the filter runs before the cache** — this ordering is a security requirement. Imagine the admin previously visited `youtube.com`, which got cached. If the cache check ran first, a `student` trying to visit `youtube.com` would receive the cached response without ever hitting the filter — completely bypassing access control. By running the filter first, blocked users always get 403 regardless of cache state.

If blocked, `403 Forbidden` is sent and we return immediately. No DNS lookup, no connection to the remote server, no wasted resources. The log records:
```
[14:23:45] Thread-3 test2(user) | example.com | HTTP | BLOCKED
```

---

## Phase 9 — The Cache Check

```cpp
string cachedResponse;
if (isGet && cache.get(cacheKey, cachedResponse))
{
    cout << "CACHE HIT\n";
    send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);
    logger.log("Thread-3 test2(user)", "example.com", "HTTP", "CACHE HIT");
    close(client_socket);
    return;
}
```

`isGet` is `true`. `cache.get("GET:example.com/about", cachedResponse)` looks up the cache key.

**First visit** — the cache is empty for this key. `get()` returns `false`. We skip this block entirely and continue to the network path.

**What happens inside `cache.get()`:**

```cpp
bool LRUCache::get(const string& key, string& value)
{
    lock_guard<mutex> lock(cacheMutex);

    if (cacheMap.find(key) == cacheMap.end())
        return false;

    auto it = cacheMap[key];
    cacheList.splice(cacheList.begin(), cacheList, it);
    value = it->second;
    return true;
}
```

The `lock_guard` locks the cache's mutex. Multiple threads could call `cache.get()` simultaneously — the mutex ensures they do not corrupt the data structure.

`cacheMap.find(key)` looks up the key in the hash map. `cacheMap.end()` is the "not found" sentinel. Since the key is not found, we return `false`.

On a cache hit (second visit), it would find the entry, `splice` it to the front of the list (marking it most recently used), copy the response into `value` (passed by reference — so the caller's `cachedResponse` variable is updated), and return `true`. The caller then sends `cachedResponse` directly to the browser and returns — the entire DNS, connect, send, receive chain is skipped.

---

## Phase 10 — DNS Resolution

No cache hit. We need to contact `example.com` over the network. First, we need its IP address.

```cpp
string httpIP = resolveHost(host);
if (httpIP.empty())
{
    string response =
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n\r\n";
    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);
    return;
}
```

Inside `resolveHost("example.com")`:

```cpp
{
    lock_guard<mutex> lock(dnsCacheMutex);
    auto it = dnsCache.find("example.com");
    if (it != dnsCache.end() && time(nullptr) < it->second.second)
        return it->second.first;
}
```

Check the DNS cache first. `dnsCache` is a `unordered_map<string, pair<string, time_t>>`. The key is the hostname. The value is a pair: (IP address string, expiry timestamp).

`time(nullptr)` returns the current Unix timestamp — seconds since midnight January 1, 1970. `it->second.second` is the expiry time. If current time is before expiry, the cache entry is still valid and we return the cached IP immediately.

The inner `{ }` scope releases the lock before the DNS network call. We must not hold `dnsCacheMutex` during `getaddrinfo` — that call can take tens of milliseconds, and holding the lock the whole time would block every other thread from checking or updating the DNS cache.

**First request to `example.com`** — not in DNS cache yet. Continue to the actual DNS lookup:

```cpp
struct addrinfo hints{}, *res;
hints.ai_family   = AF_INET;
hints.ai_socktype = SOCK_STREAM;
if (getaddrinfo("example.com", nullptr, &hints, &res) != 0)
    return "";
```

`struct addrinfo hints{}` — declares the struct and zero-initialises all fields with `{}`. If we did not zero-initialise, the fields would contain random stack garbage, which would confuse `getaddrinfo`.

`hints.ai_family = AF_INET` — request only IPv4 results.  
`hints.ai_socktype = SOCK_STREAM` — request only TCP-compatible addresses.  
The second argument `nullptr` — we are not asking for a specific port, just the address.

`getaddrinfo` sends a DNS query to your configured DNS server (your router by default) and waits for a response. For `example.com` it returns the IP `93.184.216.34`.

`res` is now a pointer to a linked list of `addrinfo` structures — one for each returned address. We only use the first one.

```cpp
char ip[INET_ADDRSTRLEN];
inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr,
          ip, sizeof(ip));
freeaddrinfo(res);
```

`INET_ADDRSTRLEN` is 16 — enough for `"255.255.255.255"` plus null terminator.

`res->ai_addr` is a pointer to a generic `sockaddr`. `reinterpret_cast<struct sockaddr_in*>(...)` forces the compiler to treat those bytes as the IPv4-specific `sockaddr_in` type — they are the same memory, just interpreted differently. `->sin_addr` is the binary IPv4 address (4 bytes).

`inet_ntop(AF_INET, ..., ip, sizeof(ip))` — "network to presentation" — converts those 4 bytes into the readable string `"93.184.216.34"` and writes it into `ip`.

`freeaddrinfo(res)` — `getaddrinfo` allocated heap memory for the result. This releases it. Without it, we would leak a small amount of memory on every DNS lookup.

```cpp
string ipStr(ip);
{
    lock_guard<mutex> lock(dnsCacheMutex);
    dnsCache["example.com"] = {"93.184.216.34", time(nullptr) + 30};
}
return ipStr;
```

Store in the DNS cache: key `"example.com"`, value pair of (IP string, expiry 30 seconds from now). Lock the mutex for this write. Any thread asking for `example.com` within the next 30 seconds gets `"93.184.216.34"` instantly without a network call.

Back in `handleClient`, `httpIP = "93.184.216.34"`.

---

## Phase 11 — Creating the Remote Socket

```cpp
int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
if (remote_socket < 0)
{
    close(client_socket);
    return;
}
```

`socket(AF_INET, SOCK_STREAM, 0)` creates a new TCP socket. Returns a file descriptor integer — let's say `9`. This socket is not yet connected to anything. It is just a communication endpoint that the OS has allocated.

`AF_INET` — IPv4 family.  
`SOCK_STREAM` — TCP (stream-oriented, reliable, ordered). The alternative `SOCK_DGRAM` would be UDP (unreliable, unordered, but faster for things like video streaming).  
`0` — let the OS choose the protocol (it will choose TCP given SOCK_STREAM).

If `socket()` returns a negative number, something went wrong at the OS level (out of file descriptors, out of memory). We close the client socket and return. We do not send an error response here — the browser will handle the disconnection.

Now we have two open sockets:
- `7` — the browser's connection to the proxy
- `9` — a new socket that will be the proxy's connection to `example.com`

---

## Phase 12 — Connecting to the Remote Server

```cpp
struct sockaddr_in httpAddr{};
httpAddr.sin_family = AF_INET;
httpAddr.sin_port   = htons(80);
inet_pton(AF_INET, httpIP.c_str(), &httpAddr.sin_addr);
```

Build the destination address structure for `example.com:80`:

`sockaddr_in` is the IPv4 socket address structure. Its fields:
- `sin_family = AF_INET` — marks this as an IPv4 address
- `sin_port = htons(80)` — port 80 in network byte order. `htons` stands for "host to network short". Most CPUs (Intel/AMD/ARM) are little-endian: they store 80 as `0x50 0x00`. But network protocols require big-endian: `0x00 0x50`. `htons` does this byte swap. Always use `htons` for port numbers.
- `inet_pton(AF_INET, "93.184.216.34", &httpAddr.sin_addr)` — "presentation to network" — converts the IP address string into 4 binary bytes stored in `sin_addr`. This is the reverse of `inet_ntop`.

```cpp
if (!connectWithTimeout(remote_socket, (sockaddr*)&httpAddr, sizeof(httpAddr)))
{
    string response =
    "HTTP/1.1 504 Gateway Timeout\r\n"
    "Connection: close\r\n\r\n";
    send(client_socket, response.c_str(), response.length(), 0);
    close(remote_socket);
    close(client_socket);
    return;
}
```

`(sockaddr*)&httpAddr` — `connect()` takes a generic `sockaddr*` pointer. `httpAddr` is a `sockaddr_in`. The cast tells the compiler: "treat the address of this `sockaddr_in` as if it were a pointer to a `sockaddr`." This is safe because `sockaddr_in` has the same layout as `sockaddr` at the start.

Inside `connectWithTimeout`:

1. `fcntl(fd, F_GETFL, 0)` — reads the current socket flags as a bitmask integer
2. `fcntl(fd, F_SETFL, flags | O_NONBLOCK)` — sets the `O_NONBLOCK` bit. The `|` is bitwise OR — it sets the O_NONBLOCK bit without changing any other bits. Now socket `9` is in non-blocking mode.
3. `connect(9, addr, addrlen)` — initiates the TCP handshake to `93.184.216.34:80`. In non-blocking mode, this returns immediately (does not wait). The return value is `-1` and `errno == EINPROGRESS` — meaning "handshake started in the background, not finished yet." The OS is sending the SYN packet to `example.com` right now.
4. `FD_ZERO(&writefds); FD_SET(9, &writefds)` — set up a file descriptor set watching socket `9`
5. `struct timeval tv = {10, 0}` — 10-second timeout
6. `select(10, NULL, &writefds, NULL, &tv)` — sleep until socket `9` becomes writable (meaning the TCP handshake completed — `example.com` sent SYN-ACK and our OS sent ACK), or until 10 seconds pass. The `NULL` for the read set means we are not watching for readability. We are only watching for write-readiness because a connected socket becomes writable when the handshake finishes.
7. `getsockopt(9, SOL_SOCKET, SO_ERROR, &err, &errlen)` — even if `select()` reported the socket is writable, the connection might have failed (e.g. `example.com` sent RST — connection refused). `SO_ERROR` retrieves any pending error on the socket. If `err == 0`, success.
8. `fcntl(fd, F_SETFL, flags)` — restore blocking mode. The `flags` variable was saved in step 1 — restoring it puts the socket back exactly how it was before we made it non-blocking.
9. Return `err == 0`.

If the TCP handshake fails (server down, firewall, timeout), we send `504 Gateway Timeout` to the browser. 504 specifically means "the proxy's connection to the upstream server failed or timed out." We close both sockets and return.

On success, the terminal prints:
```
Connected to HTTP server
```

---

## Phase 13 — Modifying the Request

The browser sent us a request formatted for a proxy. We cannot forward it as-is to `example.com` — it needs three modifications before the origin server will understand it correctly.

```cpp
string modifiedRequest = request;
```

We copy the original request. We never modify `request` directly — we build `modifiedRequest` as the version we will forward.

### Modification 1 — Fix the Connection Header

```cpp
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
    {
        modifiedRequest.insert(headerEnd + 2, "Connection: close\r\n");
    }
}
```

**Why this is needed:** HTTP/1.1 defaults to keep-alive — meaning after a response is sent, the connection stays open for the browser to reuse. But the proxy does not implement persistent connections in its response loop. The recv loop:

```cpp
while ((bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0)) > 0)
```

...exits only when `recv()` returns 0 or -1. With keep-alive, `example.com` would send the response, then keep the connection open waiting for more requests. `recv()` would never return 0, and the loop would block forever (or until the 30-second timeout). We force `Connection: close` to tell `example.com`: close the connection after this one response.

**Why we search for `"\r\nConnection:"`** and not just `"Connection:"` — the request also contains `Proxy-Connection: keep-alive`. If we searched for just `"Connection:"`, `find()` might match the `Connection:` text inside `Proxy-Connection:`, and we would corrupt that header instead of replacing the right one. The `\r\n` prefix anchors the search to the start of a header line.

`connPos += 2` — after finding `"\r\nConnection:"`, we add 2 to skip past the `\r\n`. Now `connPos` points directly at `C` of `Connection:`.

`modifiedRequest.find("\r\n", connPos)` — find the end of the Connection header line.

`modifiedRequest.replace(connPos, end - connPos, "Connection: close")` — replace from `connPos` for `end - connPos` characters with the new value. `replace(start, length, newText)` removes `length` characters starting at `start` and inserts `newText` in their place.

If there is no `Connection:` header at all (the `else` branch):

`modifiedRequest.find("\r\n\r\n")` — finds the blank line separating headers from body.  
`headerEnd + 2` — points two characters into the `\r\n\r\n` sequence, which is the start of the second `\r\n`.  
`modifiedRequest.insert(headerEnd + 2, "Connection: close\r\n")` — inserts the new header before the final `\r\n` that ends the headers. The result is: `...\r\nConnection: close\r\n\r\n`.

### Modification 2 — Rewrite the Absolute URL to a Relative Path

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

The browser sent: `GET http://example.com/about HTTP/1.1`.
Origin servers expect: `GET /about HTTP/1.1`.

`example.com` does not expect to see its own domain name in the request line — it already knows which server it is. If we forwarded `GET http://example.com/about HTTP/1.1`, many servers would return `400 Bad Request` because absolute URLs in the request line are a proxy-specific format.

`url = "http://example.com/about"`. Strip `"http://"` → `"example.com/about"`. Find the first `/` at position 11. `temp.substr(11)` = `"/about"`.

`(slashPos != string::npos) ? temp.substr(slashPos) : "/"` — the ternary operator: if there is a slash, take the path from it; if there is no slash (URL was just `example.com`), use `"/"` as the root path.

`modifiedRequest.replace(start + 4, url.length(), "/about")` — in the string, replace the full URL `"http://example.com/about"` with just `"/about"`.

### Modification 3 — Strip Proxy-Specific Headers

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

`for (const string& hdr : {"Proxy-Authorization", "Proxy-Connection"})` — a range-based for loop over an initialiser list. It runs twice: first with `hdr = "Proxy-Authorization"`, then with `hdr = "Proxy-Connection"`.

**Proxy-Authorization** carries your proxy username and password. If forwarded to `example.com`, the website would receive your credentials. This is a serious security vulnerability — any website you visit could harvest proxy credentials and use them to impersonate you.

**Proxy-Connection** is a keep-alive hint intended only for the proxy. Origin servers do not understand or need it. Forwarding it creates unnecessary clutter.

`string search = "\r\n" + hdr + ":"` — e.g. `"\r\nProxy-Authorization:"`. The leading `\r\n` ensures we match the start of a header line, not a `Proxy-Authorization` substring inside a header value.

`modifiedRequest.find(search)` finds the first occurrence. `modifiedRequest.find("\r\n", pos + 2)` finds the end of that header line — starting the search at `pos + 2` to skip past the `\r\n` we already matched, finding the next `\r\n` that ends the line.

`modifiedRequest.erase(pos, lineEnd - pos)` — erases from `pos` for `lineEnd - pos` characters. This removes the entire header including its leading `\r\n`. The `while` loop runs again in case the same header appeared multiple times (rare but possible).

---

## Phase 14 — Printing the Final Request

```cpp
cout << "===== FORWARDING REQUEST =====\n";
cout << modifiedRequest << endl;
```

This debug print shows what the proxy is actually sending to `example.com`. After all three modifications, `modifiedRequest` looks like:

```
GET /about HTTP/1.1\r\n
Host: example.com\r\n
Connection: close\r\n
Accept: text/html,application/xhtml+xml\r\n
Accept-Language: en-GB,en;q=0.9\r\n
User-Agent: Mozilla/5.0 (Macintosh; ...)\r\n
\r\n
```

Compare to what the browser originally sent:
- `GET http://example.com/about` → `GET /about` (absolute URL → relative path)
- `Connection: keep-alive` → `Connection: close` (force close after response)
- `Proxy-Authorization: Basic ...` → **gone** (credentials stripped)
- `Proxy-Connection: keep-alive` → **gone** (proxy-only header stripped)

Everything else — `Accept`, `Accept-Language`, `User-Agent` — is passed through unchanged. The origin server should see a normal browser request.

---

## Phase 15 — Setting the Receive Timeout

```cpp
struct timeval timeout;
timeout.tv_sec = RECV_TIMEOUT_SEC;   // 30 seconds
timeout.tv_usec = 0;
setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
```

`setsockopt` ("set socket option") configures a behaviour of socket `9`.

Arguments:
- `remote_socket` (`9`) — which socket to configure
- `SOL_SOCKET` — the option is at the socket level (the most general level, as opposed to `IPPROTO_TCP` for TCP-level options)
- `SO_RCVTIMEO` — "receive timeout"
- `&timeout` — pointer to the timeout value
- `sizeof(timeout)` — the size of the timeout value in bytes

`timeval` is a structure with two fields: `tv_sec` (whole seconds) and `tv_usec` (microseconds, i.e. millionths of a second). `{30, 0}` means 30 seconds and 0 microseconds.

After this call, any `recv(remote_socket, ...)` will return `-1` with `errno == EAGAIN` (or `EWOULDBLOCK` on some systems) if `example.com` sends no data for 30 consecutive seconds. This prevents Thread 3 from being frozen indefinitely by a server that accepts the connection but then goes silent. The worker thread will eventually time out and become available for new clients.

---

## Phase 16 — Sending the Request

```cpp
send(remote_socket, modifiedRequest.c_str(), modifiedRequest.size(), 0);
```

This is the single most critical line in HTTP handling. It sends `modifiedRequest` to `example.com` through socket `9`.

`c_str()` returns a raw `const char*` pointer to the string's internal character array. `send()` is a C function and requires a raw pointer. `size()` returns the number of characters. The `0` flag means normal send.

Without this line — which was the original showstopper bug — `example.com` would sit waiting for a request that never came. Our `recv()` loop would eventually time out returning nothing. The browser would see `ERR_EMPTY_RESPONSE`.

After `send()`, `example.com` receives our request and begins preparing its response.

---

## Phase 17 — Receiving the Response and Forwarding It to the Browser

```cpp
string fullResponse;
bool capacityReserved = false;

while ((bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0)) > 0)
{
    fullResponse.append(buffer.data(), bytes);
    send(client_socket, buffer.data(), bytes, 0);
```

`while ((bytes = recv(...)) > 0)` — this is an assignment inside a condition. `recv()` is called, its return value is stored in `bytes`, and then the loop checks `bytes > 0`. This loop runs until:
- `recv()` returns `0` — `example.com` closed the connection (which it will because we set `Connection: close`)
- `recv()` returns `-1` — an error or the 30-second timeout

Each iteration reads up to 65,536 bytes from socket `9` and immediately forwards it to the browser via socket `7`. This streaming approach means the browser starts receiving data immediately as it arrives, not after the entire response has been buffered. For large pages, the user sees progressive loading.

`fullResponse.append(buffer.data(), bytes)` — accumulates the entire response in a string for possible caching. We need the full response to store in the cache, but we forward each chunk immediately without waiting for the whole thing.

### Memory Optimisation — Content-Length Reserve

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
```

When a C++ `string` grows beyond its current allocated capacity, it must:
1. Allocate a new, larger block of memory (typically double the size)
2. Copy all existing content to the new block
3. Free the old block

For a 50 KB response arriving in 1 KB chunks, this reallocation could happen 6–7 times without optimisation. Each reallocation copies all previous data.

By parsing the `Content-Length` header (which tells us the body size upfront), we can call `fullResponse.reserve(total)` once after seeing the headers, pre-allocating exactly the right amount of memory. No further reallocations will occur during the recv loop.

`fullResponse.find("\r\n\r\n")` — finds the blank line between headers and body. If the first chunk contained the full headers (very likely — headers are usually small), `headerEnd != string::npos`.

`clPos + 15` — `"Content-Length:"` is 15 characters. Skipping past it lands on the value. The `while` loop skips any spaces between the colon and the number (some servers send `Content-Length:  1256` with extra spaces).

`stoll(fullResponse.substr(valStart, valEnd - valStart))` — `stoll` is "string to long long". It converts the string `"1256"` to the integer `1256`. `long long` handles very large file sizes.

`fullResponse.reserve(headerEnd + 4 + contentLength)` — reserve the header size, plus 4 for the `\r\n\r\n` itself, plus the body size.

`capacityReserved = true` — we set this flag so the if-block only runs once, not on every iteration of the recv loop.

### What the response looks like arriving from `example.com`

```
HTTP/1.1 200 OK\r\n
Content-Type: text/html; charset=UTF-8\r\n
Content-Length: 1256\r\n
Cache-Control: max-age=604800\r\n
Date: Tue, 06 May 2026 14:23:45 GMT\r\n
\r\n
<!doctype html>
<html>
<head>
    <title>Example Domain</title>
    ...
</head>
<body>
    <h1>Example Domain</h1>
    ...
</body>
</html>
```

Each chunk from `recv()` is forwarded to the browser and appended to `fullResponse`. When `example.com` closes the connection, `recv()` returns `0` and the loop exits.

---

## Phase 18 — Logging

```cpp
cout << "Response sent back to browser\n";

logger.log(
    "Thread-3 test2(user)",
    "example.com",
    "HTTP",
    "ALLOWED"
);
```

Inside `Logger::log`:

```cpp
void Logger::log(const string& user, const string& host, const string& type, const string& status)
{
    lock_guard<mutex> lock(logMutex);

    if (logFile.is_open())
    {
        time_t now = time(0);
        struct tm* tm_info = localtime(&now);
        char timeStr[9];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);
        logFile << "[" << timeStr << "] " << user << " | " << host << " | " << type << " | " << status << endl;
        logFile.flush();
    }
}
```

`lock_guard<mutex> lock(logMutex)` — Thread 3 might try to write to the log at the same exact moment as Thread 7 and Thread 12. Without this lock, the output lines could interleave character by character:

```
[14:[14:23:23:4545] ] Thread-3Thread-7 test2(user)admin1(admin)...
```

The mutex ensures each log line is written atomically — completely by one thread before any other thread can begin writing.

`time(0)` returns the current Unix timestamp. `localtime(&now)` converts it to a `struct tm` — a structure with fields `tm_hour`, `tm_min`, `tm_sec`, etc. in your local timezone. `strftime(timeStr, 9, "%H:%M:%S", tm_info)` formats it as `"14:23:45"` and writes into `timeStr[9]`. The 9-byte buffer holds 8 characters plus the null terminator.

`logFile.flush()` forces the OS to write the buffered data to disk immediately. Without flushing, a sudden crash could lose the last few log entries that were in the OS's write buffer.

The line written to `proxy.log`:
```
[14:23:45] Thread-3 test2(user) | example.com | HTTP | ALLOWED
```

---

## Phase 19 — Caching the Response

```cpp
if (!cacheKey.empty() && isGet && fullResponse.size() > 0
    && fullResponse.size() < 100000 && fullResponse.find("HTTP/") == 0)
{
    cache.put(cacheKey, fullResponse);
    cout << "CACHE STORED for user: " << username << endl;

    logger.log("Thread-3 test2(user)", "example.com", "HTTP", "CACHE STORED");
}
```

Five conditions must all be true before we cache:

1. `!cacheKey.empty()` — we have a cache key (we do: `"GET:example.com/about"`)
2. `isGet` — this was a GET request (it was)
3. `fullResponse.size() > 0` — the response is not empty (it contains the HTML page)
4. `fullResponse.size() < 100000` — under 100 KB. Caching very large responses would quickly fill the cache with single entries, evicting many smaller useful ones. 100 KB is a threshold that covers most HTML pages.
5. `fullResponse.find("HTTP/") == 0` — the response starts with `"HTTP/"`. This verifies it is a real HTTP response, not network garbage or an empty string left over from a failed recv loop.

All five are true. Inside `cache.put("GET:example.com/about", fullResponse)`:

```cpp
void LRUCache::put(const string& key, const string& value)
{
    lock_guard<mutex> lock(cacheMutex);

    if (cacheMap.find(key) != cacheMap.end())
    {
        cacheList.erase(cacheMap[key]);
        cacheMap.erase(key);
    }

    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();

    if ((int)cacheList.size() > capacity)
    {
        auto last = cacheList.back();
        cacheMap.erase(last.first);
        cacheList.pop_back();
    }
}
```

Lock the cache mutex (any of the 20 threads could be calling `put` simultaneously).

Check if this key already exists — if so, erase the old entry from both the list and the map before inserting the fresh one.

`cacheList.push_front({key, value})` — adds a new node at the front of the doubly-linked list. The front is the "most recently used" position. `{"GET:example.com/about", "HTTP/1.1 200 OK\r\n..."}` is the pair stored in this node.

`cacheMap[key] = cacheList.begin()` — stores an iterator (pointer) to the new front node in the hash map. This is how future lookups find the node in O(1) time.

If the list now exceeds 200 entries, evict the back node (least recently used). `cacheList.back()` is the last node. `last.first` is its key. `cacheMap.erase(last.first)` removes it from the map. `cacheList.pop_back()` removes it from the list.

The second log entry:
```
[14:23:45] Thread-3 test2(user) | example.com | HTTP | CACHE STORED
```

---

## Phase 20 — Cleanup and Thread Return

```cpp
cout << "END handling on thread: " << threadNumber << endl;
close(remote_socket);
close(client_socket);
```

`close(remote_socket)` — closes socket `9` (proxy → `example.com`). The OS sends a TCP FIN packet to `example.com`. The connection tears down gracefully. File descriptor `9` is freed and may be reused for future sockets.

`close(client_socket)` — closes socket `7` (proxy → browser). The OS sends FIN to the browser. The browser sees the connection close cleanly and knows the response is complete. File descriptor `7` is freed.

`handleClient` returns. Thread 3's stack frame is popped. Execution returns to `workerThread()`:

```cpp
void ProxyServer::workerThread()
{
    while (true)
    {
        int client_socket;
        {
            unique_lock<mutex> lock(queueMutex);
            cv.wait(lock, [this] { return !clientQueue.empty(); });
            client_socket = clientQueue.front();
            clientQueue.pop();
        }
        handleClient(client_socket);  // ← just returned from here
    }
    // loops back up to cv.wait
}
```

Thread 3 loops back to `cv.wait()` and goes to sleep, ready for the next client.

---

## The Second Visit — The Cache Hit Path

You refresh the page. The browser sends the exact same request to the proxy. Everything from Phases 1–9 plays out identically — the main thread accepts the connection, a worker wakes up, reads the request, authenticates, checks the filter.

Then at Phase 9:

```cpp
string cachedResponse;
if (isGet && cache.get("GET:example.com/about", cachedResponse))
{
    cout << "CACHE HIT\n";
    send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);
    logger.log("Thread-7 test2(user)", "example.com", "HTTP", "CACHE HIT");
    close(client_socket);
    return;
}
```

`cache.get` finds the key. It splices the node to the front of the list (marking it most recently used), copies the response into `cachedResponse`, and returns `true`.

`send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0)` — sends the entire cached HTML page directly to the browser in one shot.

`close(client_socket)` — closes the socket. `return` — exits `handleClient`.

**Phases 10–20 are entirely skipped.** No DNS lookup, no socket creation, no TCP handshake to `example.com`, no request building, no header modification, no send, no recv loop. The browser gets the page in a fraction of the time.

Log entry:
```
[14:24:02] Thread-7 test2(user) | example.com | HTTP | CACHE HIT
```

---

## Summary: The Full HTTP Timeline

```
Browser                   Proxy (Thread 3)              example.com
   |                           |                              |
   |--- TCP SYN -----------→   |                              |
   |←-- TCP SYN-ACK ---------  |                              |
   |--- TCP ACK -----------→   |                              |
   |                           | [accept() returns fd=7]      |
   |                           | [push 7 to queue]            |
   |                           | [notify_one() wakes Thread 3]|
   |                           |                              |
   |--- GET http://...about →  |                              |
   |    Proxy-Authorization    |                              |
   |    Proxy-Connection       |                              |
   |    Connection: keep-alive |                              |
   |                           | [recv(7) reads request]      |
   |                           | [base64 decode credentials]  |
   |                           | [SHA-256 hash + compare]     |
   |                           | → role = "user"              |
   |                           |                              |
   |                           | [extractHost → example.com]  |
   |                           | [build cache key]            |
   |                           | [isBlockedForRole → false]   |
   |                           | [cache.get → miss]           |
   |                           |                              |
   |                           | [resolveHost: DNS cache miss]|
   |                           |--- DNS query ---------------→(DNS Server)
   |                           |←-- 93.184.216.34 -----------(DNS Server)
   |                           | [store in DNS cache 30s TTL] |
   |                           |                              |
   |                           |--- TCP SYN ---------------→  |
   |                           |←-- TCP SYN-ACK -----------   |
   |                           |--- TCP ACK ---------------→  |
   |                           | [connectWithTimeout OK]       |
   |                           |                              |
   |                           | [copy request]               |
   |                           | [Connection: close]          |
   |                           | [GET /about (not full URL)]  |
   |                           | [strip Proxy-Authorization]  |
   |                           | [strip Proxy-Connection]     |
   |                           | [SO_RCVTIMEO = 30s]          |
   |                           |                              |
   |                           |--- GET /about HTTP/1.1 ---→  |
   |                           |    Host: example.com         |
   |                           |    Connection: close         |
   |                           |    Accept: text/html         |
   |                           |    (credentials gone)        |
   |                           |                              |
   |                           |←-- HTTP/1.1 200 OK --------  |
   |←-- HTTP/1.1 200 OK -----  |    Content-Length: 1256      |
   |←-- (HTML streaming) ----  |←-- (HTML body) -----------  |
   |                           | [reserve(1256 + headers)]    |
   |←-- (more HTML) ---------  |←-- (more HTML) -----------  |
   |                           |                              |
   |                           |←-- TCP FIN (conn: close) --  |
   |                           | [recv returns 0, loop exits] |
   |                           |                              |
   |                           | [logger.log ALLOWED]         |
   |                           | [cache.put key→response]     |
   |                           | [logger.log CACHE STORED]    |
   |                           |                              |
   |←-- TCP FIN (close) -----  |                              |
   |                           | [close(9), close(7)]         |
   |                           | [Thread 3 → cv.wait()]       |

=== SECOND VISIT (cache hit) ===

   |--- TCP SYN -----------→   |                              |
   |←-- TCP SYN-ACK ---------  |                              |
   |--- TCP ACK -----------→   |                              |
   |--- GET http://...about →  |                              |
   |                           | [auth OK, filter OK]         |
   |                           | [cache.get → HIT!]           |
   |←-- HTTP/1.1 200 OK -----  | [send cached response]       |
   |←-- (full HTML page) ----  | [close(7)]                   |
   |                           | [Thread 7 → cv.wait()]       |
   |                           |  (DNS, TCP, send all skipped)|
```

On the first visit: authentication, filter, DNS, TCP connect, request modification, send, recv loop, caching — every phase runs.

On the second visit: authentication, filter, cache hit — done. The page arrives from memory instead of the network.