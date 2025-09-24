#include "MidiWriter.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <map>

// --- Helper Functions for Endian Swapping ---
void write_be_16(std::ofstream& file, uint16_t val) {
    uint8_t data[2] = { static_cast<uint8_t>((val >> 8) & 0xFF), static_cast<uint8_t>(val & 0xFF) };
    file.write(reinterpret_cast<const char*>(data), 2);
}

void write_be_32(std::ofstream& file, uint32_t val) {
    uint8_t data[4] = { static_cast<uint8_t>((val >> 24) & 0xFF), static_cast<uint8_t>((val >> 16) & 0xFF), static_cast<uint8_t>((val >> 8) & 0xFF), static_cast<uint8_t>(val & 0xFF) };
    file.write(reinterpret_cast<const char*>(data), 4);
}

// --- MidiTrack Class Implementation ---

MidiTrack::MidiTrack() : current_time(0), last_status_byte(0) {}

void MidiTrack::write_variable_length(std::vector<uint8_t>& buffer, uint32_t value) const {
    if (value == 0) {
        buffer.push_back(0x00);
        return;
    }
    uint8_t bytes[5];
    int count = 0;
    do {
        bytes[count++] = value & 0x7F;
        value >>= 7;
    } while (value > 0);
    for (int i = count - 1; i >= 0; --i) {
        if (i > 0) {
            buffer.push_back(bytes[i] | 0x80);
        } else {
            buffer.push_back(bytes[i]);
        }
    }
}

void MidiTrack::add_event(uint32_t delta_time, const std::vector<uint8_t>& event_data) {
    if (event_data.empty()) return;
    current_time += delta_time;
    events.push_back({current_time, event_data});
}

void MidiTrack::add_note_on(uint32_t delta_time, uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel > 15 || note > 127 || velocity > 127) return;
    add_event(delta_time, {static_cast<uint8_t>(0x90 | channel), note, velocity});
}

void MidiTrack::add_note_off(uint32_t delta_time, uint8_t channel, uint8_t note) {
    if (channel > 15 || note > 127) return;
    add_event(delta_time, {static_cast<uint8_t>(0x90 | channel), note, (uint8_t)0});
}

void MidiTrack::add_program_change(uint32_t delta_time, uint8_t channel, uint8_t program) {
    if (channel > 15 || program > 127) return;
    add_event(delta_time, {static_cast<uint8_t>(0xC0 | channel), program});
}

void MidiTrack::add_control_change(uint32_t delta_time, uint8_t channel, uint8_t controller, uint8_t value) {
    if (channel > 15 || controller > 127 || value > 127) return;
    add_event(delta_time, {static_cast<uint8_t>(0xB0 | channel), controller, value});
}

void MidiTrack::add_pitch_bend(uint32_t delta_time, uint8_t channel, uint16_t value) {
    if (channel > 15 || value > 16383) return;
    uint8_t lsb = value & 0x7F;
    uint8_t msb = (value >> 7) & 0x7F;
    add_event(delta_time, {static_cast<uint8_t>(0xE0 | channel), lsb, msb});
}

void MidiTrack::add_meta_event(uint32_t delta_time, uint8_t type, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> event_data;
    event_data.push_back(0xFF);
    event_data.push_back(type);
    
    std::vector<uint8_t> len_bytes;
    write_variable_length(len_bytes, data.size());
    event_data.insert(event_data.end(), len_bytes.begin(), len_bytes.end());
    event_data.insert(event_data.end(), data.begin(), data.end());
    
    add_event(delta_time, event_data);
}

void MidiTrack::add_tempo_change(uint32_t delta_time, uint32_t tempo) {
    std::vector<uint8_t> tempo_data = {
        static_cast<uint8_t>((tempo >> 16) & 0xFF),
        static_cast<uint8_t>((tempo >> 8) & 0xFF),
        static_cast<uint8_t>(tempo & 0xFF)
    };
    add_meta_event(delta_time, 0x51, tempo_data);
}

uint32_t MidiTrack::get_current_time() const {
    return current_time;
}

