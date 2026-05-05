#include "Logger.h"
#include <fstream>
#include <ctime>
using namespace std;

Logger::Logger()
{
    logFile.open("proxy.log", std::ios::app);
}

void Logger::log(const std::string& user,
                 const std::string& host,
                 const std::string& type,
                 const std::string& status)
{
    std::lock_guard<std::mutex> lock(logMutex);

    if (logFile.is_open())
    {
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