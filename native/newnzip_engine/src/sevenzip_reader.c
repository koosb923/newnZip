#include "sevenzip_reader.h"

#ifdef NEWNZIP_HAS_LZMA
#include <lzma.h>
#endif

static const unsigned char SEVENZIP_SIGNATURE[6] = {0x37, 0x7a, 0xbc, 0xaf, 0x27, 0x1c};

enum {
    SEVENZIP_K_END = 0x00,
    SEVENZIP_K_HEADER = 0x01,
    SEVENZIP_K_ARCHIVE_PROPERTIES = 0x02,
    SEVENZIP_K_ADDITIONAL_STREAMS_INFO = 0x03,
    SEVENZIP_K_MAIN_STREAMS_INFO = 0x04,
    SEVENZIP_K_FILES_INFO = 0x05,
    SEVENZIP_K_PACK_INFO = 0x06,
    SEVENZIP_K_UNPACK_INFO = 0x07,
    SEVENZIP_K_SUBSTREAMS_INFO = 0x08,
    SEVENZIP_K_SIZE = 0x09,
    SEVENZIP_K_CRC = 0x0a,
    SEVENZIP_K_FOLDER = 0x0b,
    SEVENZIP_K_CODERS_UNPACK_SIZE = 0x0c,
    SEVENZIP_K_NUM_UNPACK_STREAM = 0x0d,
    SEVENZIP_K_EMPTY_STREAM = 0x0e,
    SEVENZIP_K_EMPTY_FILE = 0x0f,
    SEVENZIP_K_ANTI = 0x10,
    SEVENZIP_K_NAME = 0x11,
    SEVENZIP_K_ENCODED_HEADER = 0x17
};

typedef struct {
    const unsigned char *data;
    size_t size;
    size_t offset;
} SevenZipBuffer;

typedef struct {
    uint64_t num_folders;
    uint64_t *folder_output_counts;
} SevenZipStreamsInfo;

typedef enum {
    SEVENZIP_CODER_COPY = 0,
    SEVENZIP_CODER_LZMA1 = 1,
    SEVENZIP_CODER_LZMA2 = 2
} SevenZipCoderMethod;

typedef struct {
    uint64_t pack_pos;
    uint64_t pack_size;
    uint64_t unpack_size;
    bool has_pack_crc;
    uint32_t pack_crc;
    bool has_unpack_crc;
    uint32_t unpack_crc;
    SevenZipCoderMethod coder_method;
    unsigned char coder_properties[16];
    size_t coder_properties_size;
} SevenZipEncodedHeaderInfo;

typedef struct {
    char *name;
    uint64_t size;
    bool is_directory;
    bool has_crc;
    uint32_t crc;
} SevenZipFileEntryInfo;

typedef struct {
    size_t count;
    uint64_t *sizes;
    bool *has_crc;
    uint32_t *crcs;
} SevenZipSubstreamInfo;

typedef struct {
    SevenZipEncodedHeaderInfo data_stream;
    SevenZipFileEntryInfo *files;
    size_t file_count;
} SevenZipArchiveInfo;

static SevenZipCoderMethod sevenzip_detect_coder_method(const unsigned char *codec_id, size_t codec_id_size);

static uint64_t sevenzip_read_u64(const unsigned char *bytes) {
    return (uint64_t) bytes[0] |
           ((uint64_t) bytes[1] << 8) |
           ((uint64_t) bytes[2] << 16) |
           ((uint64_t) bytes[3] << 24) |
           ((uint64_t) bytes[4] << 32) |
           ((uint64_t) bytes[5] << 40) |
           ((uint64_t) bytes[6] << 48) |
           ((uint64_t) bytes[7] << 56);
}

static void sevenzip_buffer_require(const SevenZipBuffer *buffer, size_t needed, const char *message) {
    if (!buffer || buffer->offset + needed > buffer->size) {
        fail(message);
    }
}

static uint8_t sevenzip_buffer_read_byte(SevenZipBuffer *buffer) {
    sevenzip_buffer_require(buffer, 1, "7z 헤더를 끝까지 읽지 못했습니다");
    return buffer->data[buffer->offset++];
}

static void sevenzip_buffer_skip(SevenZipBuffer *buffer, size_t amount, const char *message) {
    sevenzip_buffer_require(buffer, amount, message);
    buffer->offset += amount;
}

static uint64_t sevenzip_buffer_read_number(SevenZipBuffer *buffer) {
    uint8_t first = sevenzip_buffer_read_byte(buffer);
    uint8_t mask = 0x80u;
    uint64_t value = 0;

    for (int index = 0; index < 8; index++) {
        if ((first & mask) == 0) {
            value |= (uint64_t) (first & (mask - 1u)) << (8 * index);
            return value;
        }
        value |= (uint64_t) sevenzip_buffer_read_byte(buffer) << (8 * index);
        mask >>= 1;
    }

    return value;
}

static void sevenzip_skip_archive_properties(SevenZipBuffer *buffer) {
    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        uint64_t property_size = sevenzip_buffer_read_number(buffer);
        if (property_size > SIZE_MAX) {
            fail("7z archive property 크기가 너무 큽니다");
        }
        sevenzip_buffer_skip(buffer, (size_t) property_size, "7z archive property를 건너뛰지 못했습니다");
    }
}

static uint64_t sevenzip_count_defined_bits(const unsigned char *bytes, uint64_t count) {
    uint64_t defined = 0;
    for (uint64_t index = 0; index < count; index++) {
        if ((bytes[index / 8u] & (1u << (7u - (index % 8u)))) != 0) {
            defined++;
        }
    }
    return defined;
}

static void sevenzip_skip_digest_block(SevenZipBuffer *buffer, uint64_t count) {
    uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
    uint64_t defined_count = count;

    if (all_defined == 0) {
        uint64_t bitset_bytes = (count + 7u) / 8u;
        const unsigned char *flags;
        if (bitset_bytes > SIZE_MAX) {
            fail("7z digest bitset 크기가 너무 큽니다");
        }
        sevenzip_buffer_require(buffer, (size_t) bitset_bytes, "7z digest bitset을 읽지 못했습니다");
        flags = buffer->data + buffer->offset;
        defined_count = sevenzip_count_defined_bits(flags, count);
        buffer->offset += (size_t) bitset_bytes;
    }

    if (defined_count > SIZE_MAX / 4u) {
        fail("7z digest 개수가 너무 많습니다");
    }
    sevenzip_buffer_skip(buffer, (size_t) defined_count * 4u, "7z digest를 읽지 못했습니다");
}

