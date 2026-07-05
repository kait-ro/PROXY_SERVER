// Unit tests for WebsiteFilter class
// Tests filtering rules, blocking, and role-based access
// Build: g++ -std=c++17 -Wall test_websitefilter.cpp ../WebsiteFilter.cpp -o test_filter
// Run:   ./test_filter

#include "../WebsiteFilter.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>

using namespace std;

// Create test filter files
void createTestFilterFiles() {
    // Create blocked_sites.txt
    ofstream blocked("blocked_sites_test.txt");
    if (blocked.is_open()) {
        blocked << "malware.com\n";
        blocked << "phishing.net\n";
        blocked << "dangerous.org\n";
        blocked << "evil.io\n";
        blocked.close();
    }

    // Create ai_blocked_sites.txt
    ofstream aiBlocked("ai_blocked_sites_test.txt");
    if (aiBlocked.is_open()) {
        aiBlocked << "hackingtools.com\n";
        aiBlocked.close();
    }

    // Create ai_gaming.txt
    ofstream gaming("ai_gaming_test.txt");
    if (gaming.is_open()) {
        gaming << "steam.com\n";
        gaming << "epicgames.com\n";
        gaming << "twitch.tv\n";
        gaming.close();
    }

    // Create ai_social.txt
    ofstream social("ai_social_test.txt");
    if (social.is_open()) {
        social << "facebook.com\n";
        social << "twitter.com\n";
        social << "instagram.com\n";
        social.close();
    }
}

class TestWebsiteFilter : public WebsiteFilter {
public:
    void loadTestFiles() {
        loadSites("blocked_sites_test.txt");
        loadSites("ai_blocked_sites_test.txt");
        loadCategory("ai_gaming_test.txt", gamingSites);
        loadCategory("ai_social_test.txt", socialSites);
    }
protected:
    unordered_set<string> gamingSites;
    unordered_set<string> socialSites;
    friend class WebsiteFilter;
};

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { cout << "PASS  " << name << "\n"; }
        else      { cout << "FAIL  " << name << "\n"; ++failures; }
    };

    createTestFilterFiles();

    // Test 1: Basic blocking - exact match
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("malware.com"), "exactly blocked site is not allowed");
    }

    // Test 2: Domain normalization - http prefix
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("http://malware.com"), "http prefix is normalized");
    }

    // Test 3: Domain normalization - https prefix
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("https://malware.com"), "https prefix is normalized");
    }

    // Test 4: Domain normalization - www prefix
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("www.malware.com"), "www prefix is normalized");
    }

    // Test 5: Case insensitivity
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("MALWARE.COM"), "domains are case-insensitive");
    }

    // Test 6: Subdomain blocking - subdomain of blocked site
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("sub.malware.com"), "subdomain of blocked site is blocked");
    }

    // Test 7: Multiple subdomain levels
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("deep.sub.malware.com"), "deep subdomain of blocked site is blocked");
    }

    // Test 8: Allowed site passes through
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(filter.isAllowed("google.com"), "legitimate site is allowed");
    }

    // Test 9: Role-based - admin bypasses all filters
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isBlockedForRole("malware.com", "admin"), "admin role bypasses all blocks");
        check(!filter.isBlockedForRole("steam.com", "admin"), "admin role bypasses gaming blocks");
        check(!filter.isBlockedForRole("facebook.com", "admin"), "admin role bypasses social blocks");
    }

    // Test 10: User role blocks gaming sites
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(filter.isBlockedForRole("steam.com", "user"), "user role blocks gaming sites");
        check(filter.isBlockedForRole("epicgames.com", "user"), "user role blocks epicgames");
        check(filter.isBlockedForRole("twitch.tv", "user"), "user role blocks twitch");
    }

    // Test 11: User role blocks social sites
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(filter.isBlockedForRole("facebook.com", "user"), "user role blocks social sites");
        check(filter.isBlockedForRole("twitter.com", "user"), "user role blocks twitter");
        check(filter.isBlockedForRole("instagram.com", "user"), "user role blocks instagram");
    }

    // Test 12: User role blocks malware
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(filter.isBlockedForRole("malware.com", "user"), "user role blocks malware");
    }

    // Test 13: Guest role blocks gaming
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(filter.isBlockedForRole("steam.com", "guest"), "guest role blocks gaming");
    }

    // Test 14: Admin role allows gaming and social
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isBlockedForRole("steam.com", "admin"), "admin role allows gaming");
        check(!filter.isBlockedForRole("facebook.com", "admin"), "admin role allows social");
    }

    // Test 15: Allowed site for user role
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isBlockedForRole("google.com", "user"), "user role allows legitimate sites");
    }

    // Test 16: Path in domain is stripped
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("malware.com/path/to/page"), "path is stripped from domain");
    }

    // Test 17: Port in domain is stripped
    {
        TestWebsiteFilter filter;
        filter.loadTestFiles();
        check(!filter.isAllowed("malware.com:8080"), "port is stripped from domain");
    }

    // Cleanup
    remove("blocked_sites_test.txt");
    remove("ai_blocked_sites_test.txt");
    remove("ai_gaming_test.txt");
    remove("ai_social_test.txt");

    cout << (failures ? "\nWEBSITE FILTER TESTS FAILED\n" : "\nALL WEBSITE FILTER TESTS PASSED\n");
    return failures ? 1 : 0;
}
