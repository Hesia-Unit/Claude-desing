#ifndef ERROR_HANDLER_HPP
#define ERROR_HANDLER_HPP

#include <string>
#include <exception>
#include <memory>
#include <functional>
#include <chrono>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <opencv2/opencv.hpp>
#include "logger.hpp"

namespace hesia {

enum class ErrorSeverity {
    INFO = 0,
    WARNING = 1,
    ERROR = 2,
    CRITICAL = 3
};

enum class ErrorCategory {
    NETWORK = 0,
    VIDEO = 1,
    YOLO = 2,
    MIDAS = 3,
    MEMORY = 4,
    SYSTEM = 5,
    SYNC = 6
};

struct ErrorInfo {
    std::string message;
    ErrorSeverity severity;
    ErrorCategory category;
    std::chrono::system_clock::time_point timestamp;
    std::string context;
    int error_code;
    
    ErrorInfo(const std::string& msg, ErrorSeverity sev, ErrorCategory cat, 
              const std::string& ctx = "", int code = 0)
        : message(msg), severity(sev), category(cat), 
          timestamp(std::chrono::system_clock::now()), context(ctx), error_code(code) {}
};

class ErrorHandler {
private:
    std::shared_ptr<Logger> logger;
    std::vector<ErrorInfo> error_history;
    mutable std::mutex error_mutex;  // <-- AJOUTEZ "mutable" ici
    std::atomic<bool> emergency_mode{false};
    std::atomic<int> error_count{0};
    std::atomic<int> critical_count{0};
    
    // Callbacks pour différents types d'erreurs
    std::map<ErrorCategory, std::vector<std::function<void(const ErrorInfo&)>>> error_callbacks;
    
    // Seuils pour déclencher des actions
    static constexpr int MAX_ERRORS_PER_MINUTE = 50;
    static constexpr int MAX_CRITICAL_ERRORS = 5;
    static constexpr int ERROR_HISTORY_SIZE = 1000;
    
    std::chrono::system_clock::time_point last_error_reset;
    std::mutex reset_mutex;
    
    void check_error_thresholds();
    std::string severity_to_string(ErrorSeverity severity);
    std::string category_to_string(ErrorCategory category);
    
public:
    ErrorHandler(std::shared_ptr<Logger> log);
    ~ErrorHandler() = default;
    
    // Gestion des erreurs
    void handle_error(const std::string& message, ErrorSeverity severity, 
                    ErrorCategory category, const std::string& context = "", int error_code = 0);
    
    // Wrappers pour différents types d'exceptions
    void handle_exception(const std::exception& e, ErrorSeverity severity, 
                         ErrorCategory category, const std::string& context = "");
    
    void handle_opencv_exception(const cv::Exception& e, ErrorSeverity severity, 
                              ErrorCategory category, const std::string& context = "");
    
    // Callbacks
    void register_callback(ErrorCategory category, std::function<void(const ErrorInfo&)> callback);
    
    // État et statistiques
    bool is_emergency_mode() const { return emergency_mode.load(); }
    int get_error_count() const { return error_count.load(); }
    int get_critical_count() const { return critical_count.load(); }
    
    std::vector<ErrorInfo> get_recent_errors(int count = 10) const;
    void clear_history();
    
    // Actions automatiques
    void enter_emergency_mode();
    void exit_emergency_mode();
    
    // Macros pour faciliter l'utilisation
    #define HANDLE_ERROR(msg, sev, cat) handler->handle_error(msg, sev, cat, __FUNCTION__, __LINE__)
    #define HANDLE_EXCEPTION(e, sev, cat) handler->handle_exception(e, sev, cat, __FUNCTION__)
    #define HANDLE_OPENCV_EXCEPTION(e, sev, cat) handler->handle_opencv_exception(e, sev, cat, __FUNCTION__)
};

} // namespace hesia  // <-- AJOUTEZ CETTE LIGNE

#endif // ERROR_HANDLER_HPP