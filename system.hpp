#include <iostream>
#include <string>

#include <cstring>

#include "constants.hpp"

extern uint NR_CPU;
extern size_t SYSTEM_MEMORY;
extern size_t SYSTEM_SWAP;
extern const std::string PID;
extern std::string HOSTNAME;
extern std::string CPUS;

extern bool FATAL;

// Good enough as long as we don't go threaded
static std::ostream& logger = std::cerr;

[[noreturn]] void throw_errno(std::string const& text);
[[noreturn]] void throw_errno(int err, std::string const& text);
[[noreturn]] void throw_logic(char const* text);
[[noreturn]] void throw_logic(char const*, const char* file, int line);
[[noreturn]] void throw_logic(std::string const& text);
[[noreturn]] void throw_logic(std::string const& text, const char* file, int line);

std::string time_string(time_t time);
std::string time_string();

void init_system() COLD;
