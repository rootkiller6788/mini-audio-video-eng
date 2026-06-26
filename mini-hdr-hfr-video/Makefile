CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -g
INCLUDES = -Iinclude
SRCDIR = src
TESTDIR = tests
EXAMPLEDIR = examples
BINDIR = build

SRCS = $(SRCDIR)/hdr_core.c $(SRCDIR)/hdr_tone_mapping.c $(SRCDIR)/hdr_color.c \
       $(SRCDIR)/hfr_core.c $(SRCDIR)/hfr_motion.c $(SRCDIR)/hdr_advanced.c \
       $(SRCDIR)/hfr_advanced.c
TESTS = $(TESTDIR)/test_hdr_core.c $(TESTDIR)/test_hdr_tone.c \
        $(TESTDIR)/test_hdr_color.c $(TESTDIR)/test_hfr_core.c
EXAMPLES = $(EXAMPLEDIR)/ex1_hdr_display.c $(EXAMPLEDIR)/ex2_tone_mapping.c \
           $(EXAMPLEDIR)/ex3_frame_rate_up.c

OBJS = $(patsubst $(SRCDIR)/%.c,$(BINDIR)/%.o,$(SRCS))
TESTBINS = $(patsubst $(TESTDIR)/%.c,$(BINDIR)/%,$(TESTS))
EXAMPLEBINS = $(patsubst $(EXAMPLEDIR)/%.c,$(BINDIR)/%,$(EXAMPLES))

.PHONY: all clean test examples

all: $(OBJS)
	@echo "All objects built: $(OBJS)"

$(BINDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BINDIR)/test_%: $(TESTDIR)/test_%.c $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) -lm -o $@

$(BINDIR)/ex%: $(EXAMPLEDIR)/ex%.c $(OBJS)
	@mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) $(INCLUDES) $< $(OBJS) -lm -o $@

test: $(TESTBINS)
	@echo "Running tests..."
	@for t in $(TESTBINS); do echo "--- $$t ---"; ./$$t; done

examples: $(EXAMPLEBINS)
	@echo "Running examples..."
	@for e in $(EXAMPLEBINS); do echo "--- $$e ---"; ./$$e; done

clean:
	rm -rf $(BINDIR)
