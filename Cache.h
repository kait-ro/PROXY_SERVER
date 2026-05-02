#ifndef CACHE_H
#define CACHE_H

#include <string>
#include <unordered_map>
#include <list>

class LRUCache
{
private:
    int capacity;

    std::list<std::pair<std::string, std::string>> cacheList;

    std::unordered_map<
        std::string,
        std::list<std::pair<std::string, std::string>>::iterator
    > cacheMap;

public:
    LRUCache(int cap);

    bool exists(const std::string& key);

    std::string get(const std::string& key);

    void put(const std::string& key, const std::string& value);
};

#endif