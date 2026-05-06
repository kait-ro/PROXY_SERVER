# Proxy Server — Demo Walkthrough Script

This guide walks through every feature of the proxy server in a logical demo order. Follow the steps in sequence for the smoothest presentation.

---

## Before You Start — Setup Checklist

1. Open **two terminal windows**: one for the proxy server, one for commands/curl tests
2. Have a browser ready (Firefox or Chrome) with proxy settings configured:
   - Host: `127.0.0.1`
   - Port: `8080` (or whichever port you use)
   - Method: Manual proxy (both HTTP and HTTPS)
3. Make sure `users.txt`, `blocked_sites.txt`, `ai_blocked_sites.txt`, `ai_gaming.txt`, `ai_social.txt` are all present
4. Build: `make`

---

## Step 1 — Start the Server

**Terminal 1:**
```bash
./proxy
```

Expected output:
```
Thread-1 started
Thread-2 started
...
Thread-20 started
Proxy server running on port 8080
```

**What to say:**
> "The proxy starts and immediately creates 20 worker threads. Each thread sits idle waiting for a client to connect. This is a thread pool design — instead of creating a new thread for each request, we reuse threads to avoid the overhead of thread creation."

---

## Step 2 — Authentication: Show it Rejects Bad Credentials

In the browser (with proxy set), try visiting any site **without** configuring credentials, or use wrong credentials.

**What to say:**
> "The proxy enforces authentication. Every request must carry a `Proxy-Authorization` header. The browser encodes the username and password in Base64 and sends it with every request. If credentials are missing or wrong, the proxy responds with HTTP 407 — Proxy Authentication Required — and the browser will show a login prompt."

**What appears in the terminal:**
```
Authentication failed
```

---

## Step 3 — Authentication: Log In as a Regular User

Configure browser proxy credentials: `test2` / `[password]` (a `user` role account)

Visit `http://example.com`

**Terminal output to point out:**
```
START handling on thread: 3
DEBUG → role: 'user' username: 'test2'
Authentication successful (user)
HTTP HOST: example.com
Connected to HTTP server
===== FORWARDING REQUEST =====
GET / HTTP/1.1
Host: example.com
Connection: close
...
CACHE STORED for user: test2
END handling on thread: 3
```

**What to say:**
> "Authentication works. The proxy extracts the Base64-encoded credentials from the header, decodes them, hashes the password using SHA-256, and compares it against the stored hash in `users.txt`. Passwords are never stored in plain text. The user's role — in this case 'user' — is returned and used for all subsequent decisions."

**Show the log:**
```bash
tail proxy.log
```
Expected:
```
[14:23:45] Thread-3 test2(user) | example.com | HTTP | ALLOWED
[14:23:45] Thread-3 test2(user) | example.com | HTTP | CACHE STORED
```

---

## Step 4 — Caching: Show the Cache Hit

Reload `http://example.com` (or visit it again).

**Terminal output to point out:**
```
CACHE HIT
```

**What to say:**
> "On the second visit, the proxy checks its LRU cache first. It finds the cached response and serves it immediately — without making any network connection to `example.com`. This is significantly faster and reduces bandwidth. The cache key is the full URL path, normalized to avoid duplicates from `www.` prefix or capitalisation differences."

**Show the log:**
```
[14:23:47] Thread-5 test2(user) | example.com | HTTP | CACHE HIT
```

**Explain LRU (if asked):**
> "LRU stands for Least Recently Used. The cache holds up to 200 entries. When it is full and a new item arrives, it evicts the entry that was accessed least recently. This keeps popular pages in cache and discards ones no one has visited lately."

---

## Step 5 — HTTPS Tunnelling

Visit any HTTPS site, e.g. `https://example.com`

**Terminal output:**
```
HTTPS CONNECT to: example.com
Tunnel established successfully
```

**What to say:**
> "HTTPS traffic is handled differently from HTTP. The browser sends a special `CONNECT` request asking the proxy to open a tunnel. The proxy resolves the hostname, connects to the remote server on the correct port, then sends back '200 Connection Established'. From that point on, the proxy acts as a transparent pipe — it forwards raw bytes in both directions without reading them. This is how HTTPS privacy is preserved: the proxy never sees the actual encrypted content."

---

## Step 6 — Website Filtering: Block a Site for a Regular User

Make sure a site like `youtube.com` or `facebook.com` is in your blocked/social list. With `test2` (user role) still logged in, try to visit it.

**Browser shows:** 403 Forbidden / "Blocked by proxy"

**Terminal:**
```
Blocked HTTP site: youtube.com
```

**Log:**
```
[14:24:01] Thread-2 test2(user) | youtube.com | HTTP | BLOCKED
```

