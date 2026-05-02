#ifndef AUTHENTICATOR_H
#define AUTHENTICATOR_H

#include <string>
#include <map>

class Authenticator {
private:
    std::map<std::string, std::pair<std::string, std::string>> users;

public:
    void loadUsers(const std::string& filename);
    std::string login(const std::string& user, const std::string& pass);
    void signup(const std::string& user,
                const std::string& pass,
                const std::string& role);
};

#endif