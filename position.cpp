#include <array>

#include "position.hpp"

uint64_t Position::nr_visits_;
std::array<int, WIDTH> const Position::move_order_ = Position::generate_move_order();

std::array<int, WIDTH> Position::generate_move_order() {
    std::array<int, WIDTH> order;
    int sum = (WIDTH-1) & ~1;
    int base = sum / 2;
    for (int i=0; i < WIDTH; ++i) {
        order[i] = base;
        sum ^=1;
        base = sum - base;
    }
    return order;
}

bool Position::_won(Bitmap pos) {
    // horizontal
    Bitmap m = pos & (pos >> USED_HEIGHT);
    if (m & (m >> 2*USED_HEIGHT)) return true;

    // diagonal 1
    m = pos & (pos >> (USED_HEIGHT-1));
    if (m & (m >> 2*(USED_HEIGHT-1))) return true;

    // diagonal 2
    m = pos & (pos >> (USED_HEIGHT+1));
    if (m & (m >> 2*(USED_HEIGHT+1))) return true;

    // vertical;
    m = pos & (pos >> 1);
    if (m & (m >> 2)) return true;

    return false;
}

Position Position::play(char const* ptr, size_t size) {
    if (WIDTH >= 10) throw_logic("play doesn't support boards wider than 9");
    Position pos = *this;
    while (size--) {
        auto p = *ptr++;
        int x = p - '1';
        if (x < 0) {
            if (p == -1) throw_logic("Play does not support column 0");
            throw_logic("Invalid character in play");
        }
        if (x >= WIDTH) {
            if (p < 10) throw_logic("Play to the right of the board");
            throw_logic("Invalid character in play");
        }
        pos = pos.play(x);
    }
    return pos;
}

Position Position::play(std::istream& in) const {
    std::string line;
    Position pos = *this;
    if (!getline(in, line)) return BAD();
    auto space = line.find(' ');
    if (space != std::string::npos) line.resize(space);
    return pos.play(line);
}

void Position::to_string(char* buf) const {
    Color mover = to_move();
    for (int x=0; x < WIDTH; ++x) {
        *buf++ = '+';
        *buf++ = '-';
    }
    *buf++ = '+';
    *buf++ = '\n';

    for (int y=HEIGHT-1; y >=0; --y) {
        for (int x=0; x < WIDTH; ++x) {
            *buf++ = '|';
            auto v = get(x, y, mover);
            *buf++ = v < 0 ? '.' : v == 0 ? 'x' : 'o';
        }
        *buf ++ = '|';
        *buf ++ = '\n';
    }

    for (int x=0; x < WIDTH; ++x) {
        *buf++ = '+';
        *buf++ = '-';
    }
    *buf++ = '+';
    *buf++ = '\n';
    *buf = 0;
}

int Position::negamax() const {
    // std::cout << "Consider:\n" << *this;
    visit();
    int score = MAX_SCORE+1;

    std::array<Position, WIDTH> position;
    int nr_positions = 0;
    for (int x = 0; x < WIDTH; ++x) {
        // Can we play here ?
        if (full(x)) continue;
        position[nr_positions] = play(x);
        // Did we win ?
        // std::cout << "Expand:\n" << position[nr_positions];
        auto w = position[nr_positions].won();
        // std::cout << "Won=" << w << "\n";
        if (w) return position[nr_positions].score();
        ++nr_positions;
    }

    // If we couldn't move it's a draw
    if (nr_positions == 0) return 0;

    // No immediate wins. Go deeper
    for (int p = 0; p<nr_positions; ++p) {
        int s = position[p].negamax();
        if (s < score) score = s;
    }
    return -score;
}

int Position::alphabeta(int alpha, int beta) const {
    // std::cout << "Consider [" << alpha << ", " << beta << "]:\n" << *this;
    visit();

    std::array<Position, WIDTH> position;
    int nr_positions = 0;
    for (int x: move_order_) {
        // Can we play here ?
        if (full(x)) continue;
        position[nr_positions] = play(x);
        // Did we win ?
        // std::cout << "Expand:\n" << position[nr_positions];
        auto w = position[nr_positions].won();
        // std::cout << "Won=" << w << "\n";
        if (w) return position[nr_positions].score();
        ++nr_positions;
    }

    // If we couldn't move it's a draw
    if (nr_positions == 0) return 0;

    // No immediate win.
    // So the score is at least 1 worse than the one for winning
    int max = position[0].score() - 1;
    if (beta > max) {
        // We can't do better than max anyways, so lower beta
        beta = max;
        // Immediately return if the window closed
        if (alpha >= beta) return beta;
    }

    // Explore moves
    alpha = -alpha;
    beta  = -beta;
    for (int p = 0; p<nr_positions; ++p) {
        int s = position[p].alphabeta(beta, alpha);
        // Prune if we find better than the window
        if (s <= beta) return -s;
        // Found a value better than alpha (but worse than beta)
        // Narrow the window since we only have to do better than this latest
        if (s < alpha) alpha = s;
    }
    return -alpha;
}
