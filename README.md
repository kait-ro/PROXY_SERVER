# Custom HTTP/HTTPS Proxy Server (C++)

[![CI](https://github.com/Ace-2504/PROXY_SERVER/actions/workflows/ci.yml/badge.svg)](https://github.com/Ace-2504/PROXY_SERVER/actions/workflows/ci.yml)
![Language](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus)
![Docker](https://img.shields.io/badge/Docker-ready-2496ED?logo=docker)

A multi-threaded HTTP/HTTPS forward proxy in modern C++: LRU-based DNS caching,
HTTPS `CONNECT` tunnelling, user authentication, structured logging, and
category-based site filtering — organised into independent modules (proxy core,
auth, cache, logging, filtering).

## Architecture

| Module | File | Responsibility |
|--------|------|----------------|
| Proxy core | `proxyserver.cpp` | Accept sockets, parse requests, forward / tunnel |
| Auth | `Authenticator.cpp` | User login/signup (SHA-hashed, `users.txt`) |
| DNS cache | `LRUCache.cpp` | Thread-safe LRU host→IP cache |
| Logging | `Logger.cpp` | Structured, timestamped request logs |
| Filtering | `WebsiteFilter.cpp` | Category-based blocklists |

## Build & run

```bash
make proxy     # build (needs OpenSSL + pkg-config)
make run       # build and start
make test      # build and run the unit tests
```

### Docker

```bash
docker build -t proxy-server .
docker run -it proxy-server
```

## Tests & CI

Unit tests for the LRU cache live in `tests/` and run in CI on every push
(GitHub Actions builds the proxy on Ubuntu and runs `make test`).

## Observability

Every request is logged with timestamp, user, host, type, and status via the
`Logger` module to `proxy.log` beside the binary.
