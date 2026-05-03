#include "WebsiteFilter.h"
#include <fstream>
#include <iostream>
#include <algorithm>

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
    loadCategory("ai_gaming.txt", gamingSites);
    loadCategory("ai_social.txt", socialSites);
}

void WebsiteFilter::loadSites(const string& filename)
{
    ifstream file(filename);
    string site;

    if (!file.is_open())
    {
        cout << "Warning: Could not open " << filename << endl;
        return;
    }

    while (file >> site)
    {
        site = normalizeDomain(site);

        if (!isValidDomain(site))
            continue;

        blockedSites.insert(site);
    }

    file.close();
}

void WebsiteFilter::loadCategory(const string& filename, unordered_set<string>& categorySet)
{
    ifstream file(filename);
    string site;

    if (!file.is_open())
    {
        cout << "Warning: Could not open " << filename << endl;
        return;
    }

    while (file >> site)
    {
        site = normalizeDomain(site);

        if (!isValidDomain(site))
            continue;

        categorySet.insert(site);
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

bool WebsiteFilter::isBlockedForRole(const string& domain, const string& role)
{
    string d = normalizeDomain(domain);

    // ADMIN → no restrictions
    if (role == "admin")
        return false;

    // GENERAL BLOCK
    if (blockedSites.find(d) != blockedSites.end())
        return true;

    // SUBDOMAIN CHECK
    size_t pos = d.find('.');
    while (pos != string::npos)
    {
        string sub = d.substr(pos + 1);

        if (blockedSites.find(sub) != blockedSites.end())
            return true;

        pos = d.find('.', pos + 1);
    }

    // ROLE-BASED CATEGORY FILTERING

    if (role == "student" || role == "user")
    {
        if (gamingSites.find(d) != gamingSites.end())
            return true;

        if (adultSites.find(d) != adultSites.end())
            return true;
    }

    if (role == "restricted")
    {
        if (socialSites.find(d) != socialSites.end())
            return true;
    }

    return false;
}