static uint64_t sevenzip_skip_folder(SevenZipBuffer *buffer) {
    uint64_t num_coders = sevenzip_buffer_read_number(buffer);
    uint64_t total_input_streams = 0;
    uint64_t total_output_streams = 0;

    for (uint64_t coder_index = 0; coder_index < num_coders; coder_index++) {
        uint8_t flags = sevenzip_buffer_read_byte(buffer);
        size_t codec_id_size = (size_t) (flags & 0x0fu);
        uint64_t input_streams = 1;
        uint64_t output_streams = 1;

        if (codec_id_size == 0) {
            fail("7z coder id 크기가 올바르지 않습니다");
        }
        sevenzip_buffer_skip(buffer, codec_id_size, "7z coder id를 읽지 못했습니다");

        if ((flags & 0x10u) != 0) {
            input_streams = sevenzip_buffer_read_number(buffer);
            output_streams = sevenzip_buffer_read_number(buffer);
        }
        if ((flags & 0x20u) != 0) {
            uint64_t properties_size = sevenzip_buffer_read_number(buffer);
            if (properties_size > SIZE_MAX) {
                fail("7z coder property 크기가 너무 큽니다");
            }
            sevenzip_buffer_skip(buffer, (size_t) properties_size, "7z coder property를 읽지 못했습니다");
        }
        if ((flags & 0x80u) != 0) {
            fail("대체 coder method가 있는 7z 폴더는 아직 지원하지 않습니다");
        }

        total_input_streams += input_streams;
        total_output_streams += output_streams;
    }

    if (total_output_streams == 0) {
        fail("7z folder output stream 수가 올바르지 않습니다");
    }

    uint64_t num_bind_pairs = total_output_streams - 1u;
    for (uint64_t bind_index = 0; bind_index < num_bind_pairs; bind_index++) {
        (void) sevenzip_buffer_read_number(buffer);
        (void) sevenzip_buffer_read_number(buffer);
    }

    if (total_input_streams < num_bind_pairs) {
        fail("7z folder bind pair 정보가 올바르지 않습니다");
    }

    uint64_t num_packed_streams = total_input_streams - num_bind_pairs;
    if (num_packed_streams > 1u) {
        for (uint64_t packed_index = 0; packed_index < num_packed_streams; packed_index++) {
            (void) sevenzip_buffer_read_number(buffer);
        }
    }

    return total_output_streams;
}

static void sevenzip_skip_pack_info(SevenZipBuffer *buffer) {
    uint64_t num_pack_streams;

    (void) sevenzip_buffer_read_number(buffer);
    num_pack_streams = sevenzip_buffer_read_number(buffer);

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_SIZE) {
            for (uint64_t index = 0; index < num_pack_streams; index++) {
                (void) sevenzip_buffer_read_number(buffer);
            }
        } else if (property == SEVENZIP_K_CRC) {
            sevenzip_skip_digest_block(buffer, num_pack_streams);
        } else {
            fail("알 수 없는 7z PackInfo property입니다");
        }
    }
}

static SevenZipStreamsInfo sevenzip_skip_unpack_info(SevenZipBuffer *buffer) {
    SevenZipStreamsInfo info = {0};

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return info;
        }
        if (property == SEVENZIP_K_FOLDER) {
            info.num_folders = sevenzip_buffer_read_number(buffer);
            uint8_t external = sevenzip_buffer_read_byte(buffer);
            if (external != 0) {
                fail("외부 folder stream을 사용하는 7z는 아직 지원하지 않습니다");
            }
            if (info.num_folders > SIZE_MAX / sizeof(uint64_t)) {
                fail("7z folder 수가 너무 많습니다");
            }
            info.folder_output_counts = calloc((size_t) info.num_folders, sizeof(uint64_t));
            if (!info.folder_output_counts) {
                fail("메모리가 부족합니다");
            }
            for (uint64_t index = 0; index < info.num_folders; index++) {
                info.folder_output_counts[index] = sevenzip_skip_folder(buffer);
            }
        } else if (property == SEVENZIP_K_CODERS_UNPACK_SIZE) {
            for (uint64_t folder_index = 0; folder_index < info.num_folders; folder_index++) {
                uint64_t output_count = info.folder_output_counts ? info.folder_output_counts[folder_index] : 0;
                for (uint64_t stream_index = 0; stream_index < output_count; stream_index++) {
                    (void) sevenzip_buffer_read_number(buffer);
                }
            }
        } else if (property == SEVENZIP_K_CRC) {
            sevenzip_skip_digest_block(buffer, info.num_folders);
        } else {
            fail("알 수 없는 7z UnPackInfo property입니다");
        }
    }
}

static void sevenzip_skip_substreams_info(SevenZipBuffer *buffer, const SevenZipStreamsInfo *info) {
    uint64_t num_substreams = info ? info->num_folders : 0;

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_NUM_UNPACK_STREAM) {
            num_substreams = 0;
            for (uint64_t folder_index = 0; folder_index < (info ? info->num_folders : 0); folder_index++) {
                num_substreams += sevenzip_buffer_read_number(buffer);
            }
        } else if (property == SEVENZIP_K_SIZE) {
            if (num_substreams == 0) {
                fail("7z SubStreamsInfo size 정보가 올바르지 않습니다");
            }
            for (uint64_t index = 0; index + 1u < num_substreams; index++) {
                (void) sevenzip_buffer_read_number(buffer);
            }
        } else if (property == SEVENZIP_K_CRC) {
            sevenzip_skip_digest_block(buffer, num_substreams);
        } else {
            fail("알 수 없는 7z SubStreamsInfo property입니다");
        }
    }
}

static void sevenzip_skip_streams_info(SevenZipBuffer *buffer) {
    SevenZipStreamsInfo info = {0};

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            free(info.folder_output_counts);
            return;
        }
        if (property == SEVENZIP_K_PACK_INFO) {
            sevenzip_skip_pack_info(buffer);
        } else if (property == SEVENZIP_K_UNPACK_INFO) {
            free(info.folder_output_counts);
            info = sevenzip_skip_unpack_info(buffer);
        } else if (property == SEVENZIP_K_SUBSTREAMS_INFO) {
            sevenzip_skip_substreams_info(buffer, &info);
        } else {
            free(info.folder_output_counts);
            fail("알 수 없는 7z StreamsInfo property입니다");
        }
    }
}

