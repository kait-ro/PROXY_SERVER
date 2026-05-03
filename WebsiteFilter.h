#ifndef WEBSITE_FILTER_H
#define WEBSITE_FILTER_H
#include <string>
#include <algorithm> 
#include <unordered_set>

class WebsiteFilter {
private:
    std::unordered_set<std::string> blockedSites;

public:
    WebsiteFilter();
    void loadSites(const std::string& filename);
    bool isAllowed(const std::string& domain);
};
#endif