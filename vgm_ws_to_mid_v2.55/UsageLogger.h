#ifndef USAGE_LOGGER_H
#define USAGE_LOGGER_H

#include <string>
#include <vector>
#include <map>
#include "InstrumentConfig.h" // For InstrumentInfo

class UsageLogger {
public:
    UsageLogger(const std::string& filename);

    // Called by InstrumentConfig when a new waveform is registered
    void report_new_instrument(const InstrumentInfo& info);

    // Called by WonderSwanChip at the end of conversion to write the full log
    void write_log(const std::string& vgm_filename,
                   const InstrumentConfig& config,
                   const std::map<int, std::map<std::string, int>>& usage_data);

private:
    std::string filename;
    std::vector<InstrumentInfo> new_instruments_reported;
};

#endif // USAGE_LOGGER_H
