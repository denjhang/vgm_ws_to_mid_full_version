#include "WonderSwanChip.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <array>

const double SAMPLES_TO_TICKS = (480.0 * 120.0) / (44100.0 * 60.0);

WonderSwanChip::WonderSwanChip(MidiWriter& midi_writer, InstrumentConfig& config, UsageLogger& logger, const std::string& source_filename)
    : midi_writer(midi_writer),
      config(config),
      usage_logger(logger),
      source_filename(source_filename),
      io_ram(0x100, 0),
      internal_ram(0x4000, 0),
      channel_periods(4, 0),
      channel_volumes_left(4, 0),
      channel_volumes_right(4, 0),
      channel_enabled(4, false),
      channel_is_active(4, false),
      channel_last_note(4, 0),
      channel_last_velocity(4, -1),
      channel_last_pan(4, -1),
      channel_instrument(4, -1),
      channel_is_noise(4, false),
      channel_last_pitch_bend(4, -1),
      channel_base_note_freq(4, 0.0),
      current_sample_time(0),
      channel_last_tick_time(4, 0) {
    
    sweep_step = 0;
    sweep_time = 0;
    sweep_count = 0;
    noise_type = 0;
    noise_reset = false;
    pcm_volume_left = 0;
    pcm_volume_right = 0;

    for (int i = 0; i < 4; ++i) {
        midi_writer.add_track();
        MidiTrack& track = midi_writer.get_track(i);
        // Set a default instrument (e.g., 80: Square Lead) initially.
        // This will be overridden by the instrument config logic.
        track.add_program_change(0, i, 80); 
        channel_instrument[i] = 80;
        track.add_control_change(0, i, 7, 127); // Main Volume
        track.add_control_change(0, i, 11, 127); // Expression

        track.add_control_change(0, i, 11, 127); // Expression

        // Set RPN for pitch bend range (+/- 2 semitones)
        track.add_control_change(0, i, 101, 0); // RPN MSB
        track.add_control_change(0, i, 100, 0); // RPN LSB
        track.add_control_change(0, i, 6, static_cast<uint8_t>(channel_pitch_bend_range_semitones)); // Data Entry MSB
        track.add_control_change(0, i, 38, 0); // Data Entry LSB
    }
}

WonderSwanChip::~WonderSwanChip() {
    flush_log();
}

void WonderSwanChip::advance_time(uint16_t samples) {
    process_s_dma(samples);
    process_sweep(samples);
    for (int i = 0; i < 4; ++i) {
        check_state_and_update_midi(i);
    }
    current_sample_time += samples;
}

