#ifndef VGM_READER_H
#define VGM_READER_H

#include <string>
#include <vector>
#include <cstdint>
#include "WonderSwanChip.h"

class VgmReader {
public:
    VgmReader(WonderSwanChip& chip);
    bool load_and_parse(const std::string& filename);
    uint32_t get_loop_offset() const;
    uint32_t get_data_offset() const;
    const std::vector<uint8_t>& get_data() const;

private:
    WonderSwanChip& chip;
    std::vector<uint8_t> file_data;
    uint32_t loop_offset = 0;
    uint32_t data_offset = 0;
    bool parse();
};

#endif // VGM_READER_H
