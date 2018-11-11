#include "system.hpp"

#include <atomic>
#include <map>
#include <system_error>

#include <cstdio>
#include <cstring>
#include <ctime>

#include <sys/types.h>
#include <unistd.h>
#include <sys/sysinfo.h>

std::string const PID{std::to_string(getpid())};
std::string HOSTNAME;
std::string CPUS;

size_t SYSTEM_MEMORY;
size_t SYSTEM_SWAP;
uint NR_CPU;

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

// Linux specific
void get_cpu_string() COLD;
void get_cpu_string() {
    FILE* fp = fopen("/proc/cpuinfo", "r");
    if (!fp) throw_errno("Could not open '/proc/cpuinfo'");
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    uint nr_cpu = 0;

    static char const MODEL_NAME[] = "model name";
    std::map<std::string, uint> cpus;
    while ((nread = getline(&line, &len, fp)) != -1) {
        char const* ptr = line;
        while (isspace(*ptr)) ++ptr;
        if (memcmp(ptr, MODEL_NAME, sizeof(MODEL_NAME)-1)) continue;
        ptr += sizeof(MODEL_NAME)-1;
        while (isspace(*ptr)) ++ptr;
        if (*ptr != ':') continue;
        ++ptr;
        while (isspace(*ptr)) ++ptr;
        char* end = line+nread;
        while (end > ptr && isspace(end[-1])) --end;
        if (end <= ptr) continue;
        *end = '\0';
        ++cpus[std::string{ptr, static_cast<size_t>(end-ptr)}];
        ++nr_cpu;
    }
    free(line);
    fclose(fp);

    CPUS.clear();
    if (nr_cpu == NR_CPU) {
        for (auto& entry: cpus) {
            if (!CPUS.empty()) CPUS.append("<br />");
            CPUS.append(std::to_string(entry.second));
            CPUS.append(" x ");
            CPUS.append(entry.first);
        }
    } else if (cpus.size() == 1) {
        for (auto& entry: cpus) {
            CPUS.append(std::to_string(NR_CPU));
            CPUS.append(" x ");
            CPUS.append(entry.first);
        }
    } else
        CPUS.append(std::to_string(NR_CPU));
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

void init_system() {
    tzset();

    char hostname[100];
    hostname[sizeof(hostname)-1] = 0;
    int rc = gethostname(hostname, sizeof(hostname)-1);
    if (rc) throw_errno("Could not determine host name");
    HOSTNAME.assign(hostname);

    cpu_set_t cs;
    if (sched_getaffinity(0, sizeof(cs), &cs))
        throw_errno("Could not determine number of CPUs");
    NR_CPU = CPU_COUNT(&cs);
    get_cpu_string();

    struct sysinfo s_info;
    if (sysinfo(&s_info))
        throw_errno("Could not determine memory");
    SYSTEM_MEMORY = static_cast<size_t>(s_info.totalram ) * s_info.mem_unit;
    SYSTEM_SWAP   = static_cast<size_t>(s_info.totalswap) * s_info.mem_unit;
}