static char *sevenzip_decode_utf16le_name(const unsigned char *bytes, size_t length) {
    if ((length % 2u) != 0) {
        fail("7z 파일 이름 UTF-16 길이가 올바르지 않습니다");
    }

    char *result = calloc(length * 2u + 1u, sizeof(char));
    if (!result) {
        fail("메모리가 부족합니다");
    }

    size_t out = 0;
    for (size_t index = 0; index + 1u < length; index += 2u) {
        uint16_t codepoint = (uint16_t) bytes[index] | ((uint16_t) bytes[index + 1u] << 8);
        if (codepoint == 0) {
            break;
        }
        if (codepoint <= 0x7fu) {
            result[out++] = (char) codepoint;
        } else if (codepoint <= 0x7ffu) {
            result[out++] = (char) (0xc0u | ((codepoint >> 6) & 0x1fu));
            result[out++] = (char) (0x80u | (codepoint & 0x3fu));
        } else {
            result[out++] = (char) (0xe0u | ((codepoint >> 12) & 0x0fu));
            result[out++] = (char) (0x80u | ((codepoint >> 6) & 0x3fu));
            result[out++] = (char) (0x80u | (codepoint & 0x3fu));
        }
    }
    result[out] = '\0';
    return result;
}

static void sevenzip_print_name_list(const unsigned char *bytes, size_t length, uint64_t num_files) {
    SevenZipBuffer names = {.data = bytes, .size = length, .offset = 0};

    for (uint64_t index = 0; index < num_files; index++) {
        size_t start = names.offset;
        while (true) {
            sevenzip_buffer_require(&names, 2, "7z 파일 이름을 끝까지 읽지 못했습니다");
            if (names.data[names.offset] == 0 && names.data[names.offset + 1u] == 0) {
                size_t raw_length = names.offset - start;
                char *decoded = sevenzip_decode_utf16le_name(names.data + start, raw_length);
                printf("  %s\n", decoded);
                free(decoded);
                names.offset += 2u;
                break;
            }
            names.offset += 2u;
        }
    }
}

static bool sevenzip_read_bit(const unsigned char *bytes, size_t bit_index) {
    return (bytes[bit_index / 8u] & (1u << (7u - (bit_index % 8u)))) != 0;
}

static void sevenzip_free_substream_info(SevenZipSubstreamInfo *info) {
    if (!info) {
        return;
    }
    free(info->sizes);
    free(info->has_crc);
    free(info->crcs);
    memset(info, 0, sizeof(*info));
}

static void sevenzip_free_archive_info(SevenZipArchiveInfo *info) {
    if (!info) {
        return;
    }
    for (size_t index = 0; index < info->file_count; index++) {
        free(info->files[index].name);
    }
    free(info->files);
    memset(info, 0, sizeof(*info));
}

static void sevenzip_parse_files_info(SevenZipBuffer *buffer) {
    uint64_t num_files = sevenzip_buffer_read_number(buffer);
    bool printed_files_header = false;

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }

        uint64_t property_size = sevenzip_buffer_read_number(buffer);
        if (property_size > SIZE_MAX) {
            fail("7z FilesInfo property 크기가 너무 큽니다");
        }
        sevenzip_buffer_require(buffer, (size_t) property_size, "7z FilesInfo property를 읽지 못했습니다");

        if (property == SEVENZIP_K_NAME) {
            SevenZipBuffer property_buffer = {.data = buffer->data + buffer->offset, .size = (size_t) property_size, .offset = 0};
            uint8_t external = sevenzip_buffer_read_byte(&property_buffer);
            if (external != 0) {
                fail("외부 이름 속성을 사용하는 7z는 아직 지원하지 않습니다");
            }
            if (!printed_files_header) {
                printf("  files:\n");
                printed_files_header = true;
            }
            sevenzip_print_name_list(property_buffer.data + property_buffer.offset,
                                     property_buffer.size - property_buffer.offset,
                                     num_files);
        }

        buffer->offset += (size_t) property_size;
    }
}

static void sevenzip_parse_main_pack_info(SevenZipBuffer *buffer, SevenZipEncodedHeaderInfo *stream) {
    stream->pack_pos = sevenzip_buffer_read_number(buffer);
    uint64_t num_pack_streams = sevenzip_buffer_read_number(buffer);

    if (num_pack_streams != 1u) {
        fail("pack stream이 여러 개인 7z는 아직 네이티브 해제를 지원하지 않습니다");
    }

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_SIZE) {
            stream->pack_size = sevenzip_buffer_read_number(buffer);
        } else if (property == SEVENZIP_K_CRC) {
            uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
            if (all_defined == 0) {
                fail("선택적 CRC bitset이 있는 7z pack stream은 아직 지원하지 않습니다");
            }
            stream->has_pack_crc = true;
            sevenzip_buffer_require(buffer, 4, "7z pack stream CRC를 읽지 못했습니다");
            stream->pack_crc = read_u32(buffer->data + buffer->offset);
            buffer->offset += 4;
        } else {
            fail("알 수 없는 7z PackInfo property입니다");
        }
    }
}

static void sevenzip_parse_main_unpack_info(SevenZipBuffer *buffer, SevenZipEncodedHeaderInfo *stream) {
    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_FOLDER) {
            uint64_t num_folders = sevenzip_buffer_read_number(buffer);
            uint8_t external = sevenzip_buffer_read_byte(buffer);
            if (num_folders != 1u) {
                fail("folder가 여러 개인 7z는 아직 네이티브 해제를 지원하지 않습니다");
            }
            if (external != 0) {
                fail("외부 folder stream을 사용하는 7z는 아직 지원하지 않습니다");
            }

            uint64_t num_coders = sevenzip_buffer_read_number(buffer);
            if (num_coders != 1u) {
                fail("coder가 여러 개인 7z는 아직 네이티브 해제를 지원하지 않습니다");
            }

            uint8_t flags = sevenzip_buffer_read_byte(buffer);
            size_t codec_id_size = (size_t) (flags & 0x0fu);
            unsigned char codec_id[15];
            if ((flags & 0x10u) != 0 || (flags & 0x80u) != 0) {
                fail("복합 coder를 사용하는 7z는 아직 네이티브 해제를 지원하지 않습니다");
            }
            if (codec_id_size == 0 || codec_id_size > sizeof(codec_id)) {
                fail("7z coder id 크기가 올바르지 않습니다");
            }
            sevenzip_buffer_require(buffer, codec_id_size, "7z coder id를 읽지 못했습니다");
            memcpy(codec_id, buffer->data + buffer->offset, codec_id_size);
            buffer->offset += codec_id_size;
            stream->coder_method = sevenzip_detect_coder_method(codec_id, codec_id_size);

            stream->coder_properties_size = 0;
            if ((flags & 0x20u) != 0) {
                uint64_t properties_size = sevenzip_buffer_read_number(buffer);
                if (properties_size > sizeof(stream->coder_properties)) {
                    fail("7z coder property가 너무 큽니다");
                }
                sevenzip_buffer_require(buffer, (size_t) properties_size, "7z coder property를 읽지 못했습니다");
                memcpy(stream->coder_properties, buffer->data + buffer->offset, (size_t) properties_size);
                stream->coder_properties_size = (size_t) properties_size;
                buffer->offset += (size_t) properties_size;
            }
        } else if (property == SEVENZIP_K_CODERS_UNPACK_SIZE) {
            stream->unpack_size = sevenzip_buffer_read_number(buffer);
        } else if (property == SEVENZIP_K_CRC) {
            uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
            if (all_defined == 0) {
                fail("선택적 unpack CRC bitset이 있는 7z는 아직 지원하지 않습니다");
            }
            stream->has_unpack_crc = true;
            sevenzip_buffer_require(buffer, 4, "7z unpack CRC를 읽지 못했습니다");
            stream->unpack_crc = read_u32(buffer->data + buffer->offset);
            buffer->offset += 4;
        } else {
            fail("알 수 없는 7z UnPackInfo property입니다");
        }
    }
}

