#ifndef WEBSITE_FILTER_H
#define WEBSITE_FILTER_H

#include <string>
#include <map>

class WebsiteFilter {
private:
    std::map<std::string, bool> blockedSites;

public:
    void loadSites(const std::string& filename);
    bool isAllowed(const std::string& domain);
};

#endif