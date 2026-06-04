CC := clang
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
LDFLAGS := -lz -lpthread
PKG_CONFIG := pkg-config
ifeq ($(shell uname -s),Darwin)
LDFLAGS += -framework CoreFoundation
endif
ifeq ($(shell $(PKG_CONFIG) --exists bzip2 && echo 1),1)
CFLAGS += -DNEWNZIP_HAS_BZIP2=1 $(shell $(PKG_CONFIG) --cflags bzip2)
LDFLAGS += $(shell $(PKG_CONFIG) --libs bzip2)
endif
ifeq ($(shell $(PKG_CONFIG) --exists liblzma && echo 1),1)
CFLAGS += -DNEWNZIP_HAS_LZMA=1 $(shell $(PKG_CONFIG) --cflags liblzma)
LDFLAGS += $(shell $(PKG_CONFIG) --libs liblzma)
endif
ifeq ($(shell $(PKG_CONFIG) --exists libzstd && echo 1),1)
CFLAGS += -DNEWNZIP_HAS_ZSTD=1 $(shell $(PKG_CONFIG) --cflags libzstd)
LDFLAGS += $(shell $(PKG_CONFIG) --libs libzstd)
endif
TARGET := bin/newnzip-engine
SRC := src/main.c src/common.c src/runtime.c src/progress.c src/zip_writer.c src/zip_reader.c src/zip_crypto.c src/zip_aes.c src/tar_writer.c src/tar_reader.c src/sevenzip_reader.c src/uue_reader.c src/benchmark.c src/capabilities.c src/archive_adapter.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p bin
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf bin/newnzip-engine
