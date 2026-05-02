#ifndef WEBSITE_FILTER_H
#define WEBSITE_FILTER_H
#include <string>
#include <map>
using namespace std;
class WebsiteFilter {
private:
map<string, bool> blockedSites;
public:
void loadSites(const string& filename);
bool isAllowed(const string& domain);
};
#endif