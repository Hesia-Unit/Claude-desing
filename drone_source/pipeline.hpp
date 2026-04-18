#ifndef PIPELINE_HPP
#define PIPELINE_HPP

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <future>
#include <opencv2/opencv.hpp>
#include "buffer_manager.hpp"
#include "buffer_wrapper.hpp"
#include "logger.hpp"
#include "config.hpp"

namespace hesia {

// Forward declarations
class YOLOTrackerProcessor;
class VideoManager;
class DroneNetworkClient;

// Structure pour les frames dans le pipeline
struct PipelineFrame {
    int frame_id;
    std::chrono::steady_clock::time_point capture_time;
    cv::Mat image; // Résolution réduite
    cv::Mat yolo_output;
    cv::Mat midas_output;
    std::vector<std::vector<float>> yolo_detections;
    bool yolo_processed = false;
    bool midas_processed = false;
    bool ready_to_send = false;
    
    PipelineFrame(int id) : frame_id(id) {
        capture_time = std::chrono::steady_clock::now();
        // Pré-allouer les matrices
        image = cv::Mat::zeros(480, 270, CV_8UC3);
        yolo_output = cv::Mat::zeros(480, 270, CV_8UC3);
        midas_output = cv::Mat::zeros(480, 270, CV_8UC3);
    }
};

// Queue thread-safe avec timeout
template<typename T>
class PipelineQueue {
private:
    std::queue<T> queue;
    mutable std::mutex mutex;
    std::condition_variable cv;
    size_t max_size;
    std::string name;
    
public:
    PipelineQueue(size_t max_size = 10, const std::string& name = "PipelineQueue") 
        : max_size(max_size), name(name) {}
    
    bool push(const T& item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex);
        
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                        [this] { return queue.size() < max_size; })) {
            return false; // Timeout
        }
        
        queue.push(item);
        cv.notify_one();
        return true;
    }
    
    bool pop(T& item, int timeout_ms = 100) {
        std::unique_lock<std::mutex> lock(mutex);
        
        if (!cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), 
                        [this] { return !queue.empty(); })) {
            return false; // Timeout
        }
        
        item = queue.front();
        queue.pop();
        cv.notify_one();
        return true;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        queue = std::queue<T>();
    }
};

// Pipeline haute performance
class HighPerformancePipeline {
private:
    // Queues entre étapes
    PipelineQueue<std::shared_ptr<PipelineFrame>> capture_queue;
    PipelineQueue<std::shared_ptr<PipelineFrame>> yolo_queue;
    PipelineQueue<std::shared_ptr<PipelineFrame>> send_queue;
    
    // Threads
    std::thread capture_thread;
    std::thread yolo_thread;
    std::thread send_thread;
    
    // Contrôle
    std::atomic<bool> running{false};
    std::atomic<int> frame_counter{0};
    
    // Pointeurs vers les processeurs
    YOLOTrackerProcessor* yolo_processor;
    VideoManager* video_manager;
    DroneNetworkClient* drone_client;
    
    // Stats
    std::atomic<long long> frames_captured{0};
    std::atomic<long long> frames_processed{0};
    std::atomic<long long> frames_sent{0};
    std::atomic<long long> frames_dropped{0};
    
    // Logger
    std::shared_ptr<Logger> logger;

public:
    HighPerformancePipeline(YOLOTrackerProcessor* yolo, VideoManager* vm, DroneNetworkClient* drone)
        : capture_queue(5, "capture"), yolo_queue(5, "yolo"), send_queue(5, "send"),
          yolo_processor(yolo), video_manager(vm), drone_client(drone) {
        
        logger = setup_logger("pipeline", Config::LOG_DIR);
        logger->info("Pipeline haute performance initialisé");
    }
    
    ~HighPerformancePipeline() {
        stop();
    }
    
    void start();
    void stop();
    
    // Stats en temps réel
    struct PipelineStats {
        long long captured;
        long long processed;
        long long sent;
        long long dropped;
        double capture_fps;
        double processing_fps;
        double send_fps;
        size_t capture_queue_size;
        size_t yolo_queue_size;
        size_t send_queue_size;
    };
    
    PipelineStats get_stats() const;
    void log_stats();

private:
    std::chrono::steady_clock::time_point start_time;
    std::condition_variable cv;
    
    void capture_worker();
    void yolo_worker();
    void send_worker();
};

} // namespace hesia

#endif // PIPELINE_HPP
