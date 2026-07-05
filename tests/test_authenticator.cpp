// Unit tests for Authenticator class
// Tests login success/failure, signup, and password hashing
// Build: g++ -std=c++17 -Wall test_authenticator.cpp ../Authenticator.cpp -lssl -lcrypto -o test_auth
// Run:   ./test_auth

#include "../Authenticator.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <cstdlib>

using namespace std;

// Create a temporary test users file
void createTestUsersFile(const string& filename) {
    // Pre-create users with known hashed passwords
    // These are SHA256 hashes of the passwords
    ofstream file(filename);
    if (file.is_open()) {
        // admin / password123
        file << "admin 182f30e041d7a4da3e7ee3c8e16f3aeb46d9ff4fdb2f887f1a09e7ddb5b8e08b admin\n";
        // user1 / secret
        file << "user1 2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97fb8a73e1c user\n";
        file.close();
    }
}

int main() {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (cond) { cout << "PASS  " << name << "\n"; }
        else      { cout << "FAIL  " << name << "\n"; ++failures; }
    };

    string testFile = "test_users_temp.txt";
    createTestUsersFile(testFile);

    // Test 1: Load users and successful login
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("admin", "password123");
        check(role == "admin", "login succeeds with correct credentials");
    }

    // Test 2: Failed login - wrong password
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("admin", "wrongpassword");
        check(role == "", "login fails with wrong password");
    }

    // Test 3: Failed login - unknown user
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("nonexistent", "password");
        check(role == "", "login fails with unknown user");
    }

    // Test 4: Successful login for second user
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("user1", "secret");
        check(role == "user", "login returns correct role for user1");
    }

    // Test 5: Non-existent file doesn't crash
    {
        Authenticator auth;
        auth.loadUsers("nonexistent_file_xyz.txt");
        string role = auth.login("admin", "password123");
        check(role == "", "login fails gracefully when file missing");
    }

    // Test 6: Signup new user
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        bool result = auth.signup("newuser", "newpass", "user");
        check(result == true, "signup returns true for new user");

        // Verify new user can login (note: signup appends to users.txt)
        Authenticator auth2;
        auth2.loadUsers("users.txt");  // Will have the appended user
        string role = auth2.login("newuser", "newpass");
        check(role == "user", "newly signed up user can login");
    }

    // Test 7: Signup existing user fails
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        bool result = auth.signup("admin", "otherpass", "user");
        check(result == false, "signup fails for existing user");
    }

    // Test 8: Case sensitivity in usernames
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("ADMIN", "password123");
        check(role == "", "username matching is case-sensitive");
    }

    // Test 9: Empty credentials
    {
        Authenticator auth;
        auth.loadUsers(testFile);
        string role = auth.login("", "");
        check(role == "", "login with empty credentials fails");
    }

    // Cleanup
    remove(testFile.c_str());
    
    cout << (failures ? "\nAUTHENTICATOR TESTS FAILED\n" : "\nALL AUTHENTICATOR TESTS PASSED\n");
    return failures ? 1 : 0;
}
