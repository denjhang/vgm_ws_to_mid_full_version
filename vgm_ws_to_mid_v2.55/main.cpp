#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include "MidiWriter.h"
#include "WonderSwanChip.h"
#include "VgmReader.h"
#include "InstrumentConfig.h"
#include "UsageLogger.h"

// Recompile trigger
namespace fs = std::filesystem;

void convert_file(const std::string& input_filename, const std::string& output_filename, int num_loops, InstrumentConfig& config, UsageLogger& logger) {
    std::cout << "\n--- Converting: " << input_filename << " -> " << output_filename << " ---" << std::endl;

    MidiWriter midi_writer(480);
    size_t meta_track_idx = midi_writer.add_track();
    MidiTrack& meta_track = midi_writer.get_track(meta_track_idx);
    meta_track.add_tempo_change(0, 500000);

    WonderSwanChip chip(midi_writer, config, logger, input_filename);
    VgmReader reader(chip);

    if (!reader.load_and_parse(input_filename)) {
        std::cerr << "Failed to load or parse VGM file: " << input_filename << std::endl;
        return;
    }

    const auto& data = reader.get_data();
    uint32_t loop_offset = reader.get_loop_offset();
    uint32_t current_pos = reader.get_data_offset();
    uint32_t end_pos = data.size();

    int loops_done = 0;

    while (current_pos < end_pos) {
        uint8_t command_byte = data[current_pos];

        if (command_byte == 0x66) {
            if (loop_offset != 0 && loops_done < num_loops) {
                loops_done++;
                current_pos = loop_offset;
                continue;
            } else {
                break;
            }
        }

        switch (command_byte) {
            case 0x61: {
                if (current_pos + 2 >= end_pos) { current_pos = end_pos; continue; }
                uint16_t wait = *reinterpret_cast<const uint16_t*>(&data[current_pos + 1]);
                chip.advance_time(wait);
                current_pos += 3;
                break;
            }
            case 0x62:
                chip.advance_time(735);
                current_pos += 1;
                break;
            case 0x63:
                chip.advance_time(882);
                current_pos += 1;
                break;
            case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
            case 0x76: case 0x77: case 0x78: case 0x79: case 0x7a: case 0x7b:
            case 0x7c: case 0x7d: case 0x7e: case 0x7f:
                chip.advance_time((command_byte & 0x0F) + 1);
                current_pos += 1;
                break;
            case 0xb3: // GameBoy DMG, should be ignored
                current_pos += 3;
                break;
            case 0xbc: {
                if (current_pos + 2 >= end_pos) { current_pos = end_pos; continue; }
                uint8_t port = 0x80 + data[current_pos + 1];
                uint8_t value = data[current_pos + 2];
                chip.write_port(port, value);
                current_pos += 3;
                break;
            }
            case 0xc6: {
                if (current_pos + 3 >= end_pos) { current_pos = end_pos; continue; }
                uint16_t address = (data[current_pos + 1] << 8) | data[current_pos + 2];
                uint8_t value = data[current_pos + 3];
                chip.write_ram(address, value);
                current_pos += 4;
                break;
            }
            case 0x4f:
            case 0x50:
                current_pos += 2;
                break;
            case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
            case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
                current_pos += 3;
                break;
            case 0x67: {
                if (current_pos + 6 >= end_pos) { current_pos = end_pos; continue; }
                current_pos += 6 + (*reinterpret_cast<const uint32_t*>(&data[current_pos + 2]));
                break;
            }
            default:
                current_pos++;
                break;
        }
    }

    chip.finalize();
    midi_writer.write_to_file(output_filename);
    std::cout << "Successfully converted." << std::endl;

    // Logging is now handled internally by chip's destructor calling flush_log()
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [options] <input.vgm> <output.mid>" << std::endl;
        std::cerr << "       " << argv[0] << " -b (batch convert all .vgm in current directory)" << std::endl;
        std::cerr << "       " << argv[0] << " -s (sort instruments.ini)" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  -l <loops> : Number of loops to play (default: 2)" << std::endl;
        return 1;
    }

    std::vector<std::string> args(argv, argv + argc);
    int num_loops = 2;
    std::string input_filename, output_filename;
    std::string mode;

    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-l") {
            if (i + 1 < args.size()) {
                num_loops = std::stoi(args[i + 1]);
                i++; // Skip next argument
            }
        } else if (args[i] == "-b" || args[i] == "-s") {
            mode = args[i];
        } else if (input_filename.empty()) {
            input_filename = args[i];
        } else if (output_filename.empty()) {
            output_filename = args[i];
        }
    }

    if (mode.empty() && (input_filename.empty() || output_filename.empty())) {
        std::cerr << "Usage: " << argv[0] << " [options] <input.vgm> <output.mid>" << std::endl;
        return 1;
    }
    
    fs::path exe_path = fs::path(argv[0]);
    fs::path exe_dir = exe_path.parent_path();
    fs::path config_path = exe_dir / "instruments.ini";
    fs::path log_path = exe_dir / "conversion_log.txt";

    UsageLogger logger(log_path.string());
    InstrumentConfig config(config_path.string(), logger);
    config.load();

    if (mode == "-b") {
        std::cout << "--- Batch conversion mode ---" << std::endl;
        fs::path current_dir = ".";
        for (const auto& entry : fs::directory_iterator(current_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".vgm") {
                fs::path input_path = entry.path();
                fs::path output_path = input_path;
                output_path.replace_extension(".mid");
                convert_file(input_path.string(), output_path.string(), num_loops, config, logger);
            }
        }
        std::cout << "\n--- Batch conversion finished ---" << std::endl;
    } else if (mode == "-s") {
        std::cout << "Sorting instruments.ini by similarity..." << std::endl;
        config.sort_and_save();
        std::cout << "instruments.ini has been sorted." << std::endl;
    } else {
        convert_file(input_filename, output_filename, num_loops, config, logger);
    }

    return 0;
}
