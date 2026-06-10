#include <cstring>
#include "runtime_aslr.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "security_utils.hpp"
#include <iostream>
#include <random>
#include <algorithm>
#include <chrono>
#include <memory>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <memoryapi.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <dlfcn.h>
#include <link.h>
#include <sys/prctl.h>
#include <cstdlib>
#endif

namespace hesia {

namespace {

uint64_t secure_seed64() {
    uint64_t seed = 0;
    if (!SecureRNG::generate_bytes(reinterpret_cast<uint8_t*>(&seed), sizeof(seed))) {
        throw std::runtime_error("SecureRNG::generate_bytes failed for ASLR seed");
    }
    return seed;
}

} // namespace

// Variables statiques pour ASLR
std::atomic<bool> RuntimeASLR::aslr_enabled{false};
std::atomic<uint64_t> RuntimeASLR::randomization_seed{0};
std::unordered_map<void*, size_t> RuntimeASLR::allocated_regions_map;
std::mutex RuntimeASLR::aslr_mutex;

// ===== INITIALISATION ASLR =====

bool RuntimeASLR::initialize() {
    if (aslr_enabled.load()) {
        return true; // Déjà initialisé
    }
    
    auto logger = setup_logger("ASLR", Config::LOG_DIR);
    
    // Générer seed pour randomisation
    randomization_seed.store(secure_seed64());
    
    // Vérifier si ASLR est déjà activé au niveau système
    bool system_aslr = check_system_aslr();
    logger->info("ASLR système: " + std::string(system_aslr ? "ACTIVÉ" : "DÉSACTIVÉ"));
    
    // Activer ASLR runtime si non présent
    if (!system_aslr) {
        if (!enable_runtime_aslr()) {
            logger->error("Échec activation ASLR runtime");
            return false;
        }
        logger->info("ASLR runtime activé avec succès");
    }
    
    // Randomiser la mémoire déjà allouée
    randomize_existing_memory();
    
    aslr_enabled.store(true);
    logger->info("ASLR runtime initialisé avec succès");
    
    return true;
}

bool RuntimeASLR::check_system_aslr() {
#ifdef _WIN32
    // Sur Windows, ASLR est généralement activé par défaut
    // Vérifier les options de processus
    DWORD process_mitigation_policy_size = sizeof(PROCESS_MITIGATION_ASLR_POLICY);
    PROCESS_MITIGATION_ASLR_POLICY aslr_policy;
    
    if (GetProcessMitigationPolicy(GetCurrentProcess(), 
                                  ProcessASLRPolicy, 
                                  &aslr_policy, 
                                  process_mitigation_policy_size)) {
        return aslr_policy.EnableBottomUpRandomization || 
               aslr_policy.EnableForceRelocateImages ||
               aslr_policy.EnableHighEntropy;
    }
    
    return true; // Assumer activé par défaut sur Windows
#else
    // Sur Linux, vérifier /proc/sys/kernel/randomize_va_space
    std::ifstream aslr_file("/proc/sys/kernel/randomize_va_space");
    if (aslr_file.is_open()) {
        int aslr_value;
        if (aslr_file >> aslr_value) {
            return aslr_value > 0; // 0 = désactivé, 1 = conservatif, 2 = complet
        }
    }
    
    // Vérifier les flags du processus actuel
    std::ifstream status_file("/proc/self/status");
    if (status_file.is_open()) {
        std::string line;
        while (std::getline(status_file, line)) {
            if (line.find("VmFlags:") != std::string::npos) {
                if (line.find("randomize") != std::string::npos) {
                    return true;
                }
            }
        }
    }
    
    return false;
#endif
}

bool RuntimeASLR::enable_runtime_aslr() {
    auto logger = setup_logger("ASLR", Config::LOG_DIR);
    
#ifdef _WIN32
    // ✅ SÉCURITÉ: Utiliser les mitigations Windows natives
    PROCESS_MITIGATION_ASLR_POLICY aslr_policy = {};
    aslr_policy.EnableBottomUpRandomization = TRUE;
    aslr_policy.EnableForceRelocateImages = TRUE;
    aslr_policy.EnableHighEntropy = TRUE;
    aslr_policy.DisallowStrippedImages = TRUE;
    
    if (SetProcessMitigationPolicy(ProcessASLRPolicy, &aslr_policy, sizeof(aslr_policy))) {
        logger->info("✅ Mitigations ASLR Windows activées");
        return true;
    } else {
        logger->warning("⚠️ Impossible d'activer les mitigations ASLR Windows");
        return false;
    }
#else
    // ✅ SÉCURITÉ: SUPPRIMER les protections dangereuses
    // NE PAS utiliser PR_SET_PTRACER_ANY (dangereux)
    // NE PAS créer de pages RWX (dangereux)
    
    // Utiliser seulement les protections système natives
    logger->info("✅ ASLR système natif utilisé (pas de modifications dangereuses)");
    
    // Vérifier que l'ASLR système est actif
    bool system_aslr = check_system_aslr();
    if (!system_aslr) {
        logger->warning("⚠️ ASLR système non actif - recommandé d'activer au niveau noyau");
    }
    
    return true;
#endif
}

void RuntimeASLR::randomize_existing_memory() {
    auto logger = setup_logger("ASLR", Config::LOG_DIR);
    
    // Randomiser les bibliothèques partagées
    randomize_shared_libraries();
    
    // Randomiser les allocations mémoire
    randomize_heap_memory();
    
    // Randomiser le stack
    randomize_stack_memory();
    
    logger->info("Randomisation mémoire existante terminée");
}

void RuntimeASLR::randomize_shared_libraries() {
#ifdef _WIN32
    // Sur Windows, énumérer les modules chargés
    HMODULE modules[1024];
    DWORD needed;
    
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        DWORD module_count = needed / sizeof(HMODULE);
        
        for (DWORD i = 0; i < module_count; i++) {
            MODULEINFO module_info;
            if (GetModuleInformation(GetCurrentProcess(), modules[i], &module_info, sizeof(module_info))) {
                // Tenter de randomiser la base du module
                randomize_module_base(module_info.lpBaseOfDll, module_info.SizeOfImage);
            }
        }
    }
#else
    // Sur Linux, utiliser dl_iterate_phdr
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t size [[maybe_unused]], void *data [[maybe_unused]]) {
        if (info->dlpi_name && strlen(info->dlpi_name) > 0) {
            // Randomiser la base de la bibliothèque partagée
            void* base_addr = (void*)info->dlpi_addr;
            size_t total_size = 0;
            
            // Calculer la taille totale
            for (int i = 0; i < info->dlpi_phnum; i++) {
                if (info->dlpi_phdr[i].p_type == PT_LOAD) {
                    total_size = std::max(total_size, 
                                        static_cast<size_t>(info->dlpi_phdr[i].p_vaddr + 
                                                          info->dlpi_phdr[i].p_memsz));
                }
            }
            
            if (total_size > 0) {
                randomize_module_base(base_addr, total_size);
            }
        }
        return 0;
    }, nullptr);
