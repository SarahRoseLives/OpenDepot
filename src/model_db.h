#pragma once
#include <cstdint>
#include <cstddef>

struct ModelBlob {
    const char* model;
    const uint8_t* header;  // 800-byte AdkFileHeader
    const uint8_t* data;    // records: id4 + part2 + len2 + payload
    size_t size;
};

extern const ModelBlob kModels[];
extern const int kModelCount;
