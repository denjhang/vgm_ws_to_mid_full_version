#ifndef WONDERSWAN_CHIP_H
#define WONDERSWAN_CHIP_H

#include "MidiWriter.h"
#include <cstdint>
#include <vector>
#include <fstream>

class WonderSwanChip {
public:
    WonderSwanChip(MidiWriter& midi_writer);
    ~WonderSwanChip();
    void write_port(uint8_t port, uint8_t value);
    void advance_time(uint16_t samples);
    void finalize();
    size_t get_channel_count() const;

private:
    MidiWriter& midi_writer;
    std::vector<uint8_t> io_ram;
    std::vector<int> channel_periods;
    std::vector<int> channel_volumes_left;
    std::vector<int> channel_volumes_right;
    std::vector<bool> channel_enabled;
    std::vector<bool> channel_is_active; // New state to track if a note is currently playing
    std::vector<int> channel_last_note;
    std::vector<int> channel_last_velocity; // For volume dynamics
    std::vector<int> channel_last_pan;      // For stereo panning
    uint64_t current_sample_time; // Using uint64_t to prevent overflow with large files
    std::vector<uint32_t> channel_last_tick_time; // To calculate delta-times for each track
    std::ofstream log_file;

    int period_to_midi_note(int period);
    void check_state_and_update_midi(int channel);
};

#endif // WONDERSWAN_CHIP_H
