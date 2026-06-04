#include "common.h"
#include "capabilities.h"

typedef struct {
    const char *name;
    bool create;
    bool extract;
    bool list;
    bool native;
    const char *notes;
} FormatCapability;

static const FormatCapability FORMAT_CAPABILITIES[] = {
    {"zip", true, true, true, true, "native deflate/store, split volumes, ZipCrypto password create/extract"},
    {"7z", true, true, false, false, "7zz/7z adapter when backend is available, including password support"},
    {"rar", false, true, false, false, "7zz/7z extract adapter when backend is available"},
    {"tar", true, true, false, false, "bsdtar/libarchive adapter"},
    {"tar.gz", true, true, false, false, "bsdtar/libarchive adapter"},
    {"tar.bz2", true, true, false, false, "bsdtar/libarchive adapter"},
    {"tar.xz", true, true, false, false, "bsdtar/libarchive adapter"},
    {"tar.zstd", true, true, false, false, "bsdtar/libarchive + zstd backend when available"},
    {"zstd", true, true, false, false, "zstd stream adapter when backend is available"},
    {"wim", true, true, false, false, "wimlib-imagex create, bsdtar/libarchive extract adapter"},
    {"sfx", false, false, false, false, "planned Windows SFX builder"},
    {"gz", false, true, false, false, "gzip stream adapter"},
    {"bz2", false, true, false, false, "bzip2 stream adapter"},
    {"xz", false, true, false, false, "xz stream adapter when backend is available"},
    {"lz4", true, true, false, false, "lz4 stream adapter when backend is available"},
    {"lz5", false, false, false, false, "planned stream extract"},
    {"brotli", true, true, false, false, "brotli stream adapter when backend is available"},
    {"alz", false, false, false, false, "planned proprietary adapter"},
    {"egg", false, false, false, false, "planned encryption adapter: ZipCrypto/AES/LEA"},
    {"cab", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"iso", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"arj", false, true, false, false, "7zz/7z extract adapter when backend is available"},
    {"lzh", false, true, false, false, "7zz/7z extract adapter when backend is available"},
    {"lzma", false, false, false, false, "planned stream extract"},
    {"z", false, false, false, false, "planned stream extract"},
    {"cpio", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"rpm", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"deb", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"msi", false, false, false, false, "planned package adapter"},
    {"nsis", false, false, false, false, "planned installer adapter"},
    {"asar", false, false, false, false, "planned package adapter"},
    {"udf", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"img", false, true, false, false, "bsdtar/libarchive extract adapter"},
    {"zpaq", false, true, false, false, "7zz/7z extract adapter when backend is available"},
    {"kz", false, false, false, false, "planned extract adapter"},
    {"lizard", false, false, false, false, "planned stream extract"},
};

void command_capabilities(void) {
    printf("{\n");
    printf("  \"engine\": \"newnzip-engine\",\n");
    printf("  \"version\": 1,\n");
    printf("  \"zip_methods\": [\n");
    printf("    {\"name\":\"store\",\"create\":true,\"extract\":true,\"native\":true},\n");
    printf("    {\"name\":\"deflate\",\"create\":true,\"extract\":true,\"native\":true},\n");
    printf("    {\"name\":\"auto\",\"create\":true,\"extract\":true,\"native\":true},\n");
    printf("    {\"name\":\"deflate64\",\"create\":false,\"extract\":false,\"native\":false},\n");
    printf("    {\"name\":\"lzma\",\"create\":false,\"extract\":false,\"native\":false},\n");
    printf("    {\"name\":\"bzip2\",\"create\":false,\"extract\":false,\"native\":false},\n");
    printf("    {\"name\":\"ppmd\",\"create\":false,\"extract\":false,\"native\":false}\n");
    printf("  ],\n");
    printf("  \"features\": {\n");
    printf("    \"multicore\": true,\n");
    printf("    \"symlinks\": true,\n");
    printf("    \"zip64\": true,\n");
    printf("    \"split_zip_create\": true,\n");
    printf("    \"split_zip_extract\": true,\n");
    printf("    \"solid_compression\": false,\n");
    printf("    \"passwords\": true,\n");
    printf("    \"egg_encryption\": false\n");
    printf("  },\n");
    printf("  \"formats\": [\n");
    for (size_t i = 0; i < sizeof(FORMAT_CAPABILITIES) / sizeof(FORMAT_CAPABILITIES[0]); i++) {
        const FormatCapability *item = &FORMAT_CAPABILITIES[i];
        printf("    {\"name\":\"%s\",\"create\":%s,\"extract\":%s,\"list\":%s,\"native\":%s,\"notes\":\"%s\"}%s\n",
               item->name,
               item->create ? "true" : "false",
               item->extract ? "true" : "false",
               item->list ? "true" : "false",
               item->native ? "true" : "false",
               item->notes,
               i + 1 == sizeof(FORMAT_CAPABILITIES) / sizeof(FORMAT_CAPABILITIES[0]) ? "" : ",");
    }
    printf("  ]\n");
    printf("}\n");
}
