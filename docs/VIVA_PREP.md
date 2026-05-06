# Proxy Server — Viva Preparation

Questions are grouped from basic to advanced. Know the basic ones cold. The advanced ones show depth if asked.

---

## 1. Fundamentals — "What does this project do?"

**Q: What is a proxy server?**
A: A proxy server is an intermediary — it sits between a client (browser) and the internet. The client connects to the proxy, the proxy connects to the destination on the client's behalf, and the response comes back the same way. This allows the proxy to inspect, filter, log, cache, and authenticate traffic.

**Q: What is the difference between a forward proxy and a reverse proxy?**
A: A forward proxy (what this is) sits in front of clients — the clients know they are using a proxy and are configured to use it. A reverse proxy sits in front of servers — clients think they are talking directly to the server, but it is actually the proxy. Reverse proxies are used for load balancing, CDNs, etc.

**Q: Why would a network administrator deploy a proxy server?**
A: Several reasons: access control (block social media at work), authentication (require login before browsing), logging/auditing (track what users visit), caching (speed up repeated requests and reduce bandwidth), and security (strip sensitive headers, centralise traffic inspection).

---

## 2. Architecture — "How is it built?"

**Q: How does the server handle multiple clients at the same time?**
A: It uses a thread pool. 20 worker threads are created at startup and kept alive permanently. The main thread runs `accept()` in a loop, gets a new client socket, pushes it onto a shared queue, and signals the workers via a `condition_variable`. One worker wakes up, pops the socket, handles the client fully, then waits for the next one. This avoids the overhead of creating a new thread per request.

**Q: Why use a thread pool instead of one thread per request?**
A: Thread creation is expensive — it takes time and memory. If 1000 clients connected simultaneously, spawning 1000 threads would exhaust system resources. A thread pool bounds the number of threads to a fixed number (20 here), which is predictable and controllable.

**Q: What is a condition variable and why do you use it?**
A: A `condition_variable` lets threads sleep efficiently while waiting for work. Without it, threads would need to spin-check the queue in a loop (busy-waiting), wasting CPU. With a condition variable, worker threads call `cv.wait()` which atomically releases the mutex and puts the thread to sleep. When the main thread calls `cv.notify_one()`, exactly one sleeping thread wakes up.

**Q: What is the role of the mutex in the thread pool?**
A: The client socket queue is shared between the main thread (producer) and worker threads (consumers). Without a mutex, two threads could try to pop from the queue simultaneously and corrupt it. The mutex ensures that only one thread modifies the queue at a time.

---

## 3. HTTP vs HTTPS — "How do you handle both?"

**Q: How does the proxy handle plain HTTP traffic?**
A: The browser sends the full HTTP request including the `Host` header and an absolute URL (e.g. `GET http://example.com/page HTTP/1.1`). The proxy extracts the hostname, connects to port 80 on that server, rewrites the request to use a relative URL (`GET /page HTTP/1.1`), sets `Connection: close`, strips proxy-specific headers, then forwards it. It reads the response and sends it back to the browser.

**Q: How does the proxy handle HTTPS traffic?**
A: HTTPS uses the `CONNECT` method. The browser sends `CONNECT example.com:443 HTTP/1.1`. The proxy establishes a TCP connection to `example.com:443`, then responds `200 Connection Established`. After that, the proxy acts as a transparent tunnel — it copies bytes between the browser and the server in both directions using `select()`. It never decrypts or reads the HTTPS content.

**Q: Why can't you cache HTTPS responses?**
A: Because the proxy never sees the content — the TLS encryption happens end-to-end between the browser and the origin server. The proxy is just passing encrypted bytes, so there is nothing to cache or inspect.

**Q: What is the `CONNECT` method?**
A: It is a special HTTP method used to ask a proxy to establish a TCP tunnel. The proxy connects to the requested host and port, then becomes a bidirectional byte pipe. It was designed specifically for HTTPS proxying.

---

## 4. Authentication — "How do users log in?"

**Q: How does authentication work in HTTP proxying?**
A: HTTP proxy authentication uses the `Proxy-Authorization` header. The browser encodes `username:password` in Base64 and sends it as `Proxy-Authorization: Basic dXNlcjE6cGFzczE=`. The proxy decodes this, hashes the password with SHA-256, and compares it to the stored hash in `users.txt`.

**Q: Why Base64 and not just plain text?**
A: Base64 is not encryption — it is encoding. It just converts binary data to printable ASCII characters. The reason HTTP uses it is that headers must be ASCII text, and passwords may contain special characters. Base64 ensures safe transmission over the text protocol. It provides no security on its own.

**Q: How are passwords stored securely?**
A: Passwords are hashed with SHA-256 before storage. The `users.txt` file contains only hashes, never plain text. When a user tries to log in, their submitted password is hashed and the hashes are compared. An attacker who reads `users.txt` cannot easily recover the original passwords.

