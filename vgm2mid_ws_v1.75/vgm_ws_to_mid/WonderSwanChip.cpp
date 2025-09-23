#include "WonderSwanChip.h"
#include <algorithm> // For std::max
#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>

// Conversion factor from VGM samples (at 44100 Hz) to MIDI ticks (at 480 PPQN, 120 BPM)
const double SAMPLES_TO_TICKS = (480.0 * 120.0) / (44100.0 * 60.0);

WonderSwanChip::WonderSwanChip(MidiWriter& midi_writer)
    : midi_writer(midi_writer),
      io_ram(256, 0),
      channel_periods(4, 0),
      channel_volumes_left(4, 0),
    channel_volumes_right(4, 0),
    channel_enabled(4, false),
    channel_is_active(4, false),
    channel_last_note(4, 0),
    channel_last_velocity(4, -1),
      channel_last_pan(4, -1),
      current_sample_time(0),
      channel_last_tick_time(4, 0) {
    log_file.open("vgm_ws_to_mid/debug_output.txt", std::ios::out | std::ios::trunc);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open vgm_ws_to_mid/debug_output.txt for writing." << std::endl;
    }

    // Create a track for each of the 4 pulse channels
    for (int i = 0; i < 4; ++i) {
        midi_writer.add_track();
        MidiTrack& track = midi_writer.get_track(i);
        // Set default instrument to Square Wave (GM 81)
        track.add_program_change(0, i, 80);
        // Initialize channel volume and expression to full. This prevents
        // a volume dip at the start if the synth's default is not 127.
        track.add_control_change(0, i, 7, 127); // CC7 Main Volume
        track.add_control_change(0, i, 11, 127); // CC11 Expression
    }
}

WonderSwanChip::~WonderSwanChip() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

void WonderSwanChip::advance_time(uint16_t samples) {
    // Before advancing time, process the state of all channels based on the register values set since the last time delay.
    // This ensures that all events at the current time tick are generated before moving to the next.
    for (int i = 0; i < 4; ++i) {
        check_state_and_update_midi(i);
    }
    current_sample_time += samples;
}

void WonderSwanChip::check_state_and_update_midi(int channel) {
    uint32_t current_tick = static_cast<uint32_t>(current_sample_time * SAMPLES_TO_TICKS);
    bool is_active = channel_is_active[channel];
    bool should_be_on = channel_enabled[channel] && (channel_volumes_left[channel] > 0 || channel_volumes_right[channel] > 0);
    int current_note_pitch = period_to_midi_note(channel_periods[channel]);

    // A note can't be on if it has no pitch
    if (should_be_on && current_note_pitch == 0) {
        should_be_on = false;
    }

    log_file << "Tick: " << std::setw(6) << current_tick
             << " | Chan: " << channel
             << " | Period: " << std::setw(4) << channel_periods[channel]
             << " | Pitch: " << std::setw(3) << current_note_pitch
             << " | Vol L: " << std::setw(2) << channel_volumes_left[channel]
             << " | Vol R: " << std::setw(2) << channel_volumes_right[channel]
             << " | Enabled: " << channel_enabled[channel]
             << " | ShouldBeOn: " << should_be_on
             << " | IsActive: " << is_active;

    uint32_t delta_time = current_tick - channel_last_tick_time[channel];
    MidiTrack& track = midi_writer.get_track(channel);

    // Case 1: Note is currently ON
    if (is_active) {
        // Case 1a: Note should turn OFF
        if (!should_be_on) {
            log_file << " | Action: Note Off (Pitch: " << channel_last_note[channel] << ")" << std::endl;
            track.add_note_off(delta_time, channel, channel_last_note[channel]);
            channel_is_active[channel] = false;
            channel_last_tick_time[channel] = current_tick;
        }
        // Case 1b: Note changes pitch (slide)
        else if (current_note_pitch != channel_last_note[channel]) {
            log_file << " | Action: Pitch Change (Off: " << channel_last_note[channel] << ")" << std::endl;
            track.add_note_off(delta_time, channel, channel_last_note[channel]);
            // Note On for the new pitch will be handled below, with delta_time = 0
            channel_last_tick_time[channel] = current_tick;
            
            // Fall-through to the "should_be_on" block to immediately start the new note
            is_active = false; // Pretend the note is off so the next block will fire
        }
        // Case 1c: Note sustains, check for volume/pan changes
        else {
            int left_vol = channel_volumes_left[channel];
            int right_vol = channel_volumes_right[channel];
            double vgm_vol = std::max(left_vol, right_vol);
            double normalized_vol = vgm_vol / 15.0;
            double curved_vol = pow(normalized_vol, 0.5);
            int expression_vol = static_cast<int>(curved_vol * 127.0);
            if (expression_vol > 127) expression_vol = 127;

            int pan = 64;
            int total_vol = left_vol + right_vol;
            if (total_vol > 0) {
                pan = static_cast<int>((static_cast<double>(right_vol) / total_vol) * 127.0);
            }
            if (pan < 0) pan = 0;
            if (pan > 127) pan = 127;

            bool event_sent = false;
            if (expression_vol != channel_last_velocity[channel]) {
                log_file << " | Action: Volume Change " << channel_last_velocity[channel] << " -> " << expression_vol << std::endl;
                track.add_control_change(delta_time, channel, 11, expression_vol); // CC 11 is Expression
                channel_last_velocity[channel] = expression_vol;
                delta_time = 0;
                event_sent = true;
            }
            if (pan != channel_last_pan[channel]) {
                log_file << " | Action: Pan Change " << channel_last_pan[channel] << " -> " << pan << std::endl;
                track.add_control_change(delta_time, channel, 10, pan); // CC 10 is Pan
                channel_last_pan[channel] = pan;
                delta_time = 0;
                event_sent = true;
            }

            if (event_sent) {
                channel_last_tick_time[channel] = current_tick;
            } else {
                log_file << " | Action: Sustain (No Change)" << std::endl;
            }
        }
    }

    // Case 2: Note is currently OFF but should be ON
    if (!is_active && should_be_on) {
        delta_time = current_tick - channel_last_tick_time[channel]; // Recalculate delta time in case a note-off just happened

        int left_vol = channel_volumes_left[channel];
        int right_vol = channel_volumes_right[channel];
        double vgm_vol = std::max(left_vol, right_vol);
        double normalized_vol = vgm_vol / 15.0;
        double curved_vol = pow(normalized_vol, 0.5);
        int expression_vol = static_cast<int>(curved_vol * 127.0);
        if (expression_vol > 127) expression_vol = 127;

        int pan = 64;
        int total_vol = left_vol + right_vol;
        if (total_vol > 0) {
            pan = static_cast<int>((static_cast<double>(right_vol) / total_vol) * 127.0);
        }
        if (pan < 0) pan = 0;
        if (pan > 127) pan = 127;

        log_file << " | Action: Note On (Pitch: " << current_note_pitch << ", Expr: " << expression_vol << ")" << std::endl;

        // Set pan before note on, if it has changed
        if (pan != channel_last_pan[channel]) {
            track.add_control_change(delta_time, channel, 10, pan);
            delta_time = 0;
        }
        
        // Set the expression level first.
        if (expression_vol != channel_last_velocity[channel]) {
            track.add_control_change(delta_time, channel, 11, expression_vol);
            delta_time = 0;
        }

        // Then, send the Note On event. Using a fixed high velocity gives a consistent attack.
        // The actual audible volume is controlled by the CC11 message sent just before.
        track.add_note_on(delta_time, channel, current_note_pitch, 127);

        channel_is_active[channel] = true;
        channel_last_note[channel] = current_note_pitch;
        channel_last_velocity[channel] = expression_vol; // We track the expression volume
        channel_last_pan[channel] = pan;
        channel_last_tick_time[channel] = current_tick;
    }
    // Case 3: Note is OFF and should stay OFF
    else if (!is_active && !should_be_on) {
        log_file << " | Action: Silence" << std::endl;
    }
}

