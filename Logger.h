#ifndef LOGGER_H
#define LOGGER_H

#include <string>

class Logger {
public:
    void log(const std::string& user,
             const std::string& host,
             const std::string& type,
             const std::string& status);
};

#endif