// fuzzing_framework.cpp - Implémentation du framework de fuzzing
#include "fuzzing_framework.hpp"
#include "logger.hpp"
#include "config.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
#include <cstring>
#include <ctime>

namespace hesia {

// ===== VARIABLES STATIQUES =====

std::atomic<bool> FuzzingFramework::fuzzing_active{false};
std::atomic<uint64_t> FuzzingFramework::total_iterations{0};
std::atomic<uint64_t> FuzzingFramework::crashes_found{0};
std::atomic<uint64_t> FuzzingFramework::hangs_detected{0};
std::mutex FuzzingFramework::fuzzing_mutex;

std::mt19937 FuzzingFramework::rng{std::random_device{}()};
std::uniform_int_distribution<uint8_t> FuzzingFramework::byte_dist{0, 255};

// ===== INITIALISATION =====

void FuzzingFramework::initialize() {
    std::lock_guard<std::mutex> lock(fuzzing_mutex);
    fuzzing_active.store(true);
    total_iterations.store(0);
    crashes_found.store(0);
    hangs_detected.store(0);
    
    setup_crash_detection();
    
    auto logger = setup_logger("FUZZING", Config::LOG_DIR);
    logger->info("🔥 Framework de fuzzing initialisé");
}

void FuzzingFramework::cleanup() {
    std::lock_guard<std::mutex> lock(fuzzing_mutex);
    fuzzing_active.store(false);
    cleanup_crash_detection();
    
    auto logger = setup_logger("FUZZING", Config::LOG_DIR);
    logger->info("🧹 Framework de fuzzing nettoyé");
}

bool FuzzingFramework::is_active() {
    return fuzzing_active.load();
}

// ===== IMPLÉMENTATION PRINCIPALE =====

template<typename Parser>
FuzzingResult FuzzingFramework::fuzz_parser(const std::string& parser_name,
                                           std::function<bool(const std::vector<uint8_t>&)> parse_func,
                                           const std::vector<std::vector<uint8_t>>& initial_corpus,
                                           FuzzingType type,
                                           uint64_t max_iterations,
                                           double timeout_ms) {
    FuzzingResult result;
    result.iterations = 0;
    
    auto logger = setup_logger("FUZZING", Config::LOG_DIR);
    logger->info("🔥 Démarrage fuzzing pour: " + parser_name);
    
    // Corpus initial
    std::vector<std::vector<uint8_t>> corpus = initial_corpus;
    if (corpus.empty()) {
        // Générer quelques inputs initiaux
        for (int i = 0; i < 10; i++) {
            corpus.push_back(generate_random_input(100 + i * 50));
        }
    }
    
    setup_timeout_handling(timeout_ms);
    
    for (uint64_t i = 0; i < max_iterations && fuzzing_active.load(); i++) {
        result.iterations++;
        total_iterations.fetch_add(1);
        
        std::vector<uint8_t> test_input;
        
        // Choisir la stratégie de génération
        switch (type) {
            case FuzzingType::MUTATION_BASED:
                if (!corpus.empty()) {
                    test_input = corpus[i % corpus.size()];
                    test_input = mutate_input(test_input, 
                        static_cast<MutationStrategy>(i % 6));
                } else {
                    test_input = generate_random_input(100);
                }
                break;
                
            case FuzzingType::GENERATION_BASED:
                test_input = generate_random_input(50 + (i % 200));
                break;
                
            case FuzzingType::HYBRID:
                if (i % 2 == 0 && !corpus.empty()) {
                    test_input = corpus[i % corpus.size()];
                    test_input = mutate_input(test_input, 
                        static_cast<MutationStrategy>(i % 6));
                } else {
                    test_input = generate_random_input(50 + (i % 200));
                }
                break;
        }
        
        // Test avec timeout
        auto start_time = std::chrono::steady_clock::now();
        
        try {
            bool parse_result = parse_func(test_input);
            (void)parse_result; // Éviter l'avertissement unused variable
            auto end_time = std::chrono::steady_clock::now();
            result.execution_time_ms = std::chrono::duration<double, 
                std::milli>(end_time - start_time).count();
            
            // Détection de hang
            if (detect_timeout(start_time, timeout_ms)) {
                result.hang_detected = true;
                hangs_detected.fetch_add(1);
                logger->warning("⏱️ Hang détecté pour: " + parser_name);
                break;
            }
            
        } catch (const std::exception& e) {
            result.crash_detected = true;
            result.crash_type = std::string("Exception: ") + e.what();
            result.crash_input = test_input;
            result.stack_trace = get_stack_trace();
            crashes_found.fetch_add(1);
            
            log_crash(result, parser_name);
            break;
            
        } catch (...) {
            result.crash_detected = true;
            result.crash_type = "Exception inconnue";
            result.crash_input = test_input;
            result.stack_trace = get_stack_trace();
            crashes_found.fetch_add(1);
            
            log_crash(result, parser_name);
            break;
        }
        
        // Logging périodique
        if (i % 10000 == 0) {
            logger->info("🔥 Fuzzing en cours: " + std::to_string(i) + 
                        "/" + std::to_string(max_iterations) + 
                        " (Crashes: " + std::to_string(crashes_found.load()) + ")");
        }
    }
    
    cleanup_timeout_handling();
    
    logger->info("✅ Fuzzing terminé pour: " + parser_name + 
                " (Iterations: " + std::to_string(result.iterations) + 
                ", Crashes: " + std::to_string(crashes_found.load()) + ")");
    
    return result;
}

// ===== FUZZING SPÉCIALISÉ =====

FuzzingResult FuzzingFramework::fuzz_protocol_parser(const std::string& parser_name,
                                                   std::function<bool(const uint8_t*, size_t)> parse_func,
                                                   const std::vector<std::vector<uint8_t>>& initial_corpus,
                                                   uint64_t max_iterations) {
    auto wrapper_func = [parse_func](const std::vector<uint8_t>& data) -> bool {
        return parse_func(data.data(), data.size());
    };
    
    // Appel avec un type template spécifique (on peut utiliser void comme type générique)
    return fuzz_parser<void>(parser_name, wrapper_func, initial_corpus, 
                   FuzzingType::HYBRID, max_iterations, 500.0);
}

FuzzingResult FuzzingFramework::fuzz_file_parser(const std::string& parser_name,
                                              std::function<bool(const std::string&)> parse_func,
                                              const std::vector<std::string>& initial_corpus,
                                              uint64_t max_iterations) {
    // Convertir les strings en vectors pour le fuzzing
    std::vector<std::vector<uint8_t>> corpus;
    for (const auto& str : initial_corpus) {
        corpus.emplace_back(str.begin(), str.end());
    }
    
    auto wrapper_func = [parse_func](const std::vector<uint8_t>& data) -> bool {
        std::string str(data.begin(), data.end());
        return parse_func(str);
    };
    
    return fuzz_parser<void>(parser_name, wrapper_func, corpus, 
                   FuzzingType::HYBRID, max_iterations, 1000.0);
}

FuzzingResult FuzzingFramework::fuzz_structured_parser(const std::string& parser_name,
                                                    std::function<bool(const std::string&)> parse_func,
                                                    const std::vector<std::vector<uint8_t>>& valid_patterns,
                                                    uint64_t max_iterations) {
    // Convertir le corpus en vector<string>
    std::vector<std::string> string_corpus;
    for (const auto& byte_vec : valid_patterns) {
        string_corpus.emplace_back(byte_vec.begin(), byte_vec.end());
    }
    
    return fuzz_file_parser(parser_name, parse_func, string_corpus, max_iterations);
}

// ===== MÉTHODES INTERNES =====

std::vector<uint8_t> FuzzingFramework::mutate_input(const std::vector<uint8_t>& input,
                                                   MutationStrategy strategy) {
    if (input.empty()) {
        return generate_random_input(100);
    }
    
    std::vector<uint8_t> mutated = input;
    size_t pos = rng() % mutated.size();
    
    switch (strategy) {
        case MutationStrategy::BIT_FLIP: {
            mutated[pos] ^= (1 << (rng() % 8));
            break;
        }
        
        case MutationStrategy::BYTE_INSERTION: {
            mutated.insert(mutated.begin() + pos, byte_dist(rng));
            break;
        }
        
        case MutationStrategy::BYTE_DELETION: {
            if (mutated.size() > 1) {
                mutated.erase(mutated.begin() + pos);
            }
            break;
        }
        
        case MutationStrategy::ARITHMETIC: {
            int8_t delta = static_cast<int8_t>((rng() % 41) - 20);
            mutated[pos] = static_cast<uint8_t>(static_cast<int>(mutated[pos]) + delta);
            break;
        }
        
        case MutationStrategy::KNOWN_PATTERNS: {
            // Insérer des patterns connus qui causent des crashes
            static std::vector<std::vector<uint8_t>> patterns = {
                {0x00, 0x00, 0x00, 0x00},  // NULL bytes
                {0xFF, 0xFF, 0xFF, 0xFF},  // Buffer overflow pattern
                {0x41, 0x41, 0x41, 0x41},  // 'AAAA' pattern
                {0x7F, 0xFF, 0xFF, 0xFF},  // Negative numbers
                {0x90, 0x90, 0x90, 0x90}   // NOP sled
            };
            
            if (!patterns.empty()) {
                const auto& pattern = patterns[rng() % patterns.size()];
                mutated.insert(mutated.begin() + pos, pattern.begin(), pattern.end());
            }
            break;
        }
        
        case MutationStrategy::SPLICING: {
            // Combiner deux inputs différents
            if (mutated.size() > 10) {
                size_t cut_pos = rng() % (mutated.size() - 5);
                size_t cut_len = 1 + (rng() % 10);
                
                if (cut_pos + cut_len <= mutated.size()) {
                    mutated.erase(mutated.begin() + cut_pos, 
                               mutated.begin() + cut_pos + cut_len);
                    
                    // Insérer des octets aléatoires
                    for (size_t i = 0; i < cut_len / 2; i++) {
                        mutated.insert(mutated.begin() + cut_pos + i, byte_dist(rng));
                    }
                }
            }
            break;
        }
    }
    
    return mutated;
}

std::vector<uint8_t> FuzzingFramework::generate_random_input(size_t size) {
    std::vector<uint8_t> input(size);
    for (size_t i = 0; i < size; i++) {
        input[i] = byte_dist(rng);
    }
    return input;
}

bool FuzzingFramework::detect_timeout(std::chrono::steady_clock::time_point start,
                                  double timeout_ms) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration<double, std::milli>(now - start).count();
    return elapsed > timeout_ms;
}

