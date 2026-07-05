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
#include <atomic>
#include <unordered_map>
#include <ctime>

extern thread_local int threadNumber;

// DDoS Protection: Per-user and per-IP metrics
struct ClientMetrics {
    int activeConnections = 0;
    time_t lastRequestTime = time(nullptr);
    int requestsThisSecond = 0;
};

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
    LRUCache cache{200};
    
    // DDoS Protection
    std::unordered_map<std::string, ClientMetrics> userMetrics;
    std::unordered_map<std::string, ClientMetrics> ipMetrics;
    std::mutex metricsMutex;
    std::atomic<int> queueSize{0};
    
    // DDoS Limits (configurable)
    static constexpr int MAX_REQUESTS_PER_SEC = 100;
    static constexpr int MAX_CONNECTIONS_PER_USER = 10;
    static constexpr int MAX_CONNECTIONS_PER_IP = 20;
    static constexpr int MAX_QUEUE_SIZE = 200;
    static constexpr int IDLE_TIMEOUT_SEC = 300;
    
public:
ProxyServer(int port);
void setUser(const std::string& user, const std::string& role);
void startServer();
void handleClient(int client_socket);
std::string extractHost(const std::string& request);
void workerThread();

// DDoS Protection methods
bool checkAndUpdateUserRateLimit(const std::string& user);
bool checkAndUpdateIPRateLimit(const std::string& ip);
bool checkUserConnectionLimit(const std::string& user);
void recordUserConnection(const std::string& user, int delta);
std::string getClientIP(int client_socket);
};
#endif