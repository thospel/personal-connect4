#include "system.hpp"

#include <iostream>
#include <system_error>

#include <ctime>

#include <sys/types.h>
#include <unistd.h>

std::string const PID{std::to_string(getpid())};
std::string const VCS_COMMIT{STRINGIFY(COMMIT)};
std::string const VCS_COMMIT_TIME{STRINGIFY(COMMIT_TIME)};

bool FATAL = false;

void throw_errno(int err, std::string const& text) {
    throw(std::system_error(err, std::system_category(), text));
}

void throw_errno(std::string const& text) {
    throw_errno(errno, text);
}

void throw_logic(char const* text, const char* file, int line) {
    throw_logic(std::string{text} + " at " + file + ":" + std::to_string(line));
}

void throw_logic(std::string const& text, const char* file, int line) {
    throw_logic(text + " at " + file + ":" + std::to_string(line));
}

void throw_logic(char const* text) {
    throw_logic(std::string{text});
}

void throw_logic(std::string const& text) {
    if (FATAL) {
        // logger << text << std::endl;
        std::cerr << text << std::endl;
        abort();
    }
    throw(std::logic_error(text));
}

inline std::string _time_string(time_t time) {
    struct tm tm;

    if (!localtime_r(&time, &tm))
        throw_errno("Could not convert time to localtime");
    char buffer[80];
    if (!strftime(buffer, sizeof(buffer), "%F %T %z", &tm))
        throw_logic("strtime buffer too short");
    return std::string{buffer};
}

std::string time_string(time_t time) {
    return _time_string(time);
}

inline time_t _now() {
    time_t tm = time(nullptr);
    if (tm == static_cast<time_t>(-1)) throw_errno("Could not get time");
    return tm;
}

time_t now() {
    return _now();
}

std::string time_string() {
    return _time_string(_now());
}
