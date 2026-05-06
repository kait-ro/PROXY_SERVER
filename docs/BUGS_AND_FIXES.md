# Proxy Server — Bugs and Fixes: The Full Story

This document tells the story of every bug found in the proxy server, why it existed, what it caused, and how it was fixed. Written to be understood without deep C++ knowledge.

---

## What is this proxy server doing?

Before the bugs make sense, here is the big picture:

A **proxy server** sits between your browser and the internet. Instead of your browser talking directly to `google.com`, it talks to the proxy. The proxy checks who you are, whether you are allowed to visit that site, then either fetches the page and gives it to you, or blocks you.

This proxy specifically:
- Authenticates users (username + password)
- Enforces role-based access (admin, user, student)
- Caches pages so repeat visits are faster
- Logs everything
- Handles both plain HTTP and encrypted HTTPS traffic
- Uses 20 worker threads to serve many clients simultaneously

---

## Bug 1 — The Showstopper: HTTP Requests Were Never Sent

**Category: Critical correctness**

### What happened
When you visited any HTTP website through the proxy, the browser showed `ERR_EMPTY_RESPONSE` — a completely blank response, as if the server didn't exist.

### Why it happened
The proxy's job is:
1. Receive request from browser
2. Forward that request to the real website
3. Receive the response from the website
4. Send that response back to the browser

Step 2 was completely missing. The code opened a connection to the real website, then immediately tried to read a response from it — but never actually sent anything. It's like picking up the phone, waiting for the other person to start talking, but never saying "hello" first. The remote server just sat there waiting for a request that never came.

### The fix
One line added before the read loop:
```cpp
send(remote_socket, modifiedRequest.c_str(), modifiedRequest.size(), 0);
```
This sends the HTTP request to the remote server, so it actually knows what page you want.

---

## Bug 2 — The Cache Was Storing Garbage

**Category: Correctness**

### What happened
After Bug 1's fix, HTTP worked. But if you visited a page while the bug was active and then Bug 1 was fixed, the cache might still return a blank response for that URL.

### Why it happened
The proxy cached whatever it received — even an empty string, even an error message, even a partial response. There was no check that the thing being cached was actually a valid HTTP response.

### The fix
Before storing anything in the cache, three conditions must all be true:
1. The response is not empty
2. The response is under 100 KB (a sanity size limit)
3. The response starts with `"HTTP/"` — proof it is a real HTTP response

```cpp
if (!cacheKey.empty() && isGet &&
    fullResponse.size() > 0 &&
    fullResponse.size() < 100000 &&
    fullResponse.find("HTTP/") == 0)
```

---

## Bug 3 — Blocked Websites Could Be Accessed Via the Cache

**Category: Security**

### What happened
Imagine a student user visits `youtube.com`. The proxy blocks it and logs BLOCKED. Now an admin visits `youtube.com` — it loads fine, and the proxy caches the response. Later, the same student tries again. This time the cache check runs first, finds the cached YouTube page, and serves it — without ever checking whether the student should be blocked.

The filter had been bypassed entirely by the cache.

### Why it happened
The code was ordered: cache check → filter check. The cache always ran first.

### The fix
Swap the order. The filter check now runs before the cache:
1. Is this host blocked for this user's role? If yes → 403 Forbidden immediately.
2. Is this in the cache? If yes → serve from cache.
3. Otherwise → go fetch it from the internet.

```cpp
// Filter runs first — blocked users can't get cached content
if (filter.isBlockedForRole(host, role)) { /* 403 */ return; }

// Cache check second
if (isGet && cache.get(cacheKey, cachedResponse)) { /* serve */ return; }
```

---

## Bug 4 — Proxy Credentials Were Being Forwarded to Websites

**Category: Security**

### What happened
When your browser authenticates with the proxy, it sends a header like:
```
Proxy-Authorization: Basic dXNlcjE6cGFzczE=
```
This contains your username and password (Base64 encoded). The proxy was forwarding this header verbatim to every website you visited. So `example.com` could see your proxy credentials.

