#include "Logger.h"
#include <iostream>
#include <fstream>
#include <ctime>
#include <climits>
#include <cstring>
#include <libgen.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif
using namespace std;

// Resolve <dir-of-executable>/proxy.log in a cross-platform way so the
// proxy builds and logs correctly on macOS, Linux (CI/Docker), and elsewhere.
static string resolveLogPath()
{
    char execBuf[PATH_MAX];
#if defined(__APPLE__)
    uint32_t size = sizeof(execBuf);
    if (_NSGetExecutablePath(execBuf, &size) != 0)
        return "proxy.log";
#elif defined(__linux__)
    ssize_t len = readlink("/proc/self/exe", execBuf, sizeof(execBuf) - 1);
    if (len <= 0)
        return "proxy.log";
    execBuf[len] = '\0';
#else
    return "proxy.log";
#endif
    char dirBuf[PATH_MAX];
    strncpy(dirBuf, execBuf, PATH_MAX);
    dirBuf[PATH_MAX - 1] = '\0';
    return string(dirname(dirBuf)) + "/proxy.log";
}

Logger::Logger()
{
    string logPath = resolveLogPath();
    logFile.open(logPath, ios::app);
    if (!logFile.is_open())
        cerr << "Logger: failed to open " << logPath << " — check path and permissions\n";
}

void Logger::log(const string& user,
                 const string& host,
                 const string& type,
                 const string& status)
{
    lock_guard<mutex> lock(logMutex);
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
    lock_guard<mutex> lock(logMutex);
    if (logFile.is_open())
        logFile.close();
}
