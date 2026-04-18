#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "buffer_pool.hpp"
#include <memory>
#include <unordered_map>
#include <string>

namespace hesia {

class BufferManager {
private:
    std::unordered_map<std::string, std::shared_ptr<void>> pools;
    mutable std::mutex mutex;
    
public:
    static BufferManager& getInstance() {
        static BufferManager instance;
        return instance;
    }
    
    template<typename T>
    std::shared_ptr<BufferPool<T>> getPool(const std::string& name, 
                                          size_t initial_size = 10, 
                                          size_t max_size = 50) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = pools.find(name);
        if (it != pools.end()) {
            return std::static_pointer_cast<BufferPool<T>>(it->second);
        }
        
        auto pool = std::make_shared<BufferPool<T>>(initial_size, max_size, name);
        pools[name] = std::static_pointer_cast<void>(pool);
        return pool;
    }
    
    std::shared_ptr<MatPool> getMatPool(const std::string& name,
                                       int rows, int cols, int type,
                                       size_t initial_size = 10,
                                       size_t max_size = 50) {
        std::lock_guard<std::mutex> lock(mutex);
        
        auto it = pools.find(name);
        if (it != pools.end()) {
            return std::static_pointer_cast<MatPool>(it->second);
        }
        
        auto pool = std::make_shared<MatPool>(rows, cols, type, initial_size, max_size, name);
        pools[name] = std::static_pointer_cast<void>(pool);
        return pool;
    }
    
    void clearPool(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = pools.find(name);
        if (it != pools.end()) {
            // Pas de clear possible avec void* - supprimer simplement
            pools.erase(it);
        }
    }
    
    void clearAll() {
        std::lock_guard<std::mutex> lock(mutex);
        pools.clear();
    }
    
    // Statistiques
    struct PoolStats {
        size_t available;
        size_t in_use;
        size_t total;
    };
    
    template<typename T>
    PoolStats getStats(const std::string& name) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = pools.find(name);
        if (it != pools.end()) {
            if (auto pool = std::static_pointer_cast<BufferPool<T>>(it->second)) {
                return {pool->available_count(), pool->in_use_count(), pool->total_count()};
            }
        }
        return {0, 0, 0};
    }
    
private:
    BufferManager() = default;
    ~BufferManager() = default;
    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;
};

// Macros pour un accès facile
#define GET_MAT_POOL(name, rows, cols, type) \
    BufferManager::getInstance().getMatPool(name, rows, cols, type)

#define GET_BUFFER_POOL(T, name) \
    BufferManager::getInstance().getPool<T>(name)

} // namespace hesia

#endif // BUFFER_MANAGER_HPP
