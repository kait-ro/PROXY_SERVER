# The Complete Journey of an HTTPS Request Through the Proxy

This document traces a single HTTPS request — visiting `https://github.com` — from the moment the browser decides to connect, all the way through every function in the proxy, until the page is fully loaded and all sockets are closed.

It explains every concept, every line, every detail.

---

## Why HTTPS Is Different From HTTP

Before the journey begins, you need to understand one fundamental thing: **the proxy cannot read HTTPS traffic**.

When you visit `https://github.com`, your browser and GitHub negotiate a private encrypted channel using a protocol called TLS (Transport Layer Security). Every byte sent between your browser and GitHub is encrypted with keys that only those two parties know. The proxy is in the middle but it is completely blind — it sees only scrambled, unreadable ciphertext.

So the proxy cannot do what it does for HTTP requests (read the URL, modify headers, cache the response). Instead, it does something simpler and more humble: it opens a direct TCP pipe between your browser and the remote server, and forwards raw bytes in both directions without ever reading them.

This is called **tunnelling** or a **CONNECT tunnel**. The proxy's job is just to make a direct plumbing connection and then step back.

---

## Phase 1 — The Browser Decides It Needs a Proxy

You type `https://github.com` in your browser. The browser is configured to use the proxy at `127.0.0.1:8080`.

For a normal website (no proxy), the browser would directly connect to `github.com:443` and start TLS. But with a proxy configured, it cannot do that — it doesn't know the path to `github.com` directly. It must ask the proxy to make the connection on its behalf.

The browser does not send a `GET` or `POST` request for HTTPS. Instead it sends a special request using the `CONNECT` method. This is a standard HTTP method specifically designed for proxied tunnelling.

The browser opens a TCP connection to `127.0.0.1:8080` (your proxy) and sends:

```
CONNECT github.com:443 HTTP/1.1\r\n
Host: github.com:443\r\n
Proxy-Authorization: Basic dGVzdDI6cGFzcw==\r\n
Proxy-Connection: keep-alive\r\n
\r\n
```

What this is saying: "Proxy, please open a TCP connection to `github.com` on port 443. Once you have done that, send me confirmation and I will start sending TLS data through you."

Notice there is no page content in this request. The browser has not started TLS yet. TLS will only begin after the proxy confirms the tunnel is open.

---

## Phase 2 — The OS Hands the Connection to the Proxy

The operating system has been listening on port 8080 on behalf of the proxy (because `listen()` was called). When the browser connects, the OS completes the three-way TCP handshake automatically:

1. Browser → OS: SYN ("I want to connect")
2. OS → Browser: SYN-ACK ("Okay, connecting")
3. Browser → OS: ACK ("Confirmed")

The OS then places this completed connection into a queue. In `startServer()`, the main thread is sitting in `accept()`, which is blocked (sleeping) waiting for exactly this moment.

```cpp
int client_socket = accept(server_fd, NULL, NULL);
```

`accept()` wakes up and returns a new integer — let's say `7`. This is the **client socket file descriptor**. It is a handle the proxy uses to talk to the browser. The original `server_fd` (the listening socket) stays open, ready for the next browser connection.

The main thread does not handle the client itself. It immediately hands the socket to the worker thread pool:

```cpp
{
    unique_lock<mutex> lock(queueMutex);
    clientQueue.push(7);          // push socket 7
}
cv.notify_one();
```

`queueMutex` is locked so that no other thread can access `clientQueue` at this exact moment (preventing data corruption). The socket integer `7` is pushed onto the queue. The mutex is released when the `unique_lock` goes out of scope (end of the `{ }` block). Then `cv.notify_one()` wakes up exactly one sleeping worker thread.

---

## Phase 3 — A Worker Thread Picks Up the Client

There are 20 worker threads, each running `workerThread()` in an infinite loop. They spend most of their time sleeping inside `cv.wait()`. When `notify_one()` fires, one of them wakes up.

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

