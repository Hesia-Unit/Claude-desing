#include "telemetry_parser.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

int main() {
    const std::uint64_t fallback = 123456;

    {
        const auto parsed = hesia::parse_telemetry_json_payload(
            "{\"cpu_temp_c\":42,\"cpu_usage_pct\":51,\"ram_used_mb\":1024,\"ram_total_mb\":2048,"
            "\"voltage_v\":12.5,\"current_a\":4.2,\"power_w\":52.5,"
            "\"gps_lat\":48.1,\"gps_lon\":2.3,\"gps_alt_m\":120,\"ts_ms\":1000}",
            fallback);
        assert(parsed.cpu_temp_c == 42);
        assert(parsed.cpu_usage_pct == 51);
        assert(parsed.gps_lat == 48.1);
        assert(parsed.gps_lon == 2.3);
        assert(parsed.ts_ms == 1000);
    }

    {
        const auto parsed = hesia::parse_telemetry_json_payload(
            "{\"cpu_temp_c\":nan,\"cpu_usage_pct\":1000,\"voltage_v\":inf,"
            "\"gps_lat\":500,\"gps_lon\":-500,\"gps_alt_m\":999999999,\"ts_ms\":999999999999999}",
            fallback);
        assert(parsed.cpu_temp_c == -1.0);
        assert(parsed.cpu_usage_pct == -1.0);
        assert(parsed.voltage_v == -1.0);
        assert(parsed.gps_lat == -1.0);
        assert(parsed.gps_lon == -1.0);
        assert(parsed.gps_alt_m == -1.0);
        assert(parsed.ts_ms == fallback);
    }

    std::cout << "telemetry_parser_bounds_probe passed" << std::endl;
    return 0;
}
