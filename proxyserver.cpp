#include "proxyserver.h"
#include "Authenticator.h"
#include "WebsiteFilter.h"
#include "Logger.h"

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>  
//multithreading 
#include <pthread.h>
#include <semaphore.h>
#include <utility>

#define BUFFER_SIZE 8192

using namespace std;

sem_t thread_limit;
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

ProxyServer::ProxyServer(int port)
{
    this->port = port;
    auth.loadUsers("users.txt");
    filter.loadSites("blocked_sites.txt");
}

string ProxyServer::extractHost(const string& request)
{
    if (request.find("CONNECT") == 0)
    {
        size_t start = request.find("CONNECT ") + 8;
        size_t end = request.find(":", start);
        return request.substr(start, end - start);
    }

    size_t pos = request.find("Host:");

    if (pos == string::npos)
        return "";

    size_t start = pos + 6;
    size_t end = request.find("\r\n", start);
    string host = request.substr(start, end - start);
    size_t colonPos = host.find(":");

    if (colonPos != string::npos)
    host = host.substr(0, colonPos);

    return host;
}

void ProxyServer::setUser(const string& user, const string& role)
{
    currentUser = user;
    currentRole = role;
}

void ProxyServer::handleClient(int client_socket)
{
    vector<char> buffer(BUFFER_SIZE);

    //  RECEIVE REQUEST 
    int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
    if (bytes <= 0) return;

    string request(buffer.data(), bytes);

if (request.find("CONNECT") != 0)
{
    // cout << "\nREQUEST RECEIVED:\n" << request << endl;
}
    //  AUTHENTICATION 
    string username = currentUser;
    string role = currentRole;

    if (role == "")
    {
        cout << "Authentication failed\n";
        return;
    }

    cout << "[AUTH] " << username << " (" << role << ")\n";

    // =========================================================
    // HTTPS HANDLING
    // =========================================================
    if (request.find("CONNECT") == 0)
    {
        string host = extractHost(request);

        cout << "[HTTPS] " << host << endl;
        // FILTER 
        if (role != "admin" && !filter.isAllowed(host))
        {
            cout << "[BLOCKED] " << host << endl;
            string response =
                "HTTP/1.1 403 Forbidden\r\n\r\nBlocked by proxy";

            send(client_socket, response.c_str(), response.length(), 0);
            logger.log(username + "(" + role + ")", host, "HTTPS", "BLOCKED");
            return;
        }
        logger.log(username + "(" + role + ")", host, "HTTPS", "ALLOWED");
        //  DNS 
        struct hostent* server = gethostbyname(host.c_str());
        if (server == NULL)
        {
            cout << "DNS failed\n";
            return;
        }

        // CONNECT
        int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (remote_socket < 0)
        {
            cout << "Socket creation failed\n";
            return;
        }

        struct sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(443);

        memcpy(&server_addr.sin_addr.s_addr,
               server->h_addr,
               server->h_length);

        if (connect(remote_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0)
        {
            cout << "HTTPS connect failed\n";
            close(remote_socket);
            return;
        }

        // TUNNEL ESTABLISHED 
        string response =
            "HTTP/1.1 200 Connection Established\r\n\r\n";

        send(client_socket, response.c_str(), response.length(), 0);

        // BIDIRECTIONAL FORWARDING 
        fd_set fds;
        cout << "Tunnel established successfully\n";
        while (true)
        {
            FD_ZERO(&fds);
            FD_SET(client_socket, &fds);
            FD_SET(remote_socket, &fds);

            int maxfd = std::max(client_socket, remote_socket) + 1;

            int activity = select(maxfd, &fds, NULL, NULL, NULL);

            if (activity <= 0)
                break;

            if (FD_ISSET(client_socket, &fds))
            {
                int bytes = recv(client_socket, buffer.data(), BUFFER_SIZE, 0);
                if (bytes <= 0) break;
                send(remote_socket, buffer.data(), bytes, 0);
            }

            if (FD_ISSET(remote_socket, &fds))
            {
                int bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0);
                if (bytes <= 0) break;
                send(client_socket, buffer.data(), bytes, 0);
            }
        }

        close(remote_socket);
        return;
    }

    // =========================================================
    // HTTP HANDLING
    // =========================================================

    string host = extractHost(request);
    string key = host + request;
    cout << "[REQUEST] " << host << endl;

    //  FILTER 
    if (role != "admin" && !filter.isAllowed(host))
    {
        cout << "[BLOCKED] " << host << endl;
        string response =
            "HTTP/1.1 403 Forbidden\r\n\r\nBlocked by proxy";

        send(client_socket, response.c_str(), response.length(), 0);
        logger.log(username + "(" + role + ")", host, "HTTP", "BLOCKED");
        return;
    }
    logger.log(username + "(" + role + ")", host, "HTTP", "ALLOWED");

    pthread_mutex_lock(&cache_mutex);

    if (cache.exists(key))
    {
        string cachedResponse = cache.get(key);

        pthread_mutex_unlock(&cache_mutex);

        send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);

        cout << "[CACHE HIT] " << host << endl;
        return;
    }

    pthread_mutex_unlock(&cache_mutex);

    // DNS 
    struct hostent* server = gethostbyname(host.c_str());
    if (server == NULL)
    {
        cout << "Could not find host\n";
        return;
    }

    // CONNECT 
    int remote_socket = socket(AF_INET, SOCK_STREAM, 0); // AF_INET means ipv4 addresses(eg: 192.168.0.0) 
    // SOCK_STREAM means TCP protocol and TCP decides how data is sent and received, it also ensures that data is received in order and without errors.
    if (remote_socket < 0)
    {
        cout << "Socket creation failed\n";
        return;
    }

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(80);

    memcpy(&server_addr.sin_addr.s_addr, server->h_addr, server->h_length);

    if (connect(remote_socket, (sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        cout << "Connection to remote server failed\n";
        close(remote_socket);
        return;
    }

    cout << "[HTTP FETCH] " << host << endl;
    // FIX REQUEST LINE 
    string modifiedRequest = request;

    size_t start = modifiedRequest.find("GET ");
    size_t end = modifiedRequest.find(" HTTP/");

    if (start != string::npos && end != string::npos)
    {
        string url = modifiedRequest.substr(start + 4, end - (start + 4));

        size_t slashPos = url.find('/', 7);
        string path = "/";

        if (slashPos != string::npos)
            path = url.substr(slashPos);

        modifiedRequest = "GET " + path + modifiedRequest.substr(end);
    }

    send(remote_socket, modifiedRequest.c_str(), modifiedRequest.length(), 0);

    // TIMEOUT 
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // FORWARD RESPONSE 
    string fullResponse = "";

    while (true)
    {
        bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0);
        if (bytes <= 0) break;

        fullResponse.append(buffer.data(), bytes);

        send(client_socket, buffer.data(), bytes, 0);
    }

    cout << "Response sent back to browser\n";

    pthread_mutex_lock(&cache_mutex);
    cache.put(key, fullResponse);
    cout << "[CACHE STORE] " << host << endl;
    pthread_mutex_unlock(&cache_mutex);

    close(remote_socket);
}

void* clientHandler(void* arg)
{
    auto* data = (pair<ProxyServer*, int>*)arg;

    ProxyServer* server = data->first;
    int client_socket = data->second;

    delete data;

    server->handleClient(client_socket);
    close(client_socket);

    sem_post(&thread_limit);
    return NULL;
}

void ProxyServer::startServer()
{
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(server_fd < 0) {
        cout << "Socket creation failed\n";
        return;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    if(::bind(server_fd,(sockaddr*)&address, sizeof(address)) < 0){
        cout << "Bind failed\n"; 
        return;
    }

    listen(server_fd, 6);

    cout << "Proxy server running on port " << port << endl;

    sem_init(&thread_limit, 0, 10);

    while (true)
    {
        int client_socket = accept(server_fd, NULL, NULL);

        if (client_socket < 0) continue;

        sem_wait(&thread_limit);

        pthread_t tid;

        auto* data = new pair<ProxyServer*, int>(this, client_socket);

        pthread_create(&tid, NULL, clientHandler, data);

        pthread_detach(tid);
    }

    close(server_fd);
}