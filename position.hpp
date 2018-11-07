#include <iostream>
#include <thread>

#include <cstring>
#include <cstdint>
#include <climits>

#include "vector.hpp"
#include "system.hpp"

typedef uint64_t Bitmap;

static constexpr uint LOG2(size_t value) {
    return value <= 1 ? 0 : 1+LOG2((value+1) / 2);
}

static int const WIDTH  = 7;
static int const HEIGHT = 6;
// Most number of stones one color can play
// (one more for first player on odd area boards)
static int const MAX_STONES = (WIDTH*HEIGHT+1)/2;		// 21
static int const MAX_SCORE  = MAX_STONES+1-4;			// 18
static int const BOARD_BUFSIZE = (HEIGHT+2) * (WIDTH*2+2);	// 128
static int const GUARD_BITS = 1;
static int const USED_HEIGHT = HEIGHT + GUARD_BITS;
static_assert(GUARD_BITS > 0, "There must be GUARD_BITS");
static int const      BITS = WIDTH*USED_HEIGHT-GUARD_BITS;	// 48
static int const  KEY_BITS = BITS+1;				// 49
static int const  ALL_BITS = sizeof(Bitmap) * CHAR_BIT;		// 64
static int const LEFT_BITS = ALL_BITS-KEY_BITS;			// 15
static_assert(LEFT_BITS >= 0, "Bitmap type is too small");
// negative score, positive scores, 0 and not found = 2*MAX_SCORE+2
static int const SCORE_BITS = LOG2(2*MAX_SCORE+2);		// 6
static int const BEST_BITS  = LOG2(WIDTH);			// 3
static_assert(SCORE_BITS+BEST_BITS <= LEFT_BITS, "No space for hash results");

static Bitmap const ONE = 1;
static Bitmap const TOP_BIT  = ONE << (HEIGHT-1);
static Bitmap const BOT_BIT  = ONE;
static Bitmap const FULL_MAP = -1;
static Bitmap const KEY_MASK   = (ONE << KEY_BITS)   - 1;
static Bitmap const SCORE_MASK = (ONE << SCORE_BITS) - 1;
static Bitmap const BEST_MASK  = (ONE << BEST_BITS)  - 1;

static constexpr Bitmap BOTTOM(int n) {
    return n == 0 ? 0 : (BOTTOM(n-1) << USED_HEIGHT) | ONE;
}

static Bitmap const BOTTOM_BITS = BOTTOM(WIDTH);
static Bitmap const BOARD_MASK  = BOTTOM_BITS * ((ONE << HEIGHT)-1);

// 4 MB transposition table
static size_t const TRANSPOSITION_BITS = 19;
static size_t const TRANSPOSITION_SIZE = static_cast<size_t>(1) << TRANSPOSITION_BITS;

inline int popcount(Bitmap value) {
#ifdef __POPCNT__
    static_assert(sizeof(Bitmap) * CHAR_BIT == 64,
                  "Bitmap is not 64 bits");
    return _mm_popcnt_u64(value);
#else  // __POPCNT__
    static_assert(sizeof(Bitmap) == sizeof(unsigned long),
                  "Bitmap is not unsigned long");
    return __builtin_popcountl(value);
#endif // __POPCNT__
}
inline int clz(Bitmap value) {
    static_assert(sizeof(Bitmap) == sizeof(unsigned long),
                  "Bitmap is not unsigned long");
    return __builtin_clzl(value);
}

class Transposition {
  public:
    struct value_type {
        friend Transposition;
      public:
        value_type() {}
        ALWAYS_INLINE
        void set(Bitmap key, int value, int best) {
        value_ =
            key |
            static_cast<Bitmap>(best) << KEY_BITS |
            static_cast<Bitmap>(value + (MAX_SCORE+1)) << (KEY_BITS+BEST_BITS);
        }
        ALWAYS_INLINE
        bool get(Bitmap key, int& score, int& best) const {
            if ((value_ & KEY_MASK) != key) return false;
            score = static_cast<int>(value_ >> (KEY_BITS+BEST_BITS)) - (MAX_SCORE+1);
            best = (value_ >> KEY_BITS) & BEST_MASK;
            return true;
        }

      private:
        explicit value_type(Bitmap value): value_{value} {}
        Bitmap value_;
    };
    void clear() {
        entries_.fill(value_type{0});
        // Make sure the empty board is not a hit
        entries_[fast_hash(0)] = value_type{1};
    }
    value_type* entry(Bitmap key) {
        return &entries_[fast_hash(key)];
    }
    value_type const* entry(Bitmap key) const {
        return &entries_[fast_hash(key)];
    }
  private:
    ALWAYS_INLINE
    static Bitmap fast_hash(Bitmap key) {
        static_assert(sizeof(key) == sizeof(LCM_MULTIPLIER),
                      "Bitmap is not 64 bits. Find another multiplier");
        key *= LCM_MULTIPLIER;
        return key >> (ALL_BITS-TRANSPOSITION_BITS);
    }
    static uint64_t const LCM_MULTIPLIER = UINT64_C(6364136223846793005);