static void sevenzip_parse_main_substreams_info(SevenZipBuffer *buffer,
                                                const SevenZipEncodedHeaderInfo *stream,
                                                SevenZipSubstreamInfo *substreams) {
    uint64_t num_substreams = 1;
    uint64_t remaining_size = stream->unpack_size;

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            break;
        }
        if (property == SEVENZIP_K_NUM_UNPACK_STREAM) {
            num_substreams = sevenzip_buffer_read_number(buffer);
        } else if (property == SEVENZIP_K_SIZE) {
            if (num_substreams == 0 || num_substreams > SIZE_MAX / sizeof(uint64_t)) {
                fail("7z substream 개수가 올바르지 않습니다");
            }
            substreams->count = (size_t) num_substreams;
            substreams->sizes = calloc(substreams->count, sizeof(uint64_t));
            if (!substreams->sizes) {
                fail("메모리가 부족합니다");
            }
            for (size_t index = 0; index + 1u < substreams->count; index++) {
                uint64_t size = sevenzip_buffer_read_number(buffer);
                if (size > remaining_size) {
                    fail("7z substream 크기 정보가 올바르지 않습니다");
                }
                substreams->sizes[index] = size;
                remaining_size -= size;
            }
            substreams->sizes[substreams->count - 1u] = remaining_size;
        } else if (property == SEVENZIP_K_CRC) {
            uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
            if (num_substreams == 0 || num_substreams > SIZE_MAX) {
                fail("7z substream CRC 개수가 올바르지 않습니다");
            }
            substreams->count = (size_t) num_substreams;
            if (!substreams->sizes) {
                substreams->sizes = calloc(substreams->count, sizeof(uint64_t));
                if (!substreams->sizes) {
                    fail("메모리가 부족합니다");
                }
                if (substreams->count == 1u) {
                    substreams->sizes[0] = stream->unpack_size;
                }
            }
            substreams->has_crc = calloc(substreams->count, sizeof(bool));
            substreams->crcs = calloc(substreams->count, sizeof(uint32_t));
            if (!substreams->has_crc || !substreams->crcs) {
                fail("메모리가 부족합니다");
            }
            if (all_defined != 0) {
                for (size_t index = 0; index < substreams->count; index++) {
                    sevenzip_buffer_require(buffer, 4, "7z substream CRC를 읽지 못했습니다");
                    substreams->has_crc[index] = true;
                    substreams->crcs[index] = read_u32(buffer->data + buffer->offset);
                    buffer->offset += 4;
                }
            } else {
                uint64_t bitset_bytes = (substreams->count + 7u) / 8u;
                const unsigned char *flags;
                if (bitset_bytes > SIZE_MAX) {
                    fail("7z substream CRC bitset 크기가 너무 큽니다");
                }
                sevenzip_buffer_require(buffer, (size_t) bitset_bytes, "7z substream CRC bitset을 읽지 못했습니다");
                flags = buffer->data + buffer->offset;
                buffer->offset += (size_t) bitset_bytes;
                for (size_t index = 0; index < substreams->count; index++) {
                    if (sevenzip_read_bit(flags, index)) {
                        sevenzip_buffer_require(buffer, 4, "7z substream CRC를 읽지 못했습니다");
                        substreams->has_crc[index] = true;
                        substreams->crcs[index] = read_u32(buffer->data + buffer->offset);
                        buffer->offset += 4;
                    }
                }
            }
        } else {
            fail("알 수 없는 7z SubStreamsInfo property입니다");
        }
    }

    if (substreams->count == 0) {
        substreams->count = 1;
        substreams->sizes = calloc(1, sizeof(uint64_t));
        if (!substreams->sizes) {
            fail("메모리가 부족합니다");
        }
        substreams->sizes[0] = stream->unpack_size;
        if (stream->has_unpack_crc) {
            substreams->has_crc = calloc(1, sizeof(bool));
            substreams->crcs = calloc(1, sizeof(uint32_t));
            if (!substreams->has_crc || !substreams->crcs) {
                fail("메모리가 부족합니다");
            }
            substreams->has_crc[0] = true;
            substreams->crcs[0] = stream->unpack_crc;
        }
    }
}

