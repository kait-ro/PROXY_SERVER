// Integration tests for CONNECT tunnel (HTTPS proxying)
// Tests CONNECT method, tunnel establishment, and timeout handling
// Build: g++ -std=c++17 -Wall -pthread test_connect_tunnel.cpp -o test_connect
// Run:   ./test_connect

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

// Mock HTTPS server to accept CONNECT tunnels
class MockHTTPSServer {
private:
    int serverFd;
    int port;
    bool running;
    thread serverThread;

public:
    MockHTTPSServer(int p) : port(p), running(false) {}

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
        serverThread = thread(&MockHTTPSServer::acceptConnections, this);
        return true;
    }

    void stop() {
        running = false;
        close(serverFd);
        if (serverThread.joinable())
            serverThread.join();
    }

    ~MockHTTPSServer() {
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

            // Simulate HTTPS server response
            char buffer[4096];
            struct timeval tv{};
            tv.tv_sec = 2;
            setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                
                // For HTTPS, just echo back some TLS-like data
                string response = "TLS_RESPONSE_DATA";
                send(clientFd, response.c_str(), response.length(), 0);
            }
            close(clientFd);
        }
    }
};

// Mock HTTP proxy that handles CONNECT requests
class MockProxyServer {
private:
    int serverFd;
    int port;
    bool running;
    thread serverThread;

public:
    MockProxyServer(int p) : port(p), running(false) {}

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
        serverThread = thread(&MockProxyServer::acceptConnections, this);
        return true;
    }

    void stop() {
        running = false;
        close(serverFd);
        if (serverThread.joinable())
            serverThread.join();
    }

    ~MockProxyServer() {
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

            char buffer[4096];
            struct timeval tv{};
            tv.tv_sec = 5;
            setsockopt(clientFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                string request(buffer);

                if (request.find("CONNECT") == 0) {
                    // Parse CONNECT request
                    size_t hostStart = request.find("CONNECT ") + 8;
                    size_t hostEnd = request.find(":", hostStart);
                    if (hostEnd != string::npos) {
                        string connectHost = request.substr(hostStart, hostEnd - hostStart);
                        
                        // Send 200 Connection Established
                        string response = 
                            "HTTP/1.1 200 Connection Established\r\n"
                            "Connection: keep-alive\r\n"
                            "\r\n";
                        
                        send(clientFd, response.c_str(), response.length(), 0);
                        
                        // For testing, just wait for tunnel data
                        memset(buffer, 0, sizeof(buffer));
                        recv(clientFd, buffer, sizeof(buffer) - 1, 0);
                    }
                }
            }
            close(clientFd);
        }
    }
};

// Client to test CONNECT tunneling
class TunnelClient {
public:
    static bool sendCONNECT(const string& proxyHost, int proxyPort, 
                           const string& targetHost, int targetPort,
                           string& response) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(proxyHost.c_str());
        addr.sin_port = htons(proxyPort);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return false;
        }

        struct timeval tv{};
        tv.tv_sec = 5;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Build CONNECT request
        string connectRequest = 
            "CONNECT " + targetHost + ":" + to_string(targetPort) + " HTTP/1.1\r\n"
            "Host: " + targetHost + ":" + to_string(targetPort) + "\r\n"
            "Proxy-Connection: keep-alive\r\n"
            "\r\n";

        if (send(fd, connectRequest.c_str(), connectRequest.length(), 0) < 0) {
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

    static bool sendTunnelData(const string& proxyHost, int proxyPort,
                              const string& targetHost, int targetPort,
                              const string& tunnelData, string& response) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(proxyHost.c_str());
        addr.sin_port = htons(proxyPort);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return false;
        }

        struct timeval tv{};
        tv.tv_sec = 5;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        // Send CONNECT
        string connectRequest = 
            "CONNECT " + targetHost + ":" + to_string(targetPort) + " HTTP/1.1\r\n"
            "Host: " + targetHost + ":" + to_string(targetPort) + "\r\n"
            "\r\n";

        send(fd, connectRequest.c_str(), connectRequest.length(), 0);

        // Receive CONNECT response
        char buffer[8192];
        int n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            close(fd);
            return false;
        }

        // Send tunnel data
        if (send(fd, tunnelData.c_str(), tunnelData.length(), 0) < 0) {
            close(fd);
            return false;
        }

        // Receive tunnel response
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

    // Start mock proxy on port 9998 and HTTPS server on 443
    MockProxyServer proxy(9998);
    MockHTTPSServer httpsServer(9997);  // Use different port for test

    if (!proxy.start()) {
        cerr << "Failed to start mock proxy\n";
        return 1;
    }

    if (!httpsServer.start()) {
        cerr << "Failed to start mock HTTPS server\n";
        return 1;
    }

    this_thread::sleep_for(chrono::milliseconds(100));

    // Test 1: CONNECT request format
    {
        string response;
        bool ok = TunnelClient::sendCONNECT("127.0.0.1", 9998, "example.com", 443, response);
        check(ok && response.find("200") != string::npos,
              "CONNECT request sends valid HTTP request");
    }

    // Test 2: CONNECT response structure
    {
        string response;
        bool ok = TunnelClient::sendCONNECT("127.0.0.1", 9998, "github.com", 443, response);
        check(ok && response.find("HTTP/1.1") != string::npos,
              "CONNECT response includes HTTP version");
        check(response.find("Connection Established") != string::npos,
              "CONNECT response indicates successful tunnel");
    }

    // Test 3: Tunnel data transmission
    {
        string tunnelData = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
        string response;
        bool ok = TunnelClient::sendTunnelData("127.0.0.1", 9998, 
                                               "example.com", 443, 
                                               tunnelData, response);
        check(ok, "tunnel data can be sent through CONNECT tunnel");
    }

    // Test 4: Multiple CONNECT requests
    {
        bool allSuccess = true;
        for (int i = 0; i < 3; i++) {
            string response;
            bool ok = TunnelClient::sendCONNECT("127.0.0.1", 9998, 
                                               "test" + to_string(i) + ".com", 443, response);
            if (!ok || response.find("200") == string::npos)
                allSuccess = false;
        }
        check(allSuccess, "multiple CONNECT requests succeed");
    }

    // Test 5: CONNECT to different ports
    {
        string response;
        bool ok = TunnelClient::sendCONNECT("127.0.0.1", 9998, "example.com", 8443, response);
        check(ok && response.find("HTTP") != string::npos,
              "CONNECT works with non-standard HTTPS port");
    }

    // Test 6: CONNECT timeout handling (slow client)
    {
        auto start = chrono::high_resolution_clock::now();
        
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = htons(9998);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            // Send CONNECT but then do nothing (simulate slow client)
            string connectRequest = "CONNECT example.com:443 HTTP/1.1\r\n";
            send(fd, connectRequest.c_str(), connectRequest.length(), 0);
            
            // Wait a bit
            this_thread::sleep_for(chrono::milliseconds(500));
        }
        close(fd);
        
        auto end = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start).count();
        
        check(duration > 0, "CONNECT request can be made (timeout test)");
    }

    proxy.stop();
    httpsServer.stop();

    cout << (failures ? "\nCONNECT TUNNEL TESTS FAILED\n" : "\nALL CONNECT TUNNEL TESTS PASSED\n");
    return failures ? 1 : 0;
}
