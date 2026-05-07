#ifndef AUTHENTICATOR_H
#define AUTHENTICATOR_H
using namespace std;
#include <string>
#include <map>
class Authenticator {
private:
    map<string, pair<string, string>> users;
public:
    void loadUsers(const string& filename);
    string login(const string& user, const string& pass);
    bool signup(const string& user,
    const string& pass,
    const string& role);
};
#endif