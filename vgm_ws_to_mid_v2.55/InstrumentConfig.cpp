#include "InstrumentConfig.h"
#include "UsageLogger.h"
#include "WaveformInfo.h" // For default waveforms
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <ctime>

// Helper to trim whitespace from both ends of a string
std::string trim(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) return "";
    size_t last = s.find_last_not_of(" \t\n\r");
    return s.substr(first, (last - first + 1));
}

InstrumentConfig::InstrumentConfig(const std::string& filename, UsageLogger& logger)
    : config_filename(filename), usage_logger(logger), next_custom_wave_id(1) {}

std::string InstrumentConfig::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X");
    return ss.str();
}

void InstrumentConfig::populate_with_defaults() {
    std::string ts = get_current_timestamp();
    for (const auto& pair : DEFAULT_INSTRUMENTS) {
        InstrumentInfo info;
        info.name = pair.first;
        info.midi_instrument = pair.second.id;
        info.source = "Built-in";
        info.registered_at = ts;

        std::array<uint8_t, 32> wave_array;
        std::copy_n(pair.second.wave_data.begin(), 32, wave_array.begin());
        
        info.fingerprint = generate_fingerprint(wave_array);
        info.graph = generate_waveform_graph(wave_array);
        
        if (instruments.find(info.fingerprint) == instruments.end()) {
            instruments[info.fingerprint] = info;
        }
    }
}

void InstrumentConfig::load() {
    std::ifstream infile(config_filename);
    if (!infile.is_open()) {
        // File doesn't exist, populate with defaults and save a new one.
        populate_with_defaults();
        save();
        return;
    }

    std::string line;
    InstrumentInfo current_instrument;
    std::string current_name;
    bool in_graph = false;

    while (std::getline(infile, line)) {
        std::string trimmed_line = trim(line);

        if (in_graph) {
            if (trimmed_line.empty() || (trimmed_line[0] == '[' && trimmed_line.back() == ']')) {
                in_graph = false;
            } else {
                current_instrument.graph += line + "\n";
                continue;
            }
        }
        
        if (trimmed_line.empty() || trimmed_line[0] == ';') continue;

        if (trimmed_line[0] == '[' && trimmed_line.back() == ']') {
            if (!current_name.empty() && !current_instrument.fingerprint.empty()) {
                instruments[current_instrument.fingerprint] = current_instrument;
            }
            current_name = trimmed_line.substr(1, trimmed_line.length() - 2);
            current_instrument = InstrumentInfo{};
            current_instrument.name = current_name;
            in_graph = false;
        } else {
            std::stringstream ss(trimmed_line);
            std::string key, value;
            std::getline(ss, key, '=');
            std::getline(ss, value);
            key = trim(key);
            value = trim(value);

            if (key == "fingerprint") current_instrument.fingerprint = value;
            else if (key == "midi_instrument") current_instrument.midi_instrument = std::stoi(value);
            else if (key == "source") current_instrument.source = value;
            else if (key == "registered_at") current_instrument.registered_at = value;
            else if (key == "graph") {
                in_graph = true;
                // The rest of the line is the start of the graph
                current_instrument.graph = line.substr(line.find("=") + 1) + "\n";
            } else if (in_graph) {
                current_instrument.graph += line + "\n";
            }
        }
    }
    if (!current_name.empty() && !current_instrument.fingerprint.empty()) {
        instruments[current_instrument.fingerprint] = current_instrument;
    }
    
    if (instruments.empty()) {
        populate_with_defaults();
        save();
    }

    for(const auto& pair : instruments) {
        if (pair.second.name.rfind("CustomWave_", 0) == 0) {
            try {
                int id = std::stoi(pair.second.name.substr(11));
                if (id >= next_custom_wave_id) next_custom_wave_id = id + 1;
            } catch (...) {}
        }
    }
}

void InstrumentConfig::save() {
    std::ofstream outfile(config_filename);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open instrument config for writing: " << config_filename << std::endl;
        return;
    }

    outfile << "; Instrument configuration for vgm_ws_to_mid" << std::endl;
    outfile << "; This file is auto-generated and managed by the converter." << std::endl;
    outfile << "; You can manually edit the 'midi_instrument' for any entry." << std::endl;
    outfile << std::endl;

    // Create a vector to sort the instruments for consistent output order
    std::vector<InstrumentInfo> sorted_instruments;
    for (const auto& pair : instruments) {
        sorted_instruments.push_back(pair.second);
    }
    std::sort(sorted_instruments.begin(), sorted_instruments.end(), [](const InstrumentInfo& a, const InstrumentInfo& b) {
        return a.name < b.name;
    });

    for (const auto& info : sorted_instruments) {
        outfile << "[" << info.name << "]" << std::endl;
        outfile << "fingerprint = " << info.fingerprint << std::endl;
        outfile << "midi_instrument = " << info.midi_instrument << std::endl;
        outfile << "source = " << info.source << std::endl;
        outfile << "registered_at = " << info.registered_at << std::endl;
        outfile << "graph =" << info.graph; // Graph includes its own newlines
        outfile << std::endl;
    }
}

