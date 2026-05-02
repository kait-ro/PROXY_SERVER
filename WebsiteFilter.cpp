#include "WebsiteFilter.h"
#include <fstream>
using namespace std;
void WebsiteFilter::loadSites(const string& filename)
{
ifstream file(filename);
string site;
while (file >> site)
{
blockedSites[site] = true;
}
file.close();
}
bool WebsiteFilter::isAllowed(const string& domain)
{
for (auto &site : blockedSites)
{
if (domain.find(site.first) != string::npos)
return false;
}
return true;
}