void WonderSwanChip::check_state_and_update_midi(int channel) {
    uint32_t current_tick = static_cast<uint32_t>(current_sample_time * SAMPLES_TO_TICKS);
    MidiTrack& track = midi_writer.get_track(channel);

    int target_instrument = -1;
    std::string waveform_fingerprint = "default";

    // Determine the active sound type for the channel
    bool is_pcm = (channel == 1 && (io_ram[0x90] & 0x20) != 0);
    bool is_noise = (channel == 3 && (io_ram[0x90] & 0x80) != 0);
    bool is_wave = !is_pcm && !is_noise && ((io_ram[0x90] & (1 << channel)) != 0);

    if (is_pcm) {
        target_instrument = 119;
        waveform_fingerprint = "PCM_SOUND";
    } else if (is_noise) {
        target_instrument = 127;
        waveform_fingerprint = "NOISE_SOUND";
    } else if (is_wave) {
        uint16_t wave_base_addr = (io_ram[0x8f] << 6) + (channel * 16);
        std::array<uint8_t, 32> current_waveform_data;
        for (int i = 0; i < 16; ++i) {
            uint8_t val = internal_ram[(wave_base_addr + i) & 0x3FFF];
            current_waveform_data[i * 2] = val & 0x0F;
            current_waveform_data[i * 2 + 1] = (val >> 4) & 0x0F;
        }
        target_instrument = config.find_or_create_instrument(current_waveform_data, source_filename);
        
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for(uint8_t byte : current_waveform_data) {
            ss << std::setw(2) << static_cast<int>(byte);
        }
        waveform_fingerprint = ss.str();
    } else {
        target_instrument = 80;
        waveform_fingerprint = "PULSE_WAVE";
    }

    if (target_instrument != -1 && target_instrument != channel_instrument[channel]) {
        uint32_t delta_time = current_tick - channel_last_tick_time[channel];
        track.add_program_change(delta_time, channel, target_instrument);
        channel_instrument[channel] = target_instrument;
        channel_last_tick_time[channel] = current_tick;
    }

    bool is_active = channel_is_active[channel];
    bool should_be_on = channel_enabled[channel] && (channel_volumes_left[channel] > 0 || channel_volumes_right[channel] > 0);
    
    int current_note_pitch = period_to_midi_note(channel_periods[channel]);
    if (should_be_on && current_note_pitch == 0) {
        should_be_on = false;
    }
    
    if (is_pcm) {
        current_note_pitch = 60 + (io_ram[0x89] & 0x0F);
        should_be_on = (io_ram[0x90] & 0x20) != 0 && (pcm_volume_left > 0 || pcm_volume_right > 0);
    }

    uint32_t delta_time = current_tick - channel_last_tick_time[channel];

    // --- Note Off Logic ---
    if (is_active && !should_be_on) {
        track.add_note_off(delta_time, channel, channel_last_note[channel]);
        channel_is_active[channel] = false;
        channel_base_note_freq[channel] = 0.0;
        channel_last_tick_time[channel] = current_tick;
        delta_time = 0;
    }

    // --- Note On Logic ---
    if (!is_active && should_be_on) {
        start_new_note(channel, current_note_pitch, waveform_fingerprint);
    }
    // --- Continuous Updates (Volume, Pan, Pitch Bend) ---
    else if (is_active && should_be_on) {
        bool event_sent = false;
        
        // Volume and Pan
        int left_vol = is_pcm ? pcm_volume_left : channel_volumes_left[channel];
        int right_vol = is_pcm ? pcm_volume_right : channel_volumes_right[channel];
        double vgm_vol = std::max(left_vol, right_vol);
        int expression_vol = static_cast<int>((vgm_vol / 15.0) * 127.0);
        int pan = 64;
        int total_vol = left_vol + right_vol;
        if (total_vol > 0) pan = static_cast<int>((static_cast<double>(right_vol) / total_vol) * 127.0);

        if (expression_vol != channel_last_velocity[channel]) {
            track.add_control_change(delta_time, channel, 11, expression_vol);
            channel_last_velocity[channel] = expression_vol;
            delta_time = 0;
            event_sent = true;
        }
        if (pan != channel_last_pan[channel]) {
            track.add_control_change(delta_time, channel, 10, pan);
            channel_last_pan[channel] = pan;
            delta_time = 0;
            event_sent = true;
        }

        // Pitch Bend
        double base_freq = channel_base_note_freq[channel];
        double current_freq = period_to_freq(channel_periods[channel]);
        if (base_freq > 0 && current_freq > 0) {
            double cents_deviation = 1200.0 * log2(current_freq / base_freq);

            if (std::abs(cents_deviation) > channel_pitch_bend_range_semitones * 100.0) {
                // Deviation is too large, treat as a new note
                track.add_note_off(delta_time, channel, channel_last_note[channel]);
                channel_last_tick_time[channel] = current_tick;
                start_new_note(channel, current_note_pitch, waveform_fingerprint);
                event_sent = true; // start_new_note updates the time
            } else {
                double bend_fraction = cents_deviation / (channel_pitch_bend_range_semitones * 100.0);
                int pitch_bend_value = 8192 + static_cast<int>(bend_fraction * 8191.0);
                pitch_bend_value = std::max(0, std::min(16383, pitch_bend_value));

                if (pitch_bend_value != channel_last_pitch_bend[channel]) {
                    track.add_pitch_bend(delta_time, channel, pitch_bend_value);
                    channel_last_pitch_bend[channel] = pitch_bend_value;
                    delta_time = 0;
                    event_sent = true;
                }
            }
        }

        if (event_sent) {
            channel_last_tick_time[channel] = current_tick;
        }
    }
}