static void sevenzip_parse_plain_header_metadata(const unsigned char *bytes,
                                                 size_t length,
                                                 SevenZipArchiveInfo *archive) {
    SevenZipBuffer buffer = {.data = bytes, .size = length, .offset = 0};
    SevenZipSubstreamInfo substreams = {0};
    bool *empty_stream_flags = NULL;
    bool *empty_file_flags = NULL;
    size_t empty_stream_count = 0;
    uint8_t marker = sevenzip_buffer_read_byte(&buffer);

    if (marker != SEVENZIP_K_HEADER) {
        fail("7z next header 시작 마커가 올바르지 않습니다");
    }

    while (buffer.offset < buffer.size) {
        uint8_t property = sevenzip_buffer_read_byte(&buffer);
        if (property == SEVENZIP_K_END) {
            break;
        }
        if (property == SEVENZIP_K_ARCHIVE_PROPERTIES) {
            sevenzip_skip_archive_properties(&buffer);
        } else if (property == SEVENZIP_K_ADDITIONAL_STREAMS_INFO) {
            fail("AdditionalStreamsInfo가 있는 7z는 아직 네이티브 해제를 지원하지 않습니다");
        } else if (property == SEVENZIP_K_MAIN_STREAMS_INFO) {
            while (true) {
                uint8_t stream_property = sevenzip_buffer_read_byte(&buffer);
                if (stream_property == SEVENZIP_K_END) {
                    break;
                }
                if (stream_property == SEVENZIP_K_PACK_INFO) {
                    sevenzip_parse_main_pack_info(&buffer, &archive->data_stream);
                } else if (stream_property == SEVENZIP_K_UNPACK_INFO) {
                    sevenzip_parse_main_unpack_info(&buffer, &archive->data_stream);
                } else if (stream_property == SEVENZIP_K_SUBSTREAMS_INFO) {
                    sevenzip_parse_main_substreams_info(&buffer, &archive->data_stream, &substreams);
                } else {
                    fail("알 수 없는 7z MainStreamsInfo property입니다");
                }
            }
        } else if (property == SEVENZIP_K_FILES_INFO) {
            uint64_t num_files = sevenzip_buffer_read_number(&buffer);
            if (num_files > SIZE_MAX / sizeof(SevenZipFileEntryInfo)) {
                fail("7z 파일 수가 너무 많습니다");
            }
            archive->file_count = (size_t) num_files;
            archive->files = calloc(archive->file_count, sizeof(SevenZipFileEntryInfo));
            if (!archive->files) {
                fail("메모리가 부족합니다");
            }

            while (true) {
                uint8_t file_property = sevenzip_buffer_read_byte(&buffer);
                if (file_property == SEVENZIP_K_END) {
                    break;
                }
                uint64_t property_size = sevenzip_buffer_read_number(&buffer);
                if (property_size > SIZE_MAX) {
                    fail("7z FilesInfo property 크기가 너무 큽니다");
                }
                sevenzip_buffer_require(&buffer, (size_t) property_size, "7z FilesInfo property를 읽지 못했습니다");

                if (file_property == SEVENZIP_K_NAME) {
                    SevenZipBuffer property_buffer = {.data = buffer.data + buffer.offset, .size = (size_t) property_size, .offset = 0};
                    uint8_t external = sevenzip_buffer_read_byte(&property_buffer);
                    if (external != 0) {
                        fail("외부 이름 속성을 사용하는 7z는 아직 지원하지 않습니다");
                    }
                    for (size_t index = 0; index < archive->file_count; index++) {
                        size_t start = property_buffer.offset;
                        while (true) {
                            sevenzip_buffer_require(&property_buffer, 2, "7z 파일 이름을 끝까지 읽지 못했습니다");
                            if (property_buffer.data[property_buffer.offset] == 0 &&
                                property_buffer.data[property_buffer.offset + 1u] == 0) {
                                size_t raw_length = property_buffer.offset - start;
                                archive->files[index].name = sevenzip_decode_utf16le_name(property_buffer.data + start, raw_length);
                                property_buffer.offset += 2u;
                                break;
                            }
                            property_buffer.offset += 2u;
                        }
                    }
                } else if (file_property == SEVENZIP_K_EMPTY_STREAM) {
                    size_t bytes_needed = (archive->file_count + 7u) / 8u;
                    if ((size_t) property_size != bytes_needed) {
                        fail("7z EmptyStream bitset 크기가 올바르지 않습니다");
                    }
                    empty_stream_flags = calloc(archive->file_count, sizeof(bool));
                    if (!empty_stream_flags) {
                        fail("메모리가 부족합니다");
                    }
                    for (size_t index = 0; index < archive->file_count; index++) {
                        empty_stream_flags[index] = sevenzip_read_bit(buffer.data + buffer.offset, index);
                        if (empty_stream_flags[index]) {
                            empty_stream_count++;
                        }
                    }
                } else if (file_property == SEVENZIP_K_EMPTY_FILE) {
                    size_t bytes_needed = (empty_stream_count + 7u) / 8u;
                    if ((size_t) property_size != bytes_needed) {
                        fail("7z EmptyFile bitset 크기가 올바르지 않습니다");
                    }
                    if (empty_stream_count > 0) {
                        empty_file_flags = calloc(empty_stream_count, sizeof(bool));
                        if (!empty_file_flags) {
                            fail("메모리가 부족합니다");
                        }
                        for (size_t index = 0; index < empty_stream_count; index++) {
                            empty_file_flags[index] = sevenzip_read_bit(buffer.data + buffer.offset, index);
                        }
                    }
                } else if (file_property == SEVENZIP_K_ANTI) {
                    fail("anti item이 있는 7z는 아직 네이티브 해제를 지원하지 않습니다");
                }

                buffer.offset += (size_t) property_size;
            }
        } else {
            fail("알 수 없는 7z header property입니다");
        }
    }

    if (archive->file_count == 0 || !archive->files) {
        fail("7z 파일 목록을 읽지 못했습니다");
    }

    size_t non_empty_index = 0;
    size_t empty_index = 0;
    for (size_t index = 0; index < archive->file_count; index++) {
        bool is_empty_stream = empty_stream_flags ? empty_stream_flags[index] : false;
        if (!archive->files[index].name) {
            archive->files[index].name = duplicate_string("unnamed");
        }
        if (is_empty_stream) {
            bool is_empty_file = empty_file_flags ? empty_file_flags[empty_index] : false;
            archive->files[index].size = 0;
            archive->files[index].is_directory = !is_empty_file;
            empty_index++;
        } else {
            if (non_empty_index >= substreams.count) {
                fail("7z file stream 개수가 헤더와 맞지 않습니다");
            }
            archive->files[index].size = substreams.sizes[non_empty_index];
            archive->files[index].has_crc = substreams.has_crc ? substreams.has_crc[non_empty_index] : false;
            archive->files[index].crc = substreams.crcs ? substreams.crcs[non_empty_index] : 0;
            archive->files[index].is_directory = false;
            non_empty_index++;
        }
    }
    if (non_empty_index != substreams.count) {
        fail("7z substream 개수와 파일 개수가 맞지 않습니다");
    }

    free(empty_stream_flags);
    free(empty_file_flags);
    sevenzip_free_substream_info(&substreams);
}

static SevenZipCoderMethod sevenzip_detect_coder_method(const unsigned char *codec_id, size_t codec_id_size) {
    if (codec_id_size == 1u && codec_id[0] == 0x00u) {
        return SEVENZIP_CODER_COPY;
    }
    if (codec_id_size == 1u && codec_id[0] == 0x21u) {
        return SEVENZIP_CODER_LZMA2;
    }
    if (codec_id_size == 3u && codec_id[0] == 0x03u && codec_id[1] == 0x01u && codec_id[2] == 0x01u) {
        return SEVENZIP_CODER_LZMA1;
    }
    fail("지원하지 않는 7z encoded header coder입니다");
    return SEVENZIP_CODER_COPY;
}

