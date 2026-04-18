#ifndef BUFFER_POOL_HPP
#define BUFFER_POOL_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <opencv2/opencv.hpp>
#include "logger.hpp"
#include "config.hpp"

namespace hesia {

template<typename T>
class BufferPool {
private:
    std::queue<std::unique_ptr<T>> available;
    std::queue<std::unique_ptr<T>> in_use;
    mutable std::mutex mutex;
    std::condition_variable cv;
    size_t max_size;
    size_t total_created;
    std::string pool_name;

public:
    BufferPool(size_t initial_size = 10, size_t max_size = 50, const std::string& name = "BufferPool")
        : max_size(max_size), total_created(0), pool_name(name) {
        
        logger = setup_logger("buffer_pool_" + name, Config::LOG_DIR);
        
        // Pré-alllocation des buffers initiaux
        for (size_t i = 0; i < initial_size; ++i) {
            available.push(std::make_unique<T>());
            total_created++;
        }
        
        logger->info("BufferPool '" + name + "' initialisé avec " + std::to_string(initial_size) + " buffers");
    }
    
    ~BufferPool() {
        std::lock_guard<std::mutex> lock(mutex);
        logger->info("BufferPool '" + pool_name + "' détruit - " + std::to_string(total_created) + " buffers créés au total");
    }
    
    std::unique_ptr<T> acquire(int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex);
        
        // Attendre qu'un buffer soit disponible
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !available.empty(); })) {
            // Timeout: créer un nouveau buffer si on n'a pas dépassé le max
            if (total_created < max_size) {
                auto buffer = std::make_unique<T>();
                total_created++;
                in_use.push(std::move(buffer));
                logger->debug("BufferPool '" + pool_name + "' - nouveau buffer créé (total: " + std::to_string(total_created) + ")");
                return std::make_unique<T>();
            } else {
                logger->warning("BufferPool '" + pool_name + "' - timeout et max size atteint");
                return nullptr;
            }
        }
        
        auto buffer = std::move(available.front());
        available.pop();
        in_use.push(std::move(buffer));
        return std::move(buffer);
    }
    
    void release(std::unique_ptr<T> buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        
        // Retirer de in_use
        std::queue<std::unique_ptr<T>> new_in_use;
        bool found = false;
        while (!in_use.empty()) {
            auto current = std::move(in_use.front());
            in_use.pop();
            if (current.get() == buffer.get() && !found) {
                found = true;
                available.push(std::move(buffer));
            } else {
                new_in_use.push(std::move(current));
            }
        }
        in_use = std::move(new_in_use);
        
        if (found) {
            cv.notify_one();
        } else {
            logger->warning("BufferPool '" + pool_name + "' - tentative de release buffer non trouvé");
        }
    }
    
    size_t available_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return available.size();
    }
    
    size_t in_use_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return in_use.size();
    }
    
    size_t total_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return total_created;
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        available = std::queue<std::unique_ptr<T>>();
        in_use = std::queue<std::unique_ptr<T>>();
        logger->info("BufferPool '" + pool_name + "' vidé");
    }
    
private:
    std::shared_ptr<Logger> logger;
};

// Spécialisation pour cv::Mat avec dimensions pré-définies
class MatPool {
private:
    struct MatBuffer {
        cv::Mat mat;
        bool in_use;
        
        MatBuffer(int rows, int cols, int type) 
            : mat(rows, cols, type), in_use(false) {}
    };
    
    std::vector<std::unique_ptr<MatBuffer>> buffers;
    std::queue<size_t> available_indices;
    mutable std::mutex mutex;
    std::condition_variable cv;
    int rows, cols, type;
    size_t max_size;
    std::string pool_name;

public:
    MatPool(int rows, int cols, int type, size_t initial_size = 10, size_t max_size = 50, 
            const std::string& name = "MatPool")
        : rows(rows), cols(cols), type(type), max_size(max_size), pool_name(name) {
        
        logger = setup_logger("mat_pool_" + name, Config::LOG_DIR);
        
        // Pré-alllocation
        for (size_t i = 0; i < initial_size && i < max_size; ++i) {
            buffers.emplace_back(std::make_unique<MatBuffer>(rows, cols, type));
            available_indices.push(i);
        }
        
        logger->info("MatPool '" + name + "' (" + std::to_string(cols) + "x" + std::to_string(rows) + 
                    ") initialisé avec " + std::to_string(initial_size) + " buffers");
    }
    
    cv::Mat* acquire(int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex);
        
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !available_indices.empty(); })) {
            // Timeout: créer un nouveau buffer si possible
            if (buffers.size() < max_size) {
                size_t new_index = buffers.size();
                buffers.emplace_back(std::make_unique<MatBuffer>(rows, cols, type));
                buffers[new_index]->in_use = true;
                logger->debug("MatPool '" + pool_name + "' - nouveau buffer créé (total: " + std::to_string(buffers.size()) + ")");
                return &buffers[new_index]->mat;
            } else {
                logger->warning("MatPool '" + pool_name + "' - timeout et max size atteint");
                return nullptr;
            }
        }
        
        size_t index = available_indices.front();
        available_indices.pop();
        buffers[index]->in_use = true;
        return &buffers[index]->mat;
    }
    
    void release(cv::Mat* mat) {
        if (!mat) return;
        
        std::lock_guard<std::mutex> lock(mutex);
        
        // Trouver le buffer correspondant
        for (size_t i = 0; i < buffers.size(); ++i) {
            if (&buffers[i]->mat == mat && buffers[i]->in_use) {
                buffers[i]->in_use = false;
                available_indices.push(i);
                cv.notify_one();
                return;
            }
        }
        
        logger->warning("MatPool '" + pool_name + "' - tentative de release mat non trouvé");
    }
    
    size_t available_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return available_indices.size();
    }
    
    size_t total_count() const {
        std::lock_guard<std::mutex> lock(mutex);
        return buffers.size();
    }
    
private:
    std::shared_ptr<Logger> logger;
};

} // namespace hesia

#endif // BUFFER_POOL_HPP
