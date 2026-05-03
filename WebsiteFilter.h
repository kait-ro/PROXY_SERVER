#ifndef WEBSITE_FILTER_H
#define WEBSITE_FILTER_H
#include <string>
#include <algorithm> 
#include <unordered_set>

class WebsiteFilter {
private:
    std::unordered_set<std::string> blockedSites;
    std::unordered_set<std::string> gamingSites;
    std::unordered_set<std::string> socialSites;

public:
    WebsiteFilter();
    void loadSites(const std::string& filename);
    void loadCategory(const std::string& filename, std::unordered_set<std::string>& categorySet);
    bool isAllowed(const std::string& domain);
    bool isBlockedForRole(const std::string& domain, const std::string& role);
};
#endif