    std::array<value_type, TRANSPOSITION_SIZE> entries_;
};

class Position {
  public:
    typedef enum {
        RED    = 0,
        YELLOW = 1,
    } Color;

    Position() {}
    explicit Position(std::istream& in): Position{} {
        clear();
        *this = play(in);
    }
    explicit Position(std::string const& str): Position{} {
        clear();
        *this = play(str);
    }
    explicit Position(char const* str): Position{} {
        clear();
        *this = play(str);
    }
    explicit Position(char const* str, size_t size): Position{} {
        clear();
        *this = play(str, size);
    }

    int get(int x, int y, Color mover = RED) const {
        int pos = x * USED_HEIGHT + y;
        if (!((mask_ >> pos) & 1)) return -1;
        return ((color_ >> pos) & 1) ^ mover;
    }
    bool full(int x) const {
        return mask_ & top_bit(x);
    }
    // Bitmap of positions where valid moves end up
    Bitmap possible_bits() const {
        return (mask_ + BOTTOM_BITS) & BOARD_MASK;
    }
    Bitmap opponent_winning_bits() const;
    Bitmap winning_bits() const;
    Bitmap sensible_bits() const;
    Position play(int x) const {
        Bitmap mask  = mask_ | (mask_ + bot_bit(x));
        Bitmap color = color_ ^ mask;
        return Position{color, mask};
    }
    Position play(char const* ptr, size_t size);
    Position play(char const* ptr) { return play(ptr, strlen(ptr)); }
    Position play(std::string const& str) {
        return play(str.data(), str.length());
    }
    Position play(std::istream& in) const;
    ALWAYS_INLINE
    bool won() const { return _won(color_); }
    int nr_plies() const { return popcount(mask_); }
    int nr_plies_left() const { return WIDTH * HEIGHT - nr_plies(); }
    Color to_move() const { return static_cast<Color>(nr_plies() & 1); }
    int score() const { return (nr_plies_left()+2) / 2; }
    // Score if win after 1 more ply
    int score1() const { return (nr_plies_left()+1) / 2; }
    // Score if win after 2 more ply
    int score2() const { return (nr_plies_left()+0) / 2; }
    // Score if win after 3 more ply
    int score3() const { return (nr_plies_left()-1) / 2; }
    Bitmap key() const {
        // This is indeed a unique key for a position
        // Recover position: consider column + GUARD_BIT
        // if of the form ^0+1*$ this is the mask and there was no color_ value
        // else if of the form ^0*1+0 then the mask is all 1's starting just
        // after the first 1 and _color can be recovered by subtraction
        // after the
        return color_ + mask_;
    }

    void to_string(char *buf) const;
    std::string to_string() const {
        char buffer[BOARD_BUFSIZE+1];
        to_string(buffer);
        return buffer;
    }
    void clear() {
        color_ = 0;
        mask_  = 0;
    }

    int negamax() const;
    int solve(bool weak) const;

    friend std::ostream& operator<<(std::ostream& os, Position const& pos) {
        char buffer[BOARD_BUFSIZE+1];
        pos.to_string(buffer);
        os << buffer;
        return os;
    }

    static Position BAD() {
        return Position{FULL_MAP, FULL_MAP};
    }
    explicit operator bool() const { return mask_ != FULL_MAP; }

    static void reset() {
        nr_visits_ = 0;
        hits_      = 0;
        misses_    = 0;
        transpositions_.clear();
    };
    ALWAYS_INLINE
    static void visit() { ++nr_visits_; };
    ALWAYS_INLINE
    static void hit() { ++hits_; };
    ALWAYS_INLINE
    static void miss() { ++misses_; };

    static uint64_t nr_visits() { return nr_visits_; };
    static uint64_t hits()      { return hits_; }
    static uint64_t misses()    { return misses_; }
    ALWAYS_INLINE
    Transposition::value_type* transposition_entry() const {
        return transpositions_.entry(key());
    }

  private:
    static uint64_t nr_visits_;
    static Transposition transpositions_;
    static uint64_t hits_;
    static uint64_t misses_;
    static std::array<Bitmap, WIDTH> const move_order_;
    static std::array<Bitmap, WIDTH> generate_move_order();

    Position(Bitmap color, Bitmap mask): color_{color}, mask_{mask} {}
    static bool _won(Bitmap mask);
    static Bitmap top_bit(int y) { return TOP_BIT << y * USED_HEIGHT; }
    static Bitmap bot_bit(int y) { return BOT_BIT << y * USED_HEIGHT; }
    inline static int _score(Bitmap color) {
        return MAX_STONES+1-popcount(color);
    }

    Bitmap _winning_bits(Bitmap color) const;
    int _alphabeta(int alpha, int beta) const;
    Position _play(Bitmap move_bit) const {
        Bitmap mask  = mask_  | move_bit;
        return Position{color_ ^ mask, mask};
    }

    // bitboards are laid out column by column, top in msb,
    // one 0 guard bit inbetween
    // (0,0) is bottom left of board and is in the lsb of Bitmap
    Bitmap color_;
    Bitmap mask_;
};
