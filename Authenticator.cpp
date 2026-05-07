#include "Authenticator.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <openssl/sha.h>
#include <iomanip>

using namespace std;

string hashPassword(const string& password)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256((unsigned char*)password.c_str(), password.size(), hash);

    stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        ss << hex << setw(2) << setfill('0') << (int)hash[i];
    }

    return ss.str();
}

void Authenticator::loadUsers(const string& filename)
{
    ifstream file(filename);
    if (!file.is_open())
    {
        cerr << "Authenticator: failed to open " << filename << " — no users loaded\n";
        return;
    }
    string user, pass, role;
    while (file >> user >> pass >> role)
    {
        users[user] = {pass, role};
    }
    file.close();
}

string Authenticator::login(const string& user, const string& pass)
{
    string hashedPass = hashPassword(pass);

    if (users.find(user) != users.end() && users[user].first == hashedPass)
    {
        return users[user].second;
    }
    return "";
}

bool Authenticator::signup(const string& user, const string& pass, const string& role)
{
    if (users.find(user) != users.end())
    {
        cerr << "Authenticator: user '" << user << "' already exists\n";
        return false;
    }

    ofstream file("users.txt", ios::app);
    string hashedPass = hashPassword(pass);
    file << "\n" << user << " " << hashedPass << " " << role << endl;
    file.close();

    users[user] = {hashedPass, role};
    return true;
}