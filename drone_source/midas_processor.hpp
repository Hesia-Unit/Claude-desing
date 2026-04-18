#ifndef MIDAS_PROCESSOR_HPP
#define MIDAS_PROCESSOR_HPP

#include <opencv2/opencv.hpp>
#include "NvInfer.h"
#include <vector>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <string>
#include <filesystem>
#include <future>
#include "logger.hpp"
#include "config.hpp"

namespace hesia {

class MiDaSProcessor {
private:
    // TensorRT
    nvinfer1::IRuntime* runtime;
    nvinfer1::ICudaEngine* engine;
    nvinfer1::IExecutionContext* context;
    
    // Buffers CUDA
    std::vector<void*> buffers;
    void* input_buffer;
    void* output_buffer;
    std::vector<float> cpu_output_buffer;
    cudaStream_t cuda_stream;
    
    // Dimensions
    nvinfer1::Dims input_dims;
    nvinfer1::Dims output_dims;
    
    // Configuration
    int input_width;
    int input_height;
    int gpu_id;
    
    // Deep-Skel
    int SECTORS;
    float EMA_ALPHA;
    std::vector<cv::Mat> polar_masks;
    std::vector<float> ema_state;
    
    std::filesystem::path deep_skel_log_file;
    
    // Blackbox
    std::queue<std::pair<int, std::map<std::string, std::string>>> bb_queue;
    std::thread bb_thread;
    std::mutex bb_mutex;
    std::atomic<bool> bb_running;
    std::shared_ptr<Logger> logger;
    bool frame_log_enabled{true};
    int frame_log_every_n{30};
    bool deep_skel_log_enabled{false};
    int deep_skel_log_every_n{30};
    bool bb_log_enabled{true};
    
    // Méthodes internes
    cv::Mat normalize_and_clip_depth(const cv::Mat& depth);
    std::vector<cv::Mat> build_polar_masks(int h, int w, int sectors);
    std::vector<float> extract_semantic(const cv::Mat& depth);
    void log_deep_skel_header();
    void log_deep_skel_data(int frame_id, const std::vector<float>& state);
    void bb_writer();
    
public:
    MiDaSProcessor(int gpu_id = 0);
    ~MiDaSProcessor();
    
    std::tuple<cv::Mat, cv::Mat, std::vector<float>> process(int frame_id, const cv::Mat& frame);
    
    std::future<std::tuple<cv::Mat, cv::Mat, std::vector<float>>> 
    process_async(int frame_id, const cv::Mat& frame);
    
    std::vector<float> get_deep_skel_state() const;
    void cleanup();
};

} // namespace hesia

#endif // MIDAS_PROCESSOR_HPP
