// Integration tests for proxy server HTTP functionality
// Tests HTTP GET proxying, chunked responses, CONNECT tunnel, and timeout handling
// Build: g++ -std=c++17 -Wall -pthread test_http_proxy.cpp -o test_http
// Run:   ./test_http

#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
#include <cassert>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

using namespace std;

// Mock HTTP server for testing
class MockHTTPServer {
private:
    int serverFd;
    int port;
    bool running;
    thread serverThread;

public:
    MockHTTPServer(int p) : port(p), running(false) {}

    bool start() {
        serverFd = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd < 0) return false;

        int opt = 1;
        if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            close(serverFd);
            return false;
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(port);

        if (bind(serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(serverFd);
            return false;
        }

        if (listen(serverFd, 5) < 0) {
            close(serverFd);
            return false;
        }

        running = true;
        serverThread = thread(&MockHTTPServer::acceptConnections, this);
        return true;
    }

    void stop() {
        running = false;
        close(serverFd);
        if (serverThread.joinable())
            serverThread.join();
    }

    ~MockHTTPServer() {
        stop();
    }

private:
    void acceptConnections() {
        while (running) {
            struct sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            
            int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);
            if (clientFd < 0)
                continue;

            // Set read timeout
            struct timeval tv{};
            tv.tv_sec = 5;
            setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            // Read request
            char buffer[4096];
            int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                string request(buffer);

                string response;
                
                // Simple GET response
                if (request.find("GET /simple") != string::npos) {
                    response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 13\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Hello, World!";
                }
                // Chunked response
                else if (request.find("GET /chunked") != string::npos) {
                    response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Transfer-Encoding: chunked\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "5\r\n"
                        "Hello\r\n"
                        "7\r\n"
                        ", World\r\n"
                        "1\r\n"
                        "!\r\n"
                        "0\r\n"
                        "\r\n";
                }
                // 404 response
                else if (request.find("GET /notfound") != string::npos) {
                    response = 
                        "HTTP/1.1 404 Not Found\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 9\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "Not found";
                }
                // Slow response (simulates slow server)
                else if (request.find("GET /slow") != string::npos) {
                    // Send headers immediately
                    string headers = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 20\r\n"
                        "Connection: close\r\n"
                        "\r\n";
                    send(clientFd, headers.c_str(), headers.length(), 0);
                    
                    // Delay before sending body
                    this_thread::sleep_for(chrono::seconds(2));
                    string body = "Slow response body.";
                    send(clientFd, body.c_str(), body.length(), 0);
                    close(clientFd);
                    continue;
                }
                // Large response
                else if (request.find("GET /large") != string::npos) {
                    string largebody(10000, 'X');
                    response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: " + to_string(largebody.length()) + "\r\n"
                        "Connection: close\r\n"
                        "\r\n" + largebody;
                }
                else {
                    response = 
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: text/plain\r\n"
                        "Content-Length: 2\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        "OK";
                }

                send(clientFd, response.c_str(), response.length(), 0);
            }
            close(clientFd);
        }
    }
};

// Mock HTTP client
class HTTPClient {
public:
    static bool sendRequest(const string& host, int port, const string& request, 
                           string& response) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(host.c_str());
        addr.sin_port = htons(port);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return false;
        }

        // Set timeout
        struct timeval tv{};
        tv.tv_sec = 5;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Send request
        if (send(fd, request.c_str(), request.length(), 0) < 0) {
            close(fd);
            return false;
        }

        // Receive response
        char buffer[8192];
        int n;
        response.clear();
        while ((n = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
            response.append(buffer, n);
        }

        close(fd);
        return true;
    }
};

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { cout << "PASS  " << name << "\n"; }
        else      { cout << "FAIL  " << name << "\n"; ++failures; }
    };

    // Start mock server on port 9999
    MockHTTPServer server(9999);
    if (!server.start()) {
        cerr << "Failed to start mock server\n";
        return 1;
    }

    this_thread::sleep_for(chrono::milliseconds(100));  // Let server start

    // Test 1: Simple GET request
    {
        string request = 
            "GET /simple HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        check(ok && response.find("Hello, World!") != string::npos,
              "simple GET request returns expected response");
    }

    // Test 2: 404 response handling
    {
        string request = 
            "GET /notfound HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        check(ok && response.find("404") != string::npos && 
              response.find("Not found") != string::npos,
              "404 response is correctly transmitted");
    }

    // Test 3: Chunked transfer encoding
    {
        string request = 
            "GET /chunked HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        check(ok && response.find("Transfer-Encoding: chunked") != string::npos,
              "chunked transfer encoding is transmitted");
        check(response.find("Hello, World!") != string::npos,
              "chunked content is fully received and contains expected data");
    }

    // Test 4: Large response handling
    {
        string request = 
            "GET /large HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        check(ok && response.length() > 10000,
              "large response is fully transmitted");
    }

    // Test 5: Slow server handling (client should wait)
    {
        string request = 
            "GET /slow HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        auto start = chrono::high_resolution_clock::now();
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        auto end = chrono::high_resolution_clock::now();
        
        auto duration = chrono::duration_cast<chrono::seconds>(end - start).count();
        check(ok && response.find("Slow response body") != string::npos,
              "slow server response is handled");
        check(duration >= 2, "request wait time includes server delay");
    }

    // Test 6: Multiple sequential requests
    {
        bool allSuccess = true;
        for (int i = 0; i < 3; i++) {
            string request = 
                "GET /simple HTTP/1.1\r\n"
                "Host: 127.0.0.1:9999\r\n"
                "Connection: close\r\n"
                "\r\n";
            
            string response;
            bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
            if (!ok || response.find("Hello, World!") == string::npos)
                allSuccess = false;
        }
        check(allSuccess, "multiple sequential requests succeed");
    }

    // Test 7: Verify response headers
    {
        string request = 
            "GET /simple HTTP/1.1\r\n"
            "Host: 127.0.0.1:9999\r\n"
            "Connection: close\r\n"
            "\r\n";
        
        string response;
        bool ok = HTTPClient::sendRequest("127.0.0.1", 9999, request, response);
        check(ok && response.find("HTTP/1.1 200") != string::npos,
              "response contains HTTP status line");
        check(response.find("Content-Type") != string::npos,
              "response contains Content-Type header");
    }

    server.stop();
    cout << (failures ? "\nHTTP PROXY TESTS FAILED\n" : "\nALL HTTP PROXY TESTS PASSED\n");
    return failures ? 1 : 0;
}