Similarly, a `Proxy-Connection:` header (meant only for the proxy) was also forwarded to origin servers that had no use for it.

### Why it happened
The proxy modified the request to change the `Connection` header, but never stripped the `Proxy-*` headers before forwarding.

### The fix
Before sending the request to the remote server, loop through and erase these headers:

```cpp
for (const string& hdr : {"Proxy-Authorization", "Proxy-Connection"})
{
    string search = "\r\n" + hdr + ":";
    size_t pos = modifiedRequest.find(search);
    while (pos != string::npos)
    {
        size_t lineEnd = modifiedRequest.find("\r\n", pos + 2);
        modifiedRequest.erase(pos, lineEnd - pos);
        pos = modifiedRequest.find(search);
    }
}
```

---

## Bug 5 — DNS Lookups Were Not Thread-Safe

**Category: Thread safety**

### What happened
With 20 threads running simultaneously, if two threads both tried to resolve a hostname at the same time using the old `gethostbyname()` function, they could corrupt each other's results. One thread might get the IP address that another thread looked up, causing connections to go to the wrong server.

### Why it happened
`gethostbyname()` is an old POSIX function that stores its result in a single global buffer. When multiple threads call it at the same time, they all write to the same buffer and overwrite each other's data. This is called a **race condition**.

The HTTPS path was using `gethostbyname`. The HTTP path was using the newer `getaddrinfo` (which is thread-safe) but still called it fresh on every single request with no caching.

### The fix
Both paths now use a custom `resolveHost()` function which:
1. Checks a thread-safe in-memory DNS cache (protected by a mutex)
2. If a cached result exists and is less than 30 seconds old, returns it immediately
3. Otherwise calls `getaddrinfo` (the safe version), stores the result in the cache, returns the IP

This also makes repeated requests to the same site much faster since DNS is only looked up once per 30 seconds.

---

## Bug 6 — Thread Detach Was Done Wrong

**Category: Correctness / Crash risk**

### What happened
The proxy creates 20 worker threads. When a C++ `std::thread` object goes out of scope without being either `join()`ed or `detach()`ed, the program crashes (`std::terminate` is called).

The code created all 20 threads, then tried to detach them in a separate loop afterwards. However, `workers` is a `vector<thread>`. When you call `emplace_back` to add a thread to the vector, the vector might need to resize itself — which involves moving all existing threads. Moving a thread invalidates the original object. If the detach loop ran after the vector had resized, it might detach moved-from (invalid) thread objects.

### The fix
Detach each thread immediately after creating it, before adding the next:

```cpp
for (int i = 0; i < THREAD_COUNT; i++)
{
    workers.emplace_back([this, i]() {
        threadNumber = i + 1;
        workerThread();
    });
    workers.back().detach();  // detach right away
}
```

---

## Bug 7 — `Connection: close` Was Replacing the Wrong Header

**Category: Protocol correctness**

### What happened
The proxy needs to tell the remote server to close the connection after responding (HTTP/1.1 defaults to keep-alive, which would cause the proxy to hang waiting for more data). So it replaces the `Connection:` header with `Connection: close`.

But the search was:
```cpp
modifiedRequest.find("Connection:");
```
This would match `Proxy-Connection:` first (since it also contains the text "Connection:"), and replace the wrong header. The `Connection: close` instruction would end up embedded in the middle of the `Proxy-Connection` header's value.

### The fix
Search for `"\r\nConnection:"` instead — the `\r\n` before it ensures only the standalone `Connection:` header is matched, not `Proxy-Connection:`.

```cpp
size_t connPos = modifiedRequest.find("\r\nConnection:");
if (connPos != string::npos)
{
    connPos += 2; // skip past the \r\n to point at "Connection:"
    size_t end = modifiedRequest.find("\r\n", connPos);
    modifiedRequest.replace(connPos, end - connPos, "Connection: close");
}
else
{
    // No Connection header exists at all — add one before the blank line
    size_t headerEnd = modifiedRequest.find("\r\n\r\n");
    if (headerEnd != string::npos)
        modifiedRequest.insert(headerEnd + 2, "Connection: close\r\n");
}
```

