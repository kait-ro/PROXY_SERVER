#include "LRUCache.h"

LRUCache::LRUCache(int cap)
{
    capacity = cap;
}

bool LRUCache::get(const string& key, string& value)
{
    lock_guard<mutex> lock(cacheMutex);

    if (cacheMap.find(key) == cacheMap.end())
        return false;

    auto it = cacheMap[key];

    cacheList.splice(cacheList.begin(), cacheList, it);

    value = it->second;
    return true;
}

void LRUCache::put(const string& key, const string& value)
{
    lock_guard<mutex> lock(cacheMutex);

    if (cacheMap.find(key) != cacheMap.end())
    {
        cacheList.erase(cacheMap[key]);
        cacheMap.erase(key);
    }

    cacheList.push_front({key, value});
    cacheMap[key] = cacheList.begin();

    if ((int)cacheList.size() > capacity)
    {
        auto last = cacheList.back();
        cacheMap.erase(last.first);
        cacheList.pop_back();
    }
}