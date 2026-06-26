# Makefile — mini-display-technology: Display Technology Fundamentals
#
# Builds the display technology library, tests, and examples.
# Compiler: GCC (C99-compliant)
# Uses assert-based tests (no external test framework).

CC       := gcc
CFLAGS   := -std=c99 -Wall -Wno-misleading-indentation -O2 -g
INCLUDES := -Iinclude
LDFLAGS  := -lm

# Source files (library)
SRCS := src/display_timing.c    \
        src/color_science.c     \
        src/image_process.c     \
        src/display_interface.c \
        src/gamma_calibration.c \
        src/frame_buffer.c

# Object files
OBJS := $(SRCS:.c=.o)

# Test files
TEST_SRCS := tests/test_display_timing.c \
             tests/test_color_science.c  \
             tests/test_image_process.c  \
             tests/test_gamma.c

TEST_BINS := $(TEST_SRCS:.c=)

# Example files
EXAMPLE_SRCS := examples/example_vesa_timing.c  \
                examples/example_color_gamut.c  \
                examples/example_dithering.c

EXAMPLE_BINS := $(EXAMPLE_SRCS:.c=)

# Library
LIB := libdisplay_tech.a

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

tests/test_display_timing: tests/test_display_timing.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

tests/test_color_science: tests/test_color_science.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

tests/test_image_process: tests/test_image_process.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

tests/test_gamma: tests/test_gamma.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

# ---------------------------------------------------------------------------
# Examples (end-to-end, >30 lines with main())

examples/example_vesa_timing: examples/example_vesa_timing.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

examples/example_color_gamut: examples/example_color_gamut.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

examples/example_dithering: examples/example_dithering.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) $< -L. -ldisplay_tech $(LDFLAGS) -o $@

examples: $(EXAMPLE_BINS)

# ---------------------------------------------------------------------------
# Clean

clean:
	rm -f $(OBJS) $(LIB)
	rm -f $(TEST_BINS)
	rm -f $(EXAMPLE_BINS)
	rm -f tests/*.o examples/*.o

