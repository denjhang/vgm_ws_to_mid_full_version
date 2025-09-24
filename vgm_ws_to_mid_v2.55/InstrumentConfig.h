#ifndef INSTRUMENT_CONFIG_H
#define INSTRUMENT_CONFIG_H

#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>

// Represents a single instrument's configuration
struct InstrumentInfo {
    std::string name;
    std::string fingerprint;
    std::string graph;
    int midi_instrument;
    std::string source;
    std::string registered_at;
};

class InstrumentConfig {
public:
    InstrumentConfig(const std::string& filename, class UsageLogger& logger);
    void load();
    void save();
    void sort_and_save();
    int find_or_create_instrument(const std::array<uint8_t, 32>& waveform_data, const std::string& source_filename);
    InstrumentInfo get_instrument_by_fingerprint(const std::string& fingerprint) const;

private:
    void populate_with_defaults();
    std::string get_current_timestamp();
    std::string generate_fingerprint(const std::array<uint8_t, 32>& waveform_data);
    std::string generate_waveform_graph(const std::array<uint8_t, 32>& waveform_data);
    int analyze_waveform(const std::array<uint8_t, 32>& waveform_data);
    bool are_waveforms_similar(const std::array<uint8_t, 32>& wave1, const std::array<uint8_t, 32>& wave2, int threshold);

    std::string config_filename;
    std::unordered_map<std::string, InstrumentInfo> instruments;
    int next_custom_wave_id = 1;
    UsageLogger& usage_logger;
};

#endif // INSTRUMENT_CONFIG_H
