#include <array>

#include "position.hpp"

uint64_t Position::nr_visits_;
Transposition Position::transpositions_;

std::array<Bitmap, WIDTH> const Position::move_order_ = Position::generate_move_order();

std::array<Bitmap, WIDTH> Position::generate_move_order() {
    std::array<Bitmap, WIDTH> order;
    int sum = (WIDTH-1) & ~1;
    int base = sum / 2;
    for (int i=0; i < WIDTH; ++i) {
        order[i] = ((ONE << HEIGHT) -1) << (base * USED_HEIGHT);
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

Bitmap Position::_winning_bits(Bitmap color) const {
    // vertical (3 stones on top of each other)
    Bitmap r = (color << 1) & (color << 2) & (color << 3);

    Bitmap p;
    //horizontal
    // p = 2 stones next to each other (shifted one column to the right)
    //    .xx. => ...x
    p = (color << USED_HEIGHT) & (color << 2*USED_HEIGHT);
    // Check  Xxx?
    r |= p & (color << 3*USED_HEIGHT);
    // Check xx?X
    r |= p & (color >> USED_HEIGHT);
    // p = 2 stones next to each other (shifted one column to the left)
    //    .xx. => x...
    p = (color >> USED_HEIGHT) & (color >> 2*USED_HEIGHT);
    // Check X?xx
    r |= p & (color << USED_HEIGHT);
    // Check ?xxX
    r |= p & (color >> 3*USED_HEIGHT);

    // diagonals are simular but moving columns one up/down
    //diagonal 1
    p = (color << HEIGHT) & (color << 2*HEIGHT);
    r |= p & (color << 3*HEIGHT);
    r |= p & (color >> HEIGHT);
    p = (color >> HEIGHT) & (color >> 2*HEIGHT);
    r |= p & (color << HEIGHT);
    r |= p & (color >> 3*HEIGHT);

    //diagonal 2
    p = (color << (USED_HEIGHT+1)) & (color << 2*(USED_HEIGHT+1));
    r |= p & (color << 3*(USED_HEIGHT+1));
    r |= p & (color >> (USED_HEIGHT+1));
    p = (color >> (USED_HEIGHT+1)) & (color >> 2*(USED_HEIGHT+1));
    r |= p & (color << (USED_HEIGHT+1));
    r |= p & (color >> 3*(USED_HEIGHT+1));

    // All of them can mistakenly hit the guard bit(s),
    // but we mask them out here
    // BOARD_MASK ^ mask = bits that are actually empty
    return r & (BOARD_MASK ^ mask_);
}

FLATTEN
Bitmap Position::winning_bits() const {
    return _winning_bits(color_ ^ mask_);
}

FLATTEN
Bitmap Position::opponent_winning_bits() const {
    return _winning_bits(color_);
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

// actual_score <= alpha         THEN actual score <= return value <= alpha
// actual score  >= beta         THEN actual score >= return value >= beta
// alpha <= actual score <= beta THEN        return value = actual score
int Position::_alphabeta(int alpha, int beta) const {
    // std::cout << "Consider [" << alpha << ", " << beta << "]:\n" << *this;
    visit();

    auto possible = possible_bits();
    // places where the opponent wants a stone because he them wins
    auto opponent_win = opponent_winning_bits();
    // If any of these places is possible the opponent will play there if given
    // a chance. To prevent that we must play there ourselves (we can't just
    // win instead since _alphabeta is never called in a winning position)
    auto forced_moves = opponent_win & possible;
    if (forced_moves) {
        if (forced_moves & (forced_moves -1)) {
            // More than one forced move. We lose on the next move
            return 1-score();
        }
        // Only one forced move. We must play it
        possible = forced_moves;
    }
    // Avoid playing jus below a winning move for the opponent
    possible &= ~(opponent_win >> 1);
    if (!possible)
        // It seems that every move loses
        return 1-score();

    // Neither side wins in the next 2 moves.
    // If these will fill the board it is a draw
    // Need >= instead of =- since we don't bother to test if the next move
    // is a draw in main alphabeta()
    if (nr_plies() >= WIDTH * HEIGHT - 2) return 0;

    // Lower bound since opponen cannot win on his next move
    int min = 2-score();
    if (alpha < min) {
        alpha = min;
        if (alpha >= beta) return alpha;
    }

    int max;
    auto val = Position::get(key());
    if (val) {
        max = val - (MAX_SCORE+2);
        // std::cout << "Cached=" << max << "\n";
    } else
        // Upperbound since we cannot win on our next move
        max = opponent_score()-2;

    if (beta > max) {
        // We can't do better than max anyways, so lower beta
        beta = max;
        // Immediately return if the window closed
        if (alpha >= beta) return beta;
    }

    int current = MAX_SCORE+1;
    alpha = -alpha;
    beta  = -beta;
    // Explore moves
    for (auto order: move_order_) {
        auto move_bit = possible & order;
        if (!move_bit) continue;
        auto position = _play(move_bit);
        int s = position._alphabeta(beta, alpha);
        // std::cout << "Result [" << alpha << ", " << beta << "] = " << s << "\n" << position;
        // Prune if we find better than the window
        if (s <= beta) return -s;
        // Found a value better than alpha (but worse than beta)
        // Narrow the window since we only have to do better than this latest
        if (s < current) {
            current = s;
            if (s < alpha) alpha = s;
        }
    }
    current = -current;
    // real value <= alpha, so we are storing an upper bound
    set(key(), current + (MAX_SCORE+2));
    return current;
}

int Position::alphabeta(int alpha, int beta) const {
    // No moves at all is a draw
    auto possible = possible_bits();
    if (!possible) return 0;

    // If we can win in 1 move say we can win on the next move
    auto winning = winning_bits();
    if (winning & possible) {
        // std::cout << "Immediate win: " << winning << " " << possible << "\n";
        return opponent_score()-1;
    }

    // Otherwise go full alpha/beta
    auto score = _alphabeta(alpha, beta);
    // std::cout << "Result [" << alpha << ", " << beta << "] = " << score << "\n" << *this;
    return score;
}
