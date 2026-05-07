#include "Logger.h"
#include <iostream>
#include <fstream>
#include <ctime>
using namespace std;

Logger::Logger()
{
    logFile.open("proxy.log", std::ios::app);
    if (!logFile.is_open())
        std::cerr << "Logger: failed to open proxy.log — check path and permissions\n";
}

void Logger::log(const std::string& user,
                 const std::string& host,
                 const std::string& type,
                 const std::string& status)
{
    std::lock_guard<std::mutex> lock(logMutex);

    if (logFile.is_open())
    {
        logFile.clear();
        time_t now = time(0);
        struct tm* tm_info = localtime(&now);
        char timeStr[9];
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", tm_info);
        logFile << "[" << timeStr << "] " << user << " | " << host << " | " << type << " | " << status << std::endl;
        logFile.flush();
    }
}

Logger::~Logger()
{
    if (logFile.is_open())
        logFile.close();
}