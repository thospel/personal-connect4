#include <string>

// #define STATIC static
#define STATIC

#ifdef __GNUC__
# define RESTRICT	 __restrict__
# define NOINLINE	 __attribute__((__noinline__))
# define ALWAYS_INLINE	 __attribute__((always_inline))
# define LIKELY(x)	 __builtin_expect(!!(x),true)
# define UNLIKELY(x)	 __builtin_expect(!!(x),false)
# define HOT		 __attribute__((__hot__))
# define COLD		 __attribute__((__cold__))
// pure means does not modify any (non const) global memory.
# define PURE		 __attribute__((__pure__))
// const means does not read/modify any (non const) global memory.
# define FUNCTIONAL	 __attribute__((__const__))
# define ALLOC_SIZE(x)	 __attribute__((alloc_size(x)))
# define MALLOC		 __attribute__((malloc))
# define NONNULL         __attribute__((nonnull))
# define RETURNS_NONNULL __attribute__((returns_nonnull))
# define WARN_UNUSED     __attribute__((warn_unused_result))
# define UNUSED          __attribute__((unused))
# define BUILTIN_CONSTANT(x) __builtin_constant_p(x)
# define FILE_LINE_P     , char const* file=__builtin_FILE(), int line=__builtin_LINE()
# define FILE_LINE_PP    , char const* file, int line
# define FILE_LINE_A     , file, line
# define LOGGER          logger << file << ":" << line << ": "
#else // __GNUC__
# define RESTRICT
# define NOINLINE
# define ALWAYS_INLINE
# define LIKELY(x)	(x)
# define UNLIKELY(x)	(x)
# define HOT
# define COLD
# define PURE
# define FUNCTIONAL
# define ALLOC_SIZE(x)
# define MALLOC
# define NONNULL
# define RETURNS_NONNULL
# define WARN_UNUSED
# define UNUSED
# define BUILTIN_CONSTANT(x) true
# define FILE_LINE_P
# define FILE_LINE_PP
# define FILE_LINE_A
# define LOGGER          logger
#endif // __GNUC__

#define CAT(x, y) _CAT(x,y)
#define _CAT(x, y)	x ## y

#define STRINGIFY(x) _STRINGIFY(x)
#define _STRINGIFY(x) #x

#if defined(_MSC_VER)
     /* Microsoft C/C++-compatible compiler */
     #include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
     /* GCC-compatible compiler, targeting x86/x86-64 */
     #include <x86intrin.h>
#elif defined(__GNUC__) && defined(__ARM_NEON__)
     /* GCC-compatible compiler, targeting ARM with NEON */
     #include <arm_neon.h>
#elif defined(__GNUC__) && defined(__IWMMXT__)
     /* GCC-compatible compiler, targeting ARM with WMMX */
     #include <mmintrin.h>
#elif (defined(__GNUC__) || defined(__xlC__)) && (defined(__VEC__) || defined(__ALTIVEC__))
     /* XLC or GCC-compatible compiler, targeting PowerPC with VMX/VSX */
     #include <altivec.h>
#elif defined(__GNUC__) && defined(__SPE__)
     /* GCC-compatible compiler, targeting PowerPC with SPE */
     #include <spe.h>
#endif

extern std::string const VCS_COMMIT;
extern std::string const VCS_COMMIT_TIME;
extern const std::string PID;

extern bool FATAL;

[[noreturn]] void throw_errno(std::string const& text);
[[noreturn]] void throw_errno(int err, std::string const& text);
[[noreturn]] void throw_logic(char const* text);
[[noreturn]] void throw_logic(char const*, const char* file, int line);
[[noreturn]] void throw_logic(std::string const& text);
[[noreturn]] void throw_logic(std::string const& text, const char* file, int line);

std::string time_string(time_t time);
std::string time_string();
