# SANITIZE=-fsanitize=address -fsanitize=leak -fsanitize=undefined
# SANITIZE=-fsanitize=thread

# CC_MALLOC=-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free
# LIBS_MALLOC=-l:libtcmalloc_minimal.so.4

# -mno-vzeroupper
CXXFLAGS := -Wall -Wextra -Wformat=2 -Wfloat-equal -Wlogical-op -Wshift-overflow=2 -Wduplicated-cond -Wcast-qual -Wcast-align -Winline --param inline-unit-growth=200 --param large-function-growth=1000 --param max-inline-insns-single=800 -fno-math-errno -funsafe-math-optimizations -ffinite-math-only -ffast-math -fno-signed-zeros -fno-trapping-math -Ofast -march=native -fstrict-aliasing $(CC_MALLOC) -std=c++17 -g3 -pthread $(SANITIZE)
# CXXFLAGS += -Wrestrict
# CXXFLAGS += -D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC -D_FORTIFY_SOURCE=2
# CXXFLAGS += -D CHECK=1

LDFLAGS = -g3 -pthread $(SANITIZE)
# On NFS run once: ccache -o 'compiler_check=stat -c "%y" %compiler%;hostname'
CXX := ccache $(CXX)

LDLIBS += $(LIBS_MALLOC)

CXXFLAGS += -DCOMMIT="`git rev-parse HEAD`" -DCOMMIT_TIME="`git show -s --format=%ci HEAD`"

all: connect4

connect4.o position.o system.o revision.o: Makefile constants.hpp
connect4.o position.o system.o: system.hpp
connect4.o position.o: position.hpp
connect4.o revision.o: revision.hpp

connect4.o: connect4.cpp
position.o: position.cpp
system.o:   system.cpp
revision.o: revision.cpp git_time

connect4: connect4.o position.o system.o revision.o
	$(CXX) $(LDFLAGS) -pthread $^ $(LOADLIBES) $(LDLIBS) -o $@

git_time: FORCE
	@touch --date=@`git show -s --format=%ct HEAD` git_time

opening.txt: connect4
	echo " " | ./connect4 -T0 -g4 -k | tee opening.txt

FORCE:

.o.S:
	objdump -lwSC $< > $@

.PHONY: clean bench benchmark
clean:
	rm -f *.o *.S *.s connect4 core

realclean: clean
	rm -f connect4-*

bench benchmark: connect4
	./tester -P Test_L*
