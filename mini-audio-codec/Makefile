# Makefile — mini-audio-codec: Audio Codec Fundamentals
#
# Builds the audio codec library, tests, and examples.
# Compiler: GCC (or any C99-compliant compiler)
# Uses assert-based tests (no external test framework).

CC       := gcc
CFLAGS   := -std=c99 -Wall -Wextra -pedantic -O2 -g
INCLUDES := -Iinclude
LDFLAGS  := -lm

# Source files (library)
SRCS := src/pcm_codec.c       \
        src/adpcm_codec.c     \
        src/mdct.c            \
        src/psychoacoustic.c  \
        src/bitalloc.c        \
        src/subband.c         \
        src/entropy.c         \
        src/lpc_analysis.c

# Object files
OBJS := $(SRCS:.c=.o)

# Test files
TEST_SRCS := tests/test_pcm.c          \
             tests/test_mdct.c         \
             tests/test_psychoacoustic.c \
             tests/test_bitalloc.c     \
             tests/test_entropy.c      \
             tests/test_lpc.c

TEST_BINS := $(TEST_SRCS:.c=)

# Example files
EXAMPLE_SRCS := examples/example_wav_codec.c   \
                examples/example_mdct_demo.c   \
                examples/example_adpcm_codec.c

EXAMPLE_BINS := $(EXAMPLE_SRCS:.c=)

# Library
LIB := libaudio_codec.a

# ---------------------------------------------------------------------------
.PHONY: all clean test examples

all: $(LIB) test examples

# Static library
$(LIB): $(OBJS)
	ar rcs $@ $^

# Compile C sources to objects
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# ---------------------------------------------------------------------------
# Tests

test: $(TEST_BINS)
	@echo "=== Running Tests ==="
	@for t in $(TEST_BINS); do \
		echo "--- $$t ---"; \
		./$$t && echo "PASS: $$t" || echo "FAIL: $$t"; \
	done

tests/test_pcm: tests/test_pcm.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

tests/test_mdct: tests/test_mdct.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

tests/test_psychoacoustic: tests/test_psychoacoustic.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

tests/test_bitalloc: tests/test_bitalloc.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

tests/test_entropy: tests/test_entropy.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

tests/test_lpc: tests/test_lpc.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Examples (end-to-end, >30 lines with main())

examples/example_wav_codec: examples/example_wav_codec.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

examples/example_mdct_demo: examples/example_mdct_demo.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

examples/example_adpcm_codec: examples/example_adpcm_codec.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -laudio_codec $(LDFLAGS) -o $@

examples: $(EXAMPLE_BINS)

# ---------------------------------------------------------------------------
# Clean

clean:
	rm -f $(OBJS) $(LIB)
	rm -f $(TEST_BINS)
	rm -f $(EXAMPLE_BINS)
	rm -f tests/*.o examples/*.o
