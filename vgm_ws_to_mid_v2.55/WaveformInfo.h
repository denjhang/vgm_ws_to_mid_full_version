#ifndef WAVEFORM_INFO_H
#define WAVEFORM_INFO_H

#include <vector>
#include <string>
#include <cstdint>
#include <map>

// --- Pre-defined Waveform Data (Declarations) ---
extern const std::vector<uint8_t> PULSE_WAVE_DATA;
extern const std::vector<uint8_t> NOISE_WAVE_DATA;
extern const std::vector<uint8_t> BUILTIN_WAVE_1_DATA;
extern const std::vector<uint8_t> BUILTIN_WAVE_2_DATA;
extern const std::vector<uint8_t> BUILTIN_WAVE_3_DATA;
extern const std::vector<uint8_t> PCM_WAVE_DATA;

// --- Default Instrument Definitions ---
struct DefaultInstrument {
    int id;
    const std::vector<uint8_t>& wave_data;
    std::string description;
};

const std::map<std::string, DefaultInstrument> DEFAULT_INSTRUMENTS = {
    {"PULSE",          {80,  PULSE_WAVE_DATA,     "Pulse Wave"}},
    {"NOISE",          {127, NOISE_WAVE_DATA,     "Noise Channel"}},
    {"WAVE_BUILTIN_1", {84,  BUILTIN_WAVE_1_DATA, "Built-in Waveform (Triangle)"}},
    {"WAVE_BUILTIN_2", {28,  BUILTIN_WAVE_2_DATA, "Built-in Waveform (Sine-like)"}},
    {"WAVE_BUILTIN_3", {26,  BUILTIN_WAVE_3_DATA, "Built-in Waveform (Sawtooth)"}}
};

// --- Helper Function (Declaration) ---
std::string generate_waveform_graph(const std::vector<uint8_t>& waveform);

#endif // WAVEFORM_INFO_H
