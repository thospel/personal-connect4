#include <array>
#include <iostream>
#include <thread>
#include <vector>

#include <cstring>
#include <cstdint>
#include <climits>

#include "constants.hpp"
#include "system.hpp"

typedef uint64_t Bitmap;
std::string to_bits(Bitmap bitmap);
// std::ostream& operator<<(std::ostream& os, Bitmap bitmap);

static constexpr uint LOG2(size_t value) {
    return value <= 1 ? 0 : 1+LOG2((value+1) / 2);
}

static int const INDENT = 2;

static int const WIDTH  = 7;
static int const HEIGHT = 6;
static int const AREA   = WIDTH*HEIGHT;				// 42
// Most number of stones one color can play
// (one more for first player on odd area boards)
static int const MAX_STONES = (AREA+1)/2;			// 21
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
static Bitmap const BOTTOM_BIT  = ONE;
static Bitmap const TOP_BIT     = ONE << (HEIGHT-1);
static Bitmap const ABOVE_BIT   = ONE << HEIGHT;
static Bitmap const FULL_MAP = -1;
static Bitmap const KEY_MASK   = (ONE << KEY_BITS)   - 1;
static Bitmap const SCORE_MASK = (ONE << SCORE_BITS) - 1;
static Bitmap const BEST_MASK  = (ONE << BEST_BITS)  - 1;

static constexpr Bitmap ALTERNATING_ROWS(Bitmap row0, Bitmap row1,
                                         int n=WIDTH) {
    return
        n == 0 ? 0 :
        ALTERNATING_ROWS(row0, row1, n-1) << USED_HEIGHT | (n%2 ? row0 : row1);
}
static constexpr Bitmap REPEATING_ROWS(Bitmap model = ONE, int n=WIDTH) {
    return ALTERNATING_ROWS(model, model, n);
}

static Bitmap const BOTTOM_BITS = REPEATING_ROWS(BOTTOM_BIT);
static Bitmap const    TOP_BITS = REPEATING_ROWS(   TOP_BIT);
static Bitmap const  ABOVE_BITS = REPEATING_ROWS( ABOVE_BIT);
static Bitmap const BOARD_MASK  = REPEATING_ROWS((ONE << HEIGHT)-1);
static Bitmap const alternating_rows[2] = {
    ALTERNATING_ROWS((ONE << HEIGHT)-1, 0),
    ALTERNATING_ROWS(                0, (ONE << HEIGHT)-1),
};

// 4 MB transposition table
static size_t const TRANSPOSITION_SIZE = static_cast<size_t>(1) << 19;

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
inline int first_bit(Bitmap value) {
    static_assert(sizeof(value) == sizeof(unsigned long),
                  "Bitmap is not unsigned long");
    return (sizeof(value)*CHAR_BIT-1) - __builtin_clzl(value);
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
        static value_type INVALID() { return value_type{static_cast<Bitmap>(-1)}; }

      private:
        explicit value_type(Bitmap value): value_{value} {}
        Bitmap value_;
    };
    Transposition(size_t size=0);
    void resize(size_t size);
    void clear() HOT;
    value_type* entry(Bitmap key) {
        return &entries_[fast_hash(key)];
    }
    value_type const* entry(Bitmap key) const {
        return &entries_[fast_hash(key)];
    }
    size_t size()  const { return entries_.size();  }
    size_t bytes() const { return size() * sizeof(value_type); }

  private:
    ALWAYS_INLINE
    Bitmap fast_hash(Bitmap key) const {
        static_assert(sizeof(key) == sizeof(LCM_MULTIPLIER),
                      "Bitmap is not 64 bits. Find another multiplier");
        key *= LCM_MULTIPLIER;
        return key >> bits_;
    }
    static uint64_t const LCM_MULTIPLIER = UINT64_C(6364136223846793005);

    int bits_;
    std::vector<value_type> entries_;
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
    bool playable(int x) const {
        return ((mask_ + bottom_bit(x)) & ~BOARD_MASK) == 0;
    }
    Position play(int x) const {
        Bitmap mask  = mask_ | (mask_ + bottom_bit(x));
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
    int nr_plies_left() const { return AREA - nr_plies(); }
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
    bool operator==(Position const& rhs) const {
        return key() == rhs.key();
    }
    void to_string(char *buf, int indent=0) const;
    std::string to_string(int indent=0) const {
        char buffer[BOARD_BUFSIZE+1+(HEIGHT+2)*indent];
        to_string(buffer, indent);
        return buffer;
    }
    void clear() {
        color_ = 0;
        mask_  = 0;
    }

    int negamax() const;
    int solve(bool weak) const;
    void generate_book(std::string how, int depth, bool weak=false) const;

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

    static void init(size_t size) { transpositions_.resize(size); }
    static void reset(bool keep_transpositions = false) {
        start_depth_ = 0;
        nr_visits_ = 0;
        hits_      = 0;
        misses_    = 0;
        if (!keep_transpositions) transpositions_.clear();
    };
    void set_depth() const {
        start_depth_ = nr_plies();
    }
    int indent() const {
        return nr_plies() - start_depth_;
    }
    ALWAYS_INLINE
    static void visit() { ++nr_visits_; };
    ALWAYS_INLINE
    static void hit() { ++hits_; };
    ALWAYS_INLINE
    static void miss() { ++misses_; };

    static uint64_t nr_visits() { return nr_visits_; };
    static uint64_t hits()      { return hits_; }
    static uint64_t misses()    { return misses_; }
    static size_t transpositions_size()  { return transpositions_.size();  }
    static size_t transpositions_bytes() { return transpositions_.bytes(); }
    ALWAYS_INLINE
    Transposition::value_type* transposition_entry() const {
        return transpositions_.entry(key());
    }
    std::vector<int> principal_variation(int score, bool weak) const;

  private:
    static int start_depth_;
    static uint64_t nr_visits_;
    static Transposition transpositions_;
    static uint64_t hits_;
    static uint64_t misses_;
    static std::array<Bitmap, WIDTH> const move_order_;
    static std::array<Bitmap, WIDTH> generate_move_order();

    Position(Bitmap color, Bitmap mask): color_{color}, mask_{mask} {}
    static bool _won(Bitmap mask);
    static Bitmap    top_bit(int y) { return    TOP_BIT << y * USED_HEIGHT; }
    static Bitmap bottom_bit(int y) { return BOTTOM_BIT << y * USED_HEIGHT; }
    inline static int _score(Bitmap color) {
        return MAX_STONES+1-popcount(color);
    }

    Bitmap _winning_bits(Bitmap color) const;
    int _alphabeta(int alpha, int beta, Bitmap opponent_win) const;
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

template <>
struct std::hash<Position> {
    size_t operator()(Position const& pos) const {
        return pos.key() * LCM_MULTIPLIER;
    }
  private:
    static uint64_t const LCM_MULTIPLIER = UINT64_C(6364136223846793005);
};
