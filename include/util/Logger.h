#pragma once
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>

enum class LogLevel { DEBUG = 0, INFO = 1, WARN = 2, ERROR = 3 };

class Logger {
public:
    static LogLevel minLevel;

    static void setLevel(LogLevel l) { minLevel = l; }

    static void debug(const std::string& msg) { log(LogLevel::DEBUG, "DEBUG", msg); }
    static void info (const std::string& msg) { log(LogLevel::INFO,  "INFO ", msg); }
    static void warn (const std::string& msg) { log(LogLevel::WARN,  "WARN ", msg); }
    static void error(const std::string& msg) { log(LogLevel::ERROR, "ERROR", msg); }

    // Convenience: build message with stream syntax
    // Usage: Logger::info(Logger::fmt() << "Loaded " << n << " cells");
    struct Fmt {
        std::ostringstream ss;
        template<typename T> Fmt& operator<<(const T& v) { ss << v; return *this; }
        operator std::string() const { return ss.str(); }
    };
    static Fmt fmt() { return Fmt{}; }

private:
    static void log(LogLevel level, const char* tag, const std::string& msg) {
        if (level < minLevel) return;
        std::ostream& out = (level >= LogLevel::WARN) ? std::cerr : std::cout;
        out << "[" << tag << "] " << msg << "\n";
    }
};

inline LogLevel Logger::minLevel = LogLevel::INFO;
