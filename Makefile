# name of resulting binary
BIN		= hash

# suffix for profiling binary
SUFFIX_PROF	= _prof

# suffix for coverage binary
SUFFIX_COVERAGE	= _cov

# cmdline parameters of program
PARAMS		= input input2

# expected output of program
REF		= 8879313950759542027

GCC		= gcc

PERF		= $(shell [ -x /usr/sbin/perf ] && echo -n "/usr/sbin/perf" || which perf )
PERF_REPEATS	= 20
PERF_EVENTS	= cycles,instructions,cache-misses,branch-misses

PROF_OUTFILE	= prof.out
GPROF		= gprof

GCOV		= gcov
COVERAGE_LIBS	= -lgcov

BIN_PROF	= $(BIN)$(SUFFIX_PROF)
BIN_COVERAGE	= $(BIN)$(SUFFIX_COVERAGE)

all: prof coverage test perf

test: $(BIN) $(PARAMS)
	[ "$(REF)" = "`./$^`" ]

perf: $(BIN) $(PARAMS)
	$(PERF) stat -r $(PERF_REPEATS) -e $(PERF_EVENTS) ./$^ > /dev/null

prof: $(BIN_PROF) $(PARAMS)
	./$^ > /dev/null
	$(GPROF) $< > $(PROF_OUTFILE)

coverage: $(BIN_COVERAGE) $(PARAMS) $(BIN_COVERAGE).gcno
	./$(BIN_COVERAGE) $(PARAMS) > /dev/null
	$(GCOV) $(BIN_COVERAGE)

%: %.c
	$(GCC) -O3 $< -o $(BIN)
	$(GCC) -fno-pie -pg -lm -O $< -o $(BIN_PROF)
	$(GCC) --coverage -lm -O3 -c $< -o $(BIN_COVERAGE).o
	$(GCC) $(BIN_COVERAGE).o $(COVERAGE_LIBS) -o $(BIN_COVERAGE)

clean:
	rm -f $(BIN) $(BIN_PROF) $(BIN_COVERAGE)
	rm -f $(BIN_COVERAGE).o
	rm -f $(PROF_OUTFILE)
	rm -f *.gcov *.gcno *.gcda gmon.out
	rm -f *~

.PHONY: all test perf prof coverage clean
