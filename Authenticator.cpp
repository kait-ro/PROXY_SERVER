#include "Authenticator.h"
#include <fstream>
using namespace std;
void Authenticator::loadUsers(const string& filename)
{
ifstream file(filename);
string user, pass, role;
while (file >> user >> pass >> role)
{
users[user] = {pass, role};
}
file.close();
}
string Authenticator::login(const string& user, const
string& pass)
{
if (users.find(user) != users.end() &&
users[user].first == pass)
{
return users[user].second;
}
return "";
}
void Authenticator::signup(const string& user,
const string& pass,
const string& role)
{
ofstream file("users.txt", ios::app);
file << "\n" << user << " " << pass << " " << role <<
endl;
file.close();
users[user] = {pass, role};
}