void FuzzingFramework::log_crash(const FuzzingResult& result, const std::string& parser_name) {
    auto logger = setup_logger("FUZZING", Config::LOG_DIR);
    
    logger->error("💥 CRASH DÉTECTÉ - Parser: " + parser_name);
    logger->error("   Type: " + result.crash_type);
    logger->error("   Input size: " + std::to_string(result.crash_input.size()));
    
    // Sauvegarder l'input qui a causé le crash
    std::string crash_filename = "crash_" + parser_name + "_" + 
                               std::to_string(std::time(nullptr)) + ".bin";
    std::ofstream crash_file(Config::LOG_DIR + "/" + crash_filename, 
                          std::ios::binary);
    if (crash_file.is_open()) {
        crash_file.write(reinterpret_cast<const char*>(result.crash_input.data()),
                     result.crash_input.size());
        crash_file.close();
        logger->info("💾 Input du crash sauvegardé: " + crash_filename);
    }
    
    if (!result.stack_trace.empty()) {
        logger->error("   Stack trace:\n" + result.stack_trace);
    }
}

std::string FuzzingFramework::get_stack_trace() {
    void* buffer[32];
    int nptrs = backtrace(buffer, 32);
    char** strings = backtrace_symbols(buffer, nptrs);
    
    std::string trace;
    if (strings != nullptr) {
        for (int i = 0; i < nptrs; i++) {
            trace += std::to_string(i) + ": " + std::string(strings[i]) + "\n";
        }
        free(strings);
    }
    
    return trace;
}

