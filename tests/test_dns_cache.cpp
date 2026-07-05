// Unit tests for DNS cache mechanism
// Tests cache hits, misses, TTL expiry, and concurrent access
// This test extracts the DNS cache logic to test independently
// Build: g++ -std=c++17 -Wall -pthread test_dns_cache.cpp -o test_dns
// Run:   ./test_dns

#include <unordered_map>
#include <string>
#include <mutex>
#include <ctime>
#include <thread>
#include <iostream>
#include <chrono>
#include <cassert>

using namespace std;

// Extracted DNS cache for testing
class DNSCache {
private:
    unordered_map<string, pair<string, time_t>> cache;
    mutex cacheMutex;
    int ttlSeconds;

public:
    DNSCache(int ttl = 30) : ttlSeconds(ttl) {}

    // Returns IP address from cache if present and not expired
    bool get(const string& host, string& ip) {
        lock_guard<mutex> lock(cacheMutex);
        auto it = cache.find(host);
        if (it == cache.end())
            return false;
        
        // Check if expired
        if (time(nullptr) >= it->second.second) {
            cache.erase(it);
            return false;
        }

        ip = it->second.first;
        return true;
    }

    // Store IP in cache with TTL
    void put(const string& host, const string& ip) {
        lock_guard<mutex> lock(cacheMutex);
        cache[host] = {ip, time(nullptr) + ttlSeconds};
    }

    // For testing: get remaining TTL
    long getTTLRemaining(const string& host) {
        lock_guard<mutex> lock(cacheMutex);
        auto it = cache.find(host);
        if (it == cache.end())
            return -1;
        
        long remaining = it->second.second - time(nullptr);
        return remaining > 0 ? remaining : 0;
    }

    // For testing: clear cache
    void clear() {
        lock_guard<mutex> lock(cacheMutex);
        cache.clear();
    }

    // For testing: get cache size
    size_t size() {
        lock_guard<mutex> lock(cacheMutex);
        return cache.size();
    }
};

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { cout << "PASS  " << name << "\n"; }
        else      { cout << "FAIL  " << name << "\n"; ++failures; }
    };

    // Test 1: Basic cache miss
    {
        DNSCache cache(30);
        string ip;
        check(!cache.get("example.com", ip), "cache miss on unknown host");
    }

    // Test 2: Store and retrieve
    {
        DNSCache cache(30);
        cache.put("example.com", "93.184.216.34");
        string ip;
        check(cache.get("example.com", ip) && ip == "93.184.216.34", 
              "cache returns stored IP");
    }

    // Test 3: Multiple entries
    {
        DNSCache cache(30);
        cache.put("example.com", "93.184.216.34");
        cache.put("google.com", "142.250.185.46");
        cache.put("github.com", "140.82.113.4");
        
        string ip1, ip2, ip3;
        check(cache.get("example.com", ip1) && ip1 == "93.184.216.34" &&
              cache.get("google.com", ip2) && ip2 == "142.250.185.46" &&
              cache.get("github.com", ip3) && ip3 == "140.82.113.4",
              "cache stores and retrieves multiple entries");
    }

    // Test 4: Cache expiry after TTL
    {
        DNSCache cache(1);  // 1 second TTL
        cache.put("example.com", "93.184.216.34");
        
        // Immediately should be in cache
        string ip;
        check(cache.get("example.com", ip), "entry exists immediately after put");
        
        // Wait for expiry
        this_thread::sleep_for(chrono::seconds(2));
        
        // Should be expired
        check(!cache.get("example.com", ip), "entry expired after TTL");
    }

    // Test 5: Update refreshes entry
    {
        DNSCache cache(30);
        cache.put("example.com", "93.184.216.34");
        
        // Get to check it exists
        string ip1;
        check(cache.get("example.com", ip1), "entry exists");
        
        // Update with new IP
        cache.put("example.com", "93.184.216.35");
        
        string ip2;
        check(cache.get("example.com", ip2) && ip2 == "93.184.216.35",
              "update changes cached value");
    }

    // Test 6: Partial expiry (some entries expire, others don't)
    {
        DNSCache cache(2);  // 2 second TTL
        cache.put("example1.com", "1.1.1.1");
        
        this_thread::sleep_for(chrono::seconds(1));
        
        cache.put("example2.com", "2.2.2.2");  // Added after 1 second
        
        this_thread::sleep_for(chrono::seconds(2));  // Total: 3 seconds
        
        string ip1, ip2;
        // example1 was added at 0s, TTL is 2s, so it expires at 2s - should be gone
        check(!cache.get("example1.com", ip1), "older entry expired");
        
        // example2 was added at 1s, TTL is 2s, so it expires at 3s - should still exist
        check(cache.get("example2.com", ip2), "newer entry still valid");
    }

    // Test 7: Clear cache
    {
        DNSCache cache(30);
        cache.put("example.com", "93.184.216.34");
        cache.put("google.com", "142.250.185.46");
        check(cache.size() == 2, "cache has 2 entries");
        
        cache.clear();
        check(cache.size() == 0, "cache cleared");
    }

    // Test 8: Concurrent access - thread safety
    {
        DNSCache cache(30);
        vector<thread> threads;
        
        // Thread 1: Writers
        threads.push_back(thread([&cache]() {
            for (int i = 0; i < 100; i++) {
                cache.put("host" + to_string(i % 10), "192.168.1." + to_string(i));
            }
        }));
        
        // Thread 2: Readers
        threads.push_back(thread([&cache]() {
            for (int i = 0; i < 100; i++) {
                string ip;
                cache.get("host" + to_string(i % 10), ip);
                this_thread::sleep_for(chrono::microseconds(100));
            }
        }));
        
        // Wait for all threads
        for (auto& t : threads)
            t.join();
        
        check(true, "concurrent read/write without deadlock");
    }

    // Test 9: TTL remaining calculation
    {
        DNSCache cache(30);
        cache.put("example.com", "93.184.216.34");
        
        long ttl = cache.getTTLRemaining("example.com");
        check(ttl > 0 && ttl <= 30, "TTL remaining is positive and <= TTL");
        
        this_thread::sleep_for(chrono::seconds(1));
        
        long ttl2 = cache.getTTLRemaining("example.com");
        check(ttl2 < ttl, "TTL decreases over time");
    }

    // Test 10: Different TTL values
    {
        DNSCache cache5s(5);
        DNSCache cache60s(60);
        
        cache5s.put("example.com", "1.1.1.1");
        cache60s.put("example.com", "1.1.1.1");
        
        long ttl5 = cache5s.getTTLRemaining("example.com");
        long ttl60 = cache60s.getTTLRemaining("example.com");
        
        check(ttl60 > ttl5, "longer TTL has more time remaining");
    }

    cout << (failures ? "\nDNS CACHE TESTS FAILED\n" : "\nALL DNS CACHE TESTS PASSED\n");
    return failures ? 1 : 0;
}
