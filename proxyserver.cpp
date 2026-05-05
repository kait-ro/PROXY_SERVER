#include "proxyserver.h"
#include "Authenticator.h"
#include "WebsiteFilter.h"
#include "Logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#include <fcntl.h>
#include <errno.h>
#include <unordered_map>
#include <mutex>
#include <ctime>

#define BUFFER_SIZE 65536
#define CONNECT_TIMEOUT_SEC 10
#define RECV_TIMEOUT_SEC    30
#define DNS_CACHE_TTL_SEC   30

// Thread-safe DNS cache: hostname -> (ipv4 string, expiry timestamp)
static mutex dnsCacheMutex;
static unordered_map<string, pair<string, time_t>> dnsCache;

// Returns resolved IPv4 address string for host, or "" on failure.
// Results are cached for DNS_CACHE_TTL_SEC seconds to avoid per-request lookups.
static string resolveHost(const string& host)
{
    {
        lock_guard<mutex> lock(dnsCacheMutex);
        auto it = dnsCache.find(host);
        if (it != dnsCache.end() && time(nullptr) < it->second.second)
            return it->second.first;
    }

    struct addrinfo hints{}, *res;
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0)
        return "";

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr,
              ip, sizeof(ip));
    freeaddrinfo(res);

    string ipStr(ip);
    {
        lock_guard<mutex> lock(dnsCacheMutex);
        dnsCache[host] = {ipStr, time(nullptr) + DNS_CACHE_TTL_SEC};
    }
    return ipStr;
}

// Returns true if non-blocking connect on fd completes within CONNECT_TIMEOUT_SEC seconds.
static bool connectWithTimeout(int fd, const struct sockaddr* addr, socklen_t addrlen)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int ret = connect(fd, addr, addrlen);
    if (ret == 0)
    {
        fcntl(fd, F_SETFL, flags);
        return true;
    }
    if (errno != EINPROGRESS)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(fd, &writefds);
    struct timeval tv = {CONNECT_TIMEOUT_SEC, 0};

    ret = select(fd + 1, NULL, &writefds, NULL, &tv);
    if (ret <= 0)
    {
        fcntl(fd, F_SETFL, flags);
        return false;
    }

    int err = 0;
    socklen_t errlen = sizeof(err);
    getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &errlen);
    fcntl(fd, F_SETFL, flags);
    return err == 0;
}

using namespace std;

thread_local int threadNumber = -1;

ProxyServer::ProxyServer(int port)
{
    this->port = port;
    auth.loadUsers("users.txt");
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
    while (start < request.size() && request[start] == ' ')
    start++;
size_t end = request.find("\r\n", start);
string host = request.substr(start, end - start);
size_t colonPos = host.find(":");
if (colonPos != string::npos)
host = host.substr(0, colonPos);
transform(host.begin(), host.end(), host.begin(), ::tolower);
return host;
}
void ProxyServer::setUser(const string& user, const
string& role)
{
currentUser = user;
currentRole = role;
}

