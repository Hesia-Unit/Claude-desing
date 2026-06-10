#pragma once
#include <opencv2/opencv.hpp>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <chrono>
#include "logger.hpp"
#include "video_manager.hpp"
#include "yolo_processor.hpp"
#include "midas_processor.hpp"
#include <filesystem>
#include <functional>

namespace hesia {

struct FrameData {
    int frame_id;
    cv::Mat original_frame;
    cv::Mat yolo_result;
    cv::Mat midas_result;
    cv::Mat midas_depth_map;
    std::map<int, Detection> yolo_detections;
    std::vector<float> yolo_state;        // Format fixe pour modele sequentiel (Mamba)
    std::vector<float> deep_skel_state;
    
    FrameData() = default;
    FrameData(int id, cv::Mat frame) : frame_id(id), original_frame(std::move(frame)) {}
};

class CleanPipeline {
private:
    // Configuration GPU
    int yolo_gpu_id = 1;
    int midas_gpu_id = 1;
    
    // Contrôle
    std::atomic<bool> running{false};
    std::atomic<int> frame_counter{0};
    
    // Statistiques - CHANGEMENT ICI: utilisation de double au lieu de long double
    mutable std::mutex stats_mutex;
    std::chrono::high_resolution_clock::time_point start_time;
    std::atomic<int> frames_processed{0};
    std::atomic<int> yolo_frames_processed{0};
    std::atomic<int> midas_frames_processed{0};
    std::atomic<int> send_frames_processed{0};
    std::atomic<double> total_yolo_time{0.0};  // Changé de long double à double
    std::atomic<double> total_midas_time{0.0}; // Changé de long double à double
    std::atomic<double> total_send_time{0.0};  // Changé de long double à double
    
    // Threads
    std::thread capture_thread;
    std::thread yolo_thread;
    std::thread midas_thread;
    std::thread send_thread;
    std::thread logging_thread;
    
    // Queues
    std::queue<FrameData> yolo_queue;
    std::queue<FrameData> midas_queue;
    std::queue<FrameData> send_queue;
    std::queue<FrameData> logging_queue;
    std::mutex yolo_mutex;
    std::mutex midas_mutex;
    std::mutex send_mutex;
    std::mutex logging_mutex;
    std::condition_variable yolo_cv;
    std::condition_variable midas_cv;
    std::condition_variable send_cv;
    std::condition_variable logging_cv;
    
    // Composants
    std::shared_ptr<Logger> logger;
    std::unique_ptr<VideoManager> video_manager;
    std::unique_ptr<YOLOTrackerProcessor> yolo_processor;
    std::unique_ptr<MiDaSProcessor> midas_processor;

    bool mamba_log_enabled{false};
    int mamba_log_batch{50};
    bool m2b_trace_enabled{false};
    int m2b_trace_every_n{15};
    std::filesystem::path m2b_trace_dir;
    
    // Callback pour envoyer les frames
    std::function<void(const cv::Mat&, const cv::Mat&, int)> frame_callback;
    
    // Méthodes worker
    void capture_worker();
    void yolo_worker();
    void midas_worker();
    void send_worker();
    void log_combined_frame_to_disk(const cv::Mat& combined_frame, int frame_id);
    void frame_logging_worker();
    
    // GPU setup
    bool setup_gpu_context();

public:
    CleanPipeline();
    ~CleanPipeline();
    
    bool start();
    void stop();
    void set_gpu_ids(int yolo_gpu, int midas_gpu);
    
    // Définir le callback pour envoyer les frames
    void set_frame_callback(std::function<void(const cv::Mat&, const cv::Mat&, int)> callback) {
        frame_callback = callback;
    }
    
    struct PipelineStats {
        double avg_fps = 0.0;
        double avg_yolo_ms = 0.0;
        double avg_midas_ms = 0.0;
        double avg_send_ms = 0.0;
    };
    
    PipelineStats get_stats() const;
    void log_stats() const;
};

} // namespace hesia
