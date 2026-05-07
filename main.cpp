#include <iostream>
#include "proxyserver.h"
#include "Authenticator.h"

using namespace std;

int main()
{
    Authenticator auth;
    auth.loadUsers("users.txt");
    string choice;
    cout << "1. Login\n2. Signup\nChoice: ";
    cin >> choice;
    string username, password, role;
    if (choice == "2") {
        cout << "Enter username: ";
        cin >> username;
        cout << "Enter password: ";
        cin >> password;
        cout << "Enter role (admin/user): ";
        cin >> role;
        if (auth.signup(username, password, role))
            cout << "Signup successful!\n";
        else
            cout << "Username already exists. Please log in instead.\n";
        }
    while (true) {
    cout << "\nLogin\nUsername: ";
    cin >> username;
    cout << "Password: ";
    cin >> password;
    role = auth.login(username, password);
    if (role != "")
    {
        cout << "Login successful as " << role << endl;
        break;
        }
        else
        {
            cout << "Invalid credentials, try again.\n";
    }
    }
ProxyServer proxy(8080);
proxy.setUser(username, role);
proxy.startServer();
return 0;
}