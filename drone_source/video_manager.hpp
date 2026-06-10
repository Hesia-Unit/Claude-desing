#ifndef VIDEO_MANAGER_HPP
#define VIDEO_MANAGER_HPP

#include <opencv2/opencv.hpp>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <memory>
#include <string>
#include <filesystem>
#include "logger.hpp"
#include "config.hpp"

namespace hesia {

class VideoManager {
private:
    cv::VideoCapture cap;
    std::string video_path_;
    int frame_count;
    double fps;
    int width;
    int height;
    bool is_file_source{false};
    bool file_loop_enabled_{false};
    uint64_t loop_count{0};
    bool display_enabled{false};
    std::atomic<bool> end_of_stream_{false};
    std::atomic<bool> eof_warning_emitted_{false};
    
    std::queue<std::tuple<std::string, int, cv::Mat>> display_queue;
    std::mutex display_queue_mutex;
    
    std::thread display_thread;
    std::atomic<bool> running;
    
    std::shared_ptr<Logger> logger;
    
    void display_loop();
    bool rewind_file_source();
    
public:
    VideoManager();
    ~VideoManager();
    
    void start_display();
    std::pair<bool, cv::Mat> get_frame();
    void send_to_yolo(int frame_id, const cv::Mat& frame);
    void send_to_midas(int frame_id, const cv::Mat& frame);
    void receive_yolo_result(int frame_id, const cv::Mat& frame, const std::vector<std::vector<float>>& detections);
    void receive_midas_result(int frame_id, const cv::Mat& frame);
    bool is_end_of_stream() const noexcept { return end_of_stream_.load(); }
    void cleanup();
};

} // namespace hesia

#endif // VIDEO_MANAGER_HPP