void WonderSwanChip::write_ram(uint16_t address, uint8_t value) {
    uint16_t masked_address = address & 0x3FFF;
    if (masked_address < internal_ram.size()) {
        internal_ram[masked_address] = value;
    }
}

void WonderSwanChip::write_port(uint8_t port, uint8_t value) {
    io_ram[port] = value;
    uint16_t period;
    switch (port) {
        case 0x80: case 0x81: 
            period = ((io_ram[0x81] & 0x07) << 8) | io_ram[0x80];
            channel_periods[0] = (period == 0x7FF) ? 2048 : period;
            break;
        case 0x82: case 0x83: 
            period = ((io_ram[0x83] & 0x07) << 8) | io_ram[0x82];
            channel_periods[1] = (period == 0x7FF) ? 2048 : period;
            break;
        case 0x84: case 0x85: 
            period = ((io_ram[0x85] & 0x07) << 8) | io_ram[0x84];
            channel_periods[2] = (period == 0x7FF) ? 2048 : period;
            break;
        case 0x86: case 0x87: 
            period = ((io_ram[0x87] & 0x07) << 8) | io_ram[0x86];
            channel_periods[3] = (period == 0x7FF) ? 2048 : period;
            break;
        case 0x88: channel_volumes_left[0] = (io_ram[0x88] >> 4) & 0x0F; channel_volumes_right[0] = io_ram[0x88] & 0x0F; break;
        case 0x89: channel_volumes_left[1] = (io_ram[0x89] >> 4) & 0x0F; channel_volumes_right[1] = io_ram[0x89] & 0x0F; break;
        case 0x8A: channel_volumes_left[2] = (io_ram[0x8A] >> 4) & 0x0F; channel_volumes_right[2] = io_ram[0x8A] & 0x0F; break;
        case 0x8B: channel_volumes_left[3] = (io_ram[0x8B] >> 4) & 0x0F; channel_volumes_right[3] = io_ram[0x8B] & 0x0F; break;
        case 0x8C: sweep_step = static_cast<int8_t>(value); break;
        case 0x8D:
        {
            const double master_clock = 3072000.0;
            const double hblank_rate = master_clock / 256.0;
            const double sample_rate = 44100.0;
            double sweep_interval_in_sec = (32.0 * (value + 1.0)) / hblank_rate;
            sweep_time = static_cast<int16_t>(sweep_interval_in_sec * sample_rate);
            sweep_count = sweep_time;
            break;
        }
        case 0x8E:
            noise_type = value & 0x07;
            if (value & 0x08) noise_reset = true;
            break;
        case 0x90:
            for (int i = 0; i < 4; ++i) channel_enabled[i] = (value & (1 << i)) != 0;
            break;
        case 0x91:
            io_ram[0x91] |= 0x80;
            break;
        case 0x94:
            pcm_volume_left = ((value & 0x0C) >> 2) * 5;
            pcm_volume_right = (value & 0x03) * 5;
            break;
        // Sound DMA
        case 0x4A: case 0x4B: case 0x4C:
            s_dma_source_addr = (io_ram[0x4C] << 16) | (io_ram[0x4B] << 8) | io_ram[0x4A];
            break;
        case 0x4E: case 0x4F:
            s_dma_count = (io_ram[0x4F] << 8) | io_ram[0x4E];
            break;
        case 0x52: {
            static const uint32_t dma_cycles[4] = { 256, 192, 154, 128 };
            const double master_clock = 3072000.0;
            const double sample_rate = 44100.0;
            if ((value & 0x80) != 0) {
                s_dma_period = static_cast<uint32_t>((dma_cycles[value & 0x03] / master_clock) * sample_rate);
                if (s_dma_period == 0) s_dma_period = 1;
                s_dma_timer = s_dma_period;
            }
            break;
        }
    }
}

