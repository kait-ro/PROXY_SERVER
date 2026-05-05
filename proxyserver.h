#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H
#include "Authenticator.h"
#include "WebsiteFilter.h"
#include "Logger.h"
#include "LRUCache.h"

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <semaphore.h>
#include <atomic>

extern thread_local int threadNumber;

class ProxyServer
{
private:
int server_fd;
int port;
Authenticator auth;
WebsiteFilter filter;
Logger logger;
std::string currentUser;
std::string currentRole;
    std::queue<int> clientQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    std::mutex authMutex;
    sem_t clientSlots;
    LRUCache cache{5}; 
public:
ProxyServer(int port);
void setUser(const std::string& user, const std::string&
role);
void startServer();
void handleClient(int client_socket);
std::string extractHost(const std::string& request);
void workerThread();
};
#endif