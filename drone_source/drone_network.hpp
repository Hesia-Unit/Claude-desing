#ifndef DRONE_NETWORK_HPP
#define DRONE_NETWORK_HPP

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <queue>
#include <deque>
#include <mutex>
#include <atomic>
#include <map>
#include <chrono>
#include <condition_variable>
#include <future>
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef SOCKET socket_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int socket_t;
#endif
#include "hesia_drone.hpp"
#include "video_manager.hpp"
#include "yolo_processor.hpp"
#include "midas_processor.hpp"
#include "protocole.hpp"
#include "serialization.hpp"
#include "logger.hpp"
#include "config.hpp"
#include "video_channel.hpp"
#include "tls_channel.hpp"
#include "error_handler.hpp"
#include "clean_pipeline.hpp"
#include "security_audit.hpp"
#include "policy.hpp"
#include <opencv2/opencv.hpp>

namespace hesia {

// Définition de FrameSyncData pour synchronisation YOLO-MiDaS
struct FrameSyncData {
    int frame_id;
    cv::Mat original_frame;
    std::future<std::pair<cv::Mat, std::vector<std::vector<float>>>> yolo_future;
    std::future<std::tuple<cv::Mat, cv::Mat, std::vector<float>>> midas_future;
    
    FrameSyncData(int id, const cv::Mat& frame) 
        : frame_id(id), original_frame(frame.clone()) {}
};

struct Stats {
    std::atomic<int64_t> start_time_ms{0};
    std::atomic<uint64_t> bytes_sent{0};
    std::atomic<uint64_t> bytes_received{0};
    std::atomic<uint64_t> messages_sent{0};
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> video_frames_sent{0};
};

class DroneNetworkClient {
private:
    std::unique_ptr<HesiaDrone> drone;
    SecurityPolicy policy_;
    socket_t socket_fd;
    std::atomic<bool> connected{false};
    Stats stats;

    // TLS 1.3 transport (mTLS required)
    const bool tls_enabled;
    const bool tls_verify_peer;
    const bool tls_pin_server_pubkey;
    const uint64_t tls_rekey_bytes_threshold;
    uint64_t tls_bytes_since_rekey{0};
    std::chrono::steady_clock::time_point tls_last_rekey;
    const int tls_rekey_seconds;
    std::unique_ptr<TLSChannel> tls;
    
    std::unique_ptr<VideoManager> video_manager;
    std::unique_ptr<YOLOTrackerProcessor> yolo_processor;
    std::unique_ptr<MiDaSProcessor> midas_processor;
    std::unique_ptr<CleanPipeline> clean_pipeline;
    std::atomic<bool> video_running;
    std::atomic<bool> workers_running;
    std::thread video_thread;
    std::thread yolo_worker_thread;
    std::thread midas_worker_thread;
    std::thread telemetry_thread;
    std::thread send_thread;
    std::queue<std::map<std::string, std::string>> video_queue;
    std::mutex video_queue_mutex;
    std::mutex send_mutex;
    // Serialise la production (chaînage AAD/last_block_hash) ET la mise en file
    // des messages sécurisés afin que l'ordre fil == ordre de chaînage, et pour
    // éliminer la course de données sur l'état de session du drone.
    std::mutex secure_send_mutex;
    std::mutex send_queue_mutex;
    std::condition_variable send_queue_cv;
    std::deque<std::pair<std::vector<uint8_t>, std::string>> send_queue;
    std::atomic<bool> send_running{false};
    size_t send_queue_max{30};
    std::chrono::steady_clock::time_point last_video_enqueue_tp_{};
    std::chrono::steady_clock::time_point last_video_drop_log_tp_{};
    std::chrono::steady_clock::time_point last_send_fail_log_tp_{};
    std::chrono::steady_clock::time_point last_control_pressure_log_tp_{};
    uint64_t dropped_video_frames_{0};
    uint64_t preserved_control_messages_{0};
    std::atomic<bool> transport_failed_{false};
    
    // Synchronisation YOLO-MiDaS
    std::queue<std::unique_ptr<FrameSyncData>> frame_sync_queue;
    std::mutex frame_sync_mutex;
    std::condition_variable frame_sync_cv;
    
    // Gestion mémoire
    std::vector<cv::Mat> frame_buffer_pool;
    std::mutex frame_pool_mutex;
    static constexpr size_t FRAME_POOL_SIZE = 10;
    
    // Buffers réutilisables pour l'encodage vidéo
    cv::Mat combined_frame_buffer;
    cv::Mat resized_frame_buffer;
    std::vector<uint8_t> encoded_buffer;
    std::vector<int> jpeg_params;
    
    // Logging des frames concaténées
    std::filesystem::path frame_log_dir;
    std::atomic<bool> frame_logging_enabled{false};
    
    std::atomic<int> frame_counter;
    std::atomic<int> ping_counter;
    std::atomic<bool> telemetry_running{false};
    bool video_log_enabled{true};
    int video_log_every_n{30};
    std::shared_ptr<Logger> logger;
    std::shared_ptr<ErrorHandler> error_handler;
    std::shared_ptr<SecurityAudit> audit;
    
    // Méthodes de synchronisation
    void yolo_worker();
    void midas_worker();
    cv::Mat get_pooled_frame();
    void return_pooled_frame(cv::Mat&& frame);
    void log_combined_frame(const cv::Mat& yolo_frame, const cv::Mat& midas_frame, int frame_id);
    
    bool send_message(const std::vector<uint8_t>& message, const std::string& message_type);
    std::vector<uint8_t> receive_message(int timeout = 5);
    int transport_write_all(const uint8_t* data, size_t len);
    int transport_read_all(uint8_t* data, size_t len);
    void tls_maybe_rekey();
    void video_processing_loop();
    void send_video_frame(const cv::Mat& yolo_frame, const cv::Mat& midas_frame, int frame_id);
    void send_video_from_queue();
    void send_loop();
    bool enqueue_message(std::vector<uint8_t>&& data, const std::string& type);
    // Construit ET met en file un message sécurisé de manière atomique (sous
    // secure_send_mutex) pour préserver la cohérence du chaînage AAD lorsque
    // plusieurs threads (ping, télémétrie, données de vol) émettent en parallèle.
    bool enqueue_secure_message(const std::string& msg_type, const std::string& json_data);
    void telemetry_loop();
    void mark_transport_failure(const std::string& reason);
    bool should_log_backpressure(std::chrono::steady_clock::time_point& last_log,
                                 std::chrono::milliseconds interval);
    
public:
    DroneNetworkClient(const std::string& drone_id = "DRONE_001");
    ~DroneNetworkClient();
    
    bool connect(const std::string& host, int port, int timeout = 10, int retries = 3);
    bool handshake();
    void send_secure_ping();
    void send_secure_telemetry();
    void close();
    bool init_video_pipeline();
    void stop_video();
    void print_stats();
    int main();
    
    // Getters
    HesiaDrone* get_drone() const { return drone.get(); }
    
    // Pipeline methods
    void init_clean_pipeline();
    void stop_clean_pipeline();
    CleanPipeline::PipelineStats get_clean_pipeline_stats() const;
};

} // namespace hesia

#endif // DRONE_NETWORK_HPP
