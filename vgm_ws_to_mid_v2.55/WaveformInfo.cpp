#include "WaveformInfo.h"
#include <cmath>

// --- Pre-defined Waveform Data (Definitions) ---
const std::vector<uint8_t> PULSE_WAVE_DATA = {15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
const std::vector<uint8_t> NOISE_WAVE_DATA = {8, 2, 15, 5, 12, 9, 0, 7, 11, 4, 13, 1, 6, 10, 3, 14, 8, 2, 15, 5, 12, 9, 0, 7, 11, 4, 13, 1, 6, 10, 3, 14};
const std::vector<uint8_t> BUILTIN_WAVE_1_DATA = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}; // Triangle
const std::vector<uint8_t> BUILTIN_WAVE_2_DATA = {8, 10, 12, 14, 15, 15, 14, 12, 10, 8, 6, 4, 2, 1, 1, 2, 4, 6, 8, 10, 12, 14, 15, 15, 14, 12, 10, 8, 6, 4, 2, 1}; // Sine-like
const std::vector<uint8_t> BUILTIN_WAVE_3_DATA = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}; // Sawtooth
const std::vector<uint8_t> PCM_WAVE_DATA = {8}; // Placeholder for PCM channel

// --- Helper Function (Definition) ---
std::string generate_waveform_graph(const std::vector<uint8_t>& waveform) {
    std::string graph = ";       |               Waveform Shape              |\n";
    const int height = 8;
    for (int i = height; i >= 0; --i) {
        graph += ";       |";
        for (uint8_t sample : waveform) {
            int level = static_cast<int>(round(sample / 15.0 * height));
            if (level == i) {
                graph += "*";
            } else {
                graph += " ";
            }
        }
        graph += "|\n";
    }
    graph += ";       +---------------------------------+";
    return graph;
}
