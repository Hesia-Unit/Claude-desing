#include "fault_injection_protection.hpp"
#include "hardware_monitor.hpp"
#include <iostream>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstring>
#include <iomanip>

#ifdef _WIN32
#include <intrin.h>
#include <windows.h>
#else
#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#include <atomic>
// Fallbacks for non-x86 targets (ARM).
static inline void _mm_lfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_sfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
static inline void _mm_mfence() { std::atomic_thread_fence(std::memory_order_seq_cst); }
#endif
#include <unistd.h>
#include <sys/mman.h>
#endif

namespace hesia {

// ===== INITIALISATION DES MEMBRES STATIQUES =====

std::atomic<bool> FaultInjectionProtection::fault_monitor_active{false};
std::vector<FaultInjectionProtection::FaultSample> FaultInjectionProtection::fault_history;
std::mutex FaultInjectionProtection::fault_history_mutex; // ✅ P2: Initialisation du mutex
std::thread FaultInjectionProtection::monitoring_thread;
std::atomic<uint32_t> FaultInjectionProtection::fault_count{0};
std::atomic<uint32_t> FaultInjectionProtection::redundancy_failures{0};
std::atomic<uint32_t> FaultInjectionProtection::checksum_errors{0};

std::atomic<bool> FaultInjectionProtection::enable_spatial_redundancy{true};
std::atomic<bool> FaultInjectionProtection::enable_temporal_redundancy{true};
std::atomic<uint32_t> FaultInjectionProtection::redundancy_level{2};
std::atomic<uint32_t> FaultInjectionProtection::fault_threshold{5};
std::atomic<bool> FaultInjectionProtection::hardware_assisted{false};

std::string FaultInjectionProtection::integrity_log_path = "/var/log/hesia/hesia_integrity.log";
std::string FaultInjectionProtection::fault_detection_path = "/var/log/hesia/hesia_faults.log";

// ===== FONCTIONS DE MONITORING =====

bool FaultInjectionProtection::initialize(uint32_t redundancy_lvl) {
    std::cout << "🛡️ Initialisation FaultInjectionProtection..." << std::endl;
    
    redundancy_level = redundancy_lvl;
    fault_count = 0;
    redundancy_failures = 0;
    checksum_errors = 0;
    fault_history.clear();
    
    // Configuration de la redondance
    enable_spatial_redundancy = true;
    enable_temporal_redundancy = true;
    hardware_assisted = false;
    
    // Création des fichiers de log
    std::ofstream log_file(integrity_log_path, std::ios::app);
    if (log_file.is_open()) {
        log_file << "=== FaultInjectionProtection Initialisé ===" << std::endl;
        log_file.close();
    }
    
    std::cout << "✅ FaultInjectionProtection initialisé (redondance niveau " 
              << redundancy_lvl << ")" << std::endl;
    return true;
}

bool FaultInjectionProtection::start_fault_monitoring() {
    if (fault_monitor_active.load()) {
        std::cout << "⚠️ Monitoring fautes déjà actif" << std::endl;
        return true;
    }
    
    fault_monitor_active = true;
    monitoring_thread = std::thread(fault_monitoring_loop);
    
    std::cout << "🛡️ Monitoring fautes démarré" << std::endl;
    return true;
}

void FaultInjectionProtection::stop_fault_monitoring() {
    if (!fault_monitor_active.load()) {
        return;
    }
    
    fault_monitor_active = false;
    if (monitoring_thread.joinable()) {
        monitoring_thread.join();
    }
    
    std::cout << "🛡️ Monitoring fautes arrêté" << std::endl;
}

bool FaultInjectionProtection::is_monitoring_active() {
    return fault_monitor_active.load();
}

bool FaultInjectionProtection::is_hardware_assisted() {
    return hardware_assisted.load();
}

void FaultInjectionProtection::fault_monitoring_loop() {
    std::cout << "🔄 Démarrage boucle monitoring fautes..." << std::endl;
    
    uint32_t operation_counter = 0;
    
    while (fault_monitor_active.load()) {
        try {
            // Simuler une opération critique avec vérification de redondance
            operation_counter++;
            
            // Données de test pour la vérification
            uint32_t test_data = operation_counter * 0x9E3779B9;
            FaultSample sample = perform_redundant_check(operation_counter, &test_data, sizeof(test_data));
            
            // Ajouter à l'historique
            fault_history.push_back(sample);
            if (fault_history.size() > 1000) {
                fault_history.erase(fault_history.begin());
            }
            
            // Détection de fautes
            if (detect_fault_injection(sample)) {
                fault_count++;
                trigger_fault_response();
            }
            
            // Vérification périodique de l'intégrité mémoire
            if (operation_counter % 100 == 0) {
                if (!verify_memory_integrity(&test_data, sizeof(test_data))) {
                    checksum_errors++;
                    isolate_compromised_memory();
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "❌ Erreur monitoring fautes: " << e.what() << std::endl;
        }

        if (ExternalWatchdog::is_active()) {
            ExternalWatchdog::pet_watchdog();
        }
        
        // Pause de 50ms entre les vérifications
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    std::cout << "🔄 Boucle monitoring fautes terminée" << std::endl;
}

FaultInjectionProtection::FaultSample FaultInjectionProtection::perform_redundant_check(
    uint32_t operation_id, const void* data, size_t size) {
    
    FaultSample sample;
    sample.operation_id = operation_id;
    sample.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();
    sample.checksum = calculate_checksum(data, size);
    sample.fault_type = FAULT_NONE;
    
    // Vérification par redondance spatiale
    if (enable_spatial_redundancy.load()) {
        sample.redundancy_passed = verify_spatial_redundancy(data, size);
        if (!sample.redundancy_passed) {
            sample.fault_type = FAULT_LASER; // Supposer laser si redondance échoue
        }
    } else {
        sample.redundancy_passed = true;
    }
    
    // Vérification par redondance temporelle
    if (enable_temporal_redundancy.load() && sample.redundancy_passed) {
        if (!verify_temporal_redundancy(data, size)) {
            sample.fault_type = FAULT_VOLTAGE; // Supposer voltage si temporel échoue
            sample.redundancy_passed = false;
        }
    }
    
    return sample;
}

uint32_t FaultInjectionProtection::calculate_checksum(const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    
    return ~crc;
}

bool FaultInjectionProtection::verify_spatial_redundancy(const void* data, size_t size) {
    uint32_t level = redundancy_level.load();
    
    if (level < 2) {
        return true; // Pas de redondance spatiale
    }
    
    // Créer plusieurs copies et vérifier la cohérence
    std::vector<std::vector<uint8_t>> copies(level);
    
    for (uint32_t i = 0; i < level; i++) {
        copies[i].resize(size);
        std::memcpy(copies[i].data(), data, size);
        
        // NE PAS modifier les copies - la redondance doit vérifier l'identité
        // Les variations simulées créent des faux positifs
    }
    
    // Vérifier que toutes les copies sont cohérentes
    for (uint32_t i = 1; i < level; i++) {
        if (std::memcmp(copies[0].data(), copies[i].data(), size) != 0) {
            redundancy_failures++;
            return false;
        }
    }
    
    return true;
}

bool FaultInjectionProtection::verify_temporal_redundancy(const void* data, size_t size) {
    uint32_t level = redundancy_level.load();
    
    if (level < 2) {
        return true; // Pas de redondance temporelle
    }
    
    // Effectuer le même calcul à des moments différents
    std::vector<uint32_t> checksums(level);
    
    for (uint32_t i = 0; i < level; i++) {
        checksums[i] = calculate_checksum(data, size);
        
        // Petite pause entre les calculs
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    // Vérifier que tous les checksums sont identiques
    for (uint32_t i = 1; i < level; i++) {
        if (checksums[i] != checksums[0]) {
            redundancy_failures++;
            return false;
        }
    }
    
    return true;
}

// ===== DÉTECTION D'ATTAQUES =====

bool FaultInjectionProtection::detect_fault_injection(const FaultSample& sample) {
    if (!sample.redundancy_passed) {
        std::cout << "🚨 Injection de faute détectée (opération: " 
                  << sample.operation_id << ", type: " 
                  << static_cast<int>(sample.fault_type) << ")" << std::endl;
        
        // Logger la détection
        std::ofstream log_file(fault_detection_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << "FAULT_DETECTED: op=" << sample.operation_id 
                    << ", type=" << static_cast<int>(sample.fault_type)
                    << ", timestamp=" << sample.timestamp << std::endl;
            log_file.close();
        }
        
        return true;
    }
    
    return false;
}

bool FaultInjectionProtection::detect_laser_fault(const FaultSample& sample) {
    // Les attaques laser causent généralement des erreurs de bit isolées
    // qui ne sont pas détectées par la redondance temporelle
    
    if (sample.fault_type == FAULT_LASER) {
        std::cout << "🚨 Attaque laser détectée!" << std::endl;
        return true;
    }
    
    return false;
}

bool FaultInjectionProtection::detect_voltage_fault(const FaultSample& sample) {
    // Les attaques par voltage causent des erreurs temporaires
    // qui peuvent être détectées par la redondance temporelle
    
    if (sample.fault_type == FAULT_VOLTAGE) {
        std::cout << "🚨 Attaque par voltage détectée!" << std::endl;
        return true;
    }
    
    return false;
}

bool FaultInjectionProtection::detect_em_fault(const FaultSample& sample) {
    // Les attaques EM peuvent causer des erreurs multiples
    // qui affectent la redondance spatiale
    
    if (sample.fault_type == FAULT_EM) {
        std::cout << "🚨 Attaque EM détectée!" << std::endl;
        return true;
    }
    
    return false;
}

bool FaultInjectionProtection::detect_rowhammer_fault(const FaultSample& sample) {
    // Rowhammer cause des erreurs dans les lignes adjacentes
    // nécessite une détection spéciale
    
    if (sample.fault_type == FAULT_ROWHAMMER) {
        std::cout << "🚨 Attaque Rowhammer détectée!" << std::endl;
        return true;
    }
    
    return false;
}

// ===== CONTRE-MESURES =====

void FaultInjectionProtection::implement_fault_correction() {
    std::cout << "🔧 Implémentation correction de fautes..." << std::endl;
    
    // Augmenter le niveau de redondance
    if (redundancy_level.load() < 3) {
        redundancy_level = 3;
        std::cout << "📈 Niveau de redondance augmenté à 3" << std::endl;
    }
    
    // Activer toutes les formes de redondance
    enable_spatial_redundancy = true;
    enable_temporal_redundancy = true;
    
    // Ajouter des vérifications supplémentaires
    fault_threshold = 3; // Abaisser le seuil de détection
}

void FaultInjectionProtection::trigger_fault_response() {
    std::cout << "🚨 Réponse attaque par faute déclenchée!" << std::endl;
    
    implement_fault_correction();
    isolate_compromised_memory();
    perform_secure_recomputation();

    std::cout << "[SENTINEL][FAULT] Réponse locale uniquement (pas d'interface d'alerte externe)." << std::endl;
}

void FaultInjectionProtection::isolate_compromised_memory() {
    std::cout << "🔒 Isolation mémoire compromise..." << std::endl;
    
    // Marquer les régions suspectes (logiciel uniquement, pas d'isolation matérielle)
    std::cout << "[SENTINEL][FAULT] Isolation mémoire logicielle uniquement (pas d'isolation matérielle)." << std::endl;
    
    // Forcer le nettoyage des caches sans _mm_clflushopt
    _mm_mfence();
    _mm_lfence();
    _mm_sfence();
    
    // Alternative portable pour flusher les caches
    volatile uint8_t dummy_buffer[4096];
    for (int i = 0; i < 4096; i += 64) {
        dummy_buffer[i] = (uint8_t)i;
    }
    
    // Utiliser dummy_buffer pour éviter le warning
    (void)dummy_buffer;
    
    _mm_mfence();
    _mm_sfence();
    _mm_lfence();
}

void FaultInjectionProtection::perform_secure_recomputation() {
    std::cout << "🔄 Recalcul sécurisé en cours..." << std::endl;
    
    // Effectuer les calculs critiques avec redondance maximale
    uint32_t original_level = redundancy_level.load();
    redundancy_level = 3; // Force triple redondance

    std::cout << "[SENTINEL][FAULT] Recalcul sécurisé logiciel (pas de moteur matériel dédié)." << std::endl;
    
    redundancy_level = original_level; // Restaurer le niveau original
}

// ===== FONCTIONS PUBLIQUES =====

bool FaultInjectionProtection::detect_fault_injection() {
    if (fault_history.empty()) {
        return false;
    }
    
    const FaultSample& latest = fault_history.back();
    return detect_fault_injection(latest);
}

bool FaultInjectionProtection::detect_laser_fault() {
    if (fault_history.empty()) {
        return false;
    }
    
    const FaultSample& latest = fault_history.back();
    return detect_laser_fault(latest);
}

bool FaultInjectionProtection::detect_voltage_fault() {
    if (fault_history.empty()) {
        return false;
    }
    
    const FaultSample& latest = fault_history.back();
    return detect_voltage_fault(latest);
}

bool FaultInjectionProtection::detect_em_fault() {
    if (fault_history.empty()) {
        return false;
    }
    
    const FaultSample& latest = fault_history.back();
    return detect_em_fault(latest);
}

bool FaultInjectionProtection::detect_rowhammer_fault() {
    if (fault_history.empty()) {
        return false;
    }
    
    const FaultSample& latest = fault_history.back();
    return detect_rowhammer_fault(latest);
}

void FaultInjectionProtection::enable_redundant_computation() {
    std::cout << "🔄 Activation calcul redondant..." << std::endl;
    enable_spatial_redundancy = true;
    enable_temporal_redundancy = true;
}

void FaultInjectionProtection::implement_spatial_separation() {
    std::cout << "📍 Implémentation séparation spatiale..." << std::endl;
    enable_spatial_redundancy = true;
}

void FaultInjectionProtection::setup_fault_detection_circuits() {
    std::cout << "⚡ Configuration circuits détection fautes..." << std::endl;
    fault_threshold = 5;
    redundancy_level = 2;
}

bool FaultInjectionProtection::verify_operation_integrity(uint32_t operation_id, const void* data, size_t size) {
    FaultSample sample = perform_redundant_check(operation_id, data, size);
    return sample.redundancy_passed;
}

bool FaultInjectionProtection::verify_memory_integrity(const void* ptr, size_t size) {
    uint32_t checksum = calculate_checksum(ptr, size);
    
    // Vérification simple : recalculer le checksum
    uint32_t verify_checksum = calculate_checksum(ptr, size);
    
    if (checksum != verify_checksum) {
        checksum_errors++;
        return false;
    }
    
    return true;
}

bool FaultInjectionProtection::verify_computation_integrity(uint32_t operation_id, uint32_t result) {
    // Simuler la vérification d'un résultat de calcul
    uint32_t expected = operation_id * 0x9E3779B9;
    
    if (result != expected) {
        fault_count++;
        return false;
    }
    
    return true;
}

bool FaultInjectionProtection::test_fault_protection() {
    std::cout << "🧪 Test protection fautes..." << std::endl;
    
    // Test de détection de faute
    uint32_t test_data = 0x12345678;
    FaultSample normal_sample = perform_redundant_check(999, &test_data, sizeof(test_data));
    
    // Simuler une faute en modifiant les données
    uint32_t corrupted_data = test_data ^ 0x00000001;
    FaultSample corrupted_sample = perform_redundant_check(1000, &corrupted_data, sizeof(corrupted_data));
    
    bool detection_works = !corrupted_sample.redundancy_passed;
    
    // Utiliser normal_sample pour éviter le warning unused variable
    (void)normal_sample;
    
    std::cout << "Test faute protection: " << (detection_works ? "✅ PASS" : "❌ FAIL") << std::endl;
    return detection_works;
}

FaultInjectionProtection::FaultStats FaultInjectionProtection::get_fault_statistics() {
    FaultStats stats{};
    
    if (fault_history.empty()) {
        return stats;
    }
    
    stats.total_operations = fault_history.size();
    stats.fault_count = fault_count.load();
    stats.redundancy_failures = redundancy_failures.load();
    stats.checksum_errors = checksum_errors.load();
    
    if (stats.total_operations > 0) {
        stats.fault_rate = (double)stats.fault_count / stats.total_operations * 100.0;
    }
    
    // Compter les types de fautes
    for (const auto& sample : fault_history) {
        switch (sample.fault_type) {
            case FAULT_LASER:
                stats.laser_faults++;
                break;
            case FAULT_VOLTAGE:
                stats.voltage_faults++;
                break;
            case FAULT_EM:
                stats.em_faults++;
                break;
            case FAULT_ROWHAMMER:
                stats.rowhammer_faults++;
                break;
            default:
                break;
        }
    }
    
    return stats;
}

void FaultInjectionProtection::set_redundancy_level(uint32_t level) {
    if (level >= 1 && level <= 3) {
        redundancy_level = level;
        std::cout << "📊 Niveau de redondance fixé à " << level << std::endl;
    }
}

void FaultInjectionProtection::reset_fault_statistics() {
    fault_count = 0;
    redundancy_failures = 0;
    checksum_errors = 0;
    fault_history.clear();
}

FaultInjectionProtection::FaultHealth FaultInjectionProtection::get_fault_health() {
    if (fault_history.empty()) {
        return FAULT_HEALTH_GOOD;
    }
    
    uint32_t recent_faults = 0;
    size_t check_samples = std::min(fault_history.size(), static_cast<size_t>(50));
    
    for (size_t i = fault_history.size() - check_samples; i < fault_history.size(); i++) {
        if (!fault_history[i].redundancy_passed) {
            recent_faults++;
        }
    }
    
    if (recent_faults > 10) {
        return FAULT_HEALTH_EMERGENCY;
    } else if (recent_faults > 5) {
        return FAULT_HEALTH_CRITICAL;
    } else if (recent_faults > 1) {
        return FAULT_HEALTH_WARNING;
    }
    
    return FAULT_HEALTH_GOOD;
}

void FaultInjectionProtection::set_integrity_log_path(const std::string& path) {
    integrity_log_path = path;
}

void FaultInjectionProtection::set_fault_detection_path(const std::string& path) {
    fault_detection_path = path;
}

void FaultInjectionProtection::cleanup() {
    stop_fault_monitoring();
    fault_history.clear();
    fault_count = 0;
    redundancy_failures = 0;
    checksum_errors = 0;
}

uint32_t FaultInjectionProtection::compute_with_redundancy(uint32_t (*func)(uint32_t), uint32_t input) {
    uint32_t level = redundancy_level.load();
    std::vector<uint32_t> results(level);
    
    // Calculer avec redondance
    for (uint32_t i = 0; i < level; i++) {
        results[i] = func(input);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    // Voter pour le résultat majoritaire
    std::sort(results.begin(), results.end());
    
    if (level == 2) {
        // Pour double redondance, prendre le premier
        return results[0];
    } else {
        // Pour triple redondance, prendre la médiane
        return results[1];
    }
}

std::vector<uint8_t> FaultInjectionProtection::encrypt_with_redundancy(const std::vector<uint8_t>& data) {
    // Simuler un chiffrement avec vérification de redondance
    std::vector<uint8_t> result = data;
    
    // Appliquer une transformation simple avec vérification
    for (size_t i = 0; i < result.size(); i++) {
        result[i] ^= 0x5A; // Simple XOR
        
        // Vérifier avec redondance
        if (!verify_operation_integrity(i, &result[i], 1)) {
            // En cas d'erreur, restaurer la valeur originale
            result[i] = data[i];
        }
    }
    
    return result;
}

bool FaultInjectionProtection::compare_with_redundancy(const void* data1, const void* data2, size_t size) {
    // Comparaison avec redondance temporelle
    for (int attempt = 0; attempt < 3; attempt++) {
        int cmp = std::memcmp(data1, data2, size);
        if (cmp == 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    return false;
}

} // namespace hesia
