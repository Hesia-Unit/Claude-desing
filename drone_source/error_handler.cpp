#include "error_handler.hpp"
#include <algorithm>
#include <sstream>

namespace hesia {

ErrorHandler::ErrorHandler(std::shared_ptr<Logger> log) 
    : logger(log), last_error_reset(std::chrono::system_clock::now()) {
    error_history.reserve(ERROR_HISTORY_SIZE);
}

void ErrorHandler::handle_error(const std::string& message, ErrorSeverity severity, 
                              ErrorCategory category, const std::string& context, int error_code) {
    
    ErrorInfo error(message, severity, category, context, error_code);
    
    {
        std::lock_guard<std::mutex> lock(error_mutex);
        
        // Ajouter à l'historique
        error_history.push_back(error);
        
        // Limiter la taille de l'historique
        if (error_history.size() > ERROR_HISTORY_SIZE) {
            error_history.erase(error_history.begin(), 
                            error_history.begin() + (error_history.size() - ERROR_HISTORY_SIZE));
        }
        
        // Compteurs
        error_count++;
        if (severity == ErrorSeverity::CRITICAL) {
            critical_count++;
        }
    }
    
    // Logger
    std::string log_msg = "[" + category_to_string(category) + "] " + message;
    if (!context.empty()) {
        log_msg += " (context: " + context + ")";
    }
    if (error_code != 0) {
        log_msg += " (code: " + std::to_string(error_code) + ")";
    }
    
    switch (severity) {
        case ErrorSeverity::INFO:
            logger->info(log_msg);
            break;
        case ErrorSeverity::WARNING:
            logger->warning(log_msg);
            break;
        case ErrorSeverity::ERROR:
            logger->error(log_msg);
            break;
        case ErrorSeverity::CRITICAL:
            logger->error("[CRITICAL] " + log_msg);  // <-- CHANGEZ critical() en error()
            break;
    }
    
    // Vérifier les seuils
    check_error_thresholds();
    
    // Appeler les callbacks
    auto it = error_callbacks.find(category);
    if (it != error_callbacks.end()) {
        for (const auto& callback : it->second) {
            try {
                callback(error);
            } catch (const std::exception& e) {
                logger->error("Erreur dans callback d'erreur: " + std::string(e.what()));
            }
        }
    }
}

void ErrorHandler::handle_exception(const std::exception& e, ErrorSeverity severity, 
                                 ErrorCategory category, const std::string& context) {
    handle_error(std::string("Exception: ") + e.what(), severity, category, context);
}

void ErrorHandler::handle_opencv_exception(const cv::Exception& e, ErrorSeverity severity, 
                                        ErrorCategory category, const std::string& context) {
    std::stringstream ss;
    ss << "OpenCV Exception: " << e.what() << " (file: " << e.file 
       << ", line: " << e.line << ", func: " << e.func << ")";
    handle_error(ss.str(), severity, category, context);
}

void ErrorHandler::register_callback(ErrorCategory category, std::function<void(const ErrorInfo&)> callback) {
    std::lock_guard<std::mutex> lock(error_mutex);
    error_callbacks[category].push_back(callback);
}

std::vector<ErrorInfo> ErrorHandler::get_recent_errors(int count) const {
    std::lock_guard<std::mutex> lock(error_mutex);
    
    std::vector<ErrorInfo> recent;
    int start_idx = std::max(0, static_cast<int>(error_history.size()) - count);
    
    for (int i = start_idx; i < static_cast<int>(error_history.size()); ++i) {
        recent.push_back(error_history[i]);
    }
    
    return recent;
}

void ErrorHandler::clear_history() {
    std::lock_guard<std::mutex> lock(error_mutex);
    error_history.clear();
    error_count = 0;
    critical_count = 0;
    last_error_reset = std::chrono::system_clock::now();
}

void ErrorHandler::check_error_thresholds() {
    auto now = std::chrono::system_clock::now();
    
    {
        std::lock_guard<std::mutex> lock(reset_mutex);
        
        // Réinitialiser les compteurs chaque minute
        if (std::chrono::duration_cast<std::chrono::minutes>(now - last_error_reset).count() >= 1) {
            error_count = 0;
            last_error_reset = now;
        }
    }
    
    // Vérifier les seuils critiques
    if (critical_count.load() >= MAX_CRITICAL_ERRORS) {
        enter_emergency_mode();
    }
    
    if (error_count.load() >= MAX_ERRORS_PER_MINUTE) {
        handle_error("Trop d'erreurs détectées en une minute", 
                   ErrorSeverity::CRITICAL, ErrorCategory::SYSTEM);
    }
}

void ErrorHandler::enter_emergency_mode() {
    if (!emergency_mode.load()) {
        emergency_mode = true;
        logger->error("[CRITICAL] ENTRÉE EN MODE D'URGENCE - Arrêt des processus non critiques");  // <-- CHANGEZ ici aussi
        
        // Callback d'urgence
        ErrorInfo emergency("Mode d'urgence activé", ErrorSeverity::CRITICAL, 
                         ErrorCategory::SYSTEM);
        auto it = error_callbacks.find(ErrorCategory::SYSTEM);
        if (it != error_callbacks.end()) {
            for (const auto& callback : it->second) {
                try {
                    callback(emergency);
                } catch (const std::exception& e) {
                    logger->error("Erreur dans callback d'urgence: " + std::string(e.what()));
                }
            }
        }
    }
}

void ErrorHandler::exit_emergency_mode() {
    if (emergency_mode.load()) {
        emergency_mode = false;
        logger->info("SORTIE DU MODE D'URGENCE - Reprise normale des opérations");
    }
}

std::string ErrorHandler::severity_to_string(ErrorSeverity severity) {
    switch (severity) {
        case ErrorSeverity::INFO: return "INFO";
        case ErrorSeverity::WARNING: return "WARNING";
        case ErrorSeverity::ERROR: return "ERROR";
        case ErrorSeverity::CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

std::string ErrorHandler::category_to_string(ErrorCategory category) {
    switch (category) {
        case ErrorCategory::NETWORK: return "NETWORK";
        case ErrorCategory::VIDEO: return "VIDEO";
        case ErrorCategory::YOLO: return "YOLO";
        case ErrorCategory::MIDAS: return "MIDAS";
        case ErrorCategory::MEMORY: return "MEMORY";
        case ErrorCategory::SYSTEM: return "SYSTEM";
        case ErrorCategory::SYNC: return "SYNC";
        default: return "UNKNOWN";
    }
}

} // namespace hesia