#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>
#include <memory>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <iostream>
#include <chrono>
#include <iomanip>
#include <map>
#include <ctime>
#include <atomic>

namespace hesia {

namespace fs = std::filesystem;

class Logger {
public:
    Logger(const std::string& logger_name, const fs::path& log_dir);
    ~Logger();

    static void set_debug_enabled(bool enabled);
    static void set_file_output_enabled(bool enabled);
    void disable_file_output();

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void set_level(const std::string& level);

private:
    void write_log(const std::string& level, const std::string& message);

    static std::atomic<bool> debug_enabled;
    static std::atomic<bool> file_output_enabled;

    std::string name;
    fs::path log_file;
    std::ofstream file_stream;
    bool console_enabled;
    bool file_enabled;
    std::mutex log_mutex;
};

std::shared_ptr<Logger> setup_logger(const std::string& name, const fs::path& log_dir);

} // namespace hesia

#endif // LOGGER_HPP