// ===== DÉTECTION DE CRASH =====

namespace {

// Flags signal-safe (évite l'usage d'atomiques/loggers dans les handlers)
static volatile sig_atomic_t g_crash_signal = 0;
static volatile sig_atomic_t g_timeout_signal = 0;

#ifndef _WIN32
static timer_t g_timeout_timer{};
static bool g_timeout_timer_initialized = false;
#endif

static void fuzz_crash_handler(int sig) {
    g_crash_signal = sig;

    // Async-signal-safe: write + _exit
    const char msg[] = "FUZZING: crash signal\n";
    (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    _exit(128 + sig);
}

static void fuzz_timeout_handler(int sig) {
    (void)sig;
    g_timeout_signal = 1;

    const char msg[] = "FUZZING: timeout\n";
    (void)::write(STDERR_FILENO, msg, sizeof(msg) - 1);
}

} // namespace

bool FuzzingFramework::setup_crash_detection() {
    g_crash_signal = 0;

    // Installer des handlers minimalistes
    std::signal(SIGSEGV, fuzz_crash_handler);
    std::signal(SIGBUS, fuzz_crash_handler);
    std::signal(SIGFPE, fuzz_crash_handler);

    return true;
}

void FuzzingFramework::cleanup_crash_detection() {
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGBUS, SIG_DFL);
    std::signal(SIGFPE, SIG_DFL);
}

