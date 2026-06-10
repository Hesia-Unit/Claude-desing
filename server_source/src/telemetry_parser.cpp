#include "telemetry_parser.hpp"

#include <cctype>
#include <cstdlib>
#include <cmath>

namespace hesia {

namespace {

static bool json_extract_number(const std::string& s, const std::string& key, double& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = s.find(needle);
    if (pos == std::string::npos) return false;
    pos = s.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;
    pos += 1;
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos])) != 0) pos++;
    if (pos >= s.size()) return false;
    char* endptr = nullptr;
    const char* start = s.c_str() + pos;
    const double val = std::strtod(start, &endptr);
    if (endptr == start) return false;
    if (!std::isfinite(val)) return false;
    out = val;
    return true;
}

static bool json_extract_bounded_number(const std::string& s,
                                        const std::string& key,
                                        double& out,
                                        double min_value,
                                        double max_value) {
    double val = 0.0;
    if (!json_extract_number(s, key, val)) {
        return false;
    }
    if (val < min_value || val > max_value) {
        return false;
    }
    out = val;
    return true;
}

} // namespace

ParsedTelemetryJson parse_telemetry_json_payload(const std::string& payload,
                                                 std::uint64_t fallback_ts_ms) {
    ParsedTelemetryJson parsed;

    json_extract_bounded_number(payload, "cpu_temp_c", parsed.cpu_temp_c, -80.0, 150.0);
    json_extract_bounded_number(payload, "cpu_usage_pct", parsed.cpu_usage_pct, 0.0, 100.0);
    json_extract_bounded_number(payload, "ram_used_mb", parsed.ram_used_mb, 0.0, 1.0e9);
    json_extract_bounded_number(payload, "ram_total_mb", parsed.ram_total_mb, 0.0, 1.0e9);
    json_extract_bounded_number(payload, "voltage_v", parsed.voltage_v, 0.0, 100.0);
    json_extract_bounded_number(payload, "current_a", parsed.current_a, 0.0, 1000.0);
    json_extract_bounded_number(payload, "power_w", parsed.power_w, 0.0, 100000.0);
    json_extract_bounded_number(payload, "gps_lat", parsed.gps_lat, -90.0, 90.0);
    json_extract_bounded_number(payload, "gps_lon", parsed.gps_lon, -180.0, 180.0);
    json_extract_bounded_number(payload, "gps_alt_m", parsed.gps_alt_m, -1000.0, 100000.0);

    double ts_ms = 0.0;
    json_extract_number(payload, "ts_ms", ts_ms);
    if (ts_ms > 0.0 && ts_ms <= 4102444800000.0) {
        parsed.ts_ms = static_cast<std::uint64_t>(ts_ms);
    } else {
        parsed.ts_ms = fallback_ts_ms;
    }

    return parsed;
}

} // namespace hesia
