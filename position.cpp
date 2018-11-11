#include <array>

#include "position.hpp"

bool const DEBUG = false;
// BEST true doesn't work currently
bool const BEST  = false;

int Position::start_depth_;
uint64_t Position::nr_visits_;
uint64_t Position::hits_;
uint64_t Position::misses_;
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

std::string to_bits(Bitmap bitmap) {
    char buffer[WIDTH * (HEIGHT+1)+1];
    auto ptr = &buffer[WIDTH * (HEIGHT+1)+1];
    *--ptr = 0;
    for (int x=0; x<WIDTH; ++x) {
        for (int y=0; y<HEIGHT; ++y) {
            *--ptr = '0' + (bitmap & 1);
            bitmap >>= 1;
        }
        if (bitmap & 1) throw_logic("Filled guard bits");
        bitmap >>= 1;
        *--ptr = ' ';
    }
    return buffer+1;
}

Transposition::Transposition(size_t size): entries_{size} {}

void Transposition::resize(size_t size) {
    if (size) {
        uint bits = first_bit(size);
        size_t real_size = static_cast<size_t>(1) << bits;
        if (real_size < size) {
            ++bits;
            if (bits >= sizeof(size_t) * CHAR_BIT)
                throw_logic("Size is way too high");
            real_size *= 2;
        }
        entries_.resize(real_size);
        bits_ = ALL_BITS-bits;
    } else
        entries_.clear();
}

void Transposition::clear() {
    if (entries_.empty()) throw_logic("Attempt to clear without memory");
    std::memset(reinterpret_cast<void *>(&entries_[0]), 0, entries_.size() * sizeof(entries_[0]));
    // Make sure the empty board is not a hit
    *entry(0) = value_type::INVALID();
}

//FLATTEN
//std::ostream& operator<<(std::ostream& os, Bitmap bitmap) {
//    os << to_bits(bitmap);
//    return os;
//}

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

ALWAYS_INLINE
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

    // All of them can mistakenly hit the guard bit(s) and already filled bits
    // We mask these out here
    // BOARD_MASK ^ mask = bits that are actually empty
    return r & (BOARD_MASK ^ mask_);
}

ALWAYS_INLINE
Bitmap Position::winning_bits() const {
    return _winning_bits(color_ ^ mask_);
}

ALWAYS_INLINE
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
        if (!pos.playable(x)) throw_logic("Play in a full column");
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

void Position::to_string(char* buf, int indent) const {
    Color mover = to_move();
    for (int i=0; i<indent; ++i) *buf++ = ' ';
    for (int x=0; x < WIDTH; ++x) {
        *buf++ = '+';
        *buf++ = '-';
    }
    *buf++ = '+';
    *buf++ = '\n';

    for (int y=HEIGHT-1; y >=0; --y) {
        for (int i=0; i<indent; ++i) *buf++ = ' ';
        for (int x=0; x < WIDTH; ++x) {
            *buf++ = '|';
            auto v = get(x, y, mover);
            *buf++ = v < 0 ? '.' : v == 0 ? 'x' : 'o';
        }
        *buf ++ = '|';
        *buf ++ = '\n';
    }

    for (int i=0; i<indent; ++i) *buf++ = ' ';
    for (int x=0; x < WIDTH; ++x) {
        *buf++ = '+';
        *buf++ = '-';
    }
    *buf++ = '+';
    *buf++ = '\n';
    *buf = 0;
}

ALWAYS_INLINE
bool equal_score(int s1, int s2, bool weak) {
    if (!weak) return s1 == s2;
    return (s1 < 0 && s2 < 0) || (s1 == 0 && s2 == 0) || (s1 > 0 && s2 > 0);
}

