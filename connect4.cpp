#include <chrono>

#include <cstdlib>

#include <unistd.h>

#include "position.hpp"

// Handle commandline options.
// Simplified getopt for systems that don't have it in their library (Windows..)
class GetOpt {
  private:
    std::string const options;
    char const* const* argv;
    int nextchar = 0;
    int optind = 1;
    char ch = '?';
    char const* optarg = nullptr;

  public:
    int ind() const { return optind; }
    char const* arg() const { return optarg; }
    char option() const { return ch; }

    GetOpt(std::string const options_, char const* const* argv_) :
        options(options_), argv(argv_) {}
    char next() {
        while (1) {
            if (nextchar == 0) {
                if (!argv[optind] ||
                    argv[optind][0] != '-' ||
                    argv[optind][1] == 0) return ch = 0;
                if (argv[optind][1] == '-' && argv[optind][2] == 0) {
                    ++optind;
                    return ch = 0;
                }
                nextchar = 1;
            }
            ch = argv[optind][nextchar++];
            if (ch == 0) {
                ++optind;
                nextchar = 0;
                continue;
            }
            auto pos = options.find(ch);
            if (pos == std::string::npos) ch = '?';
            else if (options[pos+1] == ':') {
                if (argv[optind][nextchar]) {
                    optarg = &argv[optind][nextchar];
                } else {
                    optarg = argv[++optind];
                    if (!optarg) return ch = options[0] == ':' ? ':' : '?';
                }
                ++optind;
                nextchar = 0;
            }
            return ch;
        }
    }
};

using namespace std;

int main([[maybe_unused]] int argc,
         char const* const* argv) {
    uint timeout = 0;
    GetOpt options{"t:", argv};
    while (options.next()) {
        switch (options.option()) {
            case 't':
              {
                  auto tmp = atoll(options.arg());
                  if (tmp < 0) throw(range_error("timeout must not be negative"));
                  if (tmp > UINT_MAX) throw(range_error("timeout too large"));
                  timeout = tmp;
              }
              break;
            default:
              cerr << "usage: " << argv[0] << " [-t timeout]" << endl;
              exit(EXIT_FAILURE);
        }
    }

    cout << "Time " << time_string() << "\n";
    cout << "Pid: " << PID << "\n";
    cout << "Commit: " << VCS_COMMIT << "\n";

    if (timeout) alarm(timeout);
    std::string line;
    while (1) {
        if (!getline(cin, line)) break;
        auto space = line.find(' ');
        if (space != std::string::npos) line.resize(space);
        Position pos{line};
        cout << pos;
        Position::clear_visits();
        auto start = chrono::steady_clock::now();
        int score = pos.negamax();
        auto end = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
        cout << line << " " << score << " " << (duration+500)/1000 << " " << Position::nr_visits() << endl;
    }
    return 0;
}

uint64_t foo() {
    int64_t a = Position::nr_visits();
    asm("#aa");
    auto v = Position::nr_visits();
    asm("#bb");
    return v+a;
}
