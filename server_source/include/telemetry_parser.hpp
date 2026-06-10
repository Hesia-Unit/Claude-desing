#pragma once

#include <cstdint>
#include <string>

namespace hesia {

struct ParsedTelemetryJson {
    double cpu_temp_c = -1.0;
    double cpu_usage_pct = -1.0;
    double ram_used_mb = -1.0;
    double ram_total_mb = -1.0;
    double voltage_v = -1.0;
    double current_a = -1.0;
    double power_w = -1.0;
    double gps_lat = -1.0;
    double gps_lon = -1.0;
    double gps_alt_m = -1.0;
    std::uint64_t ts_ms = 0;
};

ParsedTelemetryJson parse_telemetry_json_payload(const std::string& payload,
                                                 std::uint64_t fallback_ts_ms);

} // namespace hesia
