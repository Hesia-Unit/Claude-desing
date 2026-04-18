#ifndef YOLO_PROCESSOR_HPP
#define YOLO_PROCESSOR_HPP

#include <opencv2/opencv.hpp>
#include "NvInfer.h"
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <future>
#include <unordered_map>
#include <set>
#include "logger.hpp"
#include "config.hpp"

namespace hesia {

struct Detection {
    float x1, y1, x2, y2, conf;
    int cls;
    
    Detection() : x1(0), y1(0), x2(0), y2(0), conf(0), cls(0) {}
    Detection(float x1_, float y1_, float x2_, float y2_, float conf_, int cls_)
        : x1(x1_), y1(y1_), x2(x2_), y2(y2_), conf(conf_), cls(cls_) {}
};

class EnhancedTracker {
private:
    std::map<int, Detection> tracks;
    std::map<int, int> lost;
    int next_id;
    float iou_thr;
    int max_lost;
    float new_track_conf_thr;
    bool enforce_same_class;
    
    float calculate_iou(const Detection& a, const Detection& b);
    
public:
    EnhancedTracker(float iou_thr = 0.35f,
                    int max_lost = 3,
                    float new_track_conf_thr = 0.25f,
                    bool enforce_same_class = true);

    void set_new_track_threshold(float thr) { new_track_conf_thr = thr; }
    void set_enforce_same_class(bool v) { enforce_same_class = v; }

    std::map<int, Detection> update(const std::vector<Detection>& detections);
};

class YOLOTrackerProcessor {
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
    int input_height;
    int input_width;
    
    // Configuration
    int max_objects;
    float conf_threshold;
    float nms_iou_threshold;
    int model_input_size;
    int gpu_id;
    
    std::vector<std::string> classes;
    EnhancedTracker tracker;
    int frame_count;

    // Smoothing (EMA) pour stabiliser les bbox (important pour un modèle séquentiel)
    bool enable_ema_smoothing;
    float ema_alpha; // poids sur la détection courante (0..1)
    std::unordered_map<int, Detection> ema_tracks;

    // Blackbox
    std::queue<std::tuple<int, std::map<int, Detection>, std::vector<float>>> bb_queue;
    std::thread bb_thread;
    std::mutex bb_mutex;
    std::atomic<bool> bb_running;
    std::shared_ptr<Logger> logger;
    bool frame_log_enabled{true};
    int frame_log_every_n{30};
    bool bb_log_enabled{true};
    
    // Méthodes internes
    std::tuple<cv::Mat, float, float, float> letterbox(const cv::Mat& image, int new_size = 640);
    std::vector<Detection> decode_tensorrt_output(const float* output_data, 
                                                 const std::vector<int64_t>& output_shape,
                                                 float conf_threshold, 
                                                 int model_input_size);
    std::vector<int> nms_numpy(const std::vector<Detection>& detections, float iou_threshold);
    std::vector<int> nms_class_aware(const std::vector<Detection>& detections, float iou_threshold);

    // Format fixe pour Mamba: K*(cx,cy,w,h,conf,cls) normalisés, padding si nécessaire
    std::vector<float> build_mamba_yolo_state(const std::map<int, Detection>& tracked,
                                              int orig_w,
                                              int orig_h) const;
    
    void bb_writer();
    
public:
    YOLOTrackerProcessor(int gpu_id = 0, int max_objects = 6, 
                        float conf_threshold = 0.25f, float nms_iou_threshold = 0.45f,
                        float tracker_iou_thr = 0.35f, int tracker_max_lost = 3);
    ~YOLOTrackerProcessor();
    
    // Traitement individuel (existante)
    std::tuple<cv::Mat, std::map<int, Detection>, std::vector<float>, std::vector<std::pair<int, Detection>>>
    process(int frame_id, const cv::Mat& frame);
    
    std::future<std::pair<cv::Mat, std::vector<std::vector<float>>>> 
    process_async(int frame_id, const cv::Mat& frame);

    // Variante directe pour alimenter un modèle séquentiel (Mamba)
    std::future<std::pair<cv::Mat, std::vector<float>>>
    process_async_mamba(int frame_id, const cv::Mat& frame);
    
    void cleanup();
};

} // namespace hesia

#endif // YOLO_PROCESSOR_HPP
