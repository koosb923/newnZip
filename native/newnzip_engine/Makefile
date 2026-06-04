CC := clang
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
LDFLAGS := -lz -lpthread
ifeq ($(shell uname -s),Darwin)
LDFLAGS += -framework CoreFoundation
endif
TARGET := bin/newnzip-engine
SRC := src/main.c src/common.c src/runtime.c src/progress.c src/zip_writer.c src/zip_reader.c src/zip_crypto.c src/benchmark.c src/capabilities.c src/archive_adapter.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf bin/newnzip-engine
