#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <string>
#include <numeric>
#include <set>
#include <map>
#include <algorithm>
#include <iomanip>

// Endian swap functions
uint16_t swap_uint16(uint16_t val) {
    return (val << 8) | (val >> 8);
}

uint32_t swap_uint32(uint32_t val) {
    val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF);
    return (val << 16) | (val >> 16);
}

// Read variable-length quantity
uint32_t read_variable_length(const std::vector<uint8_t>& data, size_t& pos) {
    uint32_t value = 0;
    uint8_t byte;
    if (pos >= data.size()) return 0;
    do {
        byte = data[pos++];
        value = (value << 7) | (byte & 0x7F);
    } while ((byte & 0x80) && pos < data.size());
    return value;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <midi_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        std::cerr << "Cannot open file: " << filename << std::endl;
        return 1;
    }

    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> file_data(file_size);
    if (!file.read(reinterpret_cast<char*>(file_data.data()), file_size)) {
        std::cerr << "Failed to read file." << std::endl;
        return 1;
    }

    size_t pos = 0;

    // --- Parse Header ---
    if (pos + 14 > file_data.size() || std::string(file_data.begin() + pos, file_data.begin() + pos + 4) != "MThd") {
        std::cerr << "Invalid MIDI file: MThd chunk not found." << std::endl;
        return 1;
    }
    pos += 4; // "MThd"
    uint32_t header_length = swap_uint32(*reinterpret_cast<const uint32_t*>(&file_data[pos]));
    pos += 4;
    uint16_t format = swap_uint16(*reinterpret_cast<const uint16_t*>(&file_data[pos]));
    pos += 2;
    uint16_t num_tracks = swap_uint16(*reinterpret_cast<const uint16_t*>(&file_data[pos]));
    pos += 2;
    uint16_t division = swap_uint16(*reinterpret_cast<const uint16_t*>(&file_data[pos]));
    pos += 2;

    // --- Statistics and Logging ---
    int note_on_count = 0;
    uint64_t total_ticks = 0;
    std::set<int> used_channels;
    std::vector<int> note_pitches;
    
    std::cout << "--- MIDI Event Log ---" << std::endl;
    std::cout << std::setw(8) << "Tick" << " | " << std::setw(15) << "Event" << " | " << "Ch" << " | " << std::setw(12) << "Data 1" << " | " << "Data 2" << std::endl;
    std::cout << "----------------------------------------------------------" << std::endl;

    std::map<uint8_t, std::map<uint8_t, uint64_t>> open_notes; // channel -> note -> tick

    // --- Parse Tracks ---
    for (int i = 0; i < num_tracks; ++i) {
        if (pos + 8 > file_data.size() || std::string(file_data.begin() + pos, file_data.begin() + pos + 4) != "MTrk") {
            std::cerr << "Invalid MIDI file: MTrk chunk not found for track " << i + 1 << std::endl;
            return 1;
        }
        pos += 4; // "MTrk"
        uint32_t track_length = swap_uint32(*reinterpret_cast<const uint32_t*>(&file_data[pos]));
        pos += 4;
        size_t track_end = pos + track_length;
        uint8_t last_status = 0;
        uint64_t current_tick = 0;

        while (pos < track_end) {
            uint32_t delta_time = read_variable_length(file_data, pos);
            current_tick += delta_time;
            
            uint8_t status_byte = file_data[pos];
            if ((status_byte & 0x80) == 0) { status_byte = last_status; } else { pos++; }
            last_status = status_byte;
            uint8_t event_type = status_byte & 0xF0;
            uint8_t channel = status_byte & 0x0F;

            std::cout << std::setw(8) << current_tick << " | ";

            switch (event_type) {
                case 0x90: { // Note On
                    if (pos + 2 > track_end) break;
                    uint8_t note = file_data[pos];
                    uint8_t velocity = file_data[pos + 1];
                    std::cout << std::setw(15) << "Note On" << " | " << std::setw(2) << (int)channel << " | " << std::setw(12) << (int)note << " | " << (int)velocity << std::endl;
                    if (velocity > 0) {
                        open_notes[channel][note] = current_tick;
                        note_on_count++;
                        used_channels.insert(channel);
                        note_pitches.push_back(note);
                    } else { // Note On with velocity 0 is a Note Off
                        open_notes[channel].erase(note);
                    }
                    pos += 2;
                    break;
                }
                case 0x80: { // Note Off
                    if (pos + 2 > track_end) break;
                    uint8_t note = file_data[pos];
                    uint8_t velocity = file_data[pos + 1];
                    std::cout << std::setw(15) << "Note Off" << " | " << std::setw(2) << (int)channel << " | " << std::setw(12) << (int)note << " | " << (int)velocity << std::endl;
                    open_notes[channel].erase(note);
                    used_channels.insert(channel);
                    pos += 2;
                    break;
                }
                case 0xB0: { // Control Change
                    if (pos + 2 > track_end) break;
                    uint8_t controller = file_data[pos];
                    uint8_t value = file_data[pos + 1];
                    std::cout << std::setw(15) << "Control Change" << " | " << std::setw(2) << (int)channel << " | " << std::setw(12) << (int)controller << " | " << (int)value << std::endl;
                    pos += 2;
                    break;
                }
                case 0xC0: { // Program Change
                    if (pos + 1 > track_end) break;
                    uint8_t program = file_data[pos];
                    std::cout << std::setw(15) << "Program Change" << " | " << std::setw(2) << (int)channel << " | " << std::setw(12) << (int)program << " | " << std::endl;
                    pos += 1;
                    break;
                }
                case 0xA0: case 0xE0: pos += 2; break;
                case 0xD0: pos += 1; break;
                case 0xF0: {
                    if (status_byte == 0xFF) { // Meta Event
                        uint8_t meta_type = file_data[pos++];
                        uint32_t len = read_variable_length(file_data, pos);
                        std::cout << std::setw(15) << "Meta Event" << " | " << std::setw(2) << "" << " | " << "Type: " << std::hex << (int)meta_type << std::dec << std::endl;
                        if (meta_type == 0x2F) { // End of Track
                            total_ticks = std::max(total_ticks, (uint64_t)current_tick);
                        }
                        pos += len;
                    } else { // Sysex
                        pos += read_variable_length(file_data, pos);
                    }
                    break;
                }
                default: pos = track_end; break;
            }
        }
        pos = track_end;
    }

    // --- Print Statistics ---
    std::cout << "\n--- MIDI File Statistics ---" << std::endl;
    std::cout << "File Size: " << file_size << " bytes" << std::endl;
    
    // --- Print Hanging Notes ---
    std::cout << "\n--- Hanging Note Detection ---" << std::endl;
    bool hanging_notes_found = false;
    for (const auto& channel_pair : open_notes) {
        if (!channel_pair.second.empty()) {
             if (!hanging_notes_found) {
                std::cout << "Hanging notes found:" << std::endl;
                hanging_notes_found = true;
            }
            for (const auto& note_pair : channel_pair.second) {
                std::cout << "  - Channel: " << (int)channel_pair.first
                          << ", Note: " << (int)note_pair.first
                          << ", Started at tick: " << note_pair.second << std::endl;
            }
        }
    }
    if (!hanging_notes_found) {
        std::cout << "No hanging notes detected." << std::endl;
    }

    return 0;
}