**What to say:**
> "The `WebsiteFilter` checks the requested hostname against several lists: globally blocked sites, AI-curated gaming sites, and AI-curated social media sites. Regular users (`user` role) are blocked from both global and category lists. Students (`student` role) are blocked only from globally blocked sites. The domain is normalised before the check — strip `www.`, lowercase — so `www.YouTube.com` and `youtube.com` match the same rule."

---

## Step 7 — Admin Bypass

Switch browser credentials to an admin account (e.g. `admin1` / `[password]`).

Visit the same blocked site (`youtube.com`).

**Browser shows:** The site loads normally.

**What to say:**
> "Admin users bypass the filter entirely. The `isBlockedForRole` function immediately returns false for any admin, regardless of the domain. This is a role-based access control decision — admins have unrestricted access."

---

## Step 8 — Role-Based Access: Show Student vs User Differences

Log in as a `student` role account. Try:
1. A globally blocked site → should be blocked
2. A gaming or social site (e.g. `twitch.tv`, `twitter.com`) → should be **allowed** for students

Compare with `user` role:
- `user` is blocked from gaming AND social sites
- `student` is only blocked from globally blocked sites

**What to say:**
> "There are three roles: admin, user, and student. Each role has a different filter policy. Admin sees everything. User is blocked from global blocked sites plus gaming and social categories. Student is only blocked from the global blocked list. The roles and site lists are loaded from text files at startup, so adding new blocked sites or changing role policies requires only editing a text file — no recompilation."

---

## Step 9 — Show the Log File

```bash
cat proxy.log
```
or
```bash
tail -30 proxy.log
```

**Walk through the format:**
```
[HH:MM:SS] Thread-N username(role) | hostname | HTTP/HTTPS | STATUS
```

**Statuses to point out:**
- `ALLOWED` — request forwarded
- `BLOCKED` — request denied by filter
- `CACHE HIT` — served from cache
- `CACHE STORED` — response saved to cache

**What to say:**
> "Every request is logged with a timestamp, which thread handled it, the authenticated username and their role, the destination hostname, whether it was HTTP or HTTPS, and the outcome. This gives a complete audit trail. The logger uses a mutex to ensure that concurrent threads don't write over each other's log entries."

---

## Step 10 — Security: Show Hop-by-Hop Header Stripping

Use curl to make a manual request and inspect what the proxy forwards:

```bash
curl -v -x http://test2:PASSWORD@127.0.0.1:8080 http://httpbin.org/headers
```

This returns a JSON of all headers that `httpbin.org` received.

**What to show:**
> Point out that `Proxy-Authorization` and `Proxy-Connection` do NOT appear in the JSON response — they were stripped by the proxy before forwarding.

**What to say:**
> "The `Proxy-Authorization` header carries the user's credentials. If it were forwarded to the origin server, any website could read your proxy password. The proxy strips all 'hop-by-hop' headers — headers that are only meaningful between the client and proxy — before forwarding the request. This is part of the HTTP spec and also a security requirement."

---

## Step 11 — Performance: DNS Caching (Optional Advanced Demo)

Add a print statement or point to the `resolveHost` function and explain:

**What to say:**
> "On the first request to any hostname, the proxy calls `getaddrinfo` to translate the domain name into an IP address. This is a network call to a DNS server and takes 5–50 milliseconds. On every subsequent request to the same hostname within 30 seconds, we return the cached IP immediately. With 20 threads all potentially hitting the same sites, this avoids hundreds of redundant DNS lookups per minute."

---

## Step 12 — Show Multiple Concurrent Clients

Open 5 browser tabs simultaneously, all making different requests.

**Terminal shows** different thread numbers handling different requests at the same time.

**What to say:**
> "The thread pool handles concurrency. The main thread's only job is to `accept()` new connections and push them onto a queue. Worker threads each sit in a loop: lock the queue, take a socket, unlock, handle the client. The `condition_variable` avoids busy-waiting — threads sleep until notified that work is available. With 20 threads, up to 20 clients can be served simultaneously."

---

## Closing Summary for the Demo

> "To summarise what this proxy server does:
> - It authenticates every connection using username/password with SHA-256 hashed storage
> - It enforces role-based access control across three roles: admin, user, student
> - It caches HTTP responses in an LRU cache to speed up repeated visits
> - It tunnels HTTPS traffic transparently without seeing encrypted content
> - It logs all activity with timestamps and user identity
> - It uses a 20-thread pool for concurrent connections
> - It strips sensitive headers before forwarding to protect credentials
> - It handles DNS caching and connect timeouts to remain responsive under load"