Say Thread 5 wakes up. It locks `queueMutex`, checks that the queue is not empty (it isn't — socket `7` is there), pops `7` off the front, and releases the lock. Now Thread 5 owns socket `7` and calls `handleClient(7)`.

Releasing the lock before `handleClient` is intentional and important. `handleClient` can take several seconds (waiting for the remote server). If the lock were held the whole time, no other thread could pick up new clients during that period.

---

## Phase 4 — Reading the Browser's Request

```cpp
void ProxyServer::handleClient(int client_socket)
{
    vector<char> buffer(BUFFER_SIZE);    // 65536 byte buffer
    int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
    if (bytes <= 0) return;
    string request(buffer.data(), bytes);
```

`recv()` reads data from socket `7` into the buffer. It returns the number of bytes actually received. For this CONNECT request, `bytes` might be around 150 (a typical CONNECT request is small — just headers, no body).

`string request(buffer.data(), bytes)` constructs a string from exactly those `bytes` characters. The string now holds the full CONNECT request text:

```
CONNECT github.com:443 HTTP/1.1\r\nHost: github.com:443\r\nProxy-Authorization: Basic dGVzdDI6cGFzcw==\r\n...
```

There is one special shortcut here:

```cpp
if (request.find("CONNECT") != 0)
{
    cout << "\nREQUEST RECEIVED:\n" << endl;
}
```

This suppresses the verbose print for CONNECT requests. `find("CONNECT") != 0` is true for HTTP requests (which start with GET, POST, etc.) but false for CONNECT requests. So the detailed print only happens for HTTP, not for the high-frequency HTTPS CONNECT tunnel setups.

---

## Phase 5 — Authentication

Before doing anything else, the proxy verifies the user's identity. It does not matter what type of request this is — CONNECT or GET — authentication always comes first.

```cpp
string username = "";
string role = "";

size_t authPos = request.find("Proxy-Authorization: Basic ");
```

`find` scans through the `request` string looking for the text `"Proxy-Authorization: Basic "`. If found, it returns the position (a number like 42). If not found, it returns `string::npos` — a special value meaning "not found".

Since the browser included the header, `authPos != string::npos` and we enter the authentication block:

```cpp
size_t start = request.find("Basic ") + 6;
size_t end = request.find("\r\n", start);
if (end == string::npos) end = request.size();

string encoded = request.substr(start, end - start);
```

`"Basic "` is 6 characters. Adding 6 to its position lands us on the first character of the Base64 string (`d` in `dGVzdDI6cGFzcw==`). `find("\r\n", start)` finds the end of the header line. `substr(start, end - start)` cuts out exactly the Base64 string.

The `if (end == string::npos) end = request.size()` is a safety guard. If the request is malformed and has no `\r\n`, we use the end of the string instead. Without this guard, `end - start` would be a massive number (unsigned integer wraparound), and `substr` would try to read gigabytes of memory, crashing the process.

```cpp
encoded.erase(encoded.find_last_not_of(" \r\n\t") + 1);
encoded.erase(0, encoded.find_first_not_of(" \r\n\t"));
```

Two lines to trim whitespace from both ends of the encoded string. Networks sometimes introduce stray spaces. This ensures the Base64 decoder gets clean input.

```cpp
string decoded = base64Decode(encoded);
```

`dGVzdDI6cGFzcw==` becomes `"test2:pass"`. Base64 is not encryption — it is just an encoding. Anyone can decode it. The security comes from the fact that the connection itself should be over a trusted local network (since this is a LAN proxy).

```cpp
size_t colon = decoded.find(':');
if (colon != string::npos)
{
    string user = decoded.substr(0, colon);       // "test2"
    string pass = decoded.substr(colon + 1);      // "pass"
    role = auth.login(user, pass);
    username = user;
}
```

Split on the colon. `substr(0, colon)` = everything before the colon = username. `substr(colon + 1)` = everything after = password.

`auth.login("test2", "pass")` hashes `"pass"` with SHA-256 and compares it to the stored hash in `users.txt`. If they match, it returns `"user"` (the role). If wrong, it returns `""`.

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

If authentication fails, we send back HTTP 407. The `Proxy-Authenticate` header tells the browser which authentication scheme to use (`Basic`) and the realm name (`"Proxy"` — just a label that may appear in the browser's login dialog). The browser will show a popup asking for credentials and retry.

Assuming authentication succeeds, `username = "test2"` and `role = "user"`.

---

## Phase 6 — Identifying This as a CONNECT Request

```cpp
if (request.find("CONNECT") == 0)
{
```

`find("CONNECT")` returns 0 because the string starts with `"CONNECT"`. The entire HTTPS handling block is inside this `if`.

```cpp
string host = extractHost(request);
cout << "HTTPS CONNECT to: " << host << endl;
```

`extractHost` handles CONNECT requests specially:

```cpp
if (request.find("CONNECT") == 0)
{
    size_t start = request.find("CONNECT ") + 8;
    size_t end = request.find(":", start);
    if (end == string::npos) return "";
    return request.substr(start, end - start);
}
```

`"CONNECT "` is 8 characters. Adding 8 skips past it, landing on `g` of `github.com:443`. `find(":", start)` finds the colon between the hostname and port. `substr(start, end - start)` extracts just `"github.com"` — not the port number.

`host` is now `"github.com"`.

---

## Phase 7 — The Website Filter

```cpp
if (filter.isBlockedForRole(host, role))
{
    cout << "Blocked HTTPS site: " << host << endl;
    string response =
    "HTTP/1.1 403 Forbidden\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 18\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Blocked by proxy";
    send(client_socket, response.c_str(), response.length(), 0);
    logger.log("Thread-5 test2(user)", "github.com", "HTTPS", "BLOCKED");
    close(client_socket);
    return;
}
```

`isBlockedForRole("github.com", "user")` normalises the domain (lowercase, strip www) and checks it against the blocked sets. `github.com` is not blocked for users, so this returns `false` and we skip the block.

If it WERE blocked, we send 403 Forbidden to the browser. This works even for HTTPS because we are responding to the CONNECT request itself — we have not started TLS yet. The browser receives the 403 before any encryption handshake, so it can read it and display "this site is blocked by your proxy".

This is an important timing detail: the filter runs before we connect to the remote server. No connection to `github.com` is ever made for a blocked request.

```cpp
logger.log("Thread-5 test2(user)", "github.com", "HTTPS", "ALLOWED");
```

Log the allowed access. The logger locks its own mutex, gets the current time, and writes a line like:

```
[14:23:45] Thread-5 test2(user) | github.com | HTTPS | ALLOWED
```

---

## Phase 8 — Parsing the Port Number

```cpp
int connectPort = 443;
{
    size_t portColon = request.find(":", request.find("CONNECT ") + 8);
    size_t portEnd   = request.find(" ", portColon);
    if (portColon != string::npos && portEnd != string::npos)
        connectPort = stoi(request.substr(portColon + 1, portEnd - portColon - 1));
}
```

The CONNECT line is `CONNECT github.com:443 HTTP/1.1`.

- `request.find("CONNECT ") + 8` = position of `g` in `github.com`
- `request.find(":", ...)` starting from that position = position of `:` between host and port
- `request.find(" ", portColon)` = position of space after `443`

So `portColon` points to `:` and `portEnd` points to the space after `443`.

`request.substr(portColon + 1, portEnd - portColon - 1)`:
- Start: `portColon + 1` skips the colon itself
- Length: `portEnd - portColon - 1` is the number of characters between the colon and the space (i.e. `"443"`)

`stoi("443")` converts the string to the integer 443. `connectPort` is now 443.

The port defaults to 443 if parsing fails. But it could be any number — `CONNECT api.example.com:8443 HTTP/1.1` would give port 8443, which was broken before this fix.

The inner `{ }` braces are just for organisation — they limit the scope of `portColon` and `portEnd` so they don't linger in the enclosing scope.

---

## Phase 9 — DNS Resolution (With Caching)

The proxy needs to know the IP address of `github.com` to open a TCP connection to it. The IP address is not in the CONNECT request — only the hostname.

```cpp
string httpsIP = resolveHost(host);
```

Inside `resolveHost`:

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

First check the DNS cache. `dnsCacheMutex` is locked so that two threads looking up the same hostname at the same time don't corrupt the cache. `dnsCache.find("github.com")` looks up the hostname. `it->second.second` is the expiry timestamp. `time(nullptr)` is the current Unix timestamp. If the cached entry exists and has not expired (current time < expiry), return the cached IP immediately and skip the DNS lookup entirely.

If this is the first request to `github.com` (or the cache has expired):

```cpp
    struct addrinfo hints{}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0)
        return "";
```

`getaddrinfo("github.com", nullptr, &hints, &res)` is the actual DNS lookup. It sends a query to your configured DNS server (usually your router), waits for a response, and writes the result into `res`. This is a network call — it takes time, typically 5–50 milliseconds.

`AF_INET` restricts the results to IPv4. `SOCK_STREAM` hints that we want TCP-compatible addresses.

If DNS fails (the hostname doesn't exist, DNS server is unreachable), it returns non-zero and we return an empty string.

```cpp
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr,
              ip, sizeof(ip));
    freeaddrinfo(res);
```

`res->ai_addr` is a pointer to a `sockaddr` — a generic address structure. We cast it to `sockaddr_in` (the IPv4-specific version) using `reinterpret_cast`. Then we access `sin_addr` — the binary IPv4 address (4 bytes). `inet_ntop` converts those 4 bytes into the readable string like `"140.82.121.4"` and writes it into `ip`.

`freeaddrinfo(res)` releases the memory that `getaddrinfo` allocated. If this were forgotten, it would be a memory leak — a small amount of RAM lost every request.

```cpp
    string ipStr(ip);
    {
        lock_guard<mutex> lock(dnsCacheMutex);
        dnsCache["github.com"] = {"140.82.121.4", time(nullptr) + 30};
    }
    return ipStr;
```

Lock the mutex again (now for writing) and store the result. The expiry is `time(nullptr) + 30` — the current time plus 30 seconds. Any thread asking for `github.com` within the next 30 seconds gets this cached result instantly, no DNS network call.

Back in `handleClient`, `httpsIP = "140.82.121.4"`.

If DNS had failed: `httpsIP` would be `""`. The proxy would send `502 Bad Gateway` to the browser and return.

---

## Phase 10 — Opening a TCP Connection to GitHub

Now the proxy opens its own TCP connection to `github.com:443`. This is separate from the browser's connection — the proxy is simultaneously a server (to the browser) and a client (to GitHub).

```cpp
int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
```

`socket(AF_INET, SOCK_STREAM, 0)` creates a new TCP socket. Returns a file descriptor integer, say `9`. This is now the **remote socket** — the proxy's connection to GitHub.

```cpp
struct sockaddr_in httpsAddr{};
httpsAddr.sin_family = AF_INET;
httpsAddr.sin_port   = htons(connectPort);
inet_pton(AF_INET, httpsIP.c_str(), &httpsAddr.sin_addr);
```

Build the address structure for `github.com:443`:

- `sin_family = AF_INET` — marks this as an IPv4 address
- `sin_port = htons(443)` — port 443 in network byte order. `htons` ("host to network short") converts from your CPU's native integer format to the big-endian format required by network protocols. On most modern CPUs (little-endian), 443 in memory is `0xBB 0x01`, but on the network it must be `0x01 0xBB`. `htons` does this swap.
- `inet_pton(AF_INET, "140.82.121.4", &httpsAddr.sin_addr)` — converts the IP string to 4 binary bytes and writes them into `sin_addr`

```cpp
if (!connectWithTimeout(remote_socket, (sockaddr*)&httpsAddr, sizeof(httpsAddr)))
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

`connectWithTimeout` initiates the TCP connection with a maximum 10-second wait:

1. `fcntl(fd, F_GETFL, 0)` reads the current socket flags
2. `fcntl(fd, F_SETFL, flags | O_NONBLOCK)` sets non-blocking mode — `connect()` will return immediately instead of blocking
3. `connect(remote_socket, ...)` starts the TCP handshake (SYN packet sent to GitHub)
4. `connect()` returns -1 with `errno == EINPROGRESS` — meaning "handshake started, not done yet"
5. `select(remote_socket + 1, NULL, &writefds, NULL, &tv)` waits up to 10 seconds for the socket to become writable — which happens when the handshake completes (GitHub sends back SYN-ACK and our OS sends ACK)
6. `getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen)` checks if the connection actually succeeded or failed
7. `fcntl(fd, F_SETFL, flags)` restores blocking mode
8. Returns `true` if `err == 0` (success)

If `connectWithTimeout` returns `false` (GitHub didn't respond within 10 seconds, or actively refused the connection), the proxy sends `504 Gateway Timeout` to the browser and cleans up.

Assuming success, we now have two open sockets:
- Socket `7` — the browser's connection to the proxy
- Socket `9` — the proxy's connection to GitHub

---

## Phase 11 — Announcing the Tunnel is Open

```cpp
string response = "HTTP/1.1 200 Connection Established\r\n\r\n";
send(client_socket, response.c_str(), response.length(), 0);
cout << "Tunnel established successfully\n";
```

The proxy sends `200 Connection Established` back to the browser through socket `7`. This is the proxy's way of saying: "I have opened a TCP connection to `github.com:443`. You can now start sending TLS data directly — I will forward every byte."

The `\r\n\r\n` at the end is the blank line that signals the end of HTTP headers. There is no body — this response is just a status line and the blank line.

**This is the last plain-text HTTP message in this connection.** Everything that comes after this — in both directions — is raw TLS bytes that the proxy cannot read.

---

## Phase 12 — The Browser Starts TLS

Upon receiving `200 Connection Established`, the browser immediately begins the TLS handshake with GitHub. From the proxy's perspective, this looks like a stream of binary data arriving on socket `7` (from the browser) that must be forwarded to socket `9` (to GitHub), and data arriving on socket `9` that must be forwarded to socket `7`.

The proxy enters the tunnel loop:

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

`FD_ZERO(&fds)` clears the `fd_set` bitmask (all bits to zero). `FD_SET(7, &fds)` sets bit 7. `FD_SET(9, &fds)` sets bit 9. Now `fds` represents "watch sockets 7 and 9".

`max(7, 9) + 1 = 10`. This is the range `select()` checks (fds 0 through 9).

`select(10, &fds, NULL, NULL, NULL)` — waits indefinitely (the last `NULL` means no timeout) for either socket 7 or socket 9 to have data ready to read. The middle `NULL` arguments are for write-readiness and error sets — we don't need those here.

`select()` returns the number of ready file descriptors. If it returns 0 (timeout — but we have no timeout so this can't happen) or -1 (error), we break out of the loop.

When `select()` returns a positive number, `fds` has been modified — only the bits for sockets that are actually ready to read remain set. We check each one:

```cpp
    if (FD_ISSET(client_socket, &fds))
    {
        int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
        if (bytes <= 0) break;
        send(remote_socket, buffer.data(), bytes, 0);
    }
```

`FD_ISSET(7, &fds)` — is socket 7 (browser) ready to read? If yes, `recv()` reads up to 65536 bytes of TLS data from the browser. `send(9, ...)` forwards those exact bytes to GitHub. No modification, no inspection — pure forwarding.

```cpp
    if (FD_ISSET(remote_socket, &fds))
    {
        int bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0);
        if (bytes <= 0) break;
        send(client_socket, buffer.data(), bytes, 0);
    }
}
```

`FD_ISSET(9, &fds)` — is socket 9 (GitHub) ready to read? If yes, read from GitHub and forward to the browser.

Note that both checks can be true in the same `select()` call — if both sockets have data simultaneously, we handle both before calling `select()` again.

---

## Phase 13 — What Travels Through the Tunnel

To understand the tunnel, here is what actually passes through it (even though the proxy cannot read it):

**Browser → Proxy → GitHub (forwarded as-is):**

1. **TLS ClientHello** — The browser announces which TLS version and encryption ciphers it supports
2. **TLS certificates** — GitHub sends its public certificate proving its identity
3. **Key exchange** — Browser and GitHub negotiate a shared secret key using asymmetric cryptography
4. **Encrypted HTTP/2 or HTTP/1.1** — Once the TLS session is established, the actual HTTP request (`GET / HTTP/2`) travels encrypted

**GitHub → Proxy → Browser (forwarded as-is):**

1. **TLS ServerHello** — GitHub's response to the ClientHello
2. **GitHub's TLS certificate**
3. **Encrypted HTTP response** — The actual HTML, CSS, JavaScript of the GitHub page

The proxy forwards all of this blindly. It is a pure TCP relay — it does not know TLS, does not have GitHub's private key, and cannot decrypt any of it. This is why HTTPS provides end-to-end privacy even when a proxy is in the path.

---

## Phase 14 — The Loop Runs Until the Page Loads

The tunnel loop runs continuously while you use the page. Every image, script, API call, WebSocket message — all of it flows through this loop. The loop iteration is:

1. `select()` blocks waiting for data
2. Data arrives on one or both sockets
3. `recv()` reads it
4. `send()` forwards it
5. Go back to step 1

This is highly efficient. The thread is not spinning in a busy-wait — it genuinely sleeps inside `select()` (surrendering the CPU) until there is work to do. Other threads can use the CPU freely during this time.

---

## Phase 15 — Connection Close and Cleanup

When you close the GitHub tab, the browser closes its TCP connection to the proxy. This causes `recv(client_socket, ...)` to return 0. Zero means "the other side has closed the connection cleanly — there is no more data." The condition `if (bytes <= 0) break` triggers and we exit the while loop.

Similarly, if GitHub closes its connection first (server timeout, server shutdown), `recv(remote_socket, ...)` returns 0 and the loop also breaks.

```cpp
close(remote_socket);   // close socket 9 (proxy → GitHub)
close(client_socket);   // close socket 7 (proxy → browser)
return;
```

`close()` on a socket does several things:
1. Sends a TCP FIN packet to the other side ("I am done sending")
2. Waits for the other side's FIN acknowledgement
3. Releases the file descriptor number (7 and 9 are now free for future connections)
4. Frees the OS resources for these connections

The `return` at the end exits `handleClient`. Thread 5's stack frame is popped. Thread 5 returns to `workerThread()` and loops back to `cv.wait()`, where it goes to sleep again, ready to handle the next client.

---

## Phase 16 — What the Logs Show

When this entire journey completes, `proxy.log` has one entry:

```
[14:23:45] Thread-5 test2(user) | github.com | HTTPS | ALLOWED
```

Notice what is NOT in the log: no URL path, no response size, no status code. The proxy only knows the hostname — it never saw any of the actual content. This is intentional and correct behaviour for an HTTPS proxy.

---

## Summary: The Full HTTPS Timeline

```
Browser                    Proxy (Thread 5)              GitHub
   |                            |                            |
   |--- TCP SYN --------------→ |                            |
   |←-- TCP SYN-ACK ----------- |                            |
   |--- TCP ACK --------------→ |                            |
   |                            |                            |
   |--- CONNECT github.com:443 →|                            |
   |    (with credentials)      |                            |
   |                            |-- DNS: github.com? -------→(DNS Server)
   |                            |←- IP: 140.82.121.4 -------(DNS Server)
   |                            |                            |
   |                            |--- TCP SYN -------------→ |
   |                            |←-- TCP SYN-ACK ---------- |
   |                            |--- TCP ACK -------------→ |
   |                            |  (connectWithTimeout)      |
   |                            |                            |
   |←-- 200 Connection ---------| (tunnel open)             |
   |    Established             |                            |
   |                            |                            |
   |=== TLS ClientHello ======→ |========= forwarded ======→|
   |←== TLS ServerHello ======= |←======== forwarded =======|
   |=== TLS Certificate ======→ |========= forwarded ======→|
   |←== TLS Certificate ======= |←======== forwarded =======|
   |=== Key Exchange =========→ |========= forwarded ======→|
   |←== Key Exchange ========== |←======== forwarded =======|
   |                            |  (TLS handshake complete)  |
   |=== GET / (encrypted) ====→ |========= forwarded ======→|
   |←== 200 OK (encrypted) ===== |←======== forwarded =======|
   |←== HTML/CSS/JS (enc.) ===== |←======== forwarded =======|
   |                            |                            |
   |--- FIN ------------------→ |                            |
   |                            |--- FIN -----------------→ |
   |                            |←-- FIN-ACK -------------- |
   |←-- FIN-ACK --------------- |                            |
   |                            |  close(remote_socket)      |
   |                            |  close(client_socket)      |
   |                            |  Thread 5 → cv.wait()      |
```

The proxy's role in the HTTPS path is entirely in the setup phase (phases 1–11). Once the tunnel is open, the proxy is invisible — it is a transparent wire, and all the security and privacy of HTTPS is fully preserved.
