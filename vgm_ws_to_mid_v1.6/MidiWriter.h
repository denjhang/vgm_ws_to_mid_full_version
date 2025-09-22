#ifndef MIDI_WRITER_H
#define MIDI_WRITER_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <numeric>

struct MidiEvent {
    uint32_t absolute_time;
    std::vector<uint8_t> event_data;
};

// Represents a single MIDI track
class MidiTrack {
public:
    MidiTrack();

    void add_event(uint32_t delta_time, const std::vector<uint8_t>& event_data);
    void add_note_on(uint32_t delta_time, uint8_t channel, uint8_t note, uint8_t velocity);
    void add_note_off(uint32_t delta_time, uint8_t channel, uint8_t note);
    void add_program_change(uint32_t delta_time, uint8_t channel, uint8_t program);
    void add_control_change(uint32_t delta_time, uint8_t channel, uint8_t controller, uint8_t value);
    void add_meta_event(uint32_t delta_time, uint8_t type, const std::vector<uint8_t>& data);
    void add_tempo_change(uint32_t delta_time, uint32_t tempo);
    
    void copy_events_from(const MidiTrack& source_track, uint32_t start_time, uint32_t end_time);
    uint32_t get_current_time() const;

    std::vector<uint8_t> get_track_data() const;

private:
    void write_variable_length(std::vector<uint8_t>& buffer, uint32_t value) const;

    std::vector<MidiEvent> events;
    uint32_t current_time = 0;
    uint8_t last_status_byte = 0;
};

// Main class to write a Format 1 MIDI file
class MidiWriter {
public:
    MidiWriter(uint16_t ticks_per_quarter_note);

    // Get a reference to a track to add events to it
    MidiTrack& get_track(size_t index);

    // Add a new track, returns the index of the new track
    size_t add_track();

    // Write the final MIDI file to the specified path
    bool write_to_file(const std::string& path);

private:
    uint16_t ticks_per_quarter_note;
    std::vector<MidiTrack> tracks;
};

#endif // MIDI_WRITER_H
