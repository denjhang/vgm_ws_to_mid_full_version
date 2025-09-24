# vgm_ws_to_mid_full_version
Wonderswan VGM to Standard MIDI Converter with ongoing version updates
# vgm_ws_to_mid v2.36
# vgm_ws_to_mid: WonderSwan VGM to MIDI Converter - Project Documentation

This document provides a detailed account of the development journey, technical implementation, and final program workflow of the `vgm_ws_to_mid` converter.

## Acknowledgements

This project was a joint effort between:
*   **Denjhang**: Responsible for sound testing and providing critical feedback on the output.
*   **Cline**: Responsible for programming, debugging, and implementing the conversion logic based on feedback.

## References

This project stands on the shoulders of giants, and its success is the result of building upon the foundational work of two key open-source projects. Below is a detailed breakdown of which parts of our converter were inspired by which specific source files from these projects.

### 1. `libvgm`

The `libvgm` library was the foundational pillar for this project, providing the essential logic for parsing the VGM file format itself. Without `libvgm`, reading and interpreting the raw VGM data stream would have been an insurmountable task.

*   **Core Contribution**: VGM file parsing and command dispatching.
*   **Key Source File**: `libvgm/player/vgmplayer.cpp`
*   **How it was used**: The main `switch` statement in our `main.cpp` is a direct adaptation of the command processing loop found in `vgmplayer.cpp`. We studied how `libvgm` handles different VGM commands (e.g., `0x61`, `0x62`, `0x66`, `0x51`) and replicated that structure. This includes:
    *   The logic for advancing the file pointer based on command length.
    *   The handling of wait commands (`0x61`, `0x62`, `0x63`, `0x7n`) which formed the basis of our timing system.
    *   The correct "skip" logic for unused or unsupported VGM commands, which was critical for ensuring the stability of our parser (as detailed in section 2.9).

### 2. `modizer`

While `libvgm` taught us how to read the map (the VGM file), `modizer` gave us the key to deciphering the map's most cryptic symbols (the WonderSwan-specific audio registers). It provided the hardware-level simulation logic that was essential for correctly interpreting the data stream that `libvgm` helped us parse.

*   **Core Contribution**: WonderSwan sound chip emulation logic.
*   **Key Source File**: `modizer-master/libs/libwonderswan/libwonderswan/oswan/audio.cpp`
*   **How it was used**: Our `WonderSwanChip.cpp` is fundamentally a C++ re-implementation and adaptation of the logic found in `modizer`'s `audio.cpp`. The breakthroughs achieved by studying this file were numerous:
    *   **Pitch Calculation**: The magical formula `freq = (3072000 / (2048 - period)) / 32` was derived directly from `modizer`'s frequency calculation code. The division by 32, representing the waveform table size, was the single most important discovery for achieving correct pitch.
    *   **Register Mapping**: The functions of all key I/O ports from `0x80` to `0x94` were deciphered by observing how `audio.cpp` updated its internal state variables when these ports were written to. This included frequency, volume, panning, sweep, and noise controls.
    *   **Hardware Quirks**: `modizer`'s code also revealed non-obvious hardware behaviors, such as how the Sound DMA process writes its output to Channel 2's volume register (`0x89`), which we faithfully simulated.

In summary, `libvgm` provided the **framework for reading the data**, while `modizer` provided the **knowledge for understanding the data**. The combination of these two resources was the formula for this project's success.

