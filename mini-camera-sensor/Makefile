# mini-camera-sensor Makefile
# CMOS/CCD Image Sensor — Solid-State Imaging Pipeline
#
# Reference curricula: MIT 6.630, Stanford EE247, Berkeley EE117,
#   Michigan EECS411, ETH 227-0455, Tsinghua EM Fields
#
# Textbooks: Nakamura (2005), Holst & Lomheim (2011),
#   Janesick (2007), Poynton (2012)

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

SRCS     = $(wildcard $(SRCDIR)/*.c)
OBJS     = $(patsubst $(SRCDIR)/%.c, $(BUILDDIR)/%.o, $(SRCS))
TESTS    = $(wildcard $(TESTDIR)/*.c)
TESTBINS = $(patsubst $(TESTDIR)/%.c, $(BUILDDIR)/test_%, $(TESTS))
EXAMPLES = $(wildcard $(EXAMDIR)/*.c)
EXAMBINS = $(patsubst $(EXAMDIR)/%.c, $(BUILDDIR)/example_%, $(EXAMPLES))
BENCHES  = $(wildcard $(BENCHDIR)/*.c)
BENCHBINS= $(patsubst $(BENCHDIR)/%.c, $(BUILDDIR)/bench_%, $(BENCHES))
LIB      = $(BUILDDIR)/libminicamsensor.a

.PHONY: all clean test examples bench demo count

all: $(LIB)

$(BUILDDIR):
	@mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(BUILDDIR)/test_%: $(TESTDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lminicamsensor $(LDFLAGS) -o $@

test: $(LIB) $(TESTBINS)
	@echo "=== Running Camera Sensor Tests ==="
	@passed=0; failed=0; \
	for t in $(TESTBINS); do \
		if $$t; then \
			passed=$$((passed+1)); \
			echo "  PASS: $$t"; \
		else \
			failed=$$((failed+1)); \
			echo "  FAIL: $$t"; \
		fi; \
	done; \
	echo "=== $$passed passed, $$failed failed ==="; \
	[ $$failed -eq 0 ] || exit 1

$(BUILDDIR)/example_%: $(EXAMDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lminicamsensor $(LDFLAGS) -o $@

examples: $(EXAMBINS)
	@echo "=== Building Examples ==="
	@for e in $(EXAMBINS); do echo "  Built: $$e"; done

$(BUILDDIR)/bench_%: $(BENCHDIR)/%.c $(LIB) | $(BUILDDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L$(BUILDDIR) -lminicamsensor $(LDFLAGS) -o $@

bench: $(BENCHBINS)
	@echo "=== Running Benchmarks ==="
	@for b in $(BENCHBINS); do $$b; done

demo: examples
	@echo "=== Running Demos ==="
	@for e in $(EXAMBINS); do echo "--- $$e ---"; $$e; done

count:
	@echo "include/: $$(find include -name '*.h' -exec cat {} + | wc -l) lines"
	@echo "src/:     $$(find src -name '*.c' -exec cat {} + | wc -l) lines"
	@echo "total:    $$(find include src \( -name '*.h' -o -name '*.c' \) -exec cat {} + | wc -l) lines"

clean:
	rm -rf $(BUILDDIR)
