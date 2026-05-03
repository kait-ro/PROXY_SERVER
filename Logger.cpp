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
        char* dt = ctime(&now);
        std::string timestamp(dt);
        timestamp.pop_back();
        logFile << "[" << timestamp << "] " << user << " | " << host << " | " << type << " | " << status << std::endl;
        logFile.flush();
    }
}

Logger::~Logger()
{
    if (logFile.is_open())
        logFile.close();
}