#include "Logger.h"
#include <fstream>
#include <ctime>
using namespace std;
void Logger::log(const string& user,
const string& host,
const string& type,
const string& status)
{
ofstream file("proxy.log", ios::app);
time_t now = time(0);
tm* local = localtime(&now);
char timeStr[10];
strftime(timeStr, sizeof(timeStr), "%H:%M:%S", local);
file << "[" << timeStr << "] "
<< user << " | "
<< host << " | "
<< type << " | "
<< status << endl;
file.close();
}