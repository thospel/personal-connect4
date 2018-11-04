#include <iostream>
#include <thread>

#include <cstring>
#include <cstdint>
#include <climits>

#include "vector.hpp"
#include "system.hpp"

typedef uint64_t Bitmap;

constexpr uint LOG2(size_t value) {
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
static_assert(SCORE_BITS <= LEFT_BITS, "No space for hash result");

static Bitmap const ONE = 1;
static Bitmap const TOP_BIT  = ONE << (HEIGHT-1);
static Bitmap const BOT_BIT  = ONE;
static Bitmap const FULL_MAP = -1;
static Bitmap const KEY_MASK = (ONE << KEY_BITS) - 1;

// 64 MB transposition table
static size_t const TRANSPOSITION_BITS = 23;
static size_t const TRANSPOSITION_SIZE = static_cast<size_t>(1) << TRANSPOSITION_BITS;

inline int popcount(Bitmap value) {
#ifdef __POPCNT__
    static_assert(sizeof(Bitmap) * CHAR_BIT == 64,
                  "Bitmap is not 64 bits");
    return _mm_popcnt_u64(value);
#else  // __POPCNT__
    static_assert(sizeof(Bitmap) == sizeof(unsigned long),
                  "Bitmap is not unsigned long");
    return __builtin_popcountll(value);
#endif // __POPCNT__
}

class Transposition {
  public:
    void clear() {
        entries_.fill(0);
        // Make sure the empty board is not a hit
        entries_[fast_hash(0)] = 1;
        hits_ = 0;
        misses_ = 0;
    }
    // Make sure the minimum value you set is 2
    // The -1 and +1 in get and set is so that the generated code will only do
    // a single conditional jump
    // (because on a cache hit the compiler can now see the value is not 0)
    // Since just before and after we do an offset by MAX_SCORE the +1 and -1
    // will in fact be optimized away
    ALWAYS_INLINE
    void set(Bitmap key, int value) {
        entries_[fast_hash(key)] = key | static_cast<Bitmap>(value-1) << KEY_BITS;
    }
    ALWAYS_INLINE
    Bitmap get(Bitmap key) const {
        auto value = entries_[fast_hash(key)];
        if ((value & KEY_MASK) != key) {
            // ++misses_;
            return 0;
        }
        // ++hits_;
        return 1 + (value >> KEY_BITS);
    }
    uint64_t hits()   const { return hits_; }
    uint64_t misses() const { return misses_; }
  private:
    ALWAYS_INLINE
    static Bitmap fast_hash(Bitmap key) {
        static_assert(sizeof(key) == sizeof(LCM_MULTIPLIER),
                      "Bitmap is not 64 bits. Find another multiplier");
        key *= LCM_MULTIPLIER;
        return key >> (ALL_BITS-TRANSPOSITION_BITS);
    }
    static uint64_t const LCM_MULTIPLIER = UINT64_C(6364136223846793005);

    mutable uint64_t hits_;
    mutable uint64_t misses_;
    std::array<Bitmap, TRANSPOSITION_SIZE> entries_;
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
    inline Position play(int x) const {
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
    Color to_move() const { return static_cast<Color>(nr_plies() & 1); }
    int score() const {
        return MAX_STONES+1-popcount(color_);
    }
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
    int alphabeta(int alpha, int beta) const;

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
        transpositions_.clear();
    };
    static void visit() { ++nr_visits_; };
    static uint64_t nr_visits() { return nr_visits_; };
    static uint64_t hits  () { return transpositions_.hits  (); };
    static uint64_t misses() { return transpositions_.misses(); };
    ALWAYS_INLINE
    static void set(Bitmap key, int value) {
        transpositions_.set(key, value);
    }
    ALWAYS_INLINE
    static Bitmap get(Bitmap key) {
        return transpositions_.get(key);
    }

  private:
    static uint64_t nr_visits_;
    static Transposition transpositions_;
    static std::array<int, WIDTH> const move_order_;
    static std::array<int, WIDTH> generate_move_order();

    Position(Bitmap color, Bitmap mask): color_{color}, mask_{mask} {}
    static bool _won(Bitmap mask);
    static Bitmap top_bit(int y) { return TOP_BIT << y * USED_HEIGHT; }
    static Bitmap bot_bit(int y) { return BOT_BIT << y * USED_HEIGHT; }
    // bitboards are laid out column by column, top in msb,
    // one 0 guard bit inbetween
    // (0,0) is bottom left of board and is in the lsb of Bitmap
    Bitmap color_;
    Bitmap mask_;
};