---

## Bug 8 — The Semaphore Did Nothing

**Category: Logic / Dead code**

### What happened
A `sem_t clientSlots` semaphore was initialized to 20 and used to "limit" concurrency. The idea was: only 20 clients at a time. But there were exactly 20 worker threads. The semaphore was acquired once per request and released once per request — and since the semaphore started at 20 and there were only 20 threads, it could never actually block anything. It was overhead with no effect.

### The fix
Remove the semaphore entirely. The thread pool already enforces the concurrency limit naturally — a thread can only handle one client at a time, so at most 20 clients run in parallel. No semaphore needed.

---

## Bug 9 — A Dead Variable Logged the Wrong Username

**Category: Logging correctness**

### What happened
There was a `bool servedFromCache = false` flag. When a cache hit occurred, it was set to `true`. Then in the log statement, the code checked this flag and used `"cached_user"` as the username if it was `true` — instead of the actual user's name.

So every cache hit was logged as if it was served to a fictional user named `"cached_user"`.

### The fix
Remove `servedFromCache` entirely. The real `username` variable is already in scope everywhere it's needed. Cache hits now log the actual username.

---

## Bug 10 — Logger Timestamps Were Verbose and Inconsistent

**Category: Quality**

### What happened
The logger was using `ctime()` to generate timestamps, which produces a string like:
```
Wed May  6 14:23:45 2026
```
This is 24 characters long and includes the day, full date, and year — far more than needed in a log file. It was also inconsistent with the format used elsewhere.

### The fix
Switch to `strftime` with `"%H:%M:%S"` to produce compact `HH:MM:SS` timestamps:
```
[14:23:45] alice(user) | example.com | HTTP | CACHE HIT
```

---

## Bug 11 — Log Label Inconsistency

**Category: Quality**

### What happened
Cache hit events were logged as `"CACHE HIT"` (with a space), but cache store events were logged as `"CACHE_STORED"` (with an underscore). Inconsistent formatting makes log parsing harder.

### The fix
Changed `"CACHE_STORED"` to `"CACHE STORED"` so all cache-related log entries use consistent space-separated labels.

---

## Bug 12 — LRU Cache Was Only 5 Entries

**Category: Performance**

### What happened
The LRU cache that stores HTTP responses was initialised with a capacity of just 5 entries:
```cpp
LRUCache cache{5};
```
After visiting 6 different pages, the cache would start evicting entries. For a proxy that handles 20 threads simultaneously, this was essentially useless.

### The fix
Increase capacity to 200:
```cpp
LRUCache cache{200};
```

---

## Bug 13 — TCP Listen Backlog Was Only 6

**Category: Performance / Reliability**

### What happened
`listen(server_fd, 6)` tells the OS: "only queue up to 6 incoming connections while I'm busy processing." If more than 6 clients try to connect at the same time, the OS will refuse them immediately with a connection reset.

For a proxy serving 20 threads, this was too small.

### The fix
```cpp
listen(server_fd, SOMAXCONN);
```
`SOMAXCONN` is a system constant (typically 128 or higher) that represents the maximum queue the OS supports. This lets the OS buffer as many incoming connections as it can handle.

---

## Bug 14 — CONNECT Port Was Always 443

**Category: Protocol correctness**

### What happened
HTTPS uses the `CONNECT` method, where the browser sends:
```
CONNECT example.com:8443 HTTP/1.1
```
The number after the colon is the port. The proxy was ignoring this port and always connecting to port 443, regardless of what the browser asked for. This broke any HTTPS service running on a non-standard port.

### The fix
Parse the port number from the CONNECT request line:
```cpp
size_t portColon = request.find(":", request.find("CONNECT ") + 8);
size_t portEnd   = request.find(" ", portColon);
int connectPort  = 443;
if (portColon != string::npos && portEnd != string::npos)
    connectPort = stoi(request.substr(portColon + 1, portEnd - portColon - 1));
```