**Q: What is HTTP 407?**
A: `407 Proxy Authentication Required`. The proxy sends this with a `Proxy-Authenticate: Basic realm="Proxy"` header when no credentials are provided. The browser sees this and shows a login dialog.

---

## 5. Caching — "How does caching work?"

**Q: What is an LRU cache?**
A: LRU stands for Least Recently Used. It is a cache with a fixed capacity. When the cache is full and a new item needs to be stored, it evicts the item that was accessed least recently — the assumption being that recently used items are more likely to be needed again. Implementation uses a doubly-linked list (for O(1) move-to-front) and a hash map (for O(1) lookup).

**Q: What types of requests do you cache, and why only those?**
A: Only GET requests are cached. GET requests are defined in the HTTP spec as safe and idempotent — they retrieve data without changing server state, and repeating them should return the same result. POST, PUT, DELETE requests may have side effects, so caching them would be incorrect.

**Q: How do you form the cache key?**
A: The key is `GET:` + normalised hostname + path. Normalisation strips `www.`, lowercases the domain, and extracts just the path portion of the URL. This ensures `http://www.Example.com/page` and `http://example.com/page` hit the same cache entry.

**Q: What checks do you perform before caching a response?**
A: Three checks: the response must be non-empty, it must start with `"HTTP/"` (to confirm it is a valid HTTP response and not a partial or garbage read), and it must be under 100 KB (to prevent large files from consuming all cache capacity).

**Q: Why is it important that the filter check runs before the cache check?**
A: If the cache check ran first, a blocked user could access a URL that a permitted user had previously cached. The filter must run before the cache so that access control is always enforced, regardless of cache state.

---

## 6. Website Filtering — "How does blocking work?"

**Q: How does the website filter work?**
A: At startup, `WebsiteFilter` reads blocked site lists from text files into `unordered_set`. For each request, it normalises the requested hostname (strip `www.`, lowercase) and looks it up in the set. It also checks parent domains — so if `evil.com` is blocked, `sub.evil.com` is also blocked.

**Q: What are the three roles and their filter rules?**
A:
- `admin`: no restrictions, bypasses filter entirely
- `user`: blocked from global blocked list + gaming sites + social media sites
- `student`: blocked only from global blocked list (not category lists)

**Q: Where do the site lists come from?**
A: From text files: `blocked_sites.txt`, `ai_blocked_sites.txt`, `ai_gaming.txt`, `ai_social.txt`. They are loaded once at startup. Adding or removing a site requires only editing the text file and restarting the proxy.

---

## 7. Thread Safety — "What could go wrong with threads?"

**Q: What is a race condition?**
A: A race condition is when the correctness of a program depends on the order in which two or more threads execute, and that order is not guaranteed. For example, if two threads both read a variable, increment it, and write it back simultaneously, one increment may be lost because both threads read the original value before either writes back.

**Q: What is a mutex?**
A: Mutex stands for mutual exclusion. It is a lock that only one thread can hold at a time. When a thread locks a mutex, any other thread that tries to lock it will block (sleep) until the first thread unlocks it. This ensures only one thread accesses the protected resource at a time.

**Q: Why was `gethostbyname` unsafe in this project?**
A: `gethostbyname` stores its result in a single static (global) buffer shared across all calls in the process. If two threads call it simultaneously, the second thread's result can overwrite the first thread's result before the first thread has finished reading it. The fix is `getaddrinfo`, which stores its result in a caller-allocated structure, so each thread has its own result buffer.

**Q: What data structures in your code require mutex protection?**
A: Three:
1. The client socket queue (`queueMutex`) — shared between the main thread and 20 workers
2. The DNS cache (`dnsCacheMutex`) — written by any thread that resolves a new hostname
3. The log file (`logMutex` in `Logger`) — any thread can log at any time

---

## 8. Sockets and Networking — "How do sockets work?"

**Q: What is a socket?**
A: A socket is an endpoint for network communication. In code, it is a file descriptor (integer) that represents a connection. You can `send()` and `recv()` data through it just like writing to and reading from a file.

**Q: Walk me through the TCP handshake for an incoming connection.**
A: The server calls `socket()` to create a socket, `bind()` to assign it an IP and port, `listen()` to mark it as a server socket, and `accept()` which blocks until a client connects, then returns a new socket for that specific client. The client calls `socket()` and `connect()`. The OS handles the three-way TCP handshake (SYN, SYN-ACK, ACK) automatically.

**Q: What does `SO_RCVTIMEO` do?**
A: It sets a receive timeout on a socket. After `setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, ...)`, any `recv()` on that socket will return an error if no data arrives within `tv` seconds. This prevents a worker thread from blocking forever waiting for a slow or unresponsive server.