std::vector<int> Position::principal_variation(int score, bool weak) const {
    std::vector<int> moves;
    auto pos = *this;
    while (1) {
        score = -score;
        auto possible = pos.possible_bits();
        if (!possible || pos.won()) break;
        // std::cout << "Analyzing " << to_bits(possible) << "\n" << pos;
        for (auto& move: move_order_) {
            Bitmap move_bit = possible & move;
            if (!move_bit) continue;
            auto p = pos._play(move_bit);
            auto s = p.solve(weak);
            // std::cout << "Try move " << to_bits(move_bit) << " -> " << s << "\n";
            if (equal_score(s, score, weak)) {
                int best = first_bit(move) / USED_HEIGHT;
                moves.emplace_back(best);
                pos = p;
                goto FOUND;
            }
        }
        // std::cout.flush();
        throw_logic("Could not find principal variation");
      FOUND:;
    }
    return moves;
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
int Position::_alphabeta(int alpha, int beta, Bitmap opponent_win) const {
    auto transposition = transposition_entry();
    __builtin_prefetch(transposition);
    // Avoid the prefetch being moved down
    asm("");

    int indent;
    if (DEBUG) {
        indent = INDENT*this->indent();
        for (int i=0; i<indent; ++i) std::cout << " ";
        std::cout << "Consider [" << alpha << ", " << beta << "]:\n" << this->to_string(indent);
        indent += INDENT;
    }

    visit();

    auto possible = possible_bits();
    // If any of these places is possible the opponent will play there if given
    // a chance. To prevent that we must play there ourselves (we can't just
    // win instead since _alphabeta is never called in a winning position)
    auto forced_moves = opponent_win & possible;
    if (forced_moves) {
        if (forced_moves & (forced_moves -1)) {
            // More than one forced move. We lose on the next move
            return -score2();
        }
        // Only one forced move. We must play it
        possible = forced_moves;
    }
    // Avoid playing just below a winning move for the opponent
    possible &= ~(opponent_win >> 1);
    if (!possible)
        // It seems that every move loses
        // (draws were already excluded so there ARE moves)
        return -score2();

    int left = nr_plies_left();
    // No need to detect draw (in 2 moves).
    // If left = 2 then (below) min = max = 0 and we will immediately return 0

    // Lower bound since opponent cannot win on his next move
    int min = 1-left/2;
    if (alpha < min) {
        alpha = min;
        if (alpha >= beta) return alpha;
    }

    struct Entry {
        Bitmap after_move;
        Bitmap winning_bits;
        int nr_threats;
    };
    std::array<Entry, WIDTH+1> order;
    std::array<int,   WIDTH+1> index;
    int pos = 1;
    int max, best;
    Bitmap best_bit;
    Bitmap my_stones = color_ ^ mask_;
    if (transposition->get(key(), max, best)) {
        hit();
        if (DEBUG) {
            for (int i=0; i<indent; ++i) std::cout << " ";
            std::cout << "Cached score=" << max << ", best=" << best << "\n";
        }
        if (BEST) {
            best_bit = ((ONE << HEIGHT) -1) << best * USED_HEIGHT & possible;
            order[pos++] = Entry{my_stones | best_bit, 0, INT_MAX};
        } else {
            best_bit = 0;
            index[0] = 0;
            order[0].nr_threats = INT_MAX;
        }
    } else {
        miss();
        // Upperbound since we cannot win on our next move
        max = (left-1)/2;
        best_bit = 0;
        index[0] = 0;
        order[0].nr_threats = INT_MAX;
    }

    if (beta > max) {
        // We can't do better than max anyways, so lower beta
        beta = max;
        // Immediately return if the window closed
        if (alpha >= beta) return beta;
    }

    // Explore moves
    if (best_bit) {
        // If we got a best move from the cache the ordering isn't so important
        // It causes some more visits due to bad ordering for the rest of the
        // moves but the speedup is still worth it
        for (auto& move: move_order_) {
            Bitmap move_bit = possible & move;
            if (!move_bit || move_bit == best_bit) continue;
            // We can actually move there
            order[pos++].after_move = my_stones | move_bit;
        }
    } else {
        auto opponent_stacked = opponent_win & (opponent_win << 1);
        // Convert to mask
        auto opponent_allowed = opponent_stacked | ABOVE_BITS;
        opponent_allowed &= ~opponent_allowed + BOTTOM_BITS;
        opponent_allowed -= BOTTOM_BITS;
        opponent_allowed &= BOARD_MASK;
        // Insertion sort based on how many threats we have
        for (int i=0; i<WIDTH; ++i) {
            Bitmap move_bit = possible & move_order_[i];
            if (!move_bit) continue;
            // We can actually move there
            Bitmap after_move = my_stones | move_bit;
            Bitmap winning_bits = _winning_bits(after_move);
            Bitmap allowed_winning_bits = winning_bits & opponent_allowed;
            // Bonus for stacked winning bits
            int nr_threats = 2*popcount(allowed_winning_bits)+((allowed_winning_bits & allowed_winning_bits >> 1) != 0);
            order[pos] = Entry{after_move, winning_bits, nr_threats};
            int p = pos;
            while (nr_threats > order[index[p-1]].nr_threats) {
                index[p] = index[p-1];
                --p;
            }
            index[p] = pos++;
        }
    }
    int current = MAX_SCORE+1;
    Bitmap move = 0;
    alpha = -alpha;
    beta  = -beta;
    for (int p=1; p<pos; ++p) {
        auto& entry = order[index[p]];
        auto after_move = entry.after_move;
        auto position = Position{after_move, after_move | mask_};
        int s = position._alphabeta(beta, alpha, entry.winning_bits);
        if (DEBUG) {
            for (int i=0; i<indent; ++i) std::cout << " ";
            std::cout << "Result [" << -alpha << ", " << -beta << "] = " << s << "\n";
        }
        // Prune if we find better than the window
        if (s <= beta) return -s;
        // Found a value better than alpha (but worse than beta)
        // Narrow the window since we only have to do better than this latest
        if (s < current) {
            current = s;
            move = after_move;
            if (s < alpha) alpha = s;
        }
    }
    current = -current;
    move ^= my_stones;
    if (BEST)
        best = first_bit(move) / USED_HEIGHT;
    else
        best = 0;
    // real value <= alpha, so we are storing an upper bound
    transposition->set(key(), current, best);
    return current;
}

int Position::solve(bool weak) const {
    // Check if opponent already won
    if (won()) {
        visit();
        return -score();
    }

    // No moves at all is a draw
    auto possible = possible_bits();
    if (!possible) {
        visit();
        return 0;
    }

    // If we can win in 1 move say we can win on the next move
    auto winning = winning_bits();
    if (winning & possible) {
        // std::cout << "Immediate win: " << winning << " " << possible << "\n";
        visit();
        return score1();
    }

    // Next move is not a win and there was only 1 spot left to play so draw
    if (nr_plies_left() == 1) {
        visit();
        return 0;
    }

    // Game doesn't finish in 1 ply, therefore:
    int min = -score2();	// win after 2 more plies
    int max =  score3();	// win after 3 more plies
    if (weak) {
        if (min < -1) min = -1;
        if (max >  1) max =  1;
    }

    int score;
    int indent = INDENT * this->indent();
    auto opponent_winning_bits = this->opponent_winning_bits();
    if (false) {
        // Next move doesn't finish the game. Go full alpha/beta
        score = _alphabeta(min, max, opponent_winning_bits);
    } else {
        // iteratively narrow the min-max exploration window
        while (min < max) {
            int med = min + (max - min)/2;
            // std::cout << "[" << min << " " << max << "]-> " << med << "\n";
            if (true) {
                if (     med <= 0 && min/2 < med) med = min/2;
                else if (med >= 0 && max/2 > med) med = max/2;
            } else {
                if (     med <= 0 && min/2 < 0) med = min/2;
                else if (med >= 0 && max/2 > 0) med = max/2;
                if (med+1 > max) med = max-1;
                if (med < min) med = min;
            }
            // Check if the actual score is greater than med
            int r = _alphabeta(med, med + 1, opponent_winning_bits);
            if (DEBUG) {
                for (int i=0; i<indent; ++i) std::cout << " ";
                std::cout << "Result [" << med << ", " << med+1 << "] = " << r << "\n";
            }
            // std::cout << med << " -> " << r << "\n";
            if (r <= med) max = r;
            else min = r;
        }
        score = min;
    }
    if (DEBUG) std::cout << "Solve: " << score << "\n";

    return score;
}

void Position::generate_book(std::string how, int depth, bool weak) const {
    if (depth > 0) {
        --depth;
        for (int x=0; x<WIDTH; ++x)
            if (playable(x)) {
                char ch = '1' + x;
                play(x).generate_book(how + ch, depth, weak);
            }
    }
    int score = solve(weak);
    std::cout << *this << how << " " << score << std::endl;
}
