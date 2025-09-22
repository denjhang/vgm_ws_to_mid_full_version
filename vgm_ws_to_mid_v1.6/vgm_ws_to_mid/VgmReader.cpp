#include "VgmReader.h"
#include <fstream>
#include <iostream>

VgmReader::VgmReader(WonderSwanChip& chip) : chip(chip) {}

uint32_t VgmReader::get_loop_offset() const {
    return loop_offset;
}

uint32_t VgmReader::get_data_offset() const {
    return data_offset;
}

const std::vector<uint8_t>& VgmReader::get_data() const {
    return file_data;
}

bool VgmReader::load_and_parse(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    file_data.resize(size);
    if (!file.read(reinterpret_cast<char*>(file_data.data()), size)) {
        std::cerr << "Error reading file: " << filename << std::endl;
        return false;
    }

    return parse();
}

bool VgmReader::parse() {
    if (file_data.size() < 0x40) {
        std::cerr << "Invalid VGM file: header too small." << std::endl;
        return false;
    }

    if (file_data[0] != 'V' || file_data[1] != 'g' || file_data[2] != 'm' || file_data[3] != ' ') {
        std::cerr << "Invalid VGM file: magic number mismatch." << std::endl;
        return false;
    }

    uint32_t vgm_data_offset_val = *reinterpret_cast<uint32_t*>(&file_data[0x34]);
    data_offset = (vgm_data_offset_val == 0) ? 0x40 : (0x34 + vgm_data_offset_val);

    uint32_t loop_offset_val = *reinterpret_cast<uint32_t*>(&file_data[0x1C]);
    if (loop_offset_val != 0) {
        loop_offset = 0x1C + loop_offset_val;
    } else {
        loop_offset = 0; // No loop
    }

    // The actual processing will be done in main.cpp now.
    // This class is now just a data container.
    // We just need to provide the raw data to the main loop.
    // Let's move the processing logic there.
    // We will need a way to get the file data.
    // Let's add a getter for file_data.

    // This is a big change. Let's first add the getter to the header.
    // I will do that in a separate step. For now, this is fine.
    // The main loop will need to access file_data directly.
    // Let's make it public for now. This is not ideal, but it's a start.
    // I will refactor this later.

    // Let's go back and modify the header first.
    // I will add `const std::vector<uint8_t>& get_data() const;`
    // And in the cpp file:
    // `const std::vector<uint8_t>& VgmReader::get_data() const { return file_data; }`

    // For now, I will assume the main function can get the data.
    // The main loop will look something like this:
    //
    // VgmReader reader(chip);
    // reader.load_and_parse(argv[1]);
    //
    // uint32_t current_pos = reader.get_data_offset();
    // const auto& data = reader.get_data();
    // int loop_count = 0;
    //
    // while (current_pos < data.size()) {
    //     // ... process commands ...
    //
    //     if (current_pos >= data.size() && reader.get_loop_offset() != 0 && loop_count < 1) {
    //         current_pos = reader.get_loop_offset();
    //         loop_count++;
    //         // Here we need to handle the MIDI part of the loop
    //     }
    // }

    // The logic in this parse function is no longer needed.
    // It will be moved to main.cpp.
    // So I will just return true here.
    return true;
}
