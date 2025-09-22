#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cstdint>

// --- Function Prototypes ---
uint32_t read_variable_length(std::istream& input);
std::string get_note_name(uint8_t note_number);
void print_hex(std::ostream& out, const std::vector<uint8_t>& data);

// --- Main ---
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <midi_file>" << std::endl;
        return 1;
    }

    std::ifstream midi_file(argv[1], std::ios::in | std::ios::binary);
    if (!midi_file.is_open()) {
        std::cerr << "Error opening MIDI file: " << argv[1] << std::endl;
        return 1;
    }

    std::ofstream log_file("vgm_ws_to_mid/validation_result.txt", std::ios::out);
    if (!log_file.is_open()) {
        std::cerr << "Error opening log file." << std::endl;
        return 1;
    }

    // --- Read Header Chunk ---
    char chunk_type[4];
    midi_file.read(chunk_type, 4);
    if (std::string(chunk_type, 4) != "MThd") {
        log_file << "Error: Not a valid MIDI file. 'MThd' chunk not found." << std::endl;
        return 1;
    }

    char buffer[8];
    midi_file.read(buffer, 4);
    uint32_t header_length = (uint8_t)buffer[0] << 24 | (uint8_t)buffer[1] << 16 | (uint8_t)buffer[2] << 8 | (uint8_t)buffer[3];
    
    midi_file.read(buffer, header_length);
    uint16_t format = (uint8_t)buffer[0] << 8 | (uint8_t)buffer[1];
    uint16_t num_tracks = (uint8_t)buffer[2] << 8 | (uint8_t)buffer[3];
    uint16_t division = (uint8_t)buffer[4] << 8 | (uint8_t)buffer[5];

    log_file << "MIDI Header Found" << std::endl;
    log_file << "  Format: " << format << std::endl;
    log_file << "  Tracks: " << num_tracks << std::endl;
    log_file << "  Ticks per Quarter Note: " << division << std::endl;
    log_file << "----------------------------------------" << std::endl;

    // --- Read Track Chunks ---
    for (uint16_t i = 0; i < num_tracks; ++i) {
        midi_file.read(chunk_type, 4);
        if (std::string(chunk_type, 4) != "MTrk") {
            log_file << "Error: 'MTrk' chunk expected for track " << i << ", but not found." << std::endl;
            return 1;
        }

        midi_file.read(buffer, 4);
        uint32_t track_length = (uint8_t)buffer[0] << 24 | (uint8_t)buffer[1] << 16 | (uint8_t)buffer[2] << 8 | (uint8_t)buffer[3];
        
        log_file << "\nTrack " << i << " (Length: " << track_length << " bytes)" << std::endl;

        long track_start_pos = midi_file.tellg();
        uint8_t last_status_byte = 0;

        while ((long)midi_file.tellg() < track_start_pos + track_length) {
            uint32_t delta_time = read_variable_length(midi_file);
            log_file << "  Delta: " << std::setw(5) << std::left << delta_time;

            uint8_t status_byte = midi_file.get();
            uint8_t event_type;
            uint8_t channel;

            if (status_byte < 0x80) { // Running status
                midi_file.unget(); // Put the byte back, it's data
                status_byte = last_status_byte;
            }

            event_type = status_byte & 0xF0;
            channel = status_byte & 0x0F;
            last_status_byte = status_byte;

            switch (event_type) {
                case 0x80: { // Note Off
                    uint8_t note = midi_file.get();
                    uint8_t velocity = midi_file.get();
                    log_file << " | Note Off  | Ch: " << std::setw(2) << (int)channel << " | Note: " << std::setw(3) << (int)note << " (" << std::setw(3) << get_note_name(note) << ") | Vel: " << std::setw(3) << (int)velocity << std::endl;
                    break;
                }
                case 0x90: { // Note On
                    uint8_t note = midi_file.get();
                    uint8_t velocity = midi_file.get();
                    if (velocity > 0) {
                        log_file << " | Note On   | Ch: " << std::setw(2) << (int)channel << " | Note: " << std::setw(3) << (int)note << " (" << std::setw(3) << get_note_name(note) << ") | Vel: " << std::setw(3) << (int)velocity << std::endl;
                    } else { // Velocity 0 is a Note Off
                        log_file << " | Note Off  | Ch: " << std::setw(2) << (int)channel << " | Note: " << std::setw(3) << (int)note << " (" << std::setw(3) << get_note_name(note) << ") | Vel: 0   " << std::endl;
                    }
                    break;
                }
                case 0xB0: { // Control Change
                    uint8_t controller = midi_file.get();
                    uint8_t value = midi_file.get();
                    log_file << " | CC        | Ch: " << std::setw(2) << (int)channel << " | Ctrl: " << std::setw(3) << (int)controller << " | Val: " << std::setw(3) << (int)value << std::endl;
                    break;
                }
                case 0xC0: { // Program Change
                    uint8_t program = midi_file.get();
                    log_file << " | Prog Chg  | Ch: " << std::setw(2) << (int)channel << " | Prog: " << std::setw(3) << (int)program << std::endl;
                    break;
                }
                case 0xF0: { // System Exclusive or Meta Event
                    if (status_byte == 0xFF) { // Meta Event
                        uint8_t meta_type = midi_file.get();
                        uint32_t meta_length = read_variable_length(midi_file);
                        std::vector<uint8_t> meta_data(meta_length);
                        midi_file.read((char*)meta_data.data(), meta_length);
                        
                        log_file << " | Meta Event| Type: " << std::hex << std::setw(2) << std::setfill('0') << (int)meta_type << std::dec << std::setfill(' ');
                        if (meta_type == 0x51 && meta_length == 3) { // Tempo
                            uint32_t tempo = (meta_data[0] << 16) | (meta_data[1] << 8) | meta_data[2];
                            log_file << " (Tempo) | BPM: " << 60000000 / tempo << std::endl;
                        } else if (meta_type == 0x2F) { // End of Track
                            log_file << " (End of Track)" << std::endl;
                        } else {
                            log_file << " | Len: " << meta_length << " | Data: ";
                            print_hex(log_file, meta_data);
                            log_file << std::endl;
                        }
                    } else { // Sysex
                        uint32_t sysex_length = read_variable_length(midi_file);
                        midi_file.seekg(sysex_length, std::ios::cur); // Skip sysex data
                        log_file << " | SysEx     | Len: " << sysex_length << " (skipped)" << std::endl;
                    }
                    break;
                }
                default:
                    log_file << " | Unknown Event Type: " << std::hex << (int)event_type << std::dec << std::endl;
                    // Try to recover by assuming 1 data byte for other events
                    if (event_type >= 0xA0 && event_type <= 0xEF) {
                         if (event_type < 0xC0 || event_type >= 0xE0) midi_file.get(); // 2-byte events
                         midi_file.get(); // 1-byte events
                    }
                    break;
            }
        }
    }

    log_file << "\nValidation finished." << std::endl;
    midi_file.close();
    log_file.close();

    std::cout << "MIDI analysis complete. Results are in vgm_ws_to_mid/validation_result.txt" << std::endl;

    return 0;
}

uint32_t read_variable_length(std::istream& input) {
    uint32_t value = 0;
    uint8_t byte;
    do {
        byte = input.get();
        value = (value << 7) | (byte & 0x7F);
    } while (byte & 0x80);
    return value;
}

std::string get_note_name(uint8_t note_number) {
    if (note_number > 127) return "INV";
    const char* notes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int octave = (note_number / 12) - 1;
    return std::string(notes[note_number % 12]) + std::to_string(octave);
}

void print_hex(std::ostream& out, const std::vector<uint8_t>& data) {
    out << std::hex << std::setfill('0');
    for (const auto& byte : data) {
        out << " " << std::setw(2) << (int)byte;
    }
    out << std::dec << std::setfill(' ');
}