static void sevenzip_parse_encoded_header_pack_info(SevenZipBuffer *buffer, SevenZipEncodedHeaderInfo *info) {
    info->pack_pos = sevenzip_buffer_read_number(buffer);
    uint64_t num_pack_streams = sevenzip_buffer_read_number(buffer);

    if (num_pack_streams != 1u) {
        fail("pack stream이 여러 개인 7z encoded header는 아직 지원하지 않습니다");
    }

    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_SIZE) {
            info->pack_size = sevenzip_buffer_read_number(buffer);
        } else if (property == SEVENZIP_K_CRC) {
            uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
            if (all_defined == 0) {
                fail("선택적 CRC bitset이 있는 7z encoded header는 아직 지원하지 않습니다");
            }
            info->has_pack_crc = true;
            sevenzip_buffer_require(buffer, 4, "7z encoded header pack CRC를 읽지 못했습니다");
            info->pack_crc = read_u32(buffer->data + buffer->offset);
            buffer->offset += 4;
        } else {
            fail("알 수 없는 7z encoded header PackInfo property입니다");
        }
    }
}

static void sevenzip_parse_encoded_header_unpack_info(SevenZipBuffer *buffer, SevenZipEncodedHeaderInfo *info) {
    while (true) {
        uint8_t property = sevenzip_buffer_read_byte(buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_FOLDER) {
            uint64_t num_folders = sevenzip_buffer_read_number(buffer);
            uint8_t external = sevenzip_buffer_read_byte(buffer);
            if (num_folders != 1u) {
                fail("folder가 여러 개인 7z encoded header는 아직 지원하지 않습니다");
            }
            if (external != 0) {
                fail("외부 folder stream을 사용하는 7z encoded header는 아직 지원하지 않습니다");
            }

            uint64_t num_coders = sevenzip_buffer_read_number(buffer);
            if (num_coders != 1u) {
                fail("coder가 여러 개인 7z encoded header는 아직 지원하지 않습니다");
            }

            uint8_t flags = sevenzip_buffer_read_byte(buffer);
            size_t codec_id_size = (size_t) (flags & 0x0fu);
            unsigned char codec_id[15];
            if ((flags & 0x10u) != 0 || (flags & 0x80u) != 0) {
                fail("복합 7z encoded header coder는 아직 지원하지 않습니다");
            }
            if (codec_id_size == 0 || codec_id_size > sizeof(codec_id)) {
                fail("7z encoded header coder id 크기가 올바르지 않습니다");
            }
            sevenzip_buffer_require(buffer, codec_id_size, "7z encoded header coder id를 읽지 못했습니다");
            memcpy(codec_id, buffer->data + buffer->offset, codec_id_size);
            buffer->offset += codec_id_size;
            info->coder_method = sevenzip_detect_coder_method(codec_id, codec_id_size);

            if ((flags & 0x20u) != 0) {
                uint64_t properties_size = sevenzip_buffer_read_number(buffer);
                if (properties_size > sizeof(info->coder_properties)) {
                    fail("7z encoded header coder property가 너무 큽니다");
                }
                sevenzip_buffer_require(buffer, (size_t) properties_size, "7z encoded header coder property를 읽지 못했습니다");
                memcpy(info->coder_properties, buffer->data + buffer->offset, (size_t) properties_size);
                info->coder_properties_size = (size_t) properties_size;
                buffer->offset += (size_t) properties_size;
            }
        } else if (property == SEVENZIP_K_CODERS_UNPACK_SIZE) {
            info->unpack_size = sevenzip_buffer_read_number(buffer);
        } else if (property == SEVENZIP_K_CRC) {
            uint8_t all_defined = sevenzip_buffer_read_byte(buffer);
            if (all_defined == 0) {
                fail("선택적 unpack CRC bitset이 있는 7z encoded header는 아직 지원하지 않습니다");
            }
            info->has_unpack_crc = true;
            sevenzip_buffer_require(buffer, 4, "7z encoded header unpack CRC를 읽지 못했습니다");
            info->unpack_crc = read_u32(buffer->data + buffer->offset);
            buffer->offset += 4;
        } else {
            fail("알 수 없는 7z encoded header UnPackInfo property입니다");
        }
    }
}

static SevenZipEncodedHeaderInfo sevenzip_parse_encoded_header_info(const unsigned char *bytes, size_t length) {
    SevenZipBuffer buffer = {.data = bytes, .size = length, .offset = 0};
    SevenZipEncodedHeaderInfo info = {0};

    if (sevenzip_buffer_read_byte(&buffer) != SEVENZIP_K_ENCODED_HEADER) {
        fail("encoded 7z header 마커가 올바르지 않습니다");
    }

    while (buffer.offset < buffer.size) {
        uint8_t property = sevenzip_buffer_read_byte(&buffer);
        if (property == SEVENZIP_K_END) {
            return info;
        }
        if (property == SEVENZIP_K_PACK_INFO) {
            sevenzip_parse_encoded_header_pack_info(&buffer, &info);
        } else if (property == SEVENZIP_K_UNPACK_INFO) {
            sevenzip_parse_encoded_header_unpack_info(&buffer, &info);
        } else if (property == SEVENZIP_K_SUBSTREAMS_INFO) {
            sevenzip_skip_substreams_info(&buffer, NULL);
        } else {
            fail("알 수 없는 7z encoded header property입니다");
        }
    }

    return info;
}

static unsigned char *sevenzip_decode_lzma_buffer(const SevenZipEncodedHeaderInfo *info,
                                                  const unsigned char *input,
                                                  size_t input_size,
                                                  size_t *output_size) {
#ifdef NEWNZIP_HAS_LZMA
    lzma_filter filters[2];
    lzma_options_lzma *options;
    lzma_ret ret;
    size_t in_pos = 0;
    size_t out_pos = 0;
    unsigned char *output = calloc((size_t) info->unpack_size, 1u);

    if (!output) {
        fail("메모리가 부족합니다");
    }

    memset(filters, 0, sizeof(filters));
    filters[0].id = info->coder_method == SEVENZIP_CODER_LZMA1 ? LZMA_FILTER_LZMA1EXT : LZMA_FILTER_LZMA2;
    filters[1].id = LZMA_VLI_UNKNOWN;

    ret = lzma_properties_decode(&filters[0], NULL, info->coder_properties, info->coder_properties_size);
    if (ret != LZMA_OK) {
        free(output);
        fail("7z encoded header LZMA property를 해석하지 못했습니다");
    }

    options = filters[0].options;
    if (info->coder_method == SEVENZIP_CODER_LZMA1 && options) {
        options->ext_flags = LZMA_LZMA1EXT_ALLOW_EOPM;
        lzma_set_ext_size((*options), info->unpack_size);
    }

    ret = lzma_raw_buffer_decode(filters, NULL, input, &in_pos, input_size, output, &out_pos, (size_t) info->unpack_size);
    free(filters[0].options);
    if (out_pos != (size_t) info->unpack_size) {
        free(output);
        fail("7z encoded header LZMA 데이터를 해제하지 못했습니다");
    }
    if (ret != LZMA_OK && ret != LZMA_DATA_ERROR && ret != LZMA_BUF_ERROR) {
        free(output);
        fail("7z encoded header LZMA 데이터를 해제하지 못했습니다");
    }

    *output_size = out_pos;
    return output;
#else
    (void) info;
    (void) input;
    (void) input_size;
    (void) output_size;
    fail("현재 빌드에는 LZMA 지원이 없어 7z encoded header를 읽을 수 없습니다");
#endif
}