void MidiTrack::copy_events_from(const MidiTrack& source_track, uint32_t start_time, uint32_t end_time) {
    if (end_time <= start_time) return;

    // Step 1: Collect all events to be copied into a temporary vector to avoid iterator invalidation.
    std::vector<MidiEvent> events_to_copy;
    for (const auto& event : source_track.events) {
        if (event.absolute_time >= start_time && event.absolute_time < end_time) {
            events_to_copy.push_back(event);
        }
    }

    if (events_to_copy.empty()) {
        return;
    }

    uint32_t loop_duration = end_time - start_time;
    uint32_t time_offset = this->current_time - start_time;

    // channel -> note -> is_open
    std::map<uint8_t, std::map<uint8_t, bool>> open_notes;

    // Step 2: Process the collected events, filter them, and add them to the main event list.
    for (const auto& event : events_to_copy) {
        if (event.event_data.empty()) continue;

        uint8_t status_byte = event.event_data[0];
        uint8_t status_type = status_byte & 0xF0;

        // --- Event Filtering Logic ---
        // 1. Exclude Meta Events (0xFF)
        if (status_byte == 0xFF) {
            continue;
        }
        // 2. Exclude Program Change (0xCn)
        if (status_type == 0xC0) {
            continue;
        }
        // 3. Exclude specific Control Changes (CC7 - Main Volume, CC10 - Pan)
        if (status_type == 0xB0) {
            if (event.event_data.size() > 1) {
                uint8_t controller = event.event_data[1];
                if (controller == 7 || controller == 10) {
                    continue;
                }
            }
        }
        // --- End of Filtering ---

        MidiEvent new_event = event;
        new_event.absolute_time += time_offset;
        this->events.push_back(new_event);

        // Track open notes within the loop block
        if (event.event_data.size() > 1) {
            uint8_t status = event.event_data[0] & 0xF0;
            uint8_t channel = event.event_data[0] & 0x0F;
            uint8_t note = event.event_data[1];
            uint8_t velocity = (event.event_data.size() > 2) ? event.event_data[2] : 0;

            if (status == 0x90 && velocity > 0) {
                open_notes[channel][note] = true;
            } else if (status == 0x80 || (status == 0x90 && velocity == 0)) {
                open_notes[channel].erase(note);
            }
        }
    }

    // Step 3: Close any notes that were opened within the loop block but not closed by its end.
    // The Note Off event should be at the very end of the copied block.
    uint32_t loop_end_time = this->current_time + loop_duration;
    for (auto const& [channel, notes] : open_notes) {
        for (auto const& [note, is_open] : notes) {
            if (is_open) {
                std::vector<uint8_t> note_off_data = {static_cast<uint8_t>(0x80 | channel), note, (uint8_t)0};
                this->events.push_back({loop_end_time, note_off_data});
            }
        }
    }

    this->current_time += loop_duration;
}


std::vector<uint8_t> MidiTrack::get_track_data() const {
    std::vector<uint8_t> track_data_bytes;
    uint32_t last_time = 0;
    uint8_t running_status = 0;

    // Sort events by absolute time just in case
    std::vector<MidiEvent> sorted_events = events;
    std::sort(sorted_events.begin(), sorted_events.end(), [](const MidiEvent& a, const MidiEvent& b) {
        return a.absolute_time < b.absolute_time;
    });

    for (const auto& event : sorted_events) {
        if (event.event_data.empty()) {
            continue;
        }
        uint32_t delta_time = event.absolute_time - last_time;
        write_variable_length(track_data_bytes, delta_time);

        uint8_t status_byte = event.event_data[0];
        
        bool is_meta_or_sysex = (status_byte & 0xF0) == 0xF0;

        if (is_meta_or_sysex) {
            track_data_bytes.insert(track_data_bytes.end(), event.event_data.begin(), event.event_data.end());
            running_status = 0; // Reset running status
        } else {
            if (status_byte != running_status) {
                track_data_bytes.push_back(status_byte);
                running_status = status_byte;
            }
            track_data_bytes.insert(track_data_bytes.end(), event.event_data.begin() + 1, event.event_data.end());
        }
        last_time = event.absolute_time;
    }
    return track_data_bytes;
}

// --- MidiWriter Class Implementation ---

MidiWriter::MidiWriter(uint16_t ticks_per_quarter_note) : ticks_per_quarter_note(ticks_per_quarter_note) {}

MidiTrack& MidiWriter::get_track(size_t index) {
    if (index >= tracks.size()) {
        throw std::out_of_range("Track index is out of range.");
    }
    return tracks[index];
}

size_t MidiWriter::add_track() {
    tracks.emplace_back();
    return tracks.size() - 1;
}

bool MidiWriter::write_to_file(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    file.write("MThd", 4);
    write_be_32(file, 6);
    write_be_16(file, 1);
    write_be_16(file, static_cast<uint16_t>(tracks.size()));
    write_be_16(file, ticks_per_quarter_note);

    for (auto& track : tracks) {
        track.add_meta_event(0, 0x2F, {}); // End of Track

        std::vector<uint8_t> track_data = track.get_track_data();
        file.write("MTrk", 4);
        write_be_32(file, static_cast<uint32_t>(track_data.size()));
        file.write(reinterpret_cast<const char*>(track_data.data()), track_data.size());
    }

    return file.good();
}
