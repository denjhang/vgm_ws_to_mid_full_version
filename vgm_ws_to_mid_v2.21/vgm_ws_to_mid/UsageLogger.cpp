#include "UsageLogger.h"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

UsageLogger::UsageLogger(const std::string& filename) : filename(filename) {}

void UsageLogger::report_new_instrument(const InstrumentInfo& info) {
    new_instruments_reported.push_back(info);
}

void UsageLogger::write_log(const std::string& vgm_filename,
                              const std::map<int, std::map<std::string, int>>& usage_data) {
    std::ofstream outfile(filename); // Overwrite file instead of appending
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not open log file for writing: " << filename << std::endl;
        return;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    outfile << "--- Conversion Log ---" << std::endl;
    outfile << "Timestamp: " << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %X") << std::endl;
    outfile << "Source File: " << vgm_filename << std::endl;
    outfile << std::endl;

    if (!new_instruments_reported.empty()) {
        outfile << "New Waveforms Registered:" << std::endl;
        for (const auto& info : new_instruments_reported) {
            outfile << "  - " << info.name << " (Fingerprint: " << info.fingerprint << ")" << std::endl;
        }
        outfile << std::endl;
    }
    
    if (usage_data.empty()) {
        outfile << "Waveform Usage: None" << std::endl;
    } else {
        outfile << "Waveform Usage by Channel:" << std::endl;
        
        // Sort channels by key for consistent output
        std::vector<int> channels;
        for(const auto& pair : usage_data) {
            channels.push_back(pair.first);
        }
        std::sort(channels.begin(), channels.end());

        for (int channel : channels) {
            const auto& wave_map = usage_data.at(channel);
            outfile << "  Channel " << channel << ":" << std::endl;
            for (const auto& wave_pair : wave_map) {
                outfile << "    - " << wave_pair.first 
                        << " (" << wave_pair.second << " times)" << std::endl;
            }
        }
    }
    outfile << std::endl;
}