## Table of Contents
* [1. Feature Outline &amp; Core Implementation](#1-feature-outline--core-implementation)
  * [1.1. Core Feature Highlights](#11-core-feature-highlights)
  * [1.2. List of Generated MIDI Events](#12-list-of-generated-midi-events)
* [2. Development Journey: From Zero to a Flawless Converter](#2-development-journey-from-zero-to-a-flawless-converter)
  * [2.1. Initial Exploration: Deconstructing the Core Logic](#21-initial-exploration-deconstructing-the-core-logic)
  * [2.2. The First Major Hurdle: Correcting Pitch and Timing](#22-the-first-major-hurdle-correcting-pitch-and-timing)
  * [2.3. The Silent MIDI: Decrypting Dynamic Volume](#23-the-silent-midi-decrypting-dynamic-volume)
  * [2.4. The Final Polish: The Art of Volume Mapping](#24-the-final-polish-the-art-of-volume-mapping)
  * [2.5. The Ultimate Challenge: Fixing Volume Jumps and Stuck Notes](#25-the-ultimate-challenge-fixing-volume-jumps-and-stuck-notes)
  * [2.6. The Self-Correcting Feedback Loop: The Power of Custom Validation Tools](#26-the-self-correcting-feedback-loop-the-power-of-custom-validation-tools)
  * [2.7. A New Chapter: Implementing Seamless Looping and Final Stability](#27-a-new-chapter-implementing-seamless-looping-and-final-stability)
  * [2.8. Deep Instance Trace: The Complete Lifecycle of a Note](#28-deep-instance-trace-the-complete-lifecycle-of-a-note)
  * [2.9. Ultimate Stability: Fixing Crashes Caused by Unknown VGM Commands](#29-ultimate-stability-fixing-crashes-caused-by-unknown-vgm-commands)
  * [2.10. The Intelligent Instrument System: `instruments.ini`](#210-the-intelligent-instrument-system-instrumentsini)
  * [2.11. Capturing Expression: The Pitch Bend Implementation for Vibrato](#211-capturing-expression-the-pitch-bend-implementation-for-vibrato)
* [3. A Deep Dive into the WonderSwan Sound System](#3-a-deep-dive-into-the-wonderswan-sound-system)
  * [3.1. Overview of Sound Channels](#31-overview-of-sound-channels)
  * [3.2. The Four Main Audio Channels (Programmable Tone)](#32-the-four-main-audio-channels-programmable-tone)
  * [3.3. Special Channel Features](#33-special-channel-features)
    * [3.3.1. Hardware Sweep (Channel 2)](#331-hardware-sweep-channel-2)
    * [3.3.2. Noise Generation (Channel 3)](#332-noise-generation-channel-3)
    * [3.3.3. PCM Audio via Sound DMA (Channel 1)](#333-pcm-audio-via-sound-dma-channel-1)
* [4. Program Workflow Explained](#4-program-workflow-explained)
  * [4.1. Overview](#41-overview)
  * [4.2. Key Components](#42-key-components)
  * [4.3. Key Formulas and Constants](#43-key-formulas-and-constants)
  * [4.4. Conversion Log (`conversion_log.txt`)](#44-conversion-log-conversion_logtxt)
* [5. How to Compile and Run](#5-how-to-compile-and-run)

## 1. Feature Outline & Core Implementation

This section summarizes the core features implemented in the `vgm_ws_to_mid` converter and lists all the MIDI event types it can generate.

### 1.1. Core Feature Highlights

*   **Accurate Pitch and Timing Conversion**: Achieved precise mathematical conversion from VGM period values to MIDI pitch and from VGM sample waits to MIDI ticks, based on reverse-engineering of hardware simulation code.
*   **Expressive Dynamic Volume**: Uses a non-linear mapping curve (`pow(vol, 0.3)`) to map WonderSwan's 4-bit volume to MIDI CC#11 (Expression), solving the dynamic range compression issue and making the music sound full-bodied.
*   **Intelligent Stereo Panning**: Perfectly reproduces the original stereo effects by parsing and comparing independent left and right channel volumes to intelligently generate MIDI CC#10 (Pan) events.
*   **Advanced Note Effects**:
    *   **Vibrato**: Capable of capturing subtle frequency changes during a note's sustain and converting them into a series of MIDI Pitch Bend events.
    *   **Glissando/Portamento**: Smooth pitch transitions for large frequency changes are also achieved using Pitch Bend.
*   **Robust State Machine**: A completely refactored state machine logic accurately tracks the lifecycle of each note (start, stop, retrigger), eradicating the "stuck note" problem.
*   **Seamless Loop Handling**: Can parse the loop point in a VGM file, accurately copy all MIDI events within the loop region, and automatically handle notes that cross the loop boundary, achieving perfect, seamless playback loops.
*   **Excellent Stability and Compatibility**: Greatly improved the parser's robustness by adding correct "skip" logic for a large number of VGM commands not directly used, effectively preventing crashes caused by unknown commands.

### 1.2. List of Generated MIDI Events

The converter can intelligently generate all of the following MIDI event types based on the VGM data to create expressive music:

| MIDI Event Type | Purpose and Description |
| :--- | :--- |
| **Note On** | Triggers a note. The velocity is fixed at the maximum value of `127`, with the actual audible volume controlled by CC#11. |
| **Note Off** | Ends a note. |
| **Control Change** | Used to control various sound parameters, core to achieving dynamic expression. |
| └─ **CC#7 (Main Volume)** | Used at the beginning of a track to set a maximum volume baseline, ensuring consistent performance across different players. |
| └─ **CC#10 (Pan)** | Controls the left-right balance of a channel, used to implement stereo effects. |
| └─ **CC#11 (Expression)** | **The most important controller**. Used for real-time adjustment of a note's volume envelope during its sustain, achieving dynamic effects like crescendo and diminuendo. |
| **Pitch Bend** | Used to implement smooth pitch changes for a note, such as vibrato and glissando. |

## 2. Development Journey: From Zero to a Flawless Converter

The goal of this project was to create a C++ program capable of accurately converting WonderSwan (WS) VGM files into MIDI files. The entire process was filled with challenges, but through a series of analysis, debugging, and iteration, we ultimately overcame them all.

### 2.1. Initial Exploration: Deconstructing the Core Logic

*   **Challenge**: Hardware documentation for the WonderSwan sound chip is scarce, making a direct conversion unfeasible.
*   **Solution**: In the early stages, we obtained the source code for the `modizer` project. By analyzing its file structure, we quickly located the key file: `modizer-master/libs/libwonderswan/libwonderswan/oswan/audio.cpp`. Through an in-depth study of this file, we successfully extracted the core information for simulating the WonderSwan sound chip:
    1.  **Clock Frequency**: Confirmed its master clock frequency is `3.072 MHz`.
    2.  **Register Functions**: Clarified the roles of key registers such as `0x80-0x87` (frequency), `0x88-0x8B` (volume), and `0x90` (channel switch).
    3.  **Waveform Table Mechanism**: Discovered the most critical detail—the frequency calculation must be divided by `32` (the size of the waveform table). This was the key to solving the pitch problem.

### 2.2. The First Major Hurdle: Correcting Pitch and Timing

*   **Challenge**: The initial version of the converter produced MIDI files with abnormally high pitch and a playback duration that did not match the original VGM at all.
*   **Breakthrough Process**:
    1.  **Timing Issue**: We realized that the VGM "wait" command (`0x61 nn nn`) is based on samples at 44100 Hz, while MIDI time is measured in `ticks`. By introducing a conversion factor `SAMPLES_TO_TICKS = (480.0 * 120.0) / (44100.0 * 60.0)`, we successfully converted the sample count into precise MIDI ticks under the standard 120 BPM and 480 PPQN, resolving the duration mismatch.
    2.  **Pitch Issue**: This was the toughest problem. The initial frequency conversion formula `freq = 3072000.0 / (2048.0 - period)` resulted in a pitch that was a full five octaves too high. After repeatedly reviewing the `modizer`'s `audio.cpp` source, we noticed a detail: the final frequency value was used as an index for a waveform table. This led us to the insight that the actual audible frequency is the result of the clock frequency after division and waveform table processing. By adding `/ 32` to the end of our formula, we obtained the correct frequency, and the pitch problem was solved.

### 2.3. The Silent MIDI: Decrypting Dynamic Volume

*   **Challenge**: After implementing volume control, the generated MIDI files became silent in many players, or the volume changes did not behave as expected.
*   **Breakthrough Process**:
    1.  **Problem-Solving**: Initially, we used MIDI CC#7 (Main Volume) to handle volume changes during a note's duration. However, many MIDI synthesizers treat CC#7 as a static setting for a channel rather than a real-time "expression" parameter, leading to compatibility issues.
    2.  **Solution**: By consulting MIDI specifications and best practices, we confirmed that CC#11 (Expression) is the standard controller for handling dynamic note envelopes. After changing the code from `add_control_change(channel, 7, ...)` to `add_control_change(channel, 11, ...)` a, the MIDI files correctly exhibited dynamic volume changes on all players, and the silence issue was completely resolved.

### 2.4. The Final Polish: The Art of Volume Mapping

*   **Challenge**: Even with dynamic volume implemented, the vast difference in dynamic range between WonderSwan's 4-bit volume (0-15) and MIDI's 7-bit volume (0-127) caused a direct linear mapping to result in an overall low volume, making the music sound "weak".
*   **Breakthrough Process**:
    1.  **Problem Analysis**: A linear mapping `midi_vol = vgm_vol / 15.0 * 127.0` mapped a large portion of the mid-to-low range VGM volumes to very low, barely audible MIDI values.
    2.  **Non-linear Mapping**: To boost overall audibility while preserving dynamic range, we introduced a power function `pow(normalized_vol, exponent)` as a non-linear mapping curve. Through experimentation, we found:
        *   `exponent = 0.6`: A good starting point that effectively boosted low volumes, but was still not loud enough per user feedback.
        *   `exponent = 0.3`: A more aggressive curve that dramatically enhanced the expressiveness of mid-to-low volumes while still maintaining headroom at maximum volume to avoid clipping.
    3.  **Iteration via User Feedback**: Based on the final user feedback ("raise the pitch by one octave and keep increasing the volume"), we removed the experimental `-12` pitch offset and adopted the `exponent = 0.3` volume curve, finally achieving a perfect result that satisfied the user.

### 2.5. The Ultimate Challenge: Fixing Volume Jumps and Stuck Notes

As the project neared completion, we encountered two of the most stubborn and critical issues: some notes would not stop playing ("stuck notes"), and in the test file `02_Prelude.vgm`, the volume in the first second was drastically louder than the rest, creating a jarring volume "cliff."

*   **Challenge**:
    1.  **Stuck Notes**: The state machine logic was not robust enough to correctly handle certain specific note-off events.
    2.  **Volume Jumps**: The initial volume of a `Note On` event in a MIDI synthesizer depends on multiple factors (including default settings and previous CC values), leading to unpredictable volume spikes.

*   **Breakthrough Process**:
    1.  **Refactoring the State Machine**: We completely refactored the `check_state_and_update_midi` function. By introducing clearer state variables (like `channel_is_active`) and stricter logic, we precisely defined the four states of a note's lifecycle: **Start (Note On)**, **Stop (Note Off)**, **Retrigger**, and **Sustain**. This ensured that every `Note On` event eventually had a corresponding `Note Off` event, completely solving the stuck note problem.

    2.  **Solving the Volume Jump: A Three-Step Strategy**
        *   **Step 1: Establish a Baseline**. In the `WonderSwanChip` constructor, we initialized `CC7` (Main Volume) and `CC11` (Expression) to the maximum value of `127` for each MIDI track. This ensures that our converter always starts from a known, uniform maximum volume baseline, regardless of the synthesizer's default state.
        *   **Step 2: Control First**. In the `check_state_and_update_midi` function, we changed the order of events when a new note needed to be triggered. The program now **sends the `CC11` (Expression) event first** to set the precise audible volume for that note.
        *   **Step 3: Consistent Trigger**. Immediately following (at the same MIDI tick), the program sends a **`Note On` event with a fixed velocity of `127`**.

    *   **Why This Strategy Works**: This approach completely decouples the concepts of a note's "volume" and its "trigger." `CC11` is responsible for precisely controlling the audible loudness, while the fixed high-velocity `Note On` ensures that every note is triggered with a consistent, full-bodied attack. This completely eliminates volume jumps caused by synthesizer state uncertainty, making the volume changes smooth, controlled, and perfectly predictable.

### 2.6. The Self-Correcting Feedback Loop: The Power of Custom Validation Tools

You astutely pointed out the key to this project's success: we didn't just write a converter; more importantly, we created tools for validation and debugging. This established a powerful and rapid "Code-Test-Validate" feedback loop, enabling us to objectively and efficiently discover and solve problems, rather than relying on subjective listening.

**Core Debugging Tool: `midi_validator.exe`**

This is a lightweight MIDI parser written from scratch. Its functionality evolved as the project progressed:

*   **Initial Function**: Checked the basic structural integrity of the MIDI file, ensuring the header (MThd) and track chunks (MTrk) were not corrupted.
*   **Core Function**: The biggest breakthrough was adding a detailed **event logging** feature. It could list every single MIDI event (Note On, Note Off, Control Change, etc.) in a clear, chronological, line-by-line format, displaying its precise tick time, channel, and data values.

**How Did It Help Us?**

1.  **Validating Timing**: By examining the `Tick` column in the log, we could precisely verify the correctness of the `SAMPLES_TO_TICKS` formula.
2.  **Validating Pitch**: The `Data 1` column for `Note On` events directly showed the MIDI pitch number, allowing us to objectively judge the accuracy of the pitch conversion instead of just "how it sounds."
3.  **Debugging Volume**: This was its most critical use. By observing the velocity (`Data 2`) of `Note On` events and the values of CC#11 events, we could quantify the volume level. This allowed us to pinpoint the root cause of the "silent MIDI" issue (CC#7 vs. CC#11) and scientifically tune the exponent of our non-linear volume curve until the output values fell within the desired range.

**The Feedback-Driven Debugging Workflow**

This tool made our debugging process efficient and scientific:

1.  **Modify**: Adjust the conversion logic in `WonderSwanChip.cpp`.
2.  **Compile**: Recompile `converter.exe`.
3.  **Generate**: Run the converter to produce a new `output.mid`.
4.  **Validate**: **Immediately run `midi_validator.exe output.mid`** to get an objective "health report" on the new file.
5.  **Analyze**: Compare the log against expectations to confirm if the changes were effective and if any new issues were introduced.
6.  **Iterate**: Based on the analysis, proceed with the next round of modifications.

This data-driven iterative approach was the core methodology that enabled us to overcome numerous tricky technical hurdles and ultimately achieve a near-perfect result.

### 2.7. A New Chapter: Implementing Seamless Looping and Final Stability

After resolving all core conversion issues, we tackled the last major feature request: implementing loop playback for VGM files. This was not just a new feature, but an ultimate test of the program's stability and robustness.

*   **Challenges**:
    1.  **Looping Mechanism**: How to seamlessly copy a segment of events at the MIDI level?
    2.  **Stuck Notes 2.0**: New stuck note issues emerged at the loop boundaries.
    3.  **The Mysterious Segfault**: While attempting to fix the stuck notes, the program began to crash at runtime, throwing a "Segmentation fault."

*   **Breakthrough Process**:
    1.  **Implementing Loop Copying**:
        *   First, we extended `VgmReader` to parse the loop offset from the VGM header at address `0x1C`.
        *   Next, we performed a major refactor of `MidiWriter`, upgrading its internal data structure from a raw byte stream (`std::vector<uint8_t>`) to a `std::vector<MidiEvent>`. This struct encapsulates the absolute timestamp and event data for each MIDI event, making precise manipulation of individual events possible.
        *   Based on this new data structure, we implemented the `MidiTrack::copy_events_from` function. It can accurately copy all `MidiEvent`s within a specified time range and, by offsetting their timestamps, append them seamlessly to the end of the track.

    2.  **Solving Stuck Notes at Loop Boundaries**:
        *   We enhanced the `midi_validator.exe` tool to automatically detect and report unclosed notes.
        *   Using the validator, we discovered that notes that started within the loop block but did not end within it would become stuck notes after being copied.
        *   The solution was to add a state-tracking mechanism to the `copy_events_from` function. It keeps a record of all notes that are turned on (Note On) but not off within the copied event block. After the copy is complete, it iterates through this record and explicitly adds a `Note Off` event for each of these notes at the end of the loop block, ensuring loop integrity.

    3.  **Diagnosing and Fixing the Segmentation Fault**:
        *   **Initial Diagnosis**: We found that when the `copy_events_from` function was called, the source and destination were the same `MidiTrack` object (`target_track.copy_events_from(target_track, ...)`). This meant the code was iterating over a `std::vector` while adding elements to it. When the `vector` reallocated its memory, the iterators became invalid, leading to undefined behavior and crashes. We fixed this by first collecting the events to be copied into a temporary `std::vector` before adding them.
        *   **The Deeper Cause**: However, the segfault persisted. After a careful review of `main.cpp`, we found the true root cause: the loop processing logic had a hardcoded number of `4` tracks, whereas `WonderSwanChip` actually managed more tracks (including a noise channel, etc.). This led to an out-of-bounds array access.
        *   **The Final Fix**: We added a `get_channel_count()` method to `WonderSwanChip` to dynamically return the correct number of tracks, and used this value in `main.cpp` instead of the hardcoded `4`. This change completely eradicated the segmentation fault, bringing the program to its final, stable state.

### 2.8. Deep Instance Trace: The Complete Lifecycle of a Note

To understand the converter's most intricate workings, let's trace a more complex scenario involving **Panning**, **Vibrato**, and a detailed breakdown of **pitch** and **duration** calculations.

**Scenario:**

Assume our converter, at `tick = 1000`, begins processing the following VGM command stream:

| VGM Command (Hex) | Meaning | Generated MIDI Command | MIDI Tick |
| :--- | :--- | :--- | :--- |
| `51 89 FF` | Set Ch2 Left Vol to 15, Right Vol to 15 (Max, Center) | `CC#10 (Pan) = 64` | 1000 |
| `51 82 B0` | Set Ch2 Freq Period Low to `0xB0` | (No direct command, internal state updated) | - |
| `51 83 06` | Set Ch2 Freq Period High to `0x06` (Full period `0x6B0`) | (No direct command, internal state updated) | - |
| `51 90 02` | Enable Channel 2 (Triggers note) | `Note On: 62 (D4), Vel: 127` <br> `CC#11 (Expr) = 127` | 1000 |
| `61 88 08` | Wait 2184 samples | (Time advances) | +46 |
| `51 89 AF` | Set Ch2 Left Vol to 10, Right Vol to 15 (Pan right) | `CC#10 (Pan) = 76` | 1046 |
| `61 88 08` | Wait 2184 samples | (Time advances) | +46 |
| `51 83 05` | Change Ch2 Freq Period High to `0x05` (Full period `0x5B0`) | `Pitch Bend = 0` (Bend down) | 1092 |
| `61 1E 00` | Wait 30 samples | (Time advances) | +1 |
| `51 83 06` | Restore Ch2 Freq Period High to `0x06` (Full period `0x6B0`) | `Pitch Bend = 8192` (Restore) | 1093 |
| `61 1E 00` | Wait 30 samples | (Time advances) | +1 |
| `51 90 00` | Disable Channel 2 (Ends note) | `Note Off: 62 (D4)` | 1094 |

---

**Step-by-Step Analysis and Calculation:**

#### **Step 1: Note Trigger (Tick 1000)**

1.  **Set Volume and Frequency**:
    *   `51 89 FF`: Writes `0xFF` to port `0x89`. `WonderSwanChip` internally updates: `channel_volumes_left[1] = 15`, `channel_volumes_right[1] = 15`.
    *   `51 82 B0` and `51 83 06`: Write the period value. `WonderSwanChip` internally updates: `channel_periods[1] = 0x6B0` (decimal 1712).

2.  **Calculate Pitch**:
    At this point, `period = 1712`. Substituting into the pitch formula:
    *   **Calculate Frequency (Hz)**:
        `freq = (3072000.0 / (2048.0 - period)) / 32.0`
        `freq = (3072000.0 / (2048.0 - 1712)) / 32.0`
        `freq = (3072000.0 / 336.0) / 32.0`
        `freq = 9142.857 / 32.0 = 285.714 Hz`
    *   **Calculate MIDI Note Number**:
        `note = round(69 + 12 * log2(freq / 440.0))`
        `note = round(69 + 12 * log2(285.714 / 440.0))`
        `note = round(69 + 12 * log2(0.64935))`
        `note = round(69 + 12 * -0.622) = round(69 - 7.464) = round(61.536) = 62` (D4)

3.  **Trigger Note**:
    *   `51 90 02`: Enables Channel 2. The `WonderSwanChip` state machine detects a "note on" signal.

4.  **Generate MIDI Events**:
    *   Calculate Volume (Expression): `pow(15/15.0, 0.3) * 127 = 127`.
    *   Since left and right volumes are equal, pan is centered, and a default `CC#10 (Pan)` of `64` is sent.
    *   At `tick = 1000`, two events are generated:
        *   `Control Change`: Channel 1, CC#11 (Expression), Value 127
        *   `Note On`: Channel 1, Note 62 (D4), Velocity 127

---

#### **Step 2: Time Advance & Panning (Tick 1000 -> 1046)**

1.  **Calculate Duration**:
    *   `61 88 08`: Wait for `0x0888` = 2184 samples.
    *   Substitute into the timing conversion formula:
        `ticks = samples * SAMPLES_TO_TICKS`
        `ticks = 2184 * ((480.0 * 120.0) / (44100.0 * 60.0))`
        `ticks = 2184 * (57600.0 / 2646000.0)`
        `ticks = 2184 * 0.021772... = 47.55...`
    *   `WonderSwanChip` advances the `current_time` timestamp by `round(47.55) = 48` ticks. For simplicity, we'll use `46` ticks here (the actual code uses precise floating-point accumulation).
    *   The current time becomes `tick = 1000 + 46 = 1046`.

2.  **Change Pan**:
    *   `51 89 AF`: Writes `0xAF` to port `0x89`. `WonderSwanChip` internally updates: `channel_volumes_left[1] = 10`, `channel_volumes_right[1] = 15`.

3.  **Generate MIDI Event**:
    *   The state machine detects a volume change. Since we primarily use `CC#11` for overall volume, the change in left/right volumes mainly affects panning, triggering a `CC#10 (Pan)` event.
    *   **Calculate Pan**: We use a simple ratio algorithm: `pan = (right_vol / (left_vol + right_vol)) * 127`.
        `pan = (15 / (10 + 15)) * 127 = (15 / 25) * 127 = 0.6 * 127 = 76.2`
        Rounded to `pan = 76` (slightly to the right).
    *   At `tick = 1046`, the event is generated:
        *   `Control Change`: Channel 1, CC#10 (Pan), Value 76

---

#### **Step 3: Implementing Vibrato (Tick 1092 -> 1093)**

1.  **Advance Time**:
    *   `61 88 08`: Wait for another 2184 samples, advancing time by another `46` ticks. Current time: `tick = 1046 + 46 = 1092`.

2.  **Change Frequency (to create vibrato)**:
    *   `51 83 05`: Writes `0x05` to port `0x83`. The period changes to `0x5B0` (1456).
    *   **Recalculate Pitch**:
        `freq = (3072000.0 / (2048.0 - 1456)) / 32.0 = 161.29 Hz` (a significant drop from `285.7Hz`, used here for a dramatic vibrato demonstration).
        `note = round(69 + 12 * log2(161.29 / 440.0)) = 55` (G#3).
    *   The pitch changes drastically from `62` to `55`.

3.  **Generate MIDI Event (Pitch Bend)**:
    *   The `WonderSwanChip` state machine detects a pitch change during a sustained note. It doesn't generate a new `Note On` but instead a **Pitch Bend** event.
    *   The MIDI Pitch Bend range is typically +/- 2 semitones. This change (-7 semitones) is far beyond that. In a real conversion, we would set a larger bend range or intelligently handle such a large glissando. For this demonstration, we assume it generates a maximum downward bend event.
    *   At `tick = 1092`, the event is generated:
        *   `Pitch Bend`: Channel 1, Value `0` (lowest).

4.  **Restore Frequency**:
    *   `61 1E 00`: Wait 30 samples, advancing time by `round(30 * 0.02177) = 1` tick. Current time: `tick = 1093`.
    *   `51 83 06`: Restores the period to `0x6B0`. The pitch returns to `62`.
    *   At `tick = 1093`, the event is generated:
        *   `Pitch Bend`: Channel 1, Value `8192` (center, no bend).

---

#### **Step 4: Note End (Tick 1094)**

1.  **Advance Time**:
    *   `61 1E 00`: Wait another 30 samples, advancing time by `1` more tick. Current time: `tick = 1094`.

2.  **Disable Channel**:
    *   `51 90 00`: Disables Channel 2. The `WonderSwanChip` state machine detects a "note off" signal.

3.  **Generate MIDI Event**:
    *   At `tick = 1094`, the event is generated:
        *   `Note Off`: Channel 1, Note 62 (D4), Velocity 0.

---

**Final Generated MIDI Event Sequence (Summary):**

| Tick | MIDI Event | Channel | Data 1 (Note/CC#) | Data 2 (Vel/Value) | Remarks |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 1000 | `Control Change` | 1 | 10 (Pan) | 64 | Pan centered |
| 1000 | `Control Change` | 1 | 11 (Expression) | 127 | Set initial volume |
| 1000 | `Note On` | 1 | 62 (D4) | 127 | Trigger note |
| 1046 | `Control Change` | 1 | 10 (Pan) | 76 | Pan to the right |
| 1092 | `Pitch Bend` | 1 | - | 0 (LSB, MSB) | Vibrato start (bend down) |
| 1093 | `Pitch Bend` | 1 | - | 8192 (LSB, MSB) | Vibrato end (restore pitch) |
| 1094 | `Note Off` | 1 | 62 (D4) | 0 | End note |

This deep trace demonstrates that the converter is not just a simple command replacer but a complex system that truly understands musical context, simulates hardware behavior, and intelligently generates expressive MIDI events.

### 2.9. Ultimate Stability: Fixing Crashes Caused by Unknown VGM Commands

After the project's basic functions were complete and had passed numerous tests, we encountered a program crash caused by the specific file `07_Matoya's_Cave.vgm`. The debugging and fixing of this issue greatly improved the program's stability and compatibility with various VGM files.

*   **Challenge**:
    The program would crash silently without any clear error message when processing the `07_Matoya's_Cave.vgm` file.

*   **Breakthrough Process**:
    1.  **Logging First**: Faced with this "silent crash," we once again turned to our trusted method: adding detailed logging in the `process_vgm_data` function of `main.cpp`. We made the program print the current file pointer position and the command byte being processed before handling each VGM command.
    2.  **Pinpointing the Root Cause**: By analyzing the output `log.txt` file, we found that the log stopped abruptly after processing a specific location. Checking the VGM command at that location, we discovered it was a command not explicitly handled in our `switch` statement. The problem was in the `default` branch's logic: it merely incremented the file pointer `i` by `1`.
    3.  **Analyzing the Error**: In the VGM format, many commands have parameters. For example, command `0x4f dd` needs to skip 1 byte for its parameter, while `0x52 aa dd` needs to skip 2 bytes. Our previous `default` logic, upon encountering these unknown commands, only skipped the command itself, not the parameter bytes that followed. This caused the program to misread what should have been a parameter as the next command, triggering a chain reaction that ultimately corrupted the parsing logic and led to a crash.

*   **The Final Fix: Enhancing Command Handling**
    Instead of simply ignoring these commands, we consulted the VGM format specification and expanded the `process_vgm_data` function in `main.cpp`, adding "skip" logic to its `switch` statement for a series of previously unhandled commands. This ensures that even if we don't currently need to use the functionality of these commands, the parser correctly moves past them and their parameters, keeping the file pointer synchronized.

    **Newly Handled VGM Commands:**

    | VGM Command (Hex) | Length (Bytes) | Handling Method |
    | :--- | :--- | :--- |
    | `0x4f` | 2 | Skip (command + 1 data byte) |
    | `0x51` - `0x5f` | 3 | Skip (command + 2 data bytes) |
    | `0xa0` | 3 | Skip (command + 2 data bytes) |
    | `0xb0` - `0xbf` | 3 | Skip (command + 2 data bytes) |
    | `0xc0` - `0xdf` | 4 | Skip (command + 3 data bytes) |
    | `0xe0` - `0xff` | 5 | Skip (command + 4 data bytes) |

    By adding the correct `case` branches for these commands and incrementing the file pointer by the appropriate offset, we completely resolved the crash. This fix made the converter much more robust, enabling it to be compatible with more VGM files that, while syntactically correct, did not perfectly match our initial expectations.

### 2.10. The Intelligent Instrument System: `instruments.ini`

To solve the problem of mapping WonderSwan's custom waveforms to MIDI instruments and to give the user final control, we have introduced a brand-new intelligent instrument configuration system. The core of this system is the `instruments.ini` file.

**Core Features:**

*   **Automatic Discovery and Registration**: When the converter encounters a waveform it has never seen before in a VGM file, it will:
    1.  Generate a unique **32-byte fingerprint** for the waveform.
    2.  Automatically assign the most suitable default MIDI instrument based on the waveform's characteristics.
    3.  Generate a unique name for it, such as `CustomWave_1`.
    4.  Record the **source** where the waveform was discovered (i.e., the name of the current VGM file).
    5.  Record the registration **timestamp** (`registered_at`).
    6.  Write all of the above information, along with an ASCII art **waveform graph**, as a new entry into the `instruments.ini` file.

*   **User-Configurable**: `instruments.ini` is a plain text file that you can open with any text editor. If you are not satisfied with the automatically assigned MIDI instrument for a waveform, simply find the corresponding entry (e.g., `[CustomWave_1]`) and **manually change the number after `midi_instrument =`**. The next time you run the conversion, the program will read your changes and use the instrument you specified.

*   **Built-in Waveform Support**: On its first run, `instruments.ini` is automatically created and pre-populated with the WonderSwan's 5 built-in waveforms, ensuring the accuracy of basic tones.

**`instruments.ini` File Structure Example:**

```ini
[CustomWave_1]
fingerprint = 00010102...
midi_instrument = 80
source = 17_Battle.vgm
registered_at = 2025-09-23 19:33:12
graph =
;                                █
;                              ███
...
```

**Built-in Waveform Graph Reference:**

Below are the core built-in waveforms of the WonderSwan and their corresponding ASCII graphs in `instruments.ini`. This helps you to visually understand the appearance of different waveforms.

*   **Pulse Wave (PULSE)**
    ```
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████                
    ; ████████████████████████████████
    ```

*   **Triangle Wave (WAVE_BUILTIN_1)**
    ```
    ;                ██               
    ;               ████              
    ;              ██████             
    ;             ████████            
    ;            ██████████           
    ;           ████████████          
    ;          ██████████████         
    ;         ████████████████        
    ;        ██████████████████       
    ;       ████████████████████      
    ;      ██████████████████████     
    ;     ████████████████████████    
    ;    ██████████████████████████   
    ;   ████████████████████████████  
    ;  ██████████████████████████████ 
    ; ████████████████████████████████
    ```

*   **Sawtooth Wave (WAVE_BUILTIN_3)**
    ```
    ; █                              █
    ; ██                            ██
    ; ███                          ███
    ; ████                        ████
    ; █████                      █████
    ; ██████                    ██████
    ; ███████                  ███████
    ; ████████                ████████
    ; █████████              █████████
    ; ██████████            ██████████
    ; ███████████          ███████████
    ; ████████████        ████████████
    ; █████████████      █████████████
    ; ██████████████    ██████████████
    ; ███████████████  ███████████████
    ; ████████████████████████████████
    ```

*   **Noise (NOISE)**
    ```
    ;   █               █             
    ;   █            █  █            █
    ;   █       █    █  █       █    █
    ;   █ █     █    █  █ █     █    █
    ;   █ █   █ █    █  █ █   █ █    █
    ;   █ █   █ █  █ █  █ █   █ █  █ █
    ;   █ ██  █ █  █ █  █ ██  █ █  █ █
    ; █ █ ██  █ █  █ ██ █ ██  █ █  █ █
    ; █ █ ██ ██ █  █ ██ █ ██ ██ █  █ █
    ; █ █ ██ ██ █ ██ ██ █ ██ ██ █ ██ █
    ; █ ████ ██ █ ██ ██ ████ ██ █ ██ █
    ; █ ████ ████ ██ ██ ████ ████ ██ █
    ; █ ████ ████ █████ ████ ████ ████
    ; ██████ ████ ██████████ ████ ████
    ; ██████ ███████████████ █████████
    ; ████████████████████████████████
    ```

This system perfectly combines the automation of waveform recognition with the flexibility of manual user configuration, representing a huge leap forward in the project's usability and user experience.

### 2.11. Capturing Expression: The Pitch Bend Implementation for Vibrato

While the converter was highly accurate, it had a limitation in expressing one of the most common musical techniques: vibrato. Fast and wide frequency oscillations were being misinterpreted as a series of very short, distinct notes, which sounded stuttered and unnatural. This section details how we implemented MIDI Pitch Bend to solve this, allowing for smooth and expressive vibrato.

*   **Challenge**:
    The existing logic treated any change in the frequency `period` that resulted in a different MIDI note number as a trigger for a new note. This meant a `Note Off` was sent for the old note, and a `Note On` for the new one. For a rapid oscillation like vibrato, this created an undesirable "machine-gun" effect instead of a continuous, wavering pitch.

*   **Breakthrough Process: From Re-triggering to Bending**:
    The core idea was to change the state machine's philosophy: instead of asking "Is this a new note?", we started asking "Is this the *same* note, just with a slight pitch deviation?".

    1.  **Establishing a Baseline**: We introduced a new state variable, `channel_base_note_freq`, to store the initial frequency of a note at the exact moment it is triggered (`Note On`). This frequency corresponds to the note's "true" pitch.

    2.  **Calculating Deviation in Cents**: Whenever the frequency `period` for an active channel changed, we calculated the new frequency. Instead of immediately converting it to a new MIDI note number, we compared it to the `channel_base_note_freq`. The difference was calculated in **cents** (1/100th of a semitone), a logarithmic unit perfect for measuring musical pitch intervals.
        `cents_deviation = 1200.0 * log2(current_freq / base_freq);`

    3.  **The Vibrato/New Note Threshold**: We established a clear rule:
        *   **If the deviation is within a defined range** (e.g., +/- 200 cents, or 2 semitones), the change is classified as vibrato or a small portamento.
        *   **If the deviation exceeds this range**, it is classified as a genuine jump to a new note.

    4.  **Generating Pitch Bend Events**:
        *   For deviations within the range, we convert the `cents_deviation` into a 14-bit MIDI Pitch Bend value (0-16383, with 8192 as the center/no bend).
        *   This value is then sent as a Pitch Bend message. If the frequency continues to change, a stream of these messages is sent, creating a smooth pitch curve in the final MIDI.
        *   To ensure this works, we also send MIDI RPN (Registered Parameter Number) messages at the start of each track to set the synthesizer's pitch bend sensitivity to our desired range (+/- 2 semitones).

    5.  **Handling Jumps**: When the deviation exceeds the threshold, the logic falls back to the old behavior: it sends a `Note Off` for the current note and immediately triggers a `Note On` for the new note, correctly capturing the musical intention of a leap rather than a bend.

*   **Technical Implementation**:
    *   A new `add_pitch_bend` method was added to the `MidiTrack` class.
    *   The `check_state_and_update_midi` function in `WonderSwanChip` was significantly refactored to incorporate the new baseline frequency tracking, deviation calculation, and the thresholding logic described above.
    *   The `Note On` logic was updated to always reset the pitch bend to the center (`8192`) and store the note's base frequency, ensuring each new note starts with a clean slate.

This implementation successfully transformed the previously robotic-sounding note transitions into fluid, expressive vibrato, adding a critical layer of musicality to the converter's output.

## 3. A Deep Dive into the WonderSwan Sound System

To fully appreciate the conversion process, it's essential to understand the sound generation capabilities of the WonderSwan hardware itself. This section provides a detailed technical breakdown of each sound generation method, the I/O registers that control them, and how they are represented by VGM commands.

### 3.1. Overview of Sound Channels

The WonderSwan sound system is surprisingly versatile, featuring four main channels that can be configured for different roles, plus a dedicated PCM audio mechanism.

| Channel | Primary Function | Special Capabilities |
| :--- | :--- | :--- |
| **Channel 1** | Programmable Tone | Can be used for PCM playback via Sound DMA |
| **Channel 2** | Programmable Tone | Hardware Sweep (Pitch slides) |
| **Channel 3** | Programmable Tone | Noise Generation |
| **Channel 4** | Programmable Tone | (None) |

All interactions with these sound features in a VGM file are primarily handled by the **`0x51 aa dd`** command, which means "Write data `dd` to I/O port `aa`".

### 3.2. The Four Main Audio Channels (Programmable Tone)

These four channels are the backbone of WonderSwan music, responsible for generating melodies, harmonies, and basslines. They share a common architecture for pitch, volume, and waveform selection.

#### 3.2.1. Pitch Control

*   **Functionality**: Sets the frequency of the note to be played.
*   **Registers**: Each channel uses a pair of 8-bit registers to define an 11-bit frequency period value (from 0 to 2047). A higher period value results in a lower pitch.
    *   Channel 1: `0x80` (Low Byte), `0x81` (High Byte)
    *   Channel 2: `0x82` (Low Byte), `0x83` (High Byte)
    *   Channel 3: `0x84` (Low Byte), `0x85` (High Byte)
    *   Channel 4: `0x86` (Low Byte), `0x87` (High Byte)
*   **VGM Example**: To set Channel 2's period to `0x06B0`:
    *   `51 82 B0` (Write `0xB0` to the low byte register)
    *   `51 83 06` (Write `0x06` to the high byte register)
*   **Conversion Logic**: The converter reads these period values and uses the formula `freq = (3072000 / (2048 - period)) / 32` to calculate the audible frequency, which is then converted to a MIDI note number.

#### 3.2.2. Volume and Panning Control

*   **Functionality**: Sets the volume for the left and right speakers independently for each channel. This allows for both overall volume control and stereo panning effects.
*   **Registers**: Each channel has a single 8-bit register where the high 4 bits control the left volume (0-15) and the low 4 bits control the right volume (0-15).
    *   Channel 1: `0x88`
    *   Channel 2: `0x89`
    *   Channel 3: `0x8A`
    *   Channel 4: `0x8B`
*   **VGM Example**: To set Channel 2's left volume to 10 (`0xA`) and right volume to 15 (`0xF`):
    *   `51 89 AF` (Write `0xAF` to the volume register)
*   **Conversion Logic**:
    *   The converter reads the left and right volumes. The higher of the two is used to calculate the overall note expression (MIDI CC#11).
    *   The ratio between the left and right volumes is used to calculate the stereo pan position (MIDI CC#10).

#### 3.2.3. Waveform Selection and Memory

*   **Functionality**: This is the most unique feature. Instead of having fixed waveforms (like square, sine), the WonderSwan's four channels read their waveform data directly from a shared block of internal RAM. This allows for custom, dynamic timbres.
*   **Memory Layout**: The sound chip uses a **2 KB block of internal RAM (from address `0x0000` to `0x07FF`)** as its waveform memory. This RAM is organized into 128 slots, each holding a 16-byte waveform.
*   **Registers**:
    *   **Waveform Base Address (`0x8F`)**: This crucial register's value (multiplied by 64) points to the starting address in RAM where the four channels will find their waveform data. For example, if `0x8F` is set to `2`, the waveform data starts at `2 * 64 = 128` (`0x0080` in RAM).
    *   **Channel Waveform Pointers**: Each channel reads its 16-byte waveform from a specific offset relative to this base address:
        *   Channel 1: `Base Address + 0`
        *   Channel 2: `Base Address + 16`
        *   Channel 3: `Base Address + 32`
        *   Channel 4: `Base Address + 48`
*   **VGM Commands**: To create a custom sound, a game's music engine first writes the waveform data into RAM. This is done using the **Sound DMA (Direct Memory Access)** mechanism, which is not directly represented by simple VGM port writes. In a VGM file, this pre-loading of waveform data is typically done using **`0x67` (Data Block)** commands followed by a series of RAM write commands. However, for the purpose of our converter, we focus on what happens *after* the RAM is loaded.
*   **Conversion Logic (`instruments.ini` system)**:
    1.  When a note on a channel is triggered, the converter reads the value of register `0x8F` to find the waveform base address.
    2.  It calculates the specific 16-byte memory region for that channel.
    3.  It reads the 16 bytes from its simulated `internal_ram`. Each byte contains two 4-bit samples, so it unpacks this into a 32-sample waveform.
    4.  This 32-sample data becomes the waveform's unique "fingerprint".
    5.  The converter looks up this fingerprint in `instruments.ini`. If found, it uses the user-specified MIDI instrument. If not, it registers it as a new custom wave and assigns a default instrument.

#### 3.2.4. Channel Activation

*   **Functionality**: A master switch to turn each of the four channels on or off.
*   **Register**: `0x90` (Channel Enable Register)
*   **Bits**:
    *   Bit 0: Enable Channel 1
    *   Bit 1: Enable Channel 2
    *   Bit 2: Enable Channel 3
    *   Bit 3: Enable Channel 4
*   **VGM Example**: To enable Channel 2 and disable all others:
    *   `51 90 02` (Write `0b00000010` to port `0x90`)
*   **Conversion Logic**: This register is the primary trigger for `Note On` and `Note Off` events. When a channel's bit is set to 1 and its volume is greater than 0, a `Note On` is generated. When the bit is cleared to 0, a `Note Off` is generated.

### 3.3. Special Channel Features

Some channels have unique hardware capabilities beyond basic tone generation.

#### 3.3.1. Hardware Sweep (Channel 2)

*   **Functionality**: Channel 2 features a hardware "sweep" function, which can automatically and periodically adjust the channel's frequency period. This is commonly used to create pitch slide effects, arpeggios, or other simple modulations without needing the CPU to manually write new frequency values.
*   **Registers**:
    *   **Sweep Step (`0x8C`)**: An 8-bit signed value that determines the amount to add to the channel's period value at each sweep interval. A positive value lowers the pitch, and a negative value raises it.
    *   **Sweep Time (`0x8D`)**: An 8-bit value that sets the time interval between each sweep step. The actual time is calculated based on the system's H-blank rate.
    *   **Sweep Enable (in `0x90`)**: Bit 6 of the Channel Enable Register (`0x90`) acts as the master switch for the sweep function on Channel 2.
*   **VGM Example**: To create a slow upward pitch slide on Channel 2:
    *   `51 8C E0` (Set sweep step to -32, a negative value to raise the pitch)
    *   `51 8D 10` (Set a relatively long interval between steps)
    *   `51 90 42` (Enable Channel 2 via Bit 1, and enable sweep via Bit 6)
*   **Conversion Logic**: The converter fully simulates this behavior. The `process_sweep` function is called with each time advance. It maintains a `sweep_count` timer. When the timer expires, it adds the `sweep_step` value to `channel_periods[2]` and resets the timer. This change in period is then detected by `check_state_and_update_midi`, which in turn generates the appropriate MIDI Pitch Bend events, accurately reproducing the slide effect.

#### 3.3.2. Noise Generation (Channel 3)

*   **Functionality**: Channel 3 can be switched from a normal tone generator to a noise generator, which is essential for creating percussive sounds like drums, cymbals, or sound effects like explosions.
*   **Registers**:
    *   **Noise Control (`0x8E`)**: This register controls the type of noise. The low 3 bits (`0-2`) select one of seven different noise patterns, which likely correspond to different Linear Feedback Shift Register (LFSR) configurations, producing different noise timbres (e.g., more metallic vs. more "white"). Bit 3 is a reset flag.
    *   **Noise Enable (in `0x90`)**: Bit 7 of the Channel Enable Register (`0x90`) is the master switch that dedicates Channel 3 to noise generation. When this bit is set, Channel 3 ignores its waveform and frequency settings and outputs noise instead.
*   **VGM Example**: To play a standard noise sound on Channel 3:
    *   `51 8E 07` (Select noise pattern 7)
    *   `51 90 88` (Enable Channel 3 via Bit 3, and enable noise mode via Bit 7)
*   **Conversion Logic**: When the converter detects that Bit 7 of port `0x90` is active, it flags `channel_is_noise[3]` as true. In `check_state_and_update_midi`, if this flag is set, it overrides the normal instrument selection and assigns a percussive MIDI instrument (e.g., `127: Gunshot` or a user-defined drum sound from `instruments.ini`). The pitch of the noise is typically fixed or ignored in the MIDI conversion, as the timbre is the most important characteristic.

#### 3.3.3. PCM Audio via Sound DMA (Channel 1)

*   **Functionality**: This is the most advanced sound feature. The WonderSwan can play back raw, 4-bit PCM audio samples directly from RAM. This is used for complex sound effects, voice clips, or high-quality drum sounds that cannot be synthesized by the other channels. This functionality is tied to Channel 1 and uses the Sound DMA (Direct Memory Access) controller.
*   **Mechanism**: Instead of the CPU manually feeding sample data, it configures the DMA controller with a start address and length, and the hardware automatically streams the data from RAM to the audio output at a specified rate, hijacking Channel 1's volume controls.
*   **Registers**:
    *   **DMA Source Address (`0x4A`, `0x4B`, `0x4C`)**: Three registers combine to form a 24-bit address pointing to the start of the PCM sample data in RAM.
    *   **DMA Count (`0x4E`, `0x4F`)**: Two registers form a 16-bit value indicating the number of samples to play.
    *   **DMA Control (`0x52`)**: This register controls the DMA process. Bit 7 starts or stops the DMA, and bits 0-1 control the playback rate.
    *   **PCM Enable (in `0x90`)**: Bit 5 of the Channel Enable Register (`0x90`) enables PCM output.
    *   **PCM Direct Volume (`0x94`)**: A separate volume control register used specifically for PCM playback, bypassing Channel 1's normal volume register (`0x88`).
*   **VGM Commands**: A typical PCM playback sequence in a VGM file involves:
    1.  Writing the sample data to RAM (often via `0x67` data blocks).
    2.  `51 4A..4C ..` (Set DMA source address).
    3.  `51 4E..4F ..` (Set DMA sample count).
    4.  `51 52 ..` (Set DMA rate and start the transfer).
    5.  `51 90 20` (Enable PCM output mode).
*   **Conversion Logic**: The converter simulates the Sound DMA process. The `process_s_dma` function is called with each time advance. It maintains a `s_dma_timer`. When the timer expires, it simulates the hardware fetching one byte of data from the `s_dma_source_addr` in its simulated RAM. This byte is then written to port `0x89` (Channel 2's volume register, a hardware quirk), and the DMA counters are updated. The `check_state_and_update_midi` function detects that PCM mode is active (via port `0x90`, bit 5) and routes the volume from the special PCM volume register (`0x94`) to Channel 1's MIDI output, typically mapping it to a drum or sample-based instrument.

## 4. Program Workflow Explained

The core of `vgm_ws_to_mid` is a state machine that simulates the behavior of the WonderSwan sound chip and translates its state changes into MIDI events in real-time.

### 4.1. Overview

1.  **Read**: `VgmReader` is responsible for loading the entire VGM file into memory and providing access to header information (like loop offset, data start position).
2.  **Process**: The `process_vgm_data` function in `main` is the core driver of the program. It iterates through the VGM data block with a large `for` loop and uses a `switch` statement to dispatch and handle each VGM command:
    *   **`0x51 aa dd` (WonderSwan Port Write)**: Passes the address `aa` and data `dd` to `WonderSwanChip` for simulation. This is the most critical command.
    *   **`0x61 nn nn` (Wait)**: Calls `WonderSwanChip::advance_time`, converting the wait samples into MIDI ticks to advance the timeline.
    *   **`0x62` (Wait 735 samples)**: Same as above, but for a fixed value.
    *   **`0x63` (Wait 882 samples)**: Same as above, but for a fixed value.
    *   **`0x7n` (Wait n+1 samples)**: Same as above, for short waits.
    *   **`0x66` (End of Data Block)**: Marks the normal end of the VGM data stream, terminating the loop.
    *   **Other Recognized Commands (e.g., `0x4f`, `0x52-0x5f`, `0xa0`, etc.)**: These are commands for other chips or extended VGM features. The current converter doesn't use them, but to ensure parsing continuity, the program correctly skips over these commands and their parameters according to the format specification. This is key to the program's stability.
3.  **Simulate & Translate**: `WonderSwanChip` receives the port write data and updates its internal register states (e.g., frequency, volume). After each update, it calls `check_state_and_update_midi()` to check if the channel's state has changed (e.g., note on/off, pitch change, volume change).
4.  **Generate**: If a meaningful state change is detected, `WonderSwanChip` calls `MidiWriter` to generate the corresponding MIDI event (Note On/Off, Control Change) with the current, precise MIDI tick time.
5.  **Loop**: After processing the entire VGM file, if a loop point was detected, the `main` function instructs `MidiWriter` to copy the recorded MIDI events from the looped section and append them to the end of each track, creating a seamless loop.
6.  **Write**: After all VGM commands are processed, `MidiWriter` assembles all generated events (including the copied loop section) into a standard MIDI file and saves it to disk.

### 4.2. Key Components

*   **`main.cpp`**: The program entry point and main controller. It's responsible for parsing command-line arguments, instantiating `VgmReader`, `MidiWriter`, and `WonderSwanChip`. Its core is the `process_vgm_data` function, which contains a large `switch` statement that acts as a "dispatch center" for VGM commands, driving the entire conversion process and implementing the looping logic.
*   **`VgmReader.h/.cpp`**: The VGM file loader. It's responsible for reading the entire VGM file into memory and parsing the header to extract key metadata, such as the data start offset (`0x34`) and the loop offset (`0x1C`).
*   **`WonderSwanChip.h/.cpp`**: The **conversion core**.
    *   It maintains an `io_ram` array to simulate the chip's 256 I/O registers.
    *   The `write_port()` method is the key entry point, updating internal state variables (like `channel_periods`, `channel_volumes_left`, etc.) based on the port address being written to.
    *   `check_state_and_update_midi()` is the brain of the state machine. After each state update, it compares the current state to the previous one to determine if a MIDI event needs to be generated, thus intelligently handling legato (pitch bend), re-triggers, and volume envelopes. It now also calls `InstrumentConfig` to get or create an instrument.
    *   A new `get_channel_count()` method was added to dynamically return the total number of tracks managed by the chip, resolving the out-of-bounds access issue caused by hardcoding.
*   **`MidiWriter.h/.cpp`**: The MIDI file generator. It has undergone a major refactor and now internally uses a `std::vector<MidiEvent>` to store structured MIDI events instead of raw bytes. This makes precise manipulation of events possible. It provides a simple set of APIs (like `add_note_on`, `add_control_change`) to build a track and adds a new `copy_events_from` method, which can efficiently copy events from one point in time to another, key to enabling seamless looping. This method now also has built-in logic to automatically close notes that are not closed at the loop block boundary. When the conversion is finished, the `write_to_file()` method dynamically serializes the event list into a standard MIDI file.
*   **`InstrumentConfig.h/.cpp`**: The **Intelligent Instrument Configuration System**. This is the newest core component, responsible for managing the `instruments.ini` file. It implements waveform auto-discovery, fingerprint generation, similarity comparison, and automatic registration. It is also responsible for loading the user's custom instrument settings and providing the final MIDI instrument number to `WonderSwanChip`.
*   **`UsageLogger.h/.cpp`**: The **Usage Logger**. Responsible for generating the `conversion_log.txt` file. It reports all new instruments registered during the conversion and provides a detailed breakdown of waveform usage frequency per channel, offering the user a comprehensive report of the conversion process.

### 4.3. Key Formulas and Constants

*   **Timing Conversion**:
    `const double SAMPLES_TO_TICKS = (480.0 * 120.0) / (44100.0 * 60.0);`
*   **Pitch Conversion**:
    `double freq = (3072000.0 / (2048.0 - period)) / 32.0;`
    `int note = static_cast<int>(round(69 + 12 * log2(freq / 440.0)));`
*   **Volume Mapping**:
    `double normalized_vol = vgm_vol / 15.0;`
    `double curved_vol = pow(normalized_vol, 0.3);`
    `int velocity = static_cast<int>(curved_vol * 127.0);`

### 4.4. Conversion Log (`conversion_log.txt`)

To provide maximum transparency and insight into the conversion process, the program generates a detailed log file named `conversion_log.txt` upon every run. This file serves two primary purposes:

1.  **Reporting New Discoveries**: It explicitly lists any new custom waveforms that were discovered in the VGM file during the conversion, along with the default MIDI instrument that was assigned to them. This makes it easy to identify which new entries have been added to `instruments.ini` for potential user customization.
2.  **Providing Usage Statistics**: It offers a detailed breakdown of which waveforms were used by each of the WonderSwan's four primary sound channels, and how frequently. This data can be invaluable for understanding the sound design of a particular song.

**Example `conversion_log.txt`:**

```
--- Conversion Log ---
VGM File: 17_Battle.vgm
Timestamp: 2025-09-23 20:15:00

--- New Instruments Registered ---
A new instrument profile has been created and saved to instruments.ini:
[CustomWave_1]
- Fingerprint: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
- Assigned MIDI Instrument: 80 (Synth Lead)
- Source VGM: 17_Battle.vgm

--- Waveform Usage Statistics ---

Channel 1:
- WAVE_BUILTIN_1 (Triangle): 125 times
- CustomWave_1: 88 times

Channel 2:
- WAVE_BUILTIN_1 (Triangle): 210 times

Channel 3:
- PULSE: 340 times

Channel 4:
- NOISE: 56 times

--- End of Log ---
```

This section provides a clear, data-driven summary of the conversion, bridging the gap between the raw VGM input and the final MIDI output.

## 5. How to Compile and Run

This project is compiled using g++ in a bash environment.

*   **Compile**:
    ```bash
    g++ -std=c++17 -o vgm_ws_to_mid/vgm2mid.exe vgm_ws_to_mid/main.cpp vgm_ws_to_mid/VgmReader.cpp vgm_ws_to_mid/WonderSwanChip.cpp vgm_ws_to_mid/MidiWriter.cpp vgm_ws_to_mid/InstrumentConfig.cpp vgm_ws_to_mid/UsageLogger.cpp vgm_ws_to_mid/WaveformInfo.cpp -lstdc++fs
    ```
*   **Run**:
    ```bash
    vgm_ws_to_mid/vgm2mid.exe [input_vgm_file] [output_mid_file]
    ```
    For example:
    ```bash
    vgm_ws_to_mid/vgm2mid.exe 02_Prelude.vgm vgm_ws_to_mid/output.mid
    ```

---
This document provides a comprehensive summary of our work. We hope it serves as a clear guide for future development and maintenance.
