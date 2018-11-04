#include <iostream>
#include <thread>

#include <cstring>
#include <cstdint>
#include <climits>

#include "vector.hpp"
#include "system.hpp"

typedef uint64_t Bitmap;

static int const WIDTH  = 7;
static int const HEIGHT = 6;
// Most number of stones one color can play
// (one more for first player on odd area boards)
static int const MAX_STONES = (WIDTH*HEIGHT+1)/2;	// 21
static int const MAX_SCORE  = MAX_STONES+1-4;		// 18
static int const BOARD_BUFSIZE = (HEIGHT+2) * (WIDTH*2+2);	// 128
static int const GUARD_BITS = 1;
static int const USED_HEIGHT = HEIGHT + GUARD_BITS;
static_assert(GUARD_BITS > 0, "There must be GUARD_BITS");
static int const      BITS = WIDTH*USED_HEIGHT-GUARD_BITS;	// 48
static int const  ALL_BITS = sizeof(Bitmap) * CHAR_BIT;	// 64
static int const LEFT_BITS = ALL_BITS-BITS;		// 16
static_assert(LEFT_BITS >= 0, "Bitmap type is too small");
static_assert((1 << LEFT_BITS) >= 2*MAX_SCORE+1, "No space for hash result");

static Bitmap const ONE = 1;
static Bitmap const TOP_BIT  = ONE << (HEIGHT-1);
static Bitmap const BOT_BIT  = ONE;
static Bitmap const FULL_MAP = -1;

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

    static void clear_visits() { nr_visits_ = 0; };
    static void visit() { ++nr_visits_; };
    static uint64_t nr_visits() { return nr_visits_; };

  private:
    static uint64_t nr_visits_;

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