static unsigned char *sevenzip_decode_encoded_header(const char *archive_path,
                                                     const SevenZipEncodedHeaderInfo *info,
                                                     size_t *decoded_size) {
    FILE *input = fopen(archive_path, "rb");
    unsigned char *packed;
    unsigned char *decoded = NULL;
    long absolute_offset;

    if (!input) {
        fail_errno(archive_path);
    }
    if (info->pack_size > SIZE_MAX || info->unpack_size > SIZE_MAX) {
        fclose(input);
        fail("7z encoded header 크기가 너무 큽니다");
    }

    absolute_offset = 32l + (long) info->pack_pos;
    if (fseek(input, absolute_offset, SEEK_SET) != 0) {
        fclose(input);
        fail("7z encoded header pack stream 위치로 이동하지 못했습니다");
    }

    packed = malloc((size_t) info->pack_size);
    if (!packed) {
        fclose(input);
        fail("메모리가 부족합니다");
    }
    if (fread(packed, 1, (size_t) info->pack_size, input) != (size_t) info->pack_size) {
        free(packed);
        fclose(input);
        fail("7z encoded header pack stream을 읽지 못했습니다");
    }
    fclose(input);

    if (info->has_pack_crc && (uint32_t) crc32(0L, packed, (uInt) info->pack_size) != info->pack_crc) {
        free(packed);
        fail("7z encoded header pack CRC가 올바르지 않습니다");
    }

    if (info->coder_method == SEVENZIP_CODER_COPY) {
        decoded = packed;
        *decoded_size = (size_t) info->pack_size;
    } else if (info->coder_method == SEVENZIP_CODER_LZMA1 || info->coder_method == SEVENZIP_CODER_LZMA2) {
        decoded = sevenzip_decode_lzma_buffer(info, packed, (size_t) info->pack_size, decoded_size);
        free(packed);
    } else {
        free(packed);
        fail("지원하지 않는 7z encoded header coder입니다");
    }

    if (info->has_unpack_crc && (uint32_t) crc32(0L, decoded, (uInt) *decoded_size) != info->unpack_crc) {
        free(decoded);
        fail("7z encoded header unpack CRC가 올바르지 않습니다");
    }

    return decoded;
}

static unsigned char *sevenzip_read_next_header(const char *archive_path,
                                                const SevenZipStartHeader *header,
                                                size_t *next_header_size) {
    FILE *input = fopen(archive_path, "rb");
    unsigned char *buffer;
    long absolute_offset;

    if (!input) {
        fail_errno(archive_path);
    }
    if (header->next_header_size > SIZE_MAX) {
        fclose(input);
        fail("7z next header가 너무 큽니다");
    }

    absolute_offset = 32l + (long) header->next_header_offset;
    if (fseek(input, absolute_offset, SEEK_SET) != 0) {
        fclose(input);
        fail("7z next header 위치로 이동하지 못했습니다");
    }

    buffer = malloc((size_t) header->next_header_size);
    if (!buffer) {
        fclose(input);
        fail("메모리가 부족합니다");
    }
    if (fread(buffer, 1, (size_t) header->next_header_size, input) != (size_t) header->next_header_size) {
        free(buffer);
        fclose(input);
        fail("7z next header를 읽지 못했습니다");
    }
    fclose(input);

    if ((uint32_t) crc32(0L, buffer, (uInt) header->next_header_size) != header->next_header_crc) {
        free(buffer);
        fail("7z next header CRC가 올바르지 않습니다");
    }

    *next_header_size = (size_t) header->next_header_size;
    return buffer;
}

static void sevenzip_parse_next_header(const char *archive_path, const unsigned char *bytes, size_t length) {
    SevenZipBuffer buffer = {.data = bytes, .size = length, .offset = 0};
    uint8_t marker = sevenzip_buffer_read_byte(&buffer);

    if (marker == SEVENZIP_K_ENCODED_HEADER) {
        SevenZipEncodedHeaderInfo info = sevenzip_parse_encoded_header_info(bytes, length);
        size_t decoded_size = 0;
        unsigned char *decoded = sevenzip_decode_encoded_header(archive_path, &info, &decoded_size);
        sevenzip_parse_next_header(archive_path, decoded, decoded_size);
        free(decoded);
        return;
    }
    if (marker != SEVENZIP_K_HEADER) {
        fail("7z next header 시작 마커가 올바르지 않습니다");
    }

    while (buffer.offset < buffer.size) {
        uint8_t property = sevenzip_buffer_read_byte(&buffer);
        if (property == SEVENZIP_K_END) {
            return;
        }
        if (property == SEVENZIP_K_ARCHIVE_PROPERTIES) {
            sevenzip_skip_archive_properties(&buffer);
        } else if (property == SEVENZIP_K_ADDITIONAL_STREAMS_INFO || property == SEVENZIP_K_MAIN_STREAMS_INFO) {
            sevenzip_skip_streams_info(&buffer);
        } else if (property == SEVENZIP_K_FILES_INFO) {
            sevenzip_parse_files_info(&buffer);
        } else {
            fail("알 수 없는 7z header property입니다");
        }
    }
}

static void sevenzip_load_archive_info(const char *archive_path, SevenZipArchiveInfo *archive) {
    SevenZipStartHeader header = sevenzip_read_start_header(archive_path);
    size_t next_header_size = 0;
    unsigned char *next_header = sevenzip_read_next_header(archive_path, &header, &next_header_size);
    unsigned char *decoded_header = NULL;
    size_t decoded_size = 0;

    memset(archive, 0, sizeof(*archive));

    if (next_header_size > 0 && next_header[0] == SEVENZIP_K_ENCODED_HEADER) {
        SevenZipEncodedHeaderInfo info = sevenzip_parse_encoded_header_info(next_header, next_header_size);
        decoded_header = sevenzip_decode_encoded_header(archive_path, &info, &decoded_size);
        sevenzip_parse_plain_header_metadata(decoded_header, decoded_size, archive);
        free(decoded_header);
    } else {
        sevenzip_parse_plain_header_metadata(next_header, next_header_size, archive);
    }

    free(next_header);
}

