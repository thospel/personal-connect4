#include <chrono>
#include <unordered_set>
#include <unordered_map>
#include <fstream>

#include <cstdlib>

#include <unistd.h>

#include "revision.hpp"
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
    int ind() const PURE { return optind; }
    char const* arg() const PURE { return optarg; }
    char const* next_arg() { return argv[optind++]; }
    char option() const PURE { return ch; }

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

void insert(std::unordered_map<Position, int>& preset, std::string const& book) {
    std::ifstream file;
    file.exceptions(std::ifstream::badbit);
    file.open(book);
    std::string line;
    int64_t line_nr = 0;
    Position pos;
    while (getline(file, line)) {
        ++line_nr;
        if (line.empty()) continue;
        if (!(line[0] == ' ' || ('0' <= line[0] && line[0] <= '9'))) continue;
        auto space = line.find(' ');
        if (space == std::string::npos)
            throw_logic("No score on line " + std::to_string(line_nr));
        pos.clear();
        pos = pos.play(line.data(), space);
        line.erase(0, space+1);
        int score = std::stoi(line, &space);
        if (space == 0)
            throw_logic("No score on line " + std::to_string(line_nr));
        if (score > pos.score())
            throw_logic("Impossible high score on line " + std::to_string(line_nr));
        if (score < -pos.score1())
            throw_logic("Impossible low score on line " + std::to_string(line_nr));
        preset.emplace(pos, score);
    }
    file.close();
}

int main([[maybe_unused]] int argc,
         char const* const* argv) {
    init_system();
    std::cin.exceptions(std::ifstream::badbit);

    uint timeout   = 0;
    int transposition_bits = LOG2(TRANSPOSITION_SIZE);
    bool principal = false;
    int  method    = 0;
    bool minimax   = false;
    bool keep      = false;
    int debug      = 0;
    int  generate  = -1;
    std::unordered_set<std::string> books;

    GetOpt options{"mwpt:T:kb:g:d:", argv};
    while (options.next()) {
        long long tmp;
        switch (options.option()) {
            case 't':
              tmp = atoll(options.arg());
              if (tmp < 0) throw(range_error("timeout must not be negative"));
              if (tmp > UINT_MAX) throw(range_error("timeout too large"));
              timeout = tmp;
              break;
            case 'g':
              tmp = atoll(options.arg());
              if (tmp < -1) throw(range_error("generate must not be negative"));
              if (tmp > AREA) throw(range_error("There aren't that many plies"));
              generate = tmp;
              break;
            case 'T':
              tmp = atoll(options.arg());
              if (tmp <= 0) {
                  transposition_bits = first_bit((SYSTEM_MEMORY-1) / sizeof(Transposition::value_type));
                  if (tmp < -transposition_bits)
                      throw(range_error("transposition_bits too negative"));
                  transposition_bits += tmp;
                  // By default don't take more than 8G)
                  if (tmp == 0) transposition_bits = min(transposition_bits, static_cast<int>(36 - LOG2(sizeof(Transposition::value_type) * CHAR_BIT)));
              } else {
                  if (tmp >= static_cast<int>(sizeof(size_t) * CHAR_BIT))
                      throw(range_error("transposition_bits too large"));
                  transposition_bits = tmp;
              }
              break;
            case 'd': debug = atoll(options.arg()); break;
            case 'b': books.emplace(options.arg()); break;
            case 'm': minimax   = true; break;
            case 'p': principal = true; break;
            case 'k': keep      = true; break;
            case 'w': ++method;         break;
            default:
              cerr << "usage: " << argv[0] << " [-t timeout] [-w [-w]] [-p] [-m] [-k] [-T transposition_bits] [-b opening book] [-g depth] [-d debug_level]" << endl;
              exit(EXIT_FAILURE);
        }
    }

    cout << "Board: " << WIDTH << "x" << HEIGHT << "\n";
    cout << "Time: " << time_string() << "\n";
    cout << "Pid: " << PID << "\n";
    cout << "Commit: " << VCS_COMMIT << "\n";
    cout << "CPU: " << CPUS << "\n";
    cout << "Memory: " << SYSTEM_MEMORY / (1L << 30) << " GiB\n";
    cout << "Swap: " << SYSTEM_SWAP     / (1L << 30) << " GiB\n";

    std::unordered_map<Position, int> preset;
    for (auto const& book: books)
        insert(preset, book);

    Position::init(static_cast<size_t>(1) << transposition_bits);
    cout << "Transposition table: " << Position::transpositions_bytes() / (1L << 20) << " MiB (" << Position::transpositions_size() / (1L << 20) << " Mi entries)\n";
    if (keep) Position::reset(false);
    if (timeout) alarm(timeout);
    std::string line;
    while (getline(cin, line)) {
        auto space = line.find(' ');
        if (space != std::string::npos) line.resize(space);
        Position pos{line};
        Position::reset(keep);
        for (auto const& p: preset) {
            auto relevant = p.first.relevant_bits();
            auto transposition = p.first.transposition_entry(relevant);
            p.first.set(transposition, relevant, p.second, 0);
        }
        if (generate >= 0) {
            pos.generate_book(line, generate, method);
            continue;
        }
        cout << pos;
        pos.set_depth();
        auto start = chrono::steady_clock::now();
        int score;
        if (minimax)
            score = pos.negamax();
        else score = pos.solve(method, INT_MIN, debug);
        auto end = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
        cout << "misses: " << Position::misses() << ", hits: " << Position::hits() << "\n";
        cout << line << " " << score << " " << (duration+500)/1000 << " " << Position::nr_visits() << endl;
        if (principal) {
            auto pv = pos.principal_variation(score, method);
            auto p = pos;
            for (auto move: pv) {
                p = p.play(move);
                std::cout << move+1 << "\n" << p;
            }
        }
    }
    return 0;
}
