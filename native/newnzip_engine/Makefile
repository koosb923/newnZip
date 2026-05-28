CC := clang
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
LDFLAGS := -lz -lpthread
TARGET := bin/newnzip-engine
SRC := src/main.c src/common.c src/runtime.c src/progress.c src/zip_writer.c src/zip_reader.c src/benchmark.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf bin/newnzip-engine
