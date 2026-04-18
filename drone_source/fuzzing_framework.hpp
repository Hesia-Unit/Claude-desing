#ifndef FUZZING_FRAMEWORK_HPP
#define FUZZING_FRAMEWORK_HPP

#include <vector>
#include <cstdint>
#include <functional>
#include <random>
#include <chrono>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <ctime>

namespace hesia {

// Types de fuzzing
enum class FuzzingType {
    MUTATION_BASED = 0,
    GENERATION_BASED = 1,
    HYBRID = 2
};

// Résultats de fuzzing
struct FuzzingResult {
    bool crash_detected = false;
    bool hang_detected = false;
    bool memory_leak = false;
    bool assertion_failed = false;
    uint64_t iterations = 0;
    double execution_time_ms = 0.0;
    std::string crash_type;
    std::vector<uint8_t> crash_input;
    std::string stack_trace;
};

// Stratégies de mutation
enum class MutationStrategy {
    BIT_FLIP = 0,
    BYTE_INSERTION = 1,
    BYTE_DELETION = 2,
    ARITHMETIC = 3,
    KNOWN_PATTERNS = 4,
    SPLICING = 5
};

class FuzzingFramework {
private:
    static std::atomic<bool> fuzzing_active;
    static std::atomic<uint64_t> total_iterations;
    static std::atomic<uint64_t> crashes_found;
    static std::atomic<uint64_t> hangs_detected;
    static std::mutex fuzzing_mutex;
    
    // Générateur aléatoire
    static std::mt19937 rng;
    static std::uniform_int_distribution<uint8_t> byte_dist;
    
    // Méthodes internes
    static std::vector<uint8_t> mutate_input(const std::vector<uint8_t>& input, 
                                         MutationStrategy strategy);
    static std::vector<uint8_t> generate_random_input(size_t size);
    static bool detect_timeout(std::chrono::steady_clock::time_point start, 
                           double timeout_ms);
    static void log_crash(const FuzzingResult& result, const std::string& parser_name);
    static std::string get_stack_trace();
    
public:
    // Configuration du fuzzing
    static void initialize();
    static void cleanup();
    static bool is_active();
    
    // Interface principale de fuzzing
    template<typename Parser = void>
    static FuzzingResult fuzz_parser(const std::string& parser_name,
                                   std::function<bool(const std::vector<uint8_t>&)> parse_func,
                                   const std::vector<std::vector<uint8_t>>& initial_corpus,
                                   FuzzingType type = FuzzingType::HYBRID,
                                   uint64_t max_iterations = 1000000,
                                   double timeout_ms = 1000.0);
    
    // Fuzzing pour les parsers de protocoles
    static FuzzingResult fuzz_protocol_parser(const std::string& parser_name,
                                         std::function<bool(const uint8_t*, size_t)> parse_func,
                                         const std::vector<std::vector<uint8_t>>& initial_corpus,
                                         uint64_t max_iterations = 500000);
    
    // Fuzzing pour les parsers de fichiers
    static FuzzingResult fuzz_file_parser(const std::string& parser_name,
                                      std::function<bool(const std::string&)> parse_func,
                                      const std::vector<std::string>& initial_corpus = {},
                                      uint64_t max_iterations = 500000);
    
    // Fuzzing pour les parsers de données structurées
    static FuzzingResult fuzz_structured_parser(const std::string& parser_name,
                                           std::function<bool(const std::string&)> parse_func,
                                           const std::vector<std::vector<uint8_t>>& valid_patterns = {},
                                           uint64_t max_iterations = 200000);
    
    // Statistiques
    static uint64_t get_total_iterations();
    static uint64_t get_crashes_found();
    static uint64_t get_hangs_detected();
    static void reset_statistics();
    
    // Corpus management
    static void save_corpus(const std::string& filename, 
                         const std::vector<std::vector<uint8_t>>& corpus);
    static std::vector<std::vector<uint8_t>> load_corpus(const std::string& filename);
    static void minimize_corpus(const std::vector<std::vector<uint8_t>>& corpus);
    
    // Détection de crashes
    static bool setup_crash_detection();
    static void cleanup_crash_detection();
    static bool is_crash_detected();
    
    // Timeout handling
    static void setup_timeout_handling(double timeout_ms);
    static void cleanup_timeout_handling();
};

// Macros pour faciliter le fuzzing
#define FUZZ_PARSER(parser_name, parse_func, corpus) \
    FuzzingFramework::fuzz_parser(parser_name, parse_func, corpus)

#define FUZZ_PROTOCOL_PARSER(parser_name, parse_func, corpus) \
    FuzzingFramework::fuzz_protocol_parser(parser_name, parse_func, corpus)

#define FUZZ_FILE_PARSER(parser_name, parse_func, corpus) \
    FuzzingFramework::fuzz_file_parser(parser_name, parse_func, corpus)

#define FUZZ_STRUCTURED_PARSER(parser_name, parse_func, patterns) \
    FuzzingFramework::fuzz_structured_parser(parser_name, parse_func, patterns)

} // namespace hesia

#endif // FUZZING_FRAMEWORK_HPP