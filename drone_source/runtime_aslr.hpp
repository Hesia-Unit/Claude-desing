#ifndef RUNTIME_ASLR_HPP
#define RUNTIME_ASLR_HPP

#include <vector>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <fstream>
#include <unordered_map>

namespace hesia {

// Runtime ASLR (Address Space Layout Randomization)
// Implémentation de randomisation d'espace d'adressage au runtime

class RuntimeASLR {
private:
    static std::atomic<bool> aslr_enabled;
    static std::atomic<uint64_t> randomization_seed;
    static std::unordered_map<void*, size_t> allocated_regions_map;  // Changé de vector à unordered_map
    static std::mutex aslr_mutex;
    
    // Fonctions internes
    static bool check_system_aslr();
    static bool enable_runtime_aslr();
    static void randomize_existing_memory();
    static void randomize_shared_libraries();
    static void randomize_heap_memory();
    static void randomize_stack_memory();
    static void randomize_module_base(void* base_addr, size_t size);
    
    // Validation de la randomisation
    static bool check_heap_randomization();
    static bool check_stack_randomization();
    static bool check_library_randomization();
    
public:
    // Initialisation et configuration
    static bool initialize();
    static void cleanup();
    static bool is_enabled();
    
    // Gestion de la randomisation
    static void* get_randomized_base(size_t size);
    static bool validate_aslr_protection();

    // Méthodes pour récupérer les régions allouées
    static std::vector<void*> get_allocated_regions();
    static size_t get_region_size(void* region);
    
    // Accès au seed de randomisation
    static uint64_t get_randomization_seed();
    
    // Configuration des paramètres ASLR
    static void set_randomization_seed(uint64_t seed);
    static void add_allocated_region(void* ptr, size_t size);
    static void remove_allocated_region(void* ptr);
};

} // namespace hesia

#endif // RUNTIME_ASLR_HPP