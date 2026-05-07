#include "Logger.h"
#include <iostream>
#include <fstream>
#include <ctime>
#include <climits>
#include <cstring>
#include <libgen.h>
#include <mach-o/dyld.h>
using namespace std;

// Resolve the log path relative to the binary's own directory, not the CWD.
// This ensures proxy.log always lands in the project folder regardless of
// where the binary is invoked from.
static std::string resolveLogPath()
{
    char execBuf[PATH_MAX];
    uint32_t size = sizeof(execBuf);
    if (_NSGetExecutablePath(execBuf, &size) != 0)
        return "proxy.log";
    char dirBuf[PATH_MAX];
    strncpy(dirBuf, execBuf, PATH_MAX);
    return std::string(dirname(dirBuf)) + "/proxy.log";
}

Logger::Logger()
{
    std::string logPath = resolveLogPath();
    logFile.open(logPath, std::ios::app);
    if (!logFile.is_open())
        std::cerr << "Logger: failed to open " << logPath << " — check path and permissions\n";
}

void Logger::log(const std::string& user,
                 const std::string& host,
                 const std::string& type,
                 const std::string& status)
{
    std::lock_guard<std::mutex> lock(logMutex);

    if (!logFile.is_open() || !logFile.good())
        return;

    time_t now = time(0);
    struct tm tmInfo{};
    localtime_r(&now, &tmInfo);
    char timeStr[9];
    strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &tmInfo);
    logFile << "[" << timeStr << "] " << user << " | " << host << " | " << type << " | " << status << '\n';
    logFile.flush();
}

Logger::~Logger()
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
        logFile.close();
}