static unsigned char *sevenzip_decode_main_payload(const char *archive_path,
                                                   const SevenZipEncodedHeaderInfo *stream,
                                                   size_t *decoded_size) {
    return sevenzip_decode_encoded_header(archive_path, stream, decoded_size);
}

static char *sevenzip_sanitize_name(const char *name) {
    char *normalized = normalize_archive_name(name);
    if (normalized[0] == '/' || strstr(normalized, "../") || strcmp(normalized, "..") == 0 ||
        strncmp(normalized, "../", 3) == 0) {
        free(normalized);
        fail("상위 경로나 절대 경로가 포함된 7z 항목은 아직 지원하지 않습니다");
    }
    return normalized;
}

static void sevenzip_write_regular_file(const char *path,
                                        const unsigned char *bytes,
                                        size_t size,
                                        const SevenZipFileEntryInfo *entry) {
    FILE *output;
    ensure_parent_directories(path);
    output = fopen(path, "wb");
    if (!output) {
        fail_errno(path);
    }
    if (size > 0 && fwrite(bytes, 1, size, output) != size) {
        fclose(output);
        fail_errno("7z 파일 내용을 쓰지 못했습니다");
    }
    if (fclose(output) != 0) {
        fail_errno("7z 파일을 닫지 못했습니다");
    }
    if (entry->has_crc && (uint32_t) crc32(0L, bytes, (uInt) size) != entry->crc) {
        remove_file_if_exists(path);
        fail("7z 파일 CRC가 올바르지 않습니다");
    }
}

static void sevenzip_materialize_entries(const SevenZipArchiveInfo *archive,
                                         const unsigned char *payload,
                                         size_t payload_size,
                                         const char *destination) {
    size_t payload_offset = 0;

    for (size_t index = 0; index < archive->file_count; index++) {
        const SevenZipFileEntryInfo *entry = &archive->files[index];
        char *safe_name = sevenzip_sanitize_name(entry->name);
        char *output_path = join_path(destination, safe_name);

        if (entry->is_directory) {
            if (mkdir(output_path, 0755) != 0 && errno != EEXIST) {
                free(safe_name);
                free(output_path);
                fail_errno("7z 디렉터리를 만들지 못했습니다");
            }
        } else {
            if (entry->size > payload_size - payload_offset) {
                free(safe_name);
                free(output_path);
                fail("7z payload 크기와 파일 크기가 맞지 않습니다");
            }
            sevenzip_write_regular_file(output_path, payload + payload_offset, (size_t) entry->size, entry);
            payload_offset += (size_t) entry->size;
        }

        free(safe_name);
        free(output_path);
    }

    if (payload_offset != payload_size) {
        fail("7z payload 소비 크기가 전체와 맞지 않습니다");
    }
}

bool sevenzip_has_signature(const char *archive_path) {
    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        return false;
    }
    unsigned char header[6];
    bool matches = fread(header, 1, sizeof(header), input) == sizeof(header) &&
                   memcmp(header, SEVENZIP_SIGNATURE, sizeof(SEVENZIP_SIGNATURE)) == 0;
    fclose(input);
    return matches;
}

SevenZipStartHeader sevenzip_read_start_header(const char *archive_path) {
    FILE *input = fopen(archive_path, "rb");
    if (!input) {
        fail_errno(archive_path);
    }

    unsigned char bytes[32];
    if (fread(bytes, 1, sizeof(bytes), input) != sizeof(bytes)) {
        fclose(input);
        fail("7z 시작 헤더를 읽지 못했습니다");
    }
    fclose(input);

    if (memcmp(bytes, SEVENZIP_SIGNATURE, sizeof(SEVENZIP_SIGNATURE)) != 0) {
        fail("7z 시그니처가 올바르지 않습니다");
    }

    uint32_t expected_crc = read_u32(bytes + 8);
    uint32_t actual_crc = (uint32_t) crc32(0L, bytes + 12, 20u);
    if (expected_crc != actual_crc) {
        fail("7z 시작 헤더 CRC가 올바르지 않습니다");
    }

    SevenZipStartHeader header;
    header.major_version = bytes[6];
    header.minor_version = bytes[7];
    header.next_header_offset = sevenzip_read_u64(bytes + 12);
    header.next_header_size = sevenzip_read_u64(bytes + 20);
    header.next_header_crc = read_u32(bytes + 28);
    return header;
}

void command_list_7z_native(const char *archive_path) {
    SevenZipStartHeader header = sevenzip_read_start_header(archive_path);
    size_t next_header_size = 0;
    unsigned char *next_header = sevenzip_read_next_header(archive_path, &header, &next_header_size);

    printf("7z archive: %s\n", archive_path);
    printf("  version: %u.%u\n", header.major_version, header.minor_version);
    printf("  next_header_offset: %llu\n", (unsigned long long) header.next_header_offset);
    printf("  next_header_size: %llu\n", (unsigned long long) header.next_header_size);
    printf("  next_header_crc: %08x\n", header.next_header_crc);
    sevenzip_parse_next_header(archive_path, next_header, next_header_size);
    printf("  native_reader_stage: files-info-ok\n");

    free(next_header);
}

void command_extract_7z_native(const char *archive_path, const char *destination, const RuntimeOptions *options) {
    SevenZipArchiveInfo archive;
    size_t payload_size = 0;
    unsigned char *payload;

    (void) options;
    sevenzip_load_archive_info(archive_path, &archive);
    payload = sevenzip_decode_main_payload(archive_path, &archive.data_stream, &payload_size);

    if (archive.data_stream.has_unpack_crc &&
        (uint32_t) crc32(0L, payload, (uInt) payload_size) != archive.data_stream.unpack_crc) {
        free(payload);
        sevenzip_free_archive_info(&archive);
        fail("7z 전체 payload CRC가 올바르지 않습니다");
    }

    if (mkdir(destination, 0755) != 0 && errno != EEXIST) {
        free(payload);
        sevenzip_free_archive_info(&archive);
        fail_errno("7z 목적지 디렉터리를 만들지 못했습니다");
    }
    sevenzip_materialize_entries(&archive, payload, payload_size, destination);

    free(payload);
    sevenzip_free_archive_info(&archive);
    printf("해제 완료: %s -> %s (7z native)\n", archive_path, destination);
}
