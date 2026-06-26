# mini-3d-audio Makefile
# Spatial Audio / 3D Audio Engine — HRTF, Ambisonics, Binaural, VBAP, Room Acoustics
#
# Reference curricula: MIT 6.003, Stanford EE359, Berkeley EE123, Illinois ECE310,
#   Michigan EECS351, Georgia Tech ECE4270, TU Munich Signal Processing,
#   ETH 227-0427, Tsinghua Signal & Systems
#
# Textbooks: Poynton (2012), Zwicker & Fastl (2007), Blauert (1997), Xie (2013)

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS  = -lm
INCLUDES = -Iinclude
AR       = ar
ARFLAGS  = rcs

SRCDIR   = src
INCDIR   = include
TESTDIR  = tests
EXAMDIR  = examples
BENCHDIR = benches
BUILDDIR = build

# Source files
SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))

# Test files
TESTS    = $(wildcard $(TESTDIR)/*.c)
TESTBINS = $(patsubst $(TESTDIR)/%.c, $(BUILDDIR)/test_%, $(TESTS))

# Example files
EXAMPLES = $(wildcard $(EXAMDIR)/*.c)
EXAMBINS = $(patsubst $(EXAMDIR)/%.c, $(BUILDDIR)/example_%, $(EXAMPLES))

# Bench files
BENCHES = $(wildcard $(BENCHDIR)/*.c)
BENCHBINS = $(patsubst $(BENCHDIR)/%.c, $(BUILDDIR)/bench_%, $(BENCHES))

LIB      = $(BUILDDIR)/libmini3daudio.a

.PHONY: all clean test examples bench

all: $(LIB)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

# Test compilation and execution
$(BUILDDIR)/test_%: $(TESTDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lmini3daudio $(LDFLAGS) -o $@

test: $(LIB) $(TESTBINS)
	@echo "=== Running 3D Audio Tests ==="
	@passed=0; failed=0; \
	for test in $(TESTBINS); do \
		if $$test; then \
			passed=$$((passed+1)); \
			echo "  PASS: $$test"; \
		else \
			failed=$$((failed+1)); \
			echo "  FAIL: $$test"; \
		fi; \
	done; \
	echo "=== Results: $$passed passed, $$failed failed ==="; \
	[ $$failed -eq 0 ] || exit 1

# Example compilation
$(BUILDDIR)/example_%: $(EXAMDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lmini3daudio $(LDFLAGS) -o $@

examples: $(EXAMBINS)
	@echo "=== Building Examples ==="
	@for ex in $(EXAMBINS); do echo "  Built: $$ex"; done

# Bench compilation
$(BUILDDIR)/bench_%: $(BENCHDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lmini3daudio $(LDFLAGS) -o $@

bench: $(BENCHBINS)
	@echo "=== Running Benchmarks ==="
	@for b in $(BENCHBINS); do $$b; done

# Demos
demo: examples
	@echo "=== Running Demos ==="
	@for ex in $(EXAMBINS); do echo "--- $$ex ---"; $$ex; done

# Line count
count:
	@echo "include/ lines: $$(find include -name '*.h' -exec cat {} + | wc -l)"
	@echo "src/ lines:     $$(find src -name '*.c' -exec cat {} + | wc -l)"
	@echo "total:          $$(find include src -name '*.h' -o -name '*.c' | xargs cat | wc -l)"

clean:
	rm -rf $(BUILDDIR)