**Q: What is `SOMAXCONN` and why did you change the listen backlog to use it?**
A: The `listen()` backlog is the maximum number of pending connections the OS will queue before refusing new ones. `SOMAXCONN` is a system constant (typically 128+) that represents the OS's maximum supported backlog. Using `listen(fd, 6)` (as it was originally) means the OS refuses incoming connections under any moderate load. `SOMAXCONN` lets the OS buffer as many as it supports.

**Q: How does the non-blocking connect with timeout work?**
A: `connect()` normally blocks until the connection succeeds or fails. We set the socket to `O_NONBLOCK` mode, then call `connect()`. On most systems with a remote host, `connect()` returns immediately with `errno == EINPROGRESS` (meaning "I've started connecting, not done yet"). We then call `select()` with a 10-second timeout to wait for the socket to become writable — that indicates the connection completed. We check `SO_ERROR` on the socket to see if it connected successfully or failed. Then we restore the socket to blocking mode.

---

## 9. The Bugs — "What bugs did you find and fix?"

**Q: What was the most critical bug?**
A: The missing `send()` call for HTTP requests. The proxy would connect to the remote server but never actually send the HTTP request. The remote server would wait for input that never came, and eventually the proxy's recv() would timeout and return an empty response to the browser. This was `ERR_EMPTY_RESPONSE` in every browser.

**Q: How did the cache make the missing `send()` bug worse?**
A: The cache would store the empty response (since there was no validity check). On the next visit, the browser received the cached empty response immediately — even faster delivery of a broken response. Both bugs compounded each other: Bug 1 created the empty response, Bug 2 preserved and replayed it.

**Q: Explain the security bug where blocked users could bypass the filter.**
A: The code checked the cache before checking the filter. If user A (with higher permissions) had previously visited and cached `youtube.com`, user B (who should be blocked from it) would receive the cached response without ever hitting the filter check. The fix is to always run the filter check first.

**Q: What was the semaphore bug?**
A: A semaphore was used to "limit concurrency to 20." But the semaphore was initialised to 20, and there were exactly 20 threads. The semaphore was acquired once per request and released once per request. Since it started at 20 and was never held by more than 20 threads (because there were only 20 threads), it never blocked anything. It was overhead with zero effect. Removing it simplified the code with no behaviour change.

---

## 10. Design Decisions — "Why did you do it this way?"

**Q: Why 20 threads specifically?**
A: 20 is a reasonable number for a proxy on a typical development machine. The right number depends on whether the bottleneck is CPU, memory, or network I/O. Since proxy work is heavily I/O-bound (mostly waiting for network data), you can have more threads than CPU cores. 20 is a practical choice that handles concurrent browser tabs without overwhelming system resources.

**Q: Why LRU and not a different eviction policy?**
A: LRU (Least Recently Used) is a strong heuristic for web caching because web browsing is temporal — pages you visited recently are much more likely to be visited again than pages you visited a week ago. Alternatives like LFU (Least Frequently Used) require tracking access counts which is more complex, and FIFO (First In First Out) ignores recency entirely.

**Q: Why not cache HTTPS responses?**
A: HTTPS responses are encrypted end-to-end. The proxy never sees the plaintext. All it handles is a raw byte stream that it tunnels without reading. There is no response to parse or cache.

**Q: What happens if a website is added to the blocked list while the proxy is running?**
A: It does not take effect until the proxy restarts. The site lists are read once at startup into memory. This is a limitation — a production proxy would watch the files for changes or expose an admin API to reload the lists at runtime.

**Q: How would you make this proxy production-ready?**
A: Several things: TLS/HTTPS inspection (MITM with certificate generation), proper HTTP/1.1 keep-alive support, support for HTTP/2, dynamic config reload, per-user rate limiting, more sophisticated caching (Cache-Control, ETag headers), metrics/monitoring, and graceful shutdown.

---

## Quick-Fire Answers (30 seconds each)

| Question | Answer |
|----------|--------|
| What is Base64? | An encoding (not encryption) that converts binary to printable ASCII. Does not provide security. |
| What is SHA-256? | A cryptographic hash function. One-way: you can compute the hash from a password but cannot reverse it. |
| What is `select()`? | A syscall that waits for one or more file descriptors to become ready for read/write, with an optional timeout. |
| What is `fcntl()`? | File control — used here to set/get socket flags, specifically `O_NONBLOCK`. |
| What is `INADDR_ANY`? | Tells the socket to listen on all network interfaces (0.0.0.0). |
| What is `inet_pton`? | Converts an IP address string like "93.184.216.34" to a binary network address. |
| What is `getaddrinfo`? | Translates a hostname ("example.com") to a network address. Thread-safe. Replaces `gethostbyname`. |
| What is `htons()`? | "Host to Network Short" — converts a port number from host byte order to network byte order (big-endian). |
| What does `close(fd)` do? | Closes the socket, triggering the TCP FIN/RST sequence and freeing the file descriptor. |
| What is a file descriptor? | A non-negative integer that the OS uses to track open files, sockets, and pipes. |