void InstrumentConfig::sort_and_save() {
    if (instruments.empty()) {
        return;
    }

    // Helper to decode fingerprint string to wave array
    auto decode_fingerprint = [](const std::string& fp) {
        std::array<uint8_t, 32> wave;
        for(size_t i = 0; i < 32; ++i) {
            std::string byte_str = fp.substr(i * 2, 2);
            wave[i] = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        }
        return wave;
    };

    std::vector<InstrumentInfo> all_instruments;
    for(const auto& pair : instruments) {
        all_instruments.push_back(pair.second);
    }

    std::vector<InstrumentInfo> sorted_instruments;
    std::vector<bool> processed(all_instruments.size(), false);
    
    for (size_t i = 0; i < all_instruments.size(); ++i) {
        if (processed[i]) continue;

        std::vector<InstrumentInfo> cluster;
        cluster.push_back(all_instruments[i]);
        processed[i] = true;
        
        std::array<uint8_t, 32> representative_wave = decode_fingerprint(all_instruments[i].fingerprint);

        for (size_t j = i + 1; j < all_instruments.size(); ++j) {
            if (processed[j]) continue;
            
            std::array<uint8_t, 32> candidate_wave = decode_fingerprint(all_instruments[j].fingerprint);
            if (are_waveforms_similar(representative_wave, candidate_wave, 6)) {
                cluster.push_back(all_instruments[j]);
                processed[j] = true;
            }
        }

        std::sort(cluster.begin(), cluster.end(), [](const InstrumentInfo& a, const InstrumentInfo& b){
            return a.name < b.name;
        });

        for(const auto& info : cluster) {
            sorted_instruments.push_back(info);
        }
    }

    // Overwrite the file with the sorted list
    std::ofstream outfile(config_filename);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open instrument config for writing: " << config_filename << std::endl;
        return;
    }
    
    outfile << "; Instrument configuration for vgm_ws_to_mid" << std::endl;
    outfile << "; This file is auto-generated and managed by the converter." << std::endl;
    outfile << "; You can manually edit the 'midi_instrument' for any entry." << std::endl;
    outfile << std::endl;

    for (const auto& info : sorted_instruments) {
        outfile << "[" << info.name << "]" << std::endl;
        outfile << "fingerprint = " << info.fingerprint << std::endl;
        outfile << "midi_instrument = " << info.midi_instrument << std::endl;
        outfile << "source = " << info.source << std::endl;
        outfile << "registered_at = " << info.registered_at << std::endl;
        outfile << "graph =" << info.graph;
        outfile << std::endl;
    }
    
    // Reload the in-memory map to reflect the sorted state
    load();
}

int InstrumentConfig::find_or_create_instrument(const std::array<uint8_t, 32>& waveform_data, const std::string& source_filename) {
    std::string fp = generate_fingerprint(waveform_data);

    auto it = instruments.find(fp);
    if (it != instruments.end()) {
        return it->second.midi_instrument;
    }

    // The slow similarity check has been removed to optimize performance.
    // We now rely only on exact fingerprint matching.

    InstrumentInfo new_info;
    new_info.fingerprint = fp;
    new_info.name = "CustomWave_" + std::to_string(next_custom_wave_id++);
    new_info.midi_instrument = analyze_waveform(waveform_data);
    new_info.graph = generate_waveform_graph(waveform_data);
    new_info.source = source_filename;
    new_info.registered_at = get_current_timestamp();

    instruments[fp] = new_info;
    
    usage_logger.report_new_instrument(new_info);
    save();

    return new_info.midi_instrument;
}

std::string InstrumentConfig::generate_fingerprint(const std::array<uint8_t, 32>& waveform_data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (uint8_t byte : waveform_data) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

std::string InstrumentConfig::generate_waveform_graph(const std::array<uint8_t, 32>& waveform_data) {
    std::string graph;
    for (int y = 15; y >= 0; --y) {
        graph += "\n; ";
        for (int x = 0; x < 32; ++x) {
            graph += (waveform_data[x] >= y) ? "█" : " ";
        }
    }
    return graph;
}

int InstrumentConfig::analyze_waveform(const std::array<uint8_t, 32>& waveform) {
    int high_samples = 0;
    for (uint8_t sample : waveform) if (sample > 7) high_samples++;
    if (high_samples <= 4 || high_samples >= 28) return 82;
    if (high_samples <= 8 || high_samples >= 24) return 83;
    if (high_samples >= 14 && high_samples <= 18) return 80;

    int consistent_slope_count = 0;
    int last_diff = 0;
    bool first_diff = true;
    for (size_t i = 0; i < waveform.size() - 1; ++i) {
        int current_diff = static_cast<int>(waveform[i+1]) - static_cast<int>(waveform[i]);
        if (first_diff) { last_diff = current_diff; first_diff = false; }
        else { if (std::abs(current_diff - last_diff) <= 1) consistent_slope_count++; last_diff = current_diff; }
    }
    if (consistent_slope_count > 25) return 81;

    int peaks = 0, troughs = 0;
    for (size_t i = 1; i < waveform.size() - 1; ++i) {
        if (waveform[i] > waveform[i-1] && waveform[i] > waveform[i+1]) peaks++;
        if (waveform[i] < waveform[i-1] && waveform[i] < waveform[i+1]) troughs++;
    }
    if (peaks >= 1 && troughs >= 1) return 74;

    return 80;
}

bool InstrumentConfig::are_waveforms_similar(const std::array<uint8_t, 32>& wave1, const std::array<uint8_t, 32>& wave2, int threshold) {
    int different_samples = 0;
    for (size_t i = 0; i < 32; ++i) {
        if (wave1[i] != wave2[i]) {
            different_samples++;
        }
    }
    return different_samples <= threshold;
}

InstrumentInfo InstrumentConfig::get_instrument_by_fingerprint(const std::string& fingerprint) const {
    auto it = instruments.find(fingerprint);
    if (it != instruments.end()) {
        return it->second;
    }
    return InstrumentInfo{}; // Return an empty info if not found
}
