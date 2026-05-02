#include "Cache.h"

using namespace std;

LRUCache::LRUCache(int cap)
{
    capacity = cap;
}

bool LRUCache::exists(const string& key)
{
    return cacheMap.find(key) != cacheMap.end();
}

string LRUCache::get(const string& key)
{
    auto it = cacheMap[key];

    cacheList.splice(cacheList.begin(), cacheList, it);

    return it->second;
}

void LRUCache::put(const string& key, const string& value)
{
    if (cacheMap.find(key) != cacheMap.end())
    {
        cacheList.erase(cacheMap[key]);
        cacheMap.erase(key);
    }

    if (cacheList.size() == capacity)
    {
        auto last = cacheList.back();
        cacheMap.erase(last.first);
        cacheList.pop_back();
    }

    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();
}