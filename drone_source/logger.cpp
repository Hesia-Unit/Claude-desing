#include "logger.hpp"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace hesia {

std::atomic<bool> Logger::debug_enabled{true};
std::atomic<bool> Logger::file_output_enabled{true};

void Logger::set_debug_enabled(bool enabled) {
    debug_enabled.store(enabled, std::memory_order_relaxed);
}

void Logger::set_file_output_enabled(bool enabled) {
    file_output_enabled.store(enabled, std::memory_order_relaxed);
}

void Logger::write_log(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    ss << "." << std::setfill('0') << std::setw(3) << ms.count();
    std::string timestamp = ss.str();
    
    std::string log_line = "[" + timestamp + "] [" + level + "] " + name + ": " + message + "\n";
    
    if (file_enabled && file_stream.is_open()) {
        file_stream << log_line;
        file_stream.flush();
    }
    
    if (console_enabled) {
        std::cout << log_line;
    }
}

Logger::Logger(const std::string& logger_name, const fs::path& log_dir)
    : name(logger_name), console_enabled(true),
      file_enabled(file_output_enabled.load(std::memory_order_relaxed)) {
    log_file = log_dir / (name + ".log");
    if (file_enabled) {
        file_stream.open(log_file, std::ios::app);
    }
}

Logger::~Logger() {
    if (file_stream.is_open()) {
        file_stream.close();
    }
}

void Logger::disable_file_output() {
    std::lock_guard<std::mutex> lock(log_mutex);
    file_enabled = false;
    if (file_stream.is_open()) {
        file_stream.flush();
        file_stream.close();
    }
}

void Logger::debug(const std::string& message) {
    if (!debug_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    write_log("DEBUG", message);
}

void Logger::info(const std::string& message) {
    write_log("INFO", message);
}

void Logger::warning(const std::string& message) {
    write_log("WARNING", message);
}

void Logger::error(const std::string& message) {
    write_log("ERROR", message);
}

void Logger::set_level(const std::string& level) {
    (void)level; // Simplification - dans une vraie implémentation, on filtrerait par niveau
}

std::shared_ptr<Logger> setup_logger(const std::string& name, const fs::path& log_dir) {
    return std::make_shared<Logger>(name, log_dir);
}

} // namespace hesia