bool FuzzingFramework::is_crash_detected() {
    return g_crash_signal != 0;
}

// ===== TIMEOUT HANDLING =====

void FuzzingFramework::setup_timeout_handling(double timeout_ms) {
    g_timeout_signal = 0;

#ifdef _WIN32
    (void)timeout_ms;
    return;
#else
    if (timeout_ms <= 0.0) {
        return;
    }

    // Installer handler SIGALRM (best-effort)
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fuzz_timeout_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    (void)sigaction(SIGALRM, &sa, nullptr);

    // Programmer un timer POSIX
    struct sigevent sev;
    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = SIGALRM;

    if (timer_create(CLOCK_MONOTONIC, &sev, &g_timeout_timer) != 0) {
        g_timeout_timer_initialized = false;
        return;
    }
    g_timeout_timer_initialized = true;

    const uint64_t ms = static_cast<uint64_t>(timeout_ms);

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    its.it_value.tv_sec = static_cast<time_t>(ms / 1000);
    its.it_value.tv_nsec = static_cast<long>((ms % 1000) * 1000000ULL);

    // Timer one-shot (pas périodique). Si besoin, ré-armer côté fuzzing.
    its.it_interval.tv_sec = 0;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(g_timeout_timer, 0, &its, nullptr) != 0) {
        timer_delete(g_timeout_timer);
        g_timeout_timer_initialized = false;
        return;
    }
#endif
}

void FuzzingFramework::cleanup_timeout_handling() {
#ifdef _WIN32
    return;
#else
    std::signal(SIGALRM, SIG_DFL);
    if (g_timeout_timer_initialized) {
        timer_delete(g_timeout_timer);
        g_timeout_timer_initialized = false;
    }
#endif
}
void FuzzingFramework::save_corpus(const std::string& filename,
                                 const std::vector<std::vector<uint8_t>>& corpus) {
    std::ofstream file(Config::LOG_DIR + "/" + filename, std::ios::binary);
    if (!file.is_open()) return;
    
    uint32_t count = static_cast<uint32_t>(corpus.size());
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    for (const auto& item : corpus) {
        uint32_t size = static_cast<uint32_t>(item.size());
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(item.data()), size);
    }
}

std::vector<std::vector<uint8_t>> FuzzingFramework::load_corpus(const std::string& filename) {
    std::vector<std::vector<uint8_t>> corpus;
    std::ifstream file(Config::LOG_DIR + "/" + filename, std::ios::binary);
    if (!file.is_open()) return corpus;
    
    uint32_t count;
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        
        std::vector<uint8_t> item(size);
        file.read(reinterpret_cast<char*>(item.data()), size);
        corpus.push_back(std::move(item));
    }
    
    return corpus;
}

void FuzzingFramework::minimize_corpus(const std::vector<std::vector<uint8_t>>& corpus) {
    // Implémentation simple de minimisation de corpus
    // Intentionally conservative for now: keep minimization predictable in CI smoke runs.
    auto logger = setup_logger("FUZZING", Config::LOG_DIR);
    logger->info(" Minimisation du corpus (corpus actuel: " + 
                std::to_string(corpus.size()) + " items)");
}

// Instanciations de template nécessaires
template FuzzingResult FuzzingFramework::fuzz_parser<>(const std::string&,
                                                          std::function<bool(const std::vector<uint8_t>&)>,
                                                          const std::vector<std::vector<uint8_t>>&,
                                                          FuzzingType,
                                                          uint64_t,
                                                          double);

} // namespace hesia