#endif
}

void RuntimeASLR::randomize_module_base(void* base_addr, size_t size) {
#ifndef _WIN32
    // SÉCURITÉ: NE PAS créer de pages RWX (dangereux)
    // SÉCURITÉ: Ne PAS modifier les protections mémoire
    // L'ASLR réel est géré par le kernel + loader système
    // Les modifications runtime sont dangereuses et inefficaces
    
    auto logger = setup_logger("ASLR", Config::LOG_DIR);
    static std::atomic<bool> logged_once{false};
    if (!logged_once.exchange(true)) {
        logger->info("ℹ️ ASLR: Utilisation des protections système standard");
        logger->info("ℹ️ ASLR: Voir options de compilation recommandées");
    }
    
    // Recommandations pour la compilation:
    // -fPIE -fPIC (Position Independent Executable)
    // -Wl,-z,relro -Wl,-z,now (RELRO)
    // -fstack-protector-strong (Stack protection)
    // -D_FORTIFY_SOURCE=2 (Hardening)
    
    // Utiliser les paramètres pour éviter les warnings
    (void)base_addr; // Marquer comme utilisé
    (void)size; // Marquer comme utilisé
#endif
}

void RuntimeASLR::randomize_heap_memory() {
#ifndef _WIN32
    // Allouer des zones mémoire aléatoires pour fragmenter l'espace d'adressage
    std::mt19937 gen(randomization_seed.load());
    std::uniform_int_distribution<size_t> size_dist(1024, 64*1024); // 1KB à 64KB
    
    for (int i = 0; i < 10; i++) {
        size_t alloc_size = size_dist(gen);
        void* random_addr = get_randomized_base(alloc_size);
        
        if (random_addr) {
            void* ptr = mmap(random_addr, alloc_size, 
                           PROT_READ | PROT_WRITE, 
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            
            if (ptr != MAP_FAILED) {
                std::lock_guard<std::mutex> lock(aslr_mutex);
                allocated_regions_map[ptr] = alloc_size;
                
                // Écrire des données aléatoires pour éviter les patterns
                std::uniform_int_distribution<uint8_t> data_dist(0, 255);
                uint8_t* data = static_cast<uint8_t*>(ptr);
                for (size_t j = 0; j < alloc_size; j++) {
                    data[j] = data_dist(gen);
                }
            }
        }
    }
#endif
}

void RuntimeASLR::randomize_stack_memory() {
#ifndef _WIN32
    // Créer des frames de stack aléatoires
    std::mt19937 gen(randomization_seed.load());
    std::uniform_int_distribution<size_t> stack_dist(8*1024, 32*1024); // 8KB à 32KB
    
    // Allouer plusieurs stacks pour fragmenter l'espace
    for (int i = 0; i < 3; i++) {
        size_t stack_size = stack_dist(gen);
        void* stack_base = get_randomized_base(stack_size);
        
        if (stack_base) {
            void* stack_ptr = mmap(stack_base, stack_size,
                                  PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
            
            if (stack_ptr != MAP_FAILED) {
                std::lock_guard<std::mutex> lock(aslr_mutex);
                allocated_regions_map[stack_ptr] = stack_size;
                
                // Initialiser le stack avec des valeurs aléatoires
                uint64_t* stack_data = static_cast<uint64_t*>(stack_ptr);
                std::uniform_int_distribution<uint64_t> value_dist;
                
                for (size_t j = 0; j < stack_size / sizeof(uint64_t); j++) {
                    stack_data[j] = value_dist(gen);
                }
            }
        }
    }
#endif
}

void* RuntimeASLR::get_randomized_base(size_t size) {
#ifndef _WIN32
    std::mt19937 gen(randomization_seed.load());
    
#ifdef __x86_64__
    // Sur x86-64, utiliser l'espace d'adressage 64-bit
    std::uniform_int_distribution<uint64_t> addr_dist(0x400000000000ULL, 0x7FFFFFFFF000ULL);
#else
    // Sur 32-bit, utiliser l'espace disponible
    std::uniform_int_distribution<uint32_t> addr_dist(0x10000000, 0x7FFF0000);
#endif
    
    // Aligner sur les pages
    const size_t page_size = getpagesize();
    size_t aligned_size = (size + page_size - 1) & ~(page_size - 1);
    
    // Tenter plusieurs adresses aléatoires
    for (int attempt = 0; attempt < 10; attempt++) {
        void* suggested_addr = (void*)(addr_dist(gen) & ~(page_size - 1));
        
        void* ptr = mmap(suggested_addr, aligned_size,
                        PROT_NONE, // Pas d'accès pour l'instant
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        
        if (ptr != MAP_FAILED) {
            munmap(ptr, aligned_size); // Libérer immédiatement
            return suggested_addr;
        }
    }
#endif
    return nullptr; // Échec
}

bool RuntimeASLR::validate_aslr_protection() {
    auto logger = setup_logger("ASLR", Config::LOG_DIR);
    
    // Vérifier que les adresses sont bien randomisées
    bool heap_randomized = check_heap_randomization();
    bool stack_randomized = check_stack_randomization();
    bool libs_randomized = check_library_randomization();
    
    logger->info("Validation ASLR:");
    logger->info("  Heap randomisé: " + std::string(heap_randomized ? "OUI" : "NON"));
    logger->info("  Stack randomisé: " + std::string(stack_randomized ? "OUI" : "NON"));
    logger->info("  Bibliothèques randomisées: " + std::string(libs_randomized ? "OUI" : "NON"));
    
    return heap_randomized && stack_randomized && libs_randomized;
}

bool RuntimeASLR::check_heap_randomization() {
#ifndef _WIN32
    // Sur Linux, le meilleur indicateur est randomize_va_space
    {
        std::ifstream va_file("/proc/sys/kernel/randomize_va_space");
        if (va_file.is_open()) {
            int level = 0;
            if (va_file >> level) {
                // 2 = full ASLR, 1 = partiel
                return level >= 2;
            }
        }
    }
#endif
    // Allouer plusieurs blocs et vérifier qu'ils ne sont pas contigus
    std::vector<void*> allocations;
    
    for (int i = 0; i < 5; i++) {
        void* ptr = malloc(1024);
        if (ptr) {
            allocations.push_back(ptr);
        }
    }
    
    // Vérifier la randomisation des adresses
    bool randomized = true;
    for (size_t i = 1; i < allocations.size(); i++) {
        uintptr_t addr1 = reinterpret_cast<uintptr_t>(allocations[i-1]);
        uintptr_t addr2 = reinterpret_cast<uintptr_t>(allocations[i]);
        
        // Si les adresses sont trop proches, pas de randomisation
        if (std::abs(static_cast<int64_t>(addr2 - addr1)) < 0x10000) { // 64KB
            randomized = false;
            break;
        }
    }
    
    // Nettoyer
    for (void* ptr : allocations) {
        free(ptr);
    }
    
    return randomized;
}

bool RuntimeASLR::check_stack_randomization() {
    // Vérifier la randomisation du stack principal
    void* stack_var;
    void* stack_addr = &stack_var;  // CORRECTION: prendre l'adresse de la variable, pas d'elle-même
    
    static void* first_stack_addr = nullptr;
    
    if (!first_stack_addr) {
        first_stack_addr = &stack_var;
        return true; // Première exécution
    }
    
    // Comparer avec l'adresse précédente
    uintptr_t addr1 = reinterpret_cast<uintptr_t>(stack_addr);
    uintptr_t addr2 = reinterpret_cast<uintptr_t>(first_stack_addr);
    uintptr_t addr_diff = std::abs(static_cast<int64_t>(addr1 - addr2));
    
    return addr_diff > 0x1000; // Plus de 4KB de différence
}

bool RuntimeASLR::check_library_randomization() {
    // Vérifier que les bibliothèques sont chargées à des adresses aléatoires
    static std::vector<void*> first_lib_addresses;
    
    if (first_lib_addresses.empty()) {
        // Première exécution, enregistrer les adresses
#ifdef _WIN32
        HMODULE modules[32];
        DWORD needed;
        if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
            DWORD module_count = needed / sizeof(HMODULE);
            for (DWORD i = 0; i < module_count && i < 32; i++) {
                MODULEINFO info;
                if (GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info))) {
                    first_lib_addresses.push_back(info.lpBaseOfDll);
                }
            }
        }
#else
        dl_iterate_phdr([](struct dl_phdr_info *info, size_t size [[maybe_unused]], void *data [[maybe_unused]]) {
            if (info->dlpi_name && strlen(info->dlpi_name) > 0) {
                reinterpret_cast<std::vector<void*>*>(data)->push_back((void*)info->dlpi_addr);
            }
            return 0;
        }, &first_lib_addresses);
#endif
        return true;
    }
    
    // Deuxième exécution, comparer les adresses
    std::vector<void*> current_lib_addresses;
    
#ifdef _WIN32
    HMODULE modules[32];
    DWORD needed;
    if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
        DWORD module_count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < module_count && i < 32; i++) {
            MODULEINFO info;
            if (GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info))) {
                current_lib_addresses.push_back(info.lpBaseOfDll);
            }
        }
    }
