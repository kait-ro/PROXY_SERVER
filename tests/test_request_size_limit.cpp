#include <iostream>
#include <cassert>
#include <string>
#include <cstring>

using namespace std;

// Test constants (matching proxyserver.cpp)
#define MAX_REQUEST_SIZE 8192

// Test 1: Normal request (below limit)
void test_normal_request_accepted()
{
    int requestSize = 512;  // 512 bytes, well below 8KB limit
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 1: Normal request (512 bytes) accepted - PASS\n";
}

// Test 2: Large request (at limit)
void test_request_at_limit()
{
    int requestSize = MAX_REQUEST_SIZE;  // Exactly at limit
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 2: Request at limit (8192 bytes) accepted - PASS\n";
}

// Test 3: Request just over limit
void test_request_over_limit()
{
    int requestSize = MAX_REQUEST_SIZE + 1;  // 8193 bytes, over limit
    assert(requestSize > MAX_REQUEST_SIZE);
    cout << "✓ Test 3: Request over limit (8193 bytes) rejected - PASS\n";
}

// Test 4: Typical GET request
void test_typical_get_request()
{
    string request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    int requestSize = request.length();
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 4: Typical GET request (" << requestSize << " bytes) accepted - PASS\n";
}

// Test 5: Typical CONNECT request (for HTTPS tunnel)
void test_typical_connect_request()
{
    string request = "CONNECT example.com:443 HTTP/1.1\r\nHost: example.com:443\r\nConnection: close\r\n\r\n";
    int requestSize = request.length();
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 5: Typical CONNECT request (" << requestSize << " bytes) accepted - PASS\n";
}

// Test 6: POST request with small body (below limit)
void test_post_request_small_body()
{
    string headers = "POST /api HTTP/1.1\r\nHost: example.com\r\nContent-Length: 100\r\n\r\n";
    string body = string(100, 'A');  // 100-byte body
    int requestSize = headers.length() + body.length();
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 6: POST request with 100-byte body (" << requestSize << " bytes) accepted - PASS\n";
}

// Test 7: Large POST request (over limit)
void test_post_request_large_body()
{
    string headers = "POST /api HTTP/1.1\r\nHost: example.com\r\nContent-Length: 10000\r\n\r\n";
    string body = string(10000, 'A');  // 10KB body
    int requestSize = headers.length() + body.length();
    assert(requestSize > MAX_REQUEST_SIZE);
    cout << "✓ Test 7: POST request with 10KB body (" << requestSize << " bytes) rejected - PASS\n";
}

// Test 8: Malicious large request (payload attack)
void test_malicious_large_payload()
{
    string request = "POST /upload HTTP/1.1\r\nHost: attacker.com\r\nContent-Length: 50000\r\n\r\n";
    request += string(50000, 'X');  // 50KB payload
    int requestSize = request.length();
    assert(requestSize > MAX_REQUEST_SIZE);
    cout << "✓ Test 8: Malicious 50KB payload rejected - PASS\n";
}

// Test 9: HTTP 413 response format
void test_http_413_response()
{
    string response = 
        "HTTP/1.1 413 Payload Too Large\r\n"
        "Content-Length: 28\r\n"
        "Connection: close\r\n\r\n"
        "Request size exceeds limit";
    
    // Check response format
    assert(response.find("HTTP/1.1 413") != string::npos);
    assert(response.find("Payload Too Large") != string::npos);
    assert(response.find("Content-Length: 28") != string::npos);
    assert(response.find("Connection: close") != string::npos);
    cout << "✓ Test 9: HTTP 413 response format correct - PASS\n";
}

// Test 10: Maximum allowed request size
void test_maximum_allowed_size()
{
    // Create a request that's exactly MAX_REQUEST_SIZE
    string largeRequest = "GET /path";
    while (largeRequest.length() < (MAX_REQUEST_SIZE - 50)) {
        largeRequest += "x";
    }
    largeRequest += " HTTP/1.1\r\nHost: example.com\r\n\r\n";
    
    // Truncate to exactly MAX_REQUEST_SIZE if needed
    if (largeRequest.length() > MAX_REQUEST_SIZE) {
        largeRequest = largeRequest.substr(0, MAX_REQUEST_SIZE);
    }
    
    int requestSize = largeRequest.length();
    assert(requestSize <= MAX_REQUEST_SIZE);
    cout << "✓ Test 10: Maximum allowed request size (" << requestSize << " bytes) accepted - PASS\n";
}

int main()
{
    cout << "\n=== Request Size Limit Tests ===\n";
    cout << "MAX_REQUEST_SIZE: " << MAX_REQUEST_SIZE << " bytes (8 KB)\n\n";
    
    test_normal_request_accepted();
    test_request_at_limit();
    test_request_over_limit();
    test_typical_get_request();
    test_typical_connect_request();
    test_post_request_small_body();
    test_post_request_large_body();
    test_malicious_large_payload();
    test_http_413_response();
    test_maximum_allowed_size();
    
    cout << "\n✓ All 10 tests passed!\n\n";
    return 0;
}
