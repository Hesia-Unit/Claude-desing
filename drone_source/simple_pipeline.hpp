#pragma once

#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <opencv2/opencv.hpp>
#include "yolo_processor.hpp"
#include "midas_processor.hpp"
#include "video_manager.hpp"
#include "logger.hpp"

namespace hesia {

struct FrameData {
    int frame_id;
    cv::Mat original_frame;
    cv::Mat yolo_result;
    cv::Mat midas_result;
    std::map<int, Detection> yolo_detections;
    std::vector<float> deep_skel_state;
    
    FrameData(int id, cv::Mat frame) 
        : frame_id(id), original_frame(std::move(frame)) {}
    
    // Constructeur par défaut pour std::unordered_map
    FrameData() : frame_id(0) {}
    
    // Constructeur de copie
    FrameData(const FrameData& other) 
        : frame_id(other.frame_id), 
          original_frame(other.original_frame.clone()),
          yolo_result(other.yolo_result.clone()),
          midas_result(other.midas_result.clone()),
          yolo_detections(other.yolo_detections),
          deep_skel_state(other.deep_skel_state) {}
};

class SimplePipeline {
private:
    // Processeurs IA
    std::unique_ptr<YOLOTrackerProcessor> yolo_processor;
    std::unique_ptr<MiDaSProcessor> midas_processor;
    std::unique_ptr<VideoManager> video_manager;
    
    // Threading
    std::atomic<bool> running;
    std::thread capture_thread;
    std::thread yolo_thread;
    std::thread midas_thread;
    std::thread send_thread;
    
    // Queues thread-safe
    std::queue<FrameData> capture_queue;
    std::queue<FrameData> yolo_queue;
    std::queue<FrameData> midas_queue;
    std::queue<FrameData> send_queue;
    
    std::mutex capture_mutex;
    std::mutex yolo_mutex;
    std::mutex midas_mutex;
    std::mutex send_mutex;
    
    std::condition_variable capture_cv;
    std::condition_variable yolo_cv;
    std::condition_variable midas_cv;
    std::condition_variable send_cv;
    
    // GPU management
    int yolo_gpu_id;
    int midas_gpu_id;
    
    // Logger
    std::shared_ptr<Logger> logger;
    
    // Méthodes de travail
    void capture_worker();
    void yolo_worker();
    void midas_worker();
    void send_worker();
    
    // Initialisation GPU
    bool setup_gpu_context();

public:
    SimplePipeline();
    ~SimplePipeline();
    
    // Contrôle du pipeline
    bool start();
    void stop();
    
    // Configuration
    void set_gpu_ids(int yolo_gpu, int midas_gpu);
    
    // Statistiques
    struct PipelineStats {
        uint64_t frames_processed = 0;
        uint64_t frames_sent = 0;
        double avg_fps = 0.0;
        double avg_yolo_ms = 0.0;
        double avg_midas_ms = 0.0;
        double avg_send_ms = 0.0;
    };
    
    PipelineStats get_stats() const;
    void log_stats() const;
};

} // namespace hesia