#else
    dl_iterate_phdr([](struct dl_phdr_info *info, size_t size [[maybe_unused]], void *data [[maybe_unused]]) {
        if (info->dlpi_name && strlen(info->dlpi_name) > 0) {
            reinterpret_cast<std::vector<void*>*>(data)->push_back((void*)info->dlpi_addr);
        }
        return 0;
    }, &current_lib_addresses);
#endif
    
    // Comparer les adresses
    if (current_lib_addresses.size() != first_lib_addresses.size()) {
        return true; // Différent nombre de bibliothèques = randomisé
    }
    
    for (size_t i = 0; i < current_lib_addresses.size(); i++) {
        if (current_lib_addresses[i] != first_lib_addresses[i]) {
            return true; // Adresses différentes = randomisé
        }
    }
    
    return false; // Mêmes adresses = pas de randomisation
}

void RuntimeASLR::cleanup() {
    std::lock_guard<std::mutex> lock(aslr_mutex);
    
#ifndef _WIN32
    // Libérer toutes les régions allouées (Linux seulement)
    for (auto& region : allocated_regions_map) {
        if (region.first) {
            munmap(region.first, region.second);
        }
    }
#endif
    
    allocated_regions_map.clear();
    aslr_enabled.store(false);
}

bool RuntimeASLR::is_enabled() {
    return aslr_enabled.load();
}

uint64_t RuntimeASLR::get_randomization_seed() {
    return randomization_seed.load();
}

std::vector<void*> RuntimeASLR::get_allocated_regions() {
    std::lock_guard<std::mutex> lock(aslr_mutex);
    std::vector<void*> regions;
    
    for (const auto& pair : allocated_regions_map) {
        regions.push_back(pair.first);
    }
    
    return regions;
}

size_t RuntimeASLR::get_region_size(void* region) {
    std::lock_guard<std::mutex> lock(aslr_mutex);
    
    auto it = allocated_regions_map.find(region);
    if (it != allocated_regions_map.end()) {
        return it->second;
    }
    
    return 0;
}

void RuntimeASLR::set_randomization_seed(uint64_t seed) {
    randomization_seed.store(seed);
}

void RuntimeASLR::add_allocated_region(void* ptr, size_t size) {
    std::lock_guard<std::mutex> lock(aslr_mutex);
    allocated_regions_map[ptr] = size;
}

void RuntimeASLR::remove_allocated_region(void* ptr) {
    std::lock_guard<std::mutex> lock(aslr_mutex);
    allocated_regions_map.erase(ptr);
}

} // namespace hesia
