CC       = gcc
CFLAGS   = -Wall -Wextra -std=c11 -O2 -g -Iinclude
LDFLAGS  = -lm

SRCDIR   = src
INCDIR   = include
TESTDIR  = tests
EXAMPLEDIR = examples
BUILDDIR = build

SRCS     = $(SRCDIR)/rtp.c $(SRCDIR)/rtcp.c $(SRCDIR)/rtsp.c $(SRCDIR)/mpeg_ts.c $(SRCDIR)/hls_dash.c $(SRCDIR)/session.c $(SRCDIR)/jitter_buffer.c
OBJS     = $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SRCS))
TEST_SRC = $(TESTDIR)/test_protocols.c
TEST_BIN = $(BUILDDIR)/test_protocols
EXAMPLE_SRCS = $(EXAMPLEDIR)/example_rtp_loop.c $(EXAMPLEDIR)/example_rtsp_session.c $(EXAMPLEDIR)/example_hls_generation.c
EXAMPLE_BINS = $(patsubst $(EXAMPLEDIR)/%.c,$(BUILDDIR)/%,$(EXAMPLE_SRCS))

.PHONY: all test examples clean lines check

all: $(TEST_BIN) $(EXAMPLE_BINS)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TEST_BIN): $(OBJS) $(TEST_SRC) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_rtp_loop: $(OBJS) $(EXAMPLEDIR)/example_rtp_loop.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/example_rtp_loop.c $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_rtsp_session: $(OBJS) $(EXAMPLEDIR)/example_rtsp_session.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/example_rtsp_session.c $(OBJS) $(LDFLAGS)

$(BUILDDIR)/example_hls_generation: $(OBJS) $(EXAMPLEDIR)/example_hls_generation.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(EXAMPLEDIR)/example_hls_generation.c $(OBJS) $(LDFLAGS)

examples: $(EXAMPLE_BINS)

test: $(TEST_BIN)
	@echo "=== Running mini-streaming-protocol Tests ==="
	@$(TEST_BIN)
	@echo "=== All tests completed ==="

check: $(TEST_BIN) $(EXAMPLE_BINS)
	@echo "=== Build check passed ==="

clean:
	rm -rf $(BUILDDIR)

lines:
	@echo "include/ lines:"
	@cat $(INCDIR)/*.h 2>/dev/null | wc -l
	@echo "src/ C lines:"
	@cat $(SRCDIR)/*.c 2>/dev/null | wc -l
	@echo "src/ Lean lines:"
	@cat $(SRCDIR)/*.lean 2>/dev/null | wc -l
	@echo "--- Total (include + src C): ---"
	@cat $(INCDIR)/*.h $(SRCDIR)/*.c 2>/dev/null | wc -l
