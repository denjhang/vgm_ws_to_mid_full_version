#ifndef WONDERSWAN_CHIP_H
#define WONDERSWAN_CHIP_H

#include "MidiWriter.h"
#include "InstrumentConfig.h"
#include "UsageLogger.h" // Include UsageLogger
#include <string>
#include <cstdint>
#include <vector>
#include <fstream>
#include <map>

class WonderSwanChip {
public:
    WonderSwanChip(MidiWriter& midi_writer, InstrumentConfig& config, UsageLogger& logger, const std::string& source_filename);
    ~WonderSwanChip();
    void write_port(uint8_t port, uint8_t value);
    void write_ram(uint16_t address, uint8_t value);
    void advance_time(uint16_t samples);
    void finalize();
    void flush_log();
    size_t get_channel_count() const;
    const std::map<int, std::map<std::string, int>>& get_usage_data() const;

private:
    std::map<int, std::map<std::string, int>> usage_data;
    MidiWriter& midi_writer;
    InstrumentConfig& config;
    UsageLogger& usage_logger;
    std::string source_filename;
    std::vector<uint8_t> io_ram;
    std::vector<uint8_t> internal_ram;
    std::vector<int> channel_periods;
    std::vector<int> channel_volumes_left;
    std::vector<int> channel_volumes_right;
    std::vector<bool> channel_enabled;
    std::vector<bool> channel_is_active; // New state to track if a note is currently playing
    std::vector<int> channel_last_note;
    std::vector<int> channel_last_velocity; // For volume dynamics
    std::vector<int> channel_last_pan;      // For stereo panning
    std::vector<int> channel_instrument;
    std::vector<bool> channel_is_noise;
    std::vector<int> channel_last_pitch_bend;
    std::vector<double> channel_base_note_freq;
    const double channel_pitch_bend_range_semitones = 2.0;
    uint64_t current_sample_time; // Using uint64_t to prevent overflow with large files
    std::vector<uint32_t> channel_last_tick_time; // To calculate delta-times for each track
    std::ofstream log_file;

    // Sound DMA state
    uint32_t s_dma_source_addr = 0;
    uint16_t s_dma_count = 0;
    uint32_t s_dma_timer = 0;
    uint32_t s_dma_period = 0;

    // Sweep state (for channel 2)
    int8_t sweep_step = 0;
    int16_t sweep_time = 0;
    int16_t sweep_count = 0;

    // Noise state (for channel 3)
    uint8_t noise_type = 0;
    bool noise_reset = false;

    // PCM volume
    int pcm_volume_left = 0;
    int pcm_volume_right = 0;

    // Custom waveform detection
    std::map<std::string, std::vector<uint8_t>> discovered_waveforms;

    double period_to_freq(int period);
    int period_to_midi_note(int period);
    void check_state_and_update_midi(int channel);
    void start_new_note(int channel, int note_pitch, const std::string& waveform_fingerprint);
    void process_s_dma(uint32_t samples);
    void process_sweep(uint32_t samples);
    bool are_waveforms_similar(const std::vector<uint8_t>& w1, const std::vector<uint8_t>& w2);
};

#endif // WONDERSWAN_CHIP_H
