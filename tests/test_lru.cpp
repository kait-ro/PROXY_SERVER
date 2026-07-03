// Minimal, dependency-free unit tests for LRUCache.
// Build/run:  make test
#include "../LRUCache.h"
#include <cassert>
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { std::cout << "PASS  " << name << "\n"; }
        else      { std::cout << "FAIL  " << name << "\n"; ++failures; }
    };

    // 1. basic put/get
    {
        LRUCache c(2);
        c.put("a", "1");
        std::string v;
        check(c.get("a", v) && v == "1", "get returns stored value");
    }
    // 2. miss on unknown key
    {
        LRUCache c(2);
        std::string v;
        check(!c.get("missing", v), "get misses on unknown key");
    }
    // 3. LRU eviction when over capacity
    {
        LRUCache c(2);
        c.put("a", "1");
        c.put("b", "2");
        c.put("c", "3");            // evicts least-recently-used ("a")
        std::string v;
        check(!c.get("a", v), "least-recently-used key evicted");
        check(c.get("b", v) && v == "2", "surviving key still present");
        check(c.get("c", v) && v == "3", "newest key present");
    }
    // 4. update refreshes value
    {
        LRUCache c(2);
        c.put("k", "old");
        c.put("k", "new");
        std::string v;
        check(c.get("k", v) && v == "new", "put updates existing value");
    }

    std::cout << (failures ? "\nTESTS FAILED\n" : "\nALL TESTS PASSED\n");
    return failures ? 1 : 0;
}
