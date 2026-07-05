# Proxy Server Test Suite

This directory contains comprehensive unit tests and integration tests for the proxy server.

## Test Files

### 1. `test_lru.cpp` - LRU Cache Tests
Tests the LRU (Least Recently Used) cache implementation.

**Coverage:**
- Basic put/get operations
- Cache eviction when capacity is exceeded
- LRU ordering (least recently used items are evicted first)
- Update/refresh operations
- Thread safety with concurrent access

**Build:** `make test_lru`
**Run:** `./test_lru`

### 2. `test_authenticator.cpp` - Authentication Tests
Tests user authentication with password hashing and role-based access.

**Coverage:**
- Successful login with correct credentials
- Failed login with wrong password
- Failed login with unknown user
- User signup (registration) with duplicate user handling
- Password hashing correctness
- Role assignment and retrieval
- Case-sensitive username matching
- Empty credentials handling
- File loading and persistence

**Build:** `make test_auth`
**Run:** `./test_auth`

### 3. `test_websitefilter.cpp` - Website Filtering Tests
Tests domain blocking rules and role-based filtering.

**Coverage:**
- Basic domain blocking (exact match)
- Domain normalization (http://, https://, www. prefixes)
- Case-insensitive matching
- Subdomain blocking (blocks *.blocked.com if blocked.com is blocked)
- Deep subdomain levels
- Role-based filtering:
  - Admin role: bypasses all blocks
  - User role: blocks gaming and social sites
  - Guest role: blocks gaming sites
- Path and port stripping from domains
- Legitimate site allowlisting

**Build:** `make test_filter`
**Run:** `./test_filter`

### 4. `test_dns_cache.cpp` - DNS Cache Tests
Tests DNS resolution caching with TTL (Time To Live) expiry.

**Coverage:**
- Cache hit/miss on resolution lookups
- DNS record storage and retrieval
- TTL (Time To Live) expiration (configurable, default 30s)
- Automatic cache eviction after TTL expires
- Cache update with new values
- Partial expiry (some entries expire while others remain valid)
- TTL remaining calculation
- Thread-safe concurrent read/write access
- Multiple TTL configurations

**Build:** `make test_dns`
**Run:** `./test_dns`

Note: Tests use 1-2 second TTLs for quick validation. The actual proxy uses 30s TTL.

### 5. `test_http_proxy.cpp` - HTTP Proxy Tests
Tests basic HTTP GET request proxying through the proxy server.

**Coverage:**
- Simple HTTP GET requests
- Response status codes (200, 404)
- Content-Type and other HTTP headers
- Large response body transmission (>10KB)
- **Chunked transfer encoding** (Transfer-Encoding: chunked)
- Multiple sequential requests
- Slow server simulation (server delays before sending body)
- Timeout handling with slow responses
- Full response reassembly from multiple packets

**Build:** `make test_http`
**Run:** `./test_http`

Includes a mock HTTP server that runs on port 9999 during testing.

### 6. `test_connect_tunnel.cpp` - HTTPS CONNECT Tunnel Tests
Tests HTTPS tunneling through the proxy using the CONNECT method.

**Coverage:**
- CONNECT request format and parsing
- 200 Connection Established response
- Tunnel establishment
- Tunnel data transmission (bidirectional)
- Non-standard HTTPS ports (e.g., 8443)
- Multiple concurrent CONNECT tunnels
- Timeout handling with slow clients

**Build:** `make test_connect`
**Run:** `./test_connect`

Includes mock proxy and HTTPS servers on ports 9998/9997.

## Running All Tests

To build and run all tests:
```bash
make test
```

This will:
1. Compile all test executables
2. Run each test in sequence
3. Display pass/fail counts for each test

## Building Individual Tests

```bash
make test_lru      # LRU Cache tests
make test_auth     # Authentication tests
make test_filter   # Website Filter tests
make test_dns      # DNS Cache tests
make test_http     # HTTP Proxy tests
make test_connect  # CONNECT Tunnel tests
```

## Clean Up

To remove all test binaries:
```bash
make clean
```

## Test Results Interpretation

Each test outputs:
```
PASS  test_name
FAIL  test_name

ALL <SUITE> TESTS PASSED  [exit code 0]
<SUITE> TESTS FAILED      [exit code 1]
```

## Requirements

### For Compilation
- C++17 compatible compiler (g++ 5.0+, clang 3.5+)
- OpenSSL development libraries (for Authenticator tests)
- POSIX-compliant OS (Linux, macOS, BSD)
  - Note: Tests use POSIX socket APIs and won't compile on Windows

### For Execution
- No external services required
- All tests include mock servers/components
- Tests run in isolated environments
- No network access required

## Test Architecture

### Unit Tests
- `test_lru.cpp`: Pure unit tests (no external dependencies)
- `test_auth.cpp`: Unit tests with file I/O
- `test_filter.cpp`: Unit tests with file I/O
- `test_dns_cache.cpp`: Unit tests with timing validation

### Integration Tests
- `test_http_proxy.cpp`: Mock HTTP server + client
- `test_connect_tunnel.cpp`: Mock proxy + HTTPS server

Integration tests include:
- Mock server implementations
- Mock client implementations
- Proper port binding and socket management
- Thread-safe concurrent connections
- Timeout simulation

## Key Testing Patterns

### Authentication Testing
```cpp
Authenticator auth;
auth.loadUsers("test_users.txt");
string role = auth.login("user", "password");
```

### Filter Testing
```cpp
WebsiteFilter filter;
filter.loadTestFiles();
check(!filter.isAllowed("blocked.com"), "blocked site");
check(!filter.isBlockedForRole("gaming.com", "user"), "user blocks gaming");
```

### DNS Cache Testing
```cpp
DNSCache cache(30);  // 30 second TTL
cache.put("example.com", "93.184.216.34");
string ip;
cache.get("example.com", ip);
```

### HTTP Proxy Testing
```cpp
string response;
HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
check(response.find("200 OK") != string::npos, "valid response");
```

## Debugging Failed Tests

If a test fails:

1. **Run individual test for details:**
   ```bash
   ./test_auth
   ```

2. **Check compilation output:**
   ```bash
   make test_auth 2>&1 | grep error
   ```

3. **Look for port conflicts (integration tests):**
   ```bash
   lsof -i :9998  # Check if mock servers can bind
   ```

4. **Check file permissions (file I/O tests):**
   ```bash
   ls -la test_*
   ```

## Future Test Additions

Potential areas for expansion:
- Fuzzing tests with malformed HTTP requests
- Performance/load tests with many concurrent connections
- Network stress tests (dropped packets, connection resets)
- Security tests (injection attacks, oversized headers)
- Memory leak detection with valgrind
- Code coverage analysis

## Notes

- All tests are dependency-free unit tests (single source file or minimal dependencies)
- Tests clean up temporary files after execution
- Mock servers handle multiple concurrent connections
- Thread safety is verified with concurrent access patterns
- Timeouts are tested with realistic delays (1-2 second waits)
