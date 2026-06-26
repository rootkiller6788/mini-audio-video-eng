# Makefile for mini-av-sync
# Audio-Video Synchronization Module

CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -O2 -g
LDFLAGS  = -lm

INCDIR   = include
SRCDIR   = src
TESTDIR  = tests
EXAMPLEDIR = examples
BUILDDIR = build

SRCS     = $(SRCDIR)/av_sync_core.c \
           $(SRCDIR)/av_clock.c \
           $(SRCDIR)/av_buffer.c \
           $(SRCDIR)/av_timestamp.c \
           $(SRCDIR)/av_skew.c \
           $(SRCDIR)/av_scheduler.c

OBJS     = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))

TEST_SRC = $(TESTDIR)/test_av_sync.c
TEST_BIN = $(BUILDDIR)/test_av_sync

EXAMPLE_SRCS = $(EXAMPLEDIR)/example_basic_sync.c \
               $(EXAMPLEDIR)/example_clock_recovery.c \
               $(EXAMPLEDIR)/example_stream_sync.c
EXAMPLE_BINS = $(patsubst $(EXAMPLEDIR)/%.c,$(BUILDDIR)/%,$(EXAMPLE_SRCS))

.PHONY: all test examples clean lines check

all: $(TEST_BIN) $(EXAMPLE_BINS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(TEST_BIN): $(OBJS) $(TEST_SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $(TEST_SRC) $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_basic_sync: $(OBJS) $(EXAMPLEDIR)/example_basic_sync.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $(EXAMPLEDIR)/example_basic_sync.c $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_clock_recovery: $(OBJS) $(EXAMPLEDIR)/example_clock_recovery.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $(EXAMPLEDIR)/example_clock_recovery.c $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_stream_sync: $(OBJS) $(EXAMPLEDIR)/example_stream_sync.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -o $@ $(EXAMPLEDIR)/example_stream_sync.c $(OBJS) $(LDFLAGS)

examples: $(EXAMPLE_BINS)

test: $(TEST_BIN)
	@echo "=== Running mini-av-sync Tests ==="
	@$(TEST_BIN)
	@echo "=== All tests completed ==="

check: $(TEST_BIN) $(EXAMPLE_BINS)
	@echo "=== Build check passed ==="

clean:
	rm -rf $(BUILDDIR)

lines:
	@echo "include/ lines:"
	@cat $(INCDIR)/*.h | wc -l
	@echo "src/ C lines:"
	@cat $(SRCDIR)/*.c | wc -l
	@echo "src/ Lean lines:"
	@cat $(SRCDIR)/*.lean | wc -l
	@echo "--- Total (include + src C): ---"
	@cat $(INCDIR)/*.h $(SRCDIR)/*.c | wc -l
