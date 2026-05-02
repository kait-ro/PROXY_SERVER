#ifndef PROXY_SERVER_H
#define PROXY_SERVER_H
#include "Authenticator.h"
#include "WebsiteFilter.h"
#include "Logger.h"
#include <string>
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
public:
ProxyServer(int port);
void setUser(const std::string& user, const std::string&
role);
void startServer();
void handleClient(int client_socket);
std::string extractHost(const std::string& request);
};
#endif