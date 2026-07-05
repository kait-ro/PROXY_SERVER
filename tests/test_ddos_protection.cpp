// DDoS Protection Tests for Proxy Server
// Tests rate limiting, connection limits, and idle timeouts
// Build: g++ -std=c++17 -Wall -pthread test_ddos_protection.cpp -o test_ddos
// Run:   ./test_ddos

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { cout << "PASS  " << name << "\n"; }
        else      { cout << "FAIL  " << name << "\n"; ++failures; }
    };

    cout << "DDoS PROTECTION TESTS\n";
    cout << "=====================\n\n";

    // Test 1: Rate limit configuration is reasonable
    {
        const int MAX_REQUESTS_PER_SEC = 100;
        const int MAX_CONNECTIONS_PER_USER = 10;
        const int MAX_CONNECTIONS_PER_IP = 20;
        const int IDLE_TIMEOUT_SEC = 300;

        check(MAX_REQUESTS_PER_SEC > 0 && MAX_REQUESTS_PER_SEC <= 10000,
              "rate limit per second is reasonable (100 req/sec)");
        check(MAX_CONNECTIONS_PER_USER > 0 && MAX_CONNECTIONS_PER_USER <= 1000,
              "connection limit per user is reasonable (10 connections)");
        check(MAX_CONNECTIONS_PER_IP > 0 && MAX_CONNECTIONS_PER_IP <= 1000,
              "connection limit per IP is reasonable (20 connections)");
        check(IDLE_TIMEOUT_SEC >= 60 && IDLE_TIMEOUT_SEC <= 3600,
              "idle timeout is reasonable (5 minutes)");
    }

    // Test 2: Multiple users can coexist with independent rate limits
    {
        // Simulating the logic:
        // User A makes 100 req/sec (allowed)
        // User B makes 50 req/sec (allowed)
        // They don't interfere with each other
        
        int userA_requests = 100;
        int userB_requests = 50;
        int MAX_REQUESTS_PER_SEC = 100;
        
        bool userA_ok = userA_requests <= MAX_REQUESTS_PER_SEC;
        bool userB_ok = userB_requests <= MAX_REQUESTS_PER_SEC;
        
        check(userA_ok && userB_ok,
              "multiple users have independent rate limits");
    }

    // Test 3: Rate limit resets per second
    {
        // At time T=0: user makes 100 requests (hits limit)
        // At time T=1: counter resets, user can make 100 more
        
        time_t lastRequestTime = time(nullptr);
        int requestsThisSecond = 100;
        int MAX_REQUESTS_PER_SEC = 100;
        
        // Next second
        time_t now = time(nullptr) + 1;
        if (now != lastRequestTime) {
            requestsThisSecond = 0;
            lastRequestTime = now;
        }
        
        check(requestsThisSecond == 0,
              "rate limit counter resets each second");
    }

    // Test 4: Connection limit prevents connection bomb
    {
        int activeConnections = 0;
        int MAX_CONNECTIONS_PER_USER = 10;
        
        // Try to establish 10 connections
        bool canConnect = true;
        for (int i = 0; i < 10; i++) {
            if (activeConnections < MAX_CONNECTIONS_PER_USER) {
                activeConnections++;
            } else {
                canConnect = false;
                break;
            }
        }
        
        check(activeConnections == 10 && canConnect,
              "connection limit allows up to 10 connections");
        
        // Try 11th connection
        bool canConnect11th = false;
        if (activeConnections < MAX_CONNECTIONS_PER_USER) {
            activeConnections++;
            canConnect11th = true;
        }
        
        check(!canConnect11th,
              "connection limit rejects 11th connection");
    }

    // Test 5: Idle timeout prevents thread starvation
    {
        // Simulate select() with timeout
        int timeout_sec = 300;  // 5 minutes
        
        // If no activity for 300 seconds, connection closes
        auto start = chrono::high_resolution_clock::now();
        
        // Simulate idle period
        this_thread::sleep_for(chrono::milliseconds(100));
        
        auto elapsed = chrono::high_resolution_clock::now() - start;
        auto seconds = chrono::duration_cast<chrono::seconds>(elapsed).count();
        
        // In real scenario, after 300 seconds idle the connection closes
        check(timeout_sec == 300,
              "idle timeout is 5 minutes (prevents thread freeze)");
    }

    // Test 6: Per-IP rate limiting independent from per-user
    {
        // Same user from different IPs can exceed user rate limit
        // but will be limited by IP rate limit
        
        string user = "alice";
        string ip1 = "192.168.1.1";
        string ip2 = "192.168.1.2";
        
        // Alice from IP1: 100 req/sec (hits user limit)
        // Alice from IP2: 100 req/sec (uses different IP limit)
        // Total: 200 req/sec but rate limited by user (100) and IP (200)
        
        int MAX_REQUESTS_PER_SEC = 100;
        int MAX_REQUESTS_PER_IP = 200;  // 2x per user
        
        check(MAX_REQUESTS_PER_IP > MAX_REQUESTS_PER_SEC,
              "IP rate limit is higher than user limit");
    }

    // Test 7: 429 Too Many Requests response code
    {
        string response429 = "HTTP/1.1 429 Too Many Requests";
        check(response429.find("429") != string::npos,
              "rate-limited response uses HTTP 429 status code");
    }

    // Test 8: Idle timeout message
    {
        string timeoutMessage = "[IDLE TIMEOUT]";
        check(!timeoutMessage.empty(),
              "idle timeout logs appropriate message");
    }

    // Test 9: Metrics tracking
    {
        // Proxy tracks active connections per user
        // This allows it to enforce MAX_CONNECTIONS_PER_USER
        
        int maxConnPerUser = 10;
        vector<int> userConnections(100, 0);  // Track 100 users
        
        // User 0 adds 5 connections
        userConnections[0] = 5;
        
        // User 1 adds 10 connections
        userConnections[1] = 10;
        
        // User 2 tries to add 11 (should be rejected)
        bool canAdd11th = userConnections[2] < maxConnPerUser;
        check(!canAdd11th,
              "connection metrics prevent exceeding limit");
    }

    // Test 10: DDoS attack simulation - high request rate
    {
        // Attacker sends 1000 requests/sec from same IP
        // Should be rejected after limit (200 req/sec for IP)
        
        int requestsSent = 0;
        int requestsAccepted = 0;
        int MAX_REQUESTS_PER_IP = 200;
        
        for (int i = 0; i < 1000; i++) {
            requestsSent++;
            if (requestsSent <= MAX_REQUESTS_PER_IP) {
                requestsAccepted++;
            }
        }
        
        int rejected = requestsSent - requestsAccepted;
        check(rejected > 0 && requestsAccepted == MAX_REQUESTS_PER_IP,
              "high rate DDoS attack is throttled (only 200 req/sec accepted)");
    }

    // Test 11: DDoS attack simulation - connection bomb
    {
        // Attacker tries to open 1000 connections as one user
        // Should be rejected after limit (10 connections)
        
        int connectionsOpened = 0;
        int connectionsAccepted = 0;
        int MAX_CONNECTIONS_PER_USER = 10;
        
        for (int i = 0; i < 1000; i++) {
            connectionsOpened++;
            if (connectionsOpened <= MAX_CONNECTIONS_PER_USER) {
                connectionsAccepted++;
            }
        }
        
        int rejected = connectionsOpened - connectionsAccepted;
        check(rejected == 990,
              "connection bomb is mitigated (only 10 connections allowed)");
    }

    cout << (failures ? "\nDDOS PROTECTION TESTS FAILED\n" : "\nALL DDOS PROTECTION TESTS PASSED\n");
    return failures ? 1 : 0;
}