void WonderSwanChip::process_s_dma(uint32_t samples) {
    if (s_dma_period == 0 || s_dma_count == 0) return;
    s_dma_timer -= samples;
    while (s_dma_timer <= 0) {
        uint8_t pcm_data = internal_ram[s_dma_source_addr & 0x3FFF];
        write_port(0x89, pcm_data);
        s_dma_source_addr++;
        s_dma_count--;
        if (s_dma_count == 0) {
            s_dma_period = 0;
            io_ram[0x52] &= ~0x80;
            break;
        }
        s_dma_timer += s_dma_period;
    }
}

void WonderSwanChip::process_sweep(uint32_t samples) {
    if (sweep_step != 0 && (io_ram[0x90] & 0x40)) {
        sweep_count -= samples;
        while (sweep_count <= 0) {
            if (sweep_time > 0) sweep_count += sweep_time;
            else break;
            
            uint16_t current_period = channel_periods[2];
            current_period += sweep_step;
            current_period &= 0x7FF;
            
            io_ram[0x84] = current_period & 0xFF;
            io_ram[0x85] = (io_ram[0x85] & 0xF8) | ((current_period >> 8) & 0x07);
            channel_periods[2] = current_period;
        }
    }
}

void WonderSwanChip::finalize() {
    uint32_t final_tick = static_cast<uint32_t>(current_sample_time * SAMPLES_TO_TICKS);
    for (int i = 0; i < 4; ++i) {
        if (channel_is_active[i]) {
            uint32_t delta_time = final_tick - channel_last_tick_time[i];
            midi_writer.get_track(i).add_note_off(delta_time, i, channel_last_note[i]);
        }
    }
}

void WonderSwanChip::flush_log() {
    usage_logger.write_log(source_filename, config, usage_data);
}

size_t WonderSwanChip::get_channel_count() const {
    return channel_periods.size();
}

const std::map<int, std::map<std::string, int>>& WonderSwanChip::get_usage_data() const {
    return usage_data;
}

double WonderSwanChip::period_to_freq(int period) {
    if (period >= 2048) return 0.0;
    return (3072000.0 / (2048.0 - period)) / 32.0;
}

int WonderSwanChip::period_to_midi_note(int period) {
    double freq = period_to_freq(period);
    if (freq <= 0) return 0;
    int note = static_cast<int>(round(69 + 12 * log2(freq / 440.0)));
    return note > 127 ? 127 : note;
}

void WonderSwanChip::start_new_note(int channel, int note_pitch, const std::string& waveform_fingerprint) {
    uint32_t current_tick = static_cast<uint32_t>(current_sample_time * SAMPLES_TO_TICKS);
    uint32_t delta_time = current_tick - channel_last_tick_time[channel];
    MidiTrack& track = midi_writer.get_track(channel);

    usage_data[channel][waveform_fingerprint]++;

    bool is_pcm = (channel == 1 && (io_ram[0x90] & 0x20) != 0);
    int left_vol = is_pcm ? pcm_volume_left : channel_volumes_left[channel];
    int right_vol = is_pcm ? pcm_volume_right : channel_volumes_right[channel];
    double vgm_vol = std::max(left_vol, right_vol);
    int expression_vol = static_cast<int>((vgm_vol / 15.0) * 127.0);
    int pan = 64;
    int total_vol = left_vol + right_vol;
    if (total_vol > 0) pan = static_cast<int>((static_cast<double>(right_vol) / total_vol) * 127.0);

    if (pan != channel_last_pan[channel]) {
        track.add_control_change(delta_time, channel, 10, pan);
        delta_time = 0;
    }
    if (expression_vol != channel_last_velocity[channel]) {
        track.add_control_change(delta_time, channel, 11, expression_vol);
        delta_time = 0;
    }
    
    if (channel_last_pitch_bend[channel] != 8192) {
        track.add_pitch_bend(delta_time, channel, 8192);
        channel_last_pitch_bend[channel] = 8192;
        delta_time = 0;
    }

    track.add_note_on(delta_time, channel, note_pitch, 127);
    channel_is_active[channel] = true;
    channel_last_note[channel] = note_pitch;
    channel_base_note_freq[channel] = period_to_freq(channel_periods[channel]);
    channel_last_velocity[channel] = expression_vol;
    channel_last_pan[channel] = pan;
    channel_last_tick_time[channel] = current_tick;
}