string base64Decode(const string& input)
{
    static const string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[chars[i]] = i;

    string output;
    int val = 0, valb = -8;

    for (unsigned char c : input)
    {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0)
        {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return output;
}

void ProxyServer::handleClient(int client_socket)
{
vector<char> buffer(BUFFER_SIZE);
// RECEIVE REQUEST
int bytes = recv(client_socket, buffer.data(),
BUFFER_SIZE, 0);
if (bytes <= 0) return;
cout << "START handling on thread: " << threadNumber << endl;
string request(buffer.data(), bytes);
if (request.find("CONNECT") != 0)
{
cout << "\nREQUEST RECEIVED:\n" << endl;
}
// AUTHENTICATION
string username = "";
string role = "";

size_t authPos = request.find("Proxy-Authorization: Basic ");

if (authPos != string::npos)
{
    size_t start = request.find("Basic ") + 6;
    size_t end = request.find("\r\n", start);

    string encoded = request.substr(start, end - start);

    encoded.erase(encoded.find_last_not_of(" \r\n\t") + 1);
    encoded.erase(0, encoded.find_first_not_of(" \r\n\t"));

    string decoded = base64Decode(encoded);

    decoded.erase(decoded.find_last_not_of(" \r\n\t") + 1);
    decoded.erase(0, decoded.find_first_not_of(" \r\n\t"));

    size_t colon = decoded.find(':');

    if (colon != string::npos)
    {
        string user = decoded.substr(0, colon);
        string pass = decoded.substr(colon + 1);

        role = auth.login(user, pass);
        username = user;
    }
}
cout << "DEBUG → role: '" << role << "' username: '" << username << "'" << endl;
if (role == "")
{
    string response =
    "HTTP/1.1 407 Proxy Authentication Required\r\n"
    "Proxy-Authenticate: Basic realm=\"Proxy\"\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);

    cout << "Authentication failed\n";
    cout << "END handling on thread: " << threadNumber << endl;

    close(client_socket);
    return;
}
cout << "Authentication successful (" << role << ")\n";
//=========================================================
// HTTPS HANDLING
//=========================================================
if (request.find("CONNECT") == 0)
{
string host = extractHost(request);
cout << "HTTPS CONNECT to: " << host << endl;
// FILTER
if (filter.isBlockedForRole(host, role))
{
cout << "Blocked HTTPS site: " << host << endl;
string response =
"HTTP/1.1 403 Forbidden\r\n"
"Content-Type: text/plain\r\n"
"Content-Length: 18\r\n"
"Connection: close\r\n"
"\r\n"
"Blocked by proxy";
send(client_socket, response.c_str(), response.length(),
0);
logger.log("Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")", host, "HTTPS",
"BLOCKED");
cout << "END handling on thread: " << threadNumber << endl;
close(client_socket);
return;
}
logger.log("Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")", host, "HTTPS",
"ALLOWED");
// FIX 15: Parse port from CONNECT line (e.g. "CONNECT host:443 HTTP/1.1")
int connectPort = 443;
{
size_t portColon = request.find(":", request.find("CONNECT ") + 8);
size_t portEnd   = request.find(" ", portColon);
if (portColon != string::npos && portEnd != string::npos)
    connectPort = stoi(request.substr(portColon + 1, portEnd - portColon - 1));
}
// DNS with 30s cache — avoids repeated lookups for the same host
string httpsIP = resolveHost(host);
if (httpsIP.empty())
{
cout << "DNS failed\n";

    string response =
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);
    cout << "END handling on thread: " << threadNumber << endl;

return;
}
// CONNECT
int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
if (remote_socket < 0)
{
cout << "Socket creation failed\n";

    string response =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);
    cout << "END handling on thread: " << threadNumber << endl;
return;
}
struct sockaddr_in httpsAddr{};
httpsAddr.sin_family = AF_INET;
httpsAddr.sin_port   = htons(connectPort);
inet_pton(AF_INET, httpsIP.c_str(), &httpsAddr.sin_addr);
if (!connectWithTimeout(remote_socket, (sockaddr*)&httpsAddr, sizeof(httpsAddr)))
{
cout << "HTTPS connect failed\n";
    string response =
    "HTTP/1.1 504 Gateway Timeout\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);

    close(remote_socket);
    close(client_socket);
    cout << "END handling on thread: " << threadNumber << endl;
    return;
}
// TUNNEL ESTABLISHED
string response =
"HTTP/1.1 200 Connection Established\r\n\r\n";
send(client_socket, response.c_str(), response.length(),
0);
// BIDIRECTIONAL FORWARDING
fd_set fds;
cout << "Tunnel established successfully\n";
while (true)
{
FD_ZERO(&fds);
FD_SET(client_socket, &fds);
FD_SET(remote_socket, &fds);
int maxfd = max(client_socket, remote_socket) + 1;
int activity = select(maxfd, &fds, NULL, NULL, NULL);
if (activity <= 0)
break;
if (FD_ISSET(client_socket, &fds))
{
int bytes = recv(client_socket, buffer.data(),
BUFFER_SIZE, 0);
if (bytes <= 0) break;
send(remote_socket, buffer.data(), bytes, 0);
}
if (FD_ISSET(remote_socket, &fds))
{
int bytes = recv(remote_socket, buffer.data(),
BUFFER_SIZE, 0);
if (bytes <= 0) break;
send(client_socket, buffer.data(), bytes, 0);
}
}
close(remote_socket);
close(client_socket);
return;
}
//=========================================================
// HTTP HANDLING
//=========================================================
string host = extractHost(request);
cout << "HTTP HOST: " << host << endl;

string cacheKey;

size_t getPos = request.find("GET ");
size_t httpPos = request.find(" HTTP/");

if (getPos != string::npos && httpPos != string::npos)
{
string url = request.substr(getPos + 4, httpPos - (getPos + 4));

if (url.find("http://") == 0)
url = url.substr(7);

size_t slashPos = url.find('/');
string path = "/";

if (slashPos != string::npos)
path = url.substr(slashPos);

string normalizedHost = host;
transform(normalizedHost.begin(), normalizedHost.end(), normalizedHost.begin(), ::tolower);

if (normalizedHost.find("www.") == 0)
    normalizedHost = normalizedHost.substr(4);

cacheKey = "GET:" + normalizedHost + path;
}
else
{
cacheKey = host;
}

bool isGet = (request.find("GET ") == 0);

// FIX 3: Filter runs before cache — blocked users can't get cached content
if (filter.isBlockedForRole(host, role))
{
cout << "Blocked HTTP site: " << host << endl;
string response =
"HTTP/1.1 403 Forbidden\r\n\r\nBlocked by proxy";
send(client_socket, response.c_str(), response.length(),
0);
logger.log("Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")", host, "HTTP",
"BLOCKED");
close(client_socket);
cout << "END handling on thread: " << threadNumber << endl;
return;
}

// FIX 9/10: No servedFromCache flag; use real username in log
string cachedResponse;
if (isGet && cache.get(cacheKey, cachedResponse))
{
    cout << "CACHE HIT\n";

    send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);

    logger.log(
        "Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")",
        host,
        "HTTP",
        "CACHE HIT"
    );

    close(client_socket);
    return;
}

// DNS (cached)
string httpIP = resolveHost(host);
if (httpIP.empty())
{
    cout << "DNS resolution failed\n";

    string response =
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);
    return;
}

// CREATE SOCKET
int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
if (remote_socket < 0)
{
    cout << "Socket creation failed\n";
    close(client_socket);
    return;
}

// CONNECT
struct sockaddr_in httpAddr{};
httpAddr.sin_family = AF_INET;
httpAddr.sin_port   = htons(80);
inet_pton(AF_INET, httpIP.c_str(), &httpAddr.sin_addr);

if (!connectWithTimeout(remote_socket, (sockaddr*)&httpAddr, sizeof(httpAddr)))
{
    cout << "Connection to remote server failed\n";

    string response =
    "HTTP/1.1 504 Gateway Timeout\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);

    close(remote_socket);
    close(client_socket);
    return;
}

cout << "Connected to HTTP server\n";
// BUILD REQUEST
string modifiedRequest = request;

// FIX 7: Search for "\r\nConnection:" to avoid matching "Proxy-Connection:"
size_t connPos = modifiedRequest.find("\r\nConnection:");
if (connPos != string::npos)
{
    connPos += 2; // skip \r\n to point at "Connection:"
    size_t end = modifiedRequest.find("\r\n", connPos);
    modifiedRequest.replace(connPos, end - connPos, "Connection: close");
}
else
{
    size_t headerEnd = modifiedRequest.find("\r\n\r\n");
    if (headerEnd != string::npos)
    {
        modifiedRequest.insert(headerEnd + 2, "Connection: close\r\n");
    }
}

// Rewrite absolute URL to relative path (e.g. "GET http://host/path" → "GET /path")
size_t start = modifiedRequest.find("GET ");
size_t end = modifiedRequest.find(" HTTP/");

if (start != string::npos && end != string::npos)
{
    string url = modifiedRequest.substr(start + 4, end - (start + 4));

    if (url.find("http://") == 0)
    {
        string temp = url.substr(7);

        size_t slashPos = temp.find('/');
        string path = (slashPos != string::npos) ? temp.substr(slashPos) : "/";

        modifiedRequest.replace(start + 4, url.length(), path);
    }
}

// FIX 4: Strip hop-by-hop headers — must not be forwarded to origin server
for (const string& hdr : {"Proxy-Authorization", "Proxy-Connection"})
{
    string search = "\r\n" + hdr + ":";
    size_t pos = modifiedRequest.find(search);
    while (pos != string::npos)
    {
        size_t lineEnd = modifiedRequest.find("\r\n", pos + 2);
        if (lineEnd == string::npos) break;
        modifiedRequest.erase(pos, lineEnd - pos);
        pos = modifiedRequest.find(search);
    }
}

// DEBUG
cout << "===== FORWARDING REQUEST =====\n";
cout << modifiedRequest << endl;
// TIMEOUT
struct timeval timeout;
timeout.tv_sec = RECV_TIMEOUT_SEC;
timeout.tv_usec = 0;
setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO,
&timeout, sizeof(timeout));
// FIX 1: Send the request to the remote server before reading a response
send(remote_socket, modifiedRequest.c_str(), modifiedRequest.size(), 0);
// FORWARD RESPONSE
string fullResponse;

while ((bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0)) > 0)
{
    fullResponse.append(buffer.data(), bytes);
    send(client_socket, buffer.data(), bytes, 0);
}
cout << "Response sent back to browser\n";

logger.log(
    "Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")",
    host,
    "HTTP",
    "ALLOWED"
);

// FIX 2: Only cache valid HTTP responses (non-empty, starts with "HTTP/")
if (!cacheKey.empty() && isGet && fullResponse.size() > 0 && fullResponse.size() < 100000 && fullResponse.find("HTTP/") == 0)
{
cache.put(cacheKey, fullResponse);
cout << "CACHE STORED for user: " << username << endl;

logger.log(
    "Thread-" + to_string(threadNumber) + " " + username + "(" + role + ")",
    host,
    "HTTP",
    "CACHE STORED"
);
}
cout << "END handling on thread: " << threadNumber << endl;
close(remote_socket);
close(client_socket);
}

void ProxyServer::workerThread()
{
    while (true)
    {
        int client_socket;

        {
            unique_lock<mutex> lock(queueMutex);

            cv.wait(lock, [this] {
                return !clientQueue.empty();
            });

            client_socket = clientQueue.front();
            clientQueue.pop();
        }

        handleClient(client_socket);
    }
}

void ProxyServer::startServer()
{
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        cout << "Socket creation failed\n";
        return;
    }

    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = INADDR_ANY;

    if (::bind(server_fd, (sockaddr*)&address, sizeof(address)) < 0)
    {
        cout << "Bind failed\n";
        return;
    }

    // FIX 14: Larger backlog for a 20-thread proxy
    listen(server_fd, SOMAXCONN);

    cout << "Proxy server running on port " << port << endl;

    // FIX 6: Detach each thread immediately after creation
    const int THREAD_COUNT = 20;
    vector<thread> workers;
    for (int i = 0; i < THREAD_COUNT; i++)
    {
        workers.emplace_back([this, i]() {
            threadNumber = i + 1;
            cout << "Thread-" << threadNumber << " started\n";
            workerThread();
        });
        workers.back().detach();
    }

    while (true)
    {
        int client_socket = accept(server_fd, NULL, NULL);
        if (client_socket < 0)
            continue;

        {
            unique_lock<mutex> lock(queueMutex);
            clientQueue.push(client_socket);
        }

        cv.notify_one();
    }

    close(server_fd);
}