---

## Bug 15 — No Connect Timeout (Blocked Sites Froze Threads)

**Category: Performance / Reliability**

### What happened
`connect()` is a blocking call — it waits for the remote server to respond. If the server is unreachable (firewall drops packets, server is down), the default TCP connect timeout on most systems is about 75–130 seconds. During this entire time, the worker thread is frozen, unable to serve any other client.

With 20 threads and an aggressive user hitting blocked sites, this could freeze the entire thread pool.

### Why blocked sites caused slow loads
Here is the sequence that was observed: a student visits a blocked site → proxy sends 403 quickly → student's browser might still try to preload resources → those resource requests might slip through before the filter or get queued → threads start connecting to the blocked server which is slow → subsequent legitimate requests queue up behind frozen threads.

### The fix
Use a non-blocking `connect()` with `select()` to enforce a 10-second limit:

```cpp
static bool connectWithTimeout(int fd, const struct sockaddr* addr, socklen_t addrlen)
{
    // Set socket to non-blocking mode
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(fd, addr, addrlen);
    if (ret == 0) { fcntl(fd, F_SETFL, flags); return true; }      // connected instantly
    if (errno != EINPROGRESS) { fcntl(fd, F_SETFL, flags); return false; }  // hard failure

    // Wait up to 10 seconds for the connection to complete
    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);
    struct timeval tv = {CONNECT_TIMEOUT_SEC, 0};
    ret = select(fd + 1, NULL, &writefds, NULL, &tv);
    if (ret <= 0) { fcntl(fd, F_SETFL, flags); return false; }     // timeout

    // Check if the connection actually succeeded
    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    fcntl(fd, F_SETFL, flags);
    return err == 0;
}
```

---

## Bug 16 — recv Timeout Was 5 Seconds (Too Aggressive)

**Category: Performance**

### What happened
After connecting to a remote server, `SO_RCVTIMEO` was set to 5 seconds. This means: if no data arrives from the remote server within 5 seconds, abort. For fast servers like `example.com` this is fine. For slower servers, CDNs, or servers with large responses, 5 seconds is not enough — the proxy would time out and return a broken response mid-transfer.

### The fix
Increased to 30 seconds:
```cpp
#define RECV_TIMEOUT_SEC 30
// ...
timeout.tv_sec = RECV_TIMEOUT_SEC;
```

---

## Summary Table

| # | Bug | Category | Impact |
|---|-----|----------|--------|
| 1 | HTTP request never sent to remote server | Critical | Nothing worked at all |
| 2 | Empty/invalid responses stored in cache | Correctness | Cached failures replayed |
| 3 | Filter check after cache check | Security | Blocked users bypassed filter |
| 4 | Proxy credentials forwarded to origin | Security | Password leak to websites |
| 5 | `gethostbyname` not thread-safe | Thread safety | Race condition, wrong IPs |
| 6 | Thread detach done after loop | Crash risk | Potential crash on resize |
| 7 | `Connection:` search matched `Proxy-Connection:` | Protocol | Malformed forwarded headers |
| 8 | Semaphore initialized == thread count | Logic | Semaphore never blocked, pure overhead |
| 9 | `servedFromCache` flag used wrong username | Logging | All cache hits logged as "cached_user" |
| 10 | Logger used `ctime()` verbose timestamp | Quality | Ugly, inconsistent log format |
| 11 | `CACHE_STORED` vs `CACHE HIT` label mismatch | Quality | Inconsistent log labels |
| 12 | LRU cache capacity was 5 | Performance | Cache evicted after 5 pages |
| 13 | TCP listen backlog was 6 | Reliability | OS refused connections under load |
| 14 | CONNECT port hardcoded to 443 | Protocol | Non-standard HTTPS ports broken |
| 15 | No connect timeout | Reliability | Thread frozen up to 2 minutes |
| 16 | recv timeout was 5 seconds | Performance | Slow servers timed out mid-response |
| + | DNS lookup per request (no cache) | Performance | Extra syscall on every request |