void WonderSwanChip::write_port(uint8_t port, uint8_t value) {
    uint8_t addr = port + 0x80;
    io_ram[addr] = value;

    // In this new logic, we only update the chip's internal state.
    // The check_state_and_update_midi() calls are removed from here
    // and are now consolidated in advance_time() to prevent race conditions.
    switch (addr) {
        case 0x80: case 0x81:
            channel_periods[0] = ((io_ram[0x81] & 0x07) << 8) | io_ram[0x80];
            break;
        case 0x82: case 0x83:
            channel_periods[1] = ((io_ram[0x83] & 0x07) << 8) | io_ram[0x82];
            break;
        case 0x84: case 0x85:
            channel_periods[2] = ((io_ram[0x85] & 0x07) << 8) | io_ram[0x84];
            break;
        case 0x86: case 0x87:
            channel_periods[3] = ((io_ram[0x87] & 0x07) << 8) | io_ram[0x86];
            break;
        case 0x88:
            channel_volumes_left[0] = (io_ram[0x88] >> 4) & 0x0F;
            channel_volumes_right[0] = io_ram[0x88] & 0x0F;
            break;
        case 0x89:
            channel_volumes_left[1] = (io_ram[0x89] >> 4) & 0x0F;
            channel_volumes_right[1] = io_ram[0x89] & 0x0F;
            break;
        case 0x8A:
            channel_volumes_left[2] = (io_ram[0x8A] >> 4) & 0x0F;
            channel_volumes_right[2] = io_ram[0x8A] & 0x0F;
            break;
        case 0x8B:
            channel_volumes_left[3] = (io_ram[0x8B] >> 4) & 0x0F;
            channel_volumes_right[3] = io_ram[0x8B] & 0x0F;
            break;
        case 0x90: {
            // This register acts as a Key On/Off trigger.
            // We just update the enabled status. The state check will happen later.
            for (int i = 0; i < 4; ++i) {
                channel_enabled[i] = (value & (1 << i)) != 0;
            }
            break;
        }
        // Other registers like 0x91 (panning) are just stored.
        // Their effect is evaluated in check_state_and_update_midi.
    }
}

void WonderSwanChip::finalize() {
    // This function is called at the very end of the VGM file processing.
    // Its only job is to ensure that any notes still considered "on" are properly turned off.
    log_file << "Finalizing MIDI data..." << std::endl;
    uint32_t final_tick = static_cast<uint32_t>(current_sample_time * SAMPLES_TO_TICKS);

    for (int i = 0; i < 4; ++i) {
        if (channel_is_active[i]) {
            uint32_t delta_time = final_tick - channel_last_tick_time[i];
            log_file << "Tick: " << std::setw(6) << final_tick
                     << " | Chan: " << i
                     << " | Action: Final Note Off (Pitch: " << channel_last_note[i] << ")" << std::endl;
            midi_writer.get_track(i).add_note_off(delta_time, i, channel_last_note[i]);
        }
    }
}

size_t WonderSwanChip::get_channel_count() const {
    return channel_periods.size();
}

int WonderSwanChip::period_to_midi_note(int period) {
    if (period >= 2048) return 0;

    double freq = (3072000.0 / (2048.0 - period)) / 32.0;
    if (freq <= 0) return 0;
    
    // The pitch is now at its original calculated octave.
    int note = static_cast<int>(round(69 + 12 * log2(freq / 440.0)));
    
    return note;
}
