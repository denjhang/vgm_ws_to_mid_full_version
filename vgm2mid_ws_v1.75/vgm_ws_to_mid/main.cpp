#include <iostream>
#include <vector>
#include "MidiWriter.h"
#include "WonderSwanChip.h"
#include "VgmReader.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.vgm> <output.mid>" << std::endl;
        return 1;
    }

    std::string input_filename = argv[1];
    std::string output_filename = argv[2];

    std::cout << "VGM to MIDI conversion process started." << std::endl;

    MidiWriter midi_writer(480);
    size_t meta_track_idx = midi_writer.add_track();
    MidiTrack& meta_track = midi_writer.get_track(meta_track_idx);
    meta_track.add_tempo_change(0, 500000);

    WonderSwanChip chip(midi_writer);
    VgmReader reader(chip);

    if (!reader.load_and_parse(input_filename)) {
        std::cerr << "Failed to load or parse VGM file." << std::endl;
        return 1;
    }

    const auto& data = reader.get_data();
    uint32_t loop_offset = reader.get_loop_offset();
    uint32_t current_pos = reader.get_data_offset();
    uint32_t end_pos = data.size();

    const int num_loops = 1; // How many times to repeat the loop section. 1 means it plays once after the first time.
    int loops_done = 0;

    while (current_pos < end_pos) {
        uint8_t command_byte = data[current_pos];

        if (command_byte == 0x66) { // End of sound data
            if (loop_offset != 0 && loops_done < num_loops) {
                loops_done++;
                std::cout << "Looping back to offset 0x" << std::hex << loop_offset << " (" << loops_done << "/" << num_loops << ")" << std::dec << std::endl;
                current_pos = loop_offset;
                continue;
            } else {
                break; // End of data and no more loops
            }
        }

        switch (command_byte) {
            case 0x61: { // Wait nnnn samples
                if (current_pos + 2 >= end_pos) { current_pos = end_pos; continue; }
                uint16_t wait = *reinterpret_cast<const uint16_t*>(&data[current_pos + 1]);
                chip.advance_time(wait);
                current_pos += 3;
                break;
            }
            case 0x62: // Wait 1/60 second
                chip.advance_time(735); // 44100 / 60
                current_pos += 1;
                break;
            case 0x63: // Wait 1/50 second
                chip.advance_time(882); // 44100 / 50
                current_pos += 1;
                break;
            
            case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
            case 0x76: case 0x77: case 0x78: case 0x79: case 0x7a: case 0x7b:
            case 0x7c: case 0x7d: case 0x7e: case 0x7f:
                chip.advance_time((command_byte & 0x0F) + 1);
                current_pos += 1;
                break;

            case 0xb3: // WonderSwan I/O write
            case 0xbc: { // WonderSwan Custom I/O write
                if (current_pos + 2 >= end_pos) { current_pos = end_pos; continue; }
                uint8_t port = data[current_pos + 1];
                uint8_t value = data[current_pos + 2];
                chip.write_port(port, value);
                current_pos += 3;
                break;
            }

            // Ignored commands
            case 0x4f: // Game Gear PSG stereo
            case 0x50: // PSG
                current_pos += 2;
                break;
            case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57: case 0x58:
            case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
                current_pos += 3;
                break;
            case 0x67: { // data block
                if (current_pos + 6 >= end_pos) { current_pos = end_pos; continue; }
                current_pos += 6 + (*reinterpret_cast<const uint32_t*>(&data[current_pos + 2]));
                break;
            }
            case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: case 0x85:
            case 0x86: case 0x87: case 0x88: case 0x89: case 0x8a: case 0x8b:
            case 0x8c: case 0x8d: case 0x8e: case 0x8f:
                current_pos += 3; // DAC stream, we can ignore for WS
                break;
            case 0xa0:
                 current_pos += 3;
                 break;
            case 0xb0: case 0xb1: case 0xb2: /* 0xb3 is handled above */ case 0xb4: case 0xb5: case 0xb6: case 0xb7:
            case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbd: case 0xbe: case 0xbf:
                 current_pos += 3;
                 break;
            case 0xc0: case 0xc1: case 0xc2: case 0xc3: case 0xc4: case 0xc5: case 0xc6: case 0xc7:
            case 0xc8:
                current_pos += 4;
                break;
            case 0xd0: case 0xd1: case 0xd2: case 0xd3: case 0xd4: case 0xd5: case 0xd6:
                current_pos += 4;
                break;
            case 0xe0: case 0xe1:
                current_pos += 5;
                break;

            default:
                current_pos++; // Unknown or unhandled command, just skip
                break;
        }
    }

    chip.finalize();
    midi_writer.write_to_file(output_filename);

    std::cout << "VGM to MIDI conversion completed successfully." << std::endl;

    return 0;
}
