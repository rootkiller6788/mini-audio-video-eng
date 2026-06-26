# Makefile — mini-video-codec: H.264/H.265 Video Codec Fundamentals
#
# Builds the video codec library, tests, and examples.
# Compiler: GCC (C99-compliant)
# Uses assert-based tests (no external test framework).

CC       := gcc
CFLAGS   := -std=c99 -Wall -Wextra -pedantic -O2 -g
INCLUDES := -Iinclude
LDFLAGS  := -lm

# Source files (library)
SRCS := src/video_codec.c    \
        src/dct_2d.c         \
        src/motion_est.c     \
        src/entropy_codec.c  \
        src/prediction.c     \
        src/quantizer.c      \
        src/deblocking.c     \
        src/color_space.c

# Object files
OBJS := $(SRCS:.c=.o)

# Test files
TEST_SRCS := tests/test_video_codec.c \
             tests/test_dct_2d.c      \
             tests/test_motion_est.c  \
             tests/test_entropy.c     \
             tests/test_prediction.c  \
             tests/test_quantizer.c

TEST_BINS := $(TEST_SRCS:.c=)

# Example files
EXAMPLE_SRCS := examples/example_video_codec.c \
                examples/example_dct_demo.c    \
                examples/example_motion_comp.c

EXAMPLE_BINS := $(EXAMPLE_SRCS:.c=)

# Library
LIB := libvideo_codec.a

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

tests/test_video_codec: tests/test_video_codec.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

tests/test_dct_2d: tests/test_dct_2d.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

tests/test_motion_est: tests/test_motion_est.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

tests/test_entropy: tests/test_entropy.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

tests/test_prediction: tests/test_prediction.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

tests/test_quantizer: tests/test_quantizer.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Examples (end-to-end, >30 lines with main())

examples/example_video_codec: examples/example_video_codec.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

examples/example_dct_demo: examples/example_dct_demo.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

examples/example_motion_comp: examples/example_motion_comp.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -lvideo_codec $(LDFLAGS) -o $@

examples: $(EXAMPLE_BINS)

# ---------------------------------------------------------------------------
# Clean

clean:
	rm -f $(OBJS) $(LIB)
	rm -f $(TEST_BINS)
	rm -f $(EXAMPLE_BINS)
	rm -f tests/*.o examples/*.o src/*.o
