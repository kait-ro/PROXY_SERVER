#include "WebsiteFilter.h"
#include <fstream>
#include <iostream>

using namespace std;

string normalizeDomain(string domain)
{
    transform(domain.begin(), domain.end(), domain.begin(), ::tolower);

    if (domain.find("http://") == 0)
        domain = domain.substr(7);
    else if (domain.find("https://") == 0)
        domain = domain.substr(8);

    if (domain.find("www.") == 0)
        domain = domain.substr(4);

    size_t slash = domain.find('/');
    if (slash != string::npos)
        domain = domain.substr(0, slash);

    return domain;
}


bool isValidDomain(const string& domain)
{
    if (domain.empty())
        return false;

    if (domain.find('.') == string::npos)
        return false;

    if (domain.find(' ') != string::npos)
        return false;

    return true;
}

WebsiteFilter::WebsiteFilter()
{
    loadSites("blocked_sites.txt");
    loadSites("ai_blocked_sites.txt");  
}

void WebsiteFilter::loadSites(const string& filename)
{
    ifstream file(filename);
    string site;

    while (file >> site)
    {
        site = normalizeDomain(site);

        if (!isValidDomain(site))
            continue;

        blockedSites.insert(site);
    }
    file.close();
}

bool WebsiteFilter::isAllowed(const string& domain)
{
    string d = normalizeDomain(domain);

    if (blockedSites.find(d) != blockedSites.end())
        return false;

    size_t pos = d.find('.');

    while (pos != string::npos)
    {
        string sub = d.substr(pos + 1);

        if (blockedSites.find(sub) != blockedSites.end())
            return false;

        pos = d.find('.', pos + 1);
    }

    return true;
}