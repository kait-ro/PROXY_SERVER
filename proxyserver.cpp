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
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <cstring>
#define BUFFER_SIZE 65536
using namespace std;
ProxyServer::ProxyServer(int port)
{
    this->port = port;
    auth.loadUsers("users.txt");
    sem_init(&clientSlots, 0, 20);
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
cout << "START handling on thread: " << this_thread::get_id() << endl;
string request(buffer.data(), bytes);
if (request.find("CONNECT") != 0)
{
cout << "\nREQUEST RECEIVED:\n" << endl;
}
// AUTHENTICATION
string username = "";
string role = "";

{
    std::lock_guard<std::mutex> lock(authMutex);
    if (authCache.find(client_socket) != authCache.end())
    {
        role = authCache[client_socket];
        username = "cached_user";
    }
}
if (role == "") {
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
    if (role != "")
    {
        std::lock_guard<std::mutex> lock(authMutex);
        authCache[client_socket] = role;
    }
}
}
if (role == "")
{
    string response =
    "HTTP/1.1 407 Proxy Authentication Required\r\n"
    "Proxy-Authenticate: Basic realm=\"Proxy\"\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    cout << "Authentication failed\n";
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
logger.log(username + "(" + role + ")", host, "HTTPS",
"BLOCKED");
close(client_socket);
return;
}
logger.log(username + "(" + role + ")", host, "HTTPS",
"ALLOWED");
// DNS
struct hostent* server = gethostbyname(host.c_str());
if (server == NULL)
{
cout << "DNS failed\n";

    string response =
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);

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

return;
}
struct sockaddr_in server_addr{};
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(443);
memcpy(&server_addr.sin_addr.s_addr,
server->h_addr,
server->h_length);
if (connect(remote_socket, (sockaddr*)&server_addr,
sizeof(server_addr)) < 0)
{
cout << "HTTPS connect failed\n";
    string response =
    "HTTP/1.1 504 Gateway Timeout\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);

    close(remote_socket);
    close(client_socket);
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
int maxfd = std::max(client_socket, remote_socket) + 1;
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

cacheKey = host + path;
}
else
{
cacheKey = host;
}

bool isGet = (request.find("GET ") == 0);
string cachedResponse;
if (isGet && cache.get(cacheKey, cachedResponse))
{
cout << "CACHE HIT\n";
send(client_socket, cachedResponse.c_str(), cachedResponse.size(), 0);
close(client_socket); 
return;
}


// FILTER
if (filter.isBlockedForRole(host, role))
{
cout << "Blocked HTTP site: " << host << endl;
string response =
"HTTP/1.1 403 Forbidden\r\n\r\nBlocked by proxy";
send(client_socket, response.c_str(), response.length(),
0);
logger.log(username + "(" + role + ")", host, "HTTP",
"BLOCKED");
close(client_socket);
return;
}
logger.log(username + "(" + role + ")", host, "HTTP",
"ALLOWED");
// DNS
struct hostent* server = gethostbyname(host.c_str());
if (server == NULL)
{
    cout << "Could not find host\n";
    string response =
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);
    return;
}
// CONNECT
int remote_socket = socket(AF_INET, SOCK_STREAM, 0); // AF_INET means ipv4 addresses(eg: 192.168.0.0)
// SOCK_STREAM means TCP protocol and TCP decides how data is sent and received, it also ensures that data is received in order and without errors.
if (remote_socket < 0)
{
cout << "Socket creation failed\n";

    string response =
    "HTTP/1.1 500 Internal Server Error\r\n"
    "Connection: close\r\n\r\n";

    send(client_socket, response.c_str(), response.length(), 0);
    close(client_socket);

return;
}
struct sockaddr_in server_addr{};
server_addr.sin_family = AF_INET;
server_addr.sin_port = htons(80);
memcpy(&server_addr.sin_addr.s_addr, server->h_addr,
server->h_length);
if (connect(remote_socket, (sockaddr*)&server_addr,
sizeof(server_addr)) < 0)
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
// FIX REQUEST LINE
string modifiedRequest = request;
size_t start = modifiedRequest.find("GET ");
size_t end = modifiedRequest.find(" HTTP/");
if (start != string::npos && end != string::npos)
{
string url = modifiedRequest.substr(start + 4, end -
(start + 4));
size_t slashPos = url.find('/', 7);
string path = "/";
if (slashPos != string::npos)
path = url.substr(slashPos);
modifiedRequest = "GET " + path +
modifiedRequest.substr(end);
}
send(remote_socket, modifiedRequest.c_str(),
modifiedRequest.length(), 0);
// TIMEOUT
struct timeval timeout;
timeout.tv_sec = 1;
timeout.tv_usec = 0;
setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO,
&timeout, sizeof(timeout));
// FORWARD RESPONSE
string fullResponse;

while (true)
{
bytes = recv(remote_socket, buffer.data(), BUFFER_SIZE, 0);
if (bytes <= 0) break;

fullResponse.append(buffer.data(), bytes);
send(client_socket, buffer.data(), bytes, 0);
}
cout << "Response sent back to browser\n";

if (!cacheKey.empty() && isGet && fullResponse.size() < 100000)
{
cache.put(cacheKey, fullResponse);
cout << "CACHE STORED\n";
}
cout << "END handling on thread: " << this_thread::get_id() << endl;
close(remote_socket);
}

void ProxyServer::workerThread()
{
    while (true)
    {
        int client_socket;

        {
            std::unique_lock<std::mutex> lock(queueMutex);

            cv.wait(lock, [this] {
                return !clientQueue.empty();
            });

            client_socket = clientQueue.front();
            clientQueue.pop();
        } 

        sem_wait(&clientSlots);
        handleClient(client_socket);
        sem_post(&clientSlots); 
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

    listen(server_fd, 6);

    cout << "Proxy server running on port " << port << endl;

    const int THREAD_COUNT = 20;
    vector<thread> workers;
    for (auto& t : workers)
    t.detach();

    for (int i = 0; i < THREAD_COUNT; i++)
    {
        workers.emplace_back(&ProxyServer::workerThread, this);
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