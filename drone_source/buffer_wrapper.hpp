#ifndef BUFFER_WRAPPER_HPP
#define BUFFER_WRAPPER_HPP

#include "buffer_manager.hpp"
#include <opencv2/opencv.hpp>
#include <memory>

namespace hesia {

// Wrapper RAII pour cv::Mat pool
class PooledMat {
private:
    cv::Mat* mat_ptr;
    std::shared_ptr<MatPool> pool;
    std::string pool_name;
    
public:
    PooledMat(const std::string& pool_name, int rows, int cols, int type)
        : pool_name(pool_name) {
        pool = GET_MAT_POOL(pool_name, rows, cols, type);
        mat_ptr = pool->acquire();
        
        if (!mat_ptr) {
            // Fallback: allocation directe si pool plein
            mat_ptr = new cv::Mat(rows, cols, type);
        }
    }
    
    ~PooledMat() {
        if (mat_ptr) {
            auto pool_instance = GET_MAT_POOL(pool_name, mat_ptr->rows, mat_ptr->cols, mat_ptr->type());
            if (pool_instance) {
                pool_instance->release(mat_ptr);
            } else {
                delete mat_ptr;
            }
        }
    }
    
    // Interdiction de copie
    PooledMat(const PooledMat&) = delete;
    PooledMat& operator=(const PooledMat&) = delete;
    
    // Autorisation de déplacement
    PooledMat(PooledMat&& other) noexcept 
        : mat_ptr(other.mat_ptr), pool(other.pool), pool_name(other.pool_name) {
        other.mat_ptr = nullptr;
    }
    
    PooledMat& operator=(PooledMat&& other) noexcept {
        if (this != &other) {
            if (mat_ptr) {
                auto pool_instance = GET_MAT_POOL(pool_name, mat_ptr->rows, mat_ptr->cols, mat_ptr->type());
                if (pool_instance) {
                    pool_instance->release(mat_ptr);
                } else {
                    delete mat_ptr;
                }
            }
            
            mat_ptr = other.mat_ptr;
            pool = other.pool;
            pool_name = other.pool_name;
            other.mat_ptr = nullptr;
        }
        return *this;
    }
    
    cv::Mat& get() { return *mat_ptr; }
    const cv::Mat& get() const { return *mat_ptr; }
    
    cv::Mat* operator->() { return mat_ptr; }
    const cv::Mat* operator->() const { return mat_ptr; }
    
    cv::Mat& operator*() { return *mat_ptr; }
    const cv::Mat& operator*() const { return *mat_ptr; }
    
    operator cv::Mat&() { return *mat_ptr; }
    operator const cv::Mat&() const { return *mat_ptr; }
    
    bool is_valid() const { return mat_ptr != nullptr; }
};

// Template wrapper pour les buffers génériques
template<typename T>
class PooledBuffer {
private:
    std::unique_ptr<T> buffer;
    std::shared_ptr<BufferPool<T>> pool;
    
public:
    PooledBuffer(const std::string& pool_name, int timeout_ms = 100) {
        pool = GET_BUFFER_POOL(T, pool_name);
        buffer = pool->acquire(timeout_ms);
        
        if (!buffer) {
            // Fallback: allocation directe
            buffer = std::make_unique<T>();
        }
    }
    
    ~PooledBuffer() {
        if (buffer && pool) {
            pool->release(std::move(buffer));
        }
    }
    
    // Interdiction de copie
    PooledBuffer(const PooledBuffer&) = delete;
    PooledBuffer& operator=(const PooledBuffer&) = delete;
    
    // Autorisation de déplacement
    PooledBuffer(PooledBuffer&& other) noexcept 
        : buffer(std::move(other.buffer)), pool(std::move(other.pool)) {}
    
    PooledBuffer& operator=(PooledBuffer&& other) noexcept {
        if (this != &other) {
            if (buffer && pool) {
                pool->release(std::move(buffer));
            }
            buffer = std::move(other.buffer);
            pool = std::move(other.pool);
        }
        return *this;
    }
    
    T& get() { return *buffer; }
    const T& get() const { return *buffer; }
    
    T* operator->() { return buffer.get(); }
    const T* operator->() const { return buffer.get(); }
    
    T& operator*() { return *buffer; }
    const T& operator*() const { return *buffer; }
    
    operator T&() { return *buffer; }
    operator const T&() const { return *buffer; }
    
    bool is_valid() const { return buffer != nullptr; }
    
    // Release explicite pour transférer ownership
    std::unique_ptr<T> release() {
        return std::move(buffer);
    }
};

// Macros pour faciliter l'utilisation
#define POOLED_MAT(name, rows, cols, type) PooledMat(name, rows, cols, type)
#define POOLED_BUFFER(T, name) PooledBuffer<T>(name)

} // namespace hesia

#endif // BUFFER_WRAPPER_HPP
