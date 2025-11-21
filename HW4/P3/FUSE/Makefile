CC = gcc
CFLAGS = -Wall -Wextra -std=c99
LDFLAGS = -lfuse

# Try to get FUSE flags from pkg-config
FUSE_CFLAGS := $(shell pkg-config --cflags fuse3 2>/dev/null || pkg-config --cflags fuse 2>/dev/null || echo "-I/usr/include/fuse")
FUSE_LIBS := $(shell pkg-config --libs fuse3 2>/dev/null || pkg-config --libs fuse 2>/dev/null || echo "-lfuse")

TARGET = main_fs
SOURCES = main_fs.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(FUSE_LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(FUSE_CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

mount: $(TARGET)
	mkdir -p /tmp/myfuse
	./$(TARGET) /tmp/myfuse

unmount:
	fusermount -u /tmp/myfuse || true

.PHONY: all clean mount unmount
