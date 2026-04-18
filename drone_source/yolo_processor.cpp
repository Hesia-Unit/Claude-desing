#include "yolo_processor.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <filesystem>
#include <cuda_runtime.h>

// Logger TensorRT
class TensorRTLogger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kERROR) {
            std::cerr << "[TensorRT-ERROR] " << msg << std::endl;
        } else if (severity <= Severity::kWARNING) {
            std::cout << "[TensorRT-WARN] " << msg << std::endl;
        } else if (severity <= Severity::kINFO) {
            std::cout << "[TensorRT-INFO] " << msg << std::endl;
        }
    }
};

static TensorRTLogger gLogger;

namespace hesia {

// Classes COCO 80
static const std::vector<std::string> COCO_CLASSES = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat",
    "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
    "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball",
    "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket",
    "bottle", "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple",
    "sandwich", "orange", "broccoli", "carrot", "hot dog", "pizza", "donut", "cake",
    "chair", "couch", "potted plant", "bed", "dining table", "toilet", "tv", "laptop",
    "mouse", "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

float EnhancedTracker::calculate_iou(const Detection& a, const Detection& b) {
    float xA = std::max(a.x1, b.x1);
    float yA = std::max(a.y1, b.y1);
    float xB = std::min(a.x2, b.x2);
    float yB = std::min(a.y2, b.y2);
    
    float interW = std::max(0.0f, xB - xA);
    float interH = std::max(0.0f, yB - yA);
    float inter = interW * interH;
    
    float a_area = (a.x2 - a.x1) * (a.y2 - a.y1);
    float b_area = (b.x2 - b.x1) * (b.y2 - b.y1);
    
    return inter / (a_area + b_area - inter + 1e-6f);
}

EnhancedTracker::EnhancedTracker(float iou_thr, int max_lost, float new_track_conf_thr, bool enforce_same_class)
    : next_id(0),
      iou_thr(iou_thr),
      max_lost(max_lost),
      new_track_conf_thr(new_track_conf_thr),
      enforce_same_class(enforce_same_class) {}

std::map<int, Detection> EnhancedTracker::update(const std::vector<Detection>& detections) {
    // Stratégie: greedy matching (detections triées par conf), avec contraintes de classe et de réutilisation des tracks.
    // Objectif: stabilité temporelle (moins de "switch" d'identité) et compatibilité avec un modèle séquentiel.

    if (detections.empty()) {
        for (auto it = tracks.begin(); it != tracks.end();) {
            lost[it->first]++;
            if (lost[it->first] > max_lost) {
                lost.erase(it->first);
                it = tracks.erase(it);
            } else {
                ++it;
            }
        }
        return tracks;
    }

    // Tri par confiance décroissante
    std::vector<Detection> dets = detections;
    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.conf > b.conf;
    });

    std::set<int> used_tracks;
    std::set<int> matched_tracks;

    for (const auto& det : dets) {
        float best_iou = 0.0f;
        int best_id = -1;

        for (const auto& [tid, last] : tracks) {
            if (used_tracks.find(tid) != used_tracks.end()) {
                continue;
            }
            if (enforce_same_class && det.cls != last.cls) {
                continue;
            }
            const float score = calculate_iou(det, last);
            if (score > best_iou) {
                best_iou = score;
                best_id = tid;
            }
        }

        if (best_id != -1 && best_iou >= iou_thr) {
            tracks[best_id] = det;
            lost[best_id] = 0;
            used_tracks.insert(best_id);
            matched_tracks.insert(best_id);
        } else if (det.conf >= new_track_conf_thr) {
            const int tid = next_id++;
            tracks[tid] = det;
            lost[tid] = 0;
            used_tracks.insert(tid);
            matched_tracks.insert(tid);
        }
    }

    // Incrémenter lost pour les tracks non matchés
    for (auto it = tracks.begin(); it != tracks.end();) {
        if (matched_tracks.find(it->first) == matched_tracks.end()) {
            lost[it->first]++;
            if (lost[it->first] > max_lost) {
                lost.erase(it->first);
                it = tracks.erase(it);
                continue;
            }
        }
        ++it;
    }

    return tracks;
}

std::tuple<cv::Mat, float, float, float> YOLOTrackerProcessor::letterbox(const cv::Mat& image, int new_size) {
    int h0 = image.rows;
    int w0 = image.cols;
    
    float r = std::min(static_cast<float>(new_size) / w0, static_cast<float>(new_size) / h0);
    int new_w = static_cast<int>(std::round(w0 * r));
    int new_h = static_cast<int>(std::round(h0 * r));
    
    int dw = new_size - new_w;
    int dh = new_size - new_h;
    int top = dh / 2;
    int bottom = dh - top;
    int left = dw / 2;
    int right = dw - left;
    
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);
    
    cv::Mat padded;
    cv::copyMakeBorder(resized, padded, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    
    return std::make_tuple(padded, r, static_cast<float>(left), static_cast<float>(top));
}

std::vector<Detection> YOLOTrackerProcessor::decode_tensorrt_output(const float* output_data, 
                                                                   const std::vector<int64_t>& output_shape,
                                                                   float conf_threshold, 
                                                                   int model_input_size) {
    std::vector<Detection> detections;
    
    if (!output_data || output_shape.empty()) {
        logger->warning("Output TensorRT vide ou invalide");
        return detections;
    }
    
    // YOLOv8 TensorRT output: (1, 84, 8400) ou (1, 8400, 84)
    int num_classes = 80;
    int num_features = num_classes + 4;
    
    size_t num_predictions = 0;
    bool is_channels_first = false;
    
    if (output_shape.size() == 3) {
        if (output_shape[1] == num_features) {  // (1, 84, N)
            num_predictions = output_shape[2];
            is_channels_first = true;
        } else if (output_shape[2] == num_features) {  // (1, N, 84)
            num_predictions = output_shape[1];
            is_channels_first = false;
        } else {
            logger->error("Forme de sortie non reconnue");
            return detections;
        }
    } else if (output_shape.size() == 2) {
        // (84, N) ou (N, 84)
        if (output_shape[0] == num_features) {
            num_predictions = output_shape[1];
            is_channels_first = true;
        } else if (output_shape[1] == num_features) {
            num_predictions = output_shape[0];
            is_channels_first = false;
        } else {
            logger->error("Forme 2D non reconnue");
            return detections;
        }
    } else {
        logger->error("Dimensions de sortie non supportées: " + std::to_string(output_shape.size()));
        return detections;
    }
    
    for (size_t i = 0; i < num_predictions; ++i) {
        float cx, cy, w, h;
        
        if (is_channels_first) {
            // Format: (84, N) - channels first
            cx = output_data[i];                     // index 0*N + i
            cy = output_data[num_predictions + i];   // index 1*N + i
            w = output_data[2 * num_predictions + i]; // index 2*N + i
            h = output_data[3 * num_predictions + i]; // index 3*N + i
        } else {
            // Format: (N, 84) - channels last
            size_t base_idx = i * num_features;
            cx = output_data[base_idx];
            cy = output_data[base_idx + 1];
            w = output_data[base_idx + 2];
            h = output_data[base_idx + 3];
        }
        
        // Convertir si normalisé (0-1) vers pixels
        if (cx < 1.0f && cy < 1.0f && w < 1.0f && h < 1.0f) {
            cx *= model_input_size;
            cy *= model_input_size;
            w *= model_input_size;
            h *= model_input_size;
        }
        
        // Trouver la meilleure classe
        float best_conf = 0.0f;
        int best_class = -1;
        
        for (int c = 0; c < num_classes; ++c) {
            float conf;
            if (is_channels_first) {
                conf = output_data[(4 + c) * num_predictions + i];
            } else {
                conf = output_data[i * num_features + 4 + c];
            }
            
            if (conf > best_conf) {
                best_conf = conf;
                best_class = c;
            }
        }
        
        // Appliquer le seuil de confiance
        if (best_conf < conf_threshold || best_class < 0) {
            continue;
        }
        
        // Calculer les coordonnées du bounding box
        float x1 = cx - w / 2.0f;
        float y1 = cy - h / 2.0f;
        float x2 = cx + w / 2.0f;
        float y2 = cy + h / 2.0f;
        
        // Filtrage par taille (relatif) : evite les micro-boxes et les boxes quasi plein ecran.
        // On reste sur des seuils conservateurs pour ne pas jeter des cibles utiles.
        const float box_width = x2 - x1;
        const float box_height = y2 - y1;
        const float min_wh = 0.01f * static_cast<float>(model_input_size);   // ~6.4 px si input=640
        const float max_wh = 0.98f * static_cast<float>(model_input_size);

        if (box_width < min_wh || box_height < min_wh) {
            continue;
        }

        if (box_width > max_wh || box_height > max_wh) {
            continue;
        }
        
        // Assurer les limites
        x1 = std::max(0.0f, std::min(x1, static_cast<float>(model_input_size - 1)));
        y1 = std::max(0.0f, std::min(y1, static_cast<float>(model_input_size - 1)));
        x2 = std::max(0.0f, std::min(x2, static_cast<float>(model_input_size - 1)));
        y2 = std::max(0.0f, std::min(y2, static_cast<float>(model_input_size - 1)));
        
        if (x1 >= x2 || y1 >= y2) {
            continue;
        }
        
        detections.emplace_back(x1, y1, x2, y2, best_conf, best_class);
    }
    
    return detections;
}

std::vector<int> YOLOTrackerProcessor::nms_numpy(const std::vector<Detection>& detections, float iou_threshold) {
    if (detections.empty()) {
        return {};
    }

    auto iou = [](const Detection& a, const Detection& b) -> float {
        const float xA = std::max(a.x1, b.x1);
        const float yA = std::max(a.y1, b.y1);
        const float xB = std::min(a.x2, b.x2);
        const float yB = std::min(a.y2, b.y2);

        const float interW = std::max(0.0f, xB - xA);
        const float interH = std::max(0.0f, yB - yA);
        const float inter = interW * interH;

        const float a_area = (a.x2 - a.x1) * (a.y2 - a.y1);
        const float b_area = (b.x2 - b.x1) * (b.y2 - b.y1);

        return inter / (a_area + b_area - inter + 1e-6f);
    };
    
    std::vector<std::pair<float, int>> scores_with_idx;
    for (size_t i = 0; i < detections.size(); ++i) {
        scores_with_idx.push_back({detections[i].conf, static_cast<int>(i)});
    }
    
    std::sort(scores_with_idx.rbegin(), scores_with_idx.rend());
    
    std::vector<int> keep;
    std::vector<bool> suppressed(detections.size(), false);
    
    for (const auto& [score, i] : scores_with_idx) {
        if (suppressed[i]) continue;
        keep.push_back(i);
        
        for (size_t j = 0; j < detections.size(); ++j) {
            if (suppressed[j] || i == static_cast<int>(j)) continue;
            
            const float score_iou = iou(detections[static_cast<size_t>(i)], detections[j]);
            if (score_iou > iou_threshold) {
                suppressed[j] = true;
            }
        }
    }
    
    return keep;
}

std::vector<int> YOLOTrackerProcessor::nms_class_aware(const std::vector<Detection>& detections, float iou_threshold) {
    if (detections.empty()) {
        return {};
    }

    // Grouper par classe puis effectuer NMS dans chaque groupe.
    std::unordered_map<int, std::vector<int>> by_cls;
    by_cls.reserve(32);
    for (int i = 0; i < static_cast<int>(detections.size()); ++i) {
        by_cls[detections[static_cast<size_t>(i)].cls].push_back(i);
    }

    std::vector<int> keep_global;
    keep_global.reserve(detections.size());

    for (auto& kv : by_cls) {
        const std::vector<int>& idxs = kv.second;
        if (idxs.empty()) continue;

        std::vector<Detection> subset;
        subset.reserve(idxs.size());
        for (int gi : idxs) {
            subset.push_back(detections[static_cast<size_t>(gi)]);
        }

        const std::vector<int> keep_local = nms_numpy(subset, iou_threshold);
        for (int li : keep_local) {
            if (li < 0 || li >= static_cast<int>(idxs.size())) continue;
            keep_global.push_back(idxs[static_cast<size_t>(li)]);
        }
    }

    // Ordonner par confiance decroissante (utile pour top-K).
    std::sort(keep_global.begin(), keep_global.end(), [&](int a, int b) {
        return detections[static_cast<size_t>(a)].conf > detections[static_cast<size_t>(b)].conf;
    });

    return keep_global;
}

std::vector<float> YOLOTrackerProcessor::build_mamba_yolo_state(const std::map<int, Detection>& tracked,
                                                                int orig_w,
                                                                int orig_h) const {
    const int K = std::max(0, max_objects);
    std::vector<float> state(static_cast<size_t>(K) * 6, 0.0f);
    // Padding: conf=0, cls=-1 => "no object"
    for (int i = 0; i < K; ++i) {
        state[static_cast<size_t>(i) * 6 + 5] = -1.0f;
    }

    if (K == 0 || tracked.empty() || orig_w <= 0 || orig_h <= 0) {
        return state;
    }

    std::vector<Detection> dets;
    dets.reserve(tracked.size());
    for (const auto& kv : tracked) {
        dets.push_back(kv.second);
    }

    std::sort(dets.begin(), dets.end(), [](const Detection& a, const Detection& b) {
        return a.conf > b.conf;
    });

    const float inv_w = 1.0f / static_cast<float>(orig_w);
    const float inv_h = 1.0f / static_cast<float>(orig_h);

    const int n = std::min(K, static_cast<int>(dets.size()));
    for (int i = 0; i < n; ++i) {
        const Detection& d = dets[static_cast<size_t>(i)];
        const float cx = ((d.x1 + d.x2) * 0.5f) * inv_w;
        const float cy = ((d.y1 + d.y2) * 0.5f) * inv_h;
        const float w = (d.x2 - d.x1) * inv_w;
        const float h = (d.y2 - d.y1) * inv_h;

        auto clamp01 = [](float v) {
            return std::max(0.0f, std::min(1.0f, v));
        };

        const size_t base = static_cast<size_t>(i) * 6;
        state[base + 0] = clamp01(cx);
        state[base + 1] = clamp01(cy);
        state[base + 2] = clamp01(w);
        state[base + 3] = clamp01(h);
        state[base + 4] = std::max(0.0f, std::min(1.0f, d.conf));
        state[base + 5] = static_cast<float>(d.cls);
    }

    return state;
}

void YOLOTrackerProcessor::bb_writer() {
    if (!bb_log_enabled) {
        return;
    }
    std::filesystem::path out_dir = Config::BB_DIR / "yolo_tracker";
    std::filesystem::create_directories(out_dir);
    std::filesystem::path meta_file = out_dir / "meta.log";
    
    while (bb_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        
        std::lock_guard<std::mutex> lock(bb_mutex);
        if (!bb_queue.empty()) {
            auto [frame_id, tracked, yolo_state] = bb_queue.front();
            bb_queue.pop();
            
            const double ts = std::chrono::duration<double>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();

            std::ostringstream json;
            json.setf(std::ios::fixed);
            json << std::setprecision(6);
            json << "{\"ts\":" << ts << ",\"frame_id\":" << frame_id << ",\"tracked\":{";
            bool first_track = true;
            for (const auto& [tid, det] : tracked) {
                if (!first_track) json << ",";
                first_track = false;
                json << "\"" << tid << "\":[" << det.x1 << "," << det.y1 << "," 
                     << det.x2 << "," << det.y2 << "," << det.conf << "," << det.cls << "]";
            }
            json << "},\"yolo_state\":[";
            for (size_t i = 0; i < yolo_state.size(); ++i) {
                if (i) json << ",";
                json << yolo_state[i];
            }
            json << "]}";

            std::ofstream f(meta_file, std::ios::app);
            if (f.is_open()) {
                f << json.str() << "\n";
                f.flush();
            }
        }
    }
}

YOLOTrackerProcessor::YOLOTrackerProcessor(int gpu_id, int max_objects, 
                                         float conf_threshold, float nms_iou_threshold,
                                         float tracker_iou_thr, int tracker_max_lost)
    : runtime(nullptr),
      engine(nullptr),
      context(nullptr),
      input_buffer(nullptr),
      output_buffer(nullptr),
      cpu_output_buffer(),
      cuda_stream(nullptr),
      input_dims(),
      output_dims(),
      input_height(640),
      input_width(640),
      max_objects(max_objects),
      conf_threshold(conf_threshold),
      nms_iou_threshold(nms_iou_threshold),
      model_input_size(640),
      gpu_id(gpu_id),
      classes(COCO_CLASSES), 
      tracker(tracker_iou_thr, tracker_max_lost, conf_threshold, true),
      frame_count(0),
      enable_ema_smoothing(true),
      ema_alpha(0.70f),
      ema_tracks(),
      bb_queue(),
      bb_thread(),
      bb_mutex(),
      bb_running(true),
      logger(nullptr) {
    
    logger = setup_logger("yolo_tracker", Config::LOG_DIR);
    logger->info("🚀 Initialisation YOLO TensorRT sur GPU " + std::to_string(gpu_id));

    bool ai_log_to_file = false;
    if (!ai_log_to_file) {
        logger->disable_file_output();
        bb_log_enabled = false;
    }
    
    try {
        // 1. Initialiser CUDA
        logger->info("🔧 Initialisation CUDA pour YOLO...");
        cudaSetDevice(gpu_id);
        
        // Afficher les informations GPU
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, gpu_id);
        logger->info("📊 GPU YOLO: " + std::string(prop.name) + 
                    " (Compute " + std::to_string(prop.major) + "." + std::to_string(prop.minor) + ")");
        logger->info("💾 Mémoire GPU: " + std::to_string(prop.totalGlobalMem / 1024 / 1024) + " MB");
        logger->info("⚡ Multiprocessors: " + std::to_string(prop.multiProcessorCount));
        
        // 2. Créer le runtime TensorRT
        logger->info("🔧 Création TensorRT runtime...");
        runtime = nvinfer1::createInferRuntime(gLogger);
        if (!runtime) {
            throw std::runtime_error("Impossible de créer TensorRT runtime");
        }
        
        // 3. Charger le fichier .engine
        std::string engine_path = Config::YOLO_ENGINE;
        logger->info("Chargement engine: " + engine_path);
        
        std::ifstream engine_file(engine_path, std::ios::binary);
        if (!engine_file) {
            throw std::runtime_error("Fichier engine non trouvé: " + engine_path);
        }
        
        engine_file.seekg(0, std::ios::end);
        size_t engine_size = engine_file.tellg();
        engine_file.seekg(0, std::ios::beg);
        
        std::vector<char> engine_data(engine_size);
        engine_file.read(engine_data.data(), engine_size);
        engine_file.close();
        
        // 4. Désérialiser le moteur
        engine = runtime->deserializeCudaEngine(engine_data.data(), engine_size);
        if (!engine) {
            throw std::runtime_error("Impossible de désérialiser le moteur");
        }
        
        // 5. Créer le contexte d'exécution
        context = engine->createExecutionContext();
        if (!context) {
            throw std::runtime_error("Impossible de créer contexte d'exécution");
        }
                
        // 6. Allouer les buffers CUDA
        int nbIOTensors = engine->getNbIOTensors();
        logger->info("Nombre de IO tensors: " + std::to_string(nbIOTensors));
                
        for (int i = 0; i < nbIOTensors; ++i) {
            const char* tensor_name = engine->getIOTensorName(i);
            nvinfer1::Dims dims = engine->getTensorShape(tensor_name);
            nvinfer1::DataType dtype = engine->getTensorDataType(tensor_name);
                    
            logger->info("IO Tensor " + std::to_string(i) + ": " + tensor_name + 
                        ", dtype: " + std::to_string(static_cast<int>(dtype)));
                    
            size_t binding_size = 1;
            std::string dims_str = "[";
            for (int j = 0; j < dims.nbDims; ++j) {
                binding_size *= dims.d[j];
                dims_str += std::to_string(dims.d[j]) + (j < dims.nbDims - 1 ? ", " : "]");
            }
            logger->info("Dimensions: " + dims_str);
                    
            binding_size *= (dtype == nvinfer1::DataType::kFLOAT ? 4 : 
                           dtype == nvinfer1::DataType::kHALF ? 2 : 1);
                    
            void* d_buffer;
            cudaMalloc(&d_buffer, binding_size);
            buffers.push_back(d_buffer);
                    
            if (engine->getTensorIOMode(tensor_name) == nvinfer1::TensorIOMode::kINPUT) { // Input
                input_dims = dims;
                input_buffer = d_buffer;
                logger->info("Input buffer YOLO: " + std::to_string(binding_size) + " bytes");
                        
                // Mettre à jour la taille d'entrée
                if (dims.nbDims >= 4) {
                    input_height = dims.d[2];
                    input_width = dims.d[3];
                }
            } else { // Output
                output_dims = dims;
                output_buffer = d_buffer;
                logger->info("Output buffer YOLO: " + std::to_string(binding_size) + " bytes");
                        
                // Allouer aussi le buffer CPU pour la sortie
                cpu_output_buffer.resize(binding_size / sizeof(float));
            }
        }
                
        // 7. Créer le stream CUDA
        cudaStreamCreate(&cuda_stream);
                
        logger->info("✓ YOLO TensorRT initialisé avec succès");
        
    } catch (const std::exception& e) {
        logger->error("Erreur initialisation TensorRT: " + std::string(e.what()));
        throw;
    }
    
    if (bb_log_enabled) {
        bb_thread = std::thread(&YOLOTrackerProcessor::bb_writer, this);
    }
    logger->info("YOLO Tracker Processor initialisé (max_objects=" + 
                std::to_string(max_objects) + ", conf_threshold=" + 
                std::to_string(conf_threshold) + ")");
}

YOLOTrackerProcessor::~YOLOTrackerProcessor() {
    cleanup();
}

std::tuple<cv::Mat, std::map<int, Detection>, std::vector<float>, std::vector<std::pair<int, Detection>>>
YOLOTrackerProcessor::process(int frame_id, const cv::Mat& frame) {
    cv::Mat display_frame = frame.clone();
    std::map<int, Detection> tracked;
    std::vector<float> yolo_state;
    std::vector<std::pair<int, Detection>> selected;
    
    try {
        const int orig_h = frame.rows;
        const int orig_w = frame.cols;
        
        // 1. Letterbox preprocessing
        cv::Mat padded;
        float scale, pad_x, pad_y;
        std::tie(padded, scale, pad_x, pad_y) = letterbox(frame, model_input_size);
        
        // 2. Préparer l'input pour TensorRT
        cv::Mat blob;
        blob = cv::dnn::blobFromImage(padded, 1.0/255.0, cv::Size(model_input_size, model_input_size),
                                     cv::Scalar(), true, false, CV_32F);
        
        // 3. Copier les données vers GPU
        size_t input_size = blob.total() * sizeof(float);
        cudaMemcpyAsync(input_buffer, blob.ptr<float>(), input_size, 
                       cudaMemcpyHostToDevice, cuda_stream);
        
        // 4. Lier les buffers aux tensors
        context->setTensorAddress("images", input_buffer);
        context->setTensorAddress("output0", output_buffer);
        
        // 5. Exécuter l'inférence TensorRT
        bool success = context->enqueueV3(cuda_stream);
        
        if (!success) {
            throw std::runtime_error("Échec execution TensorRT");
        }
        
        // 6. Copier les résultats vers CPU
        size_t output_size = cpu_output_buffer.size() * sizeof(float);
        cudaMemcpyAsync(cpu_output_buffer.data(), output_buffer, output_size,
                       cudaMemcpyDeviceToHost, cuda_stream);
        
        cudaStreamSynchronize(cuda_stream);
        
        // 7. Décoder les détections
        std::vector<int64_t> output_shape;
        for (int i = 0; i < output_dims.nbDims; ++i) {
            output_shape.push_back(output_dims.d[i]);
        }
        
        std::vector<Detection> detections = decode_tensorrt_output(
            cpu_output_buffer.data(), output_shape, conf_threshold, model_input_size);
        
        // 8. Convertir vers coordonnées originales
        std::vector<Detection> detections_original;
        for (auto& det : detections) {
            Detection det_orig = det;
            det_orig.x1 = (det.x1 - pad_x) / scale;
            det_orig.y1 = (det.y1 - pad_y) / scale;
            det_orig.x2 = (det.x2 - pad_x) / scale;
            det_orig.y2 = (det.y2 - pad_y) / scale;
            
            // Limiter aux dimensions de l'image
            det_orig.x1 = std::max(0.0f, std::min(det_orig.x1, static_cast<float>(orig_w - 1)));
            det_orig.y1 = std::max(0.0f, std::min(det_orig.y1, static_cast<float>(orig_h - 1)));
            det_orig.x2 = std::max(0.0f, std::min(det_orig.x2, static_cast<float>(orig_w - 1)));
            det_orig.y2 = std::max(0.0f, std::min(det_orig.y2, static_cast<float>(orig_h - 1)));
            
            if (det_orig.x2 > det_orig.x1 && det_orig.y2 > det_orig.y1) {
                detections_original.push_back(det_orig);
            }
        }
        
        // 9. Appliquer NMS (par classe pour eviter des suppressions ...)
        std::vector<int> keep_indices;
        if (!detections_original.empty()) {
            keep_indices = nms_class_aware(detections_original, nms_iou_threshold);
        }
        
        // 10. Filtrer les détections avec NMS
        std::vector<Detection> filtered_detections;
        for (int idx : keep_indices) {
            if (idx >= 0 && idx < static_cast<int>(detections_original.size())) {
                filtered_detections.push_back(detections_original[static_cast<size_t>(idx)]);
            }
        }
        
        // 11. Mise a jour du tracker
        tracked = tracker.update(filtered_detections);

        // 11b. Lissage temporel des bbox (EMA) pour stabiliser les ...
        if (enable_ema_smoothing) {
            std::map<int, Detection> smoothed;
            std::set<int> alive;
            alive.clear();

            for (const auto& [tid, det] : tracked) {
                alive.insert(tid);

                auto it = ema_tracks.find(tid);
                if (it == ema_tracks.end()) {
                    ema_tracks[tid] = det;
                } else {
                    Detection prev = it->second;
                    Detection out = det;
                    const float a = ema_alpha;
                    const float b = 1.0f - a;
                    out.x1 = a * det.x1 + b * prev.x1;
                    out.y1 = a * det.y1 + b * prev.y1;
                    out.x2 = a * det.x2 + b * prev.x2;
                    out.y2 = a * det.y2 + b * prev.y2;
                    out.conf = a * det.conf + b * prev.conf;
                    // cls garde la derniere classe du det (tracker est deja class-aware)
                    ema_tracks[tid] = out;
                }

                smoothed[tid] = ema_tracks[tid];
            }

            // purge des tracks disparus
            for (auto it = ema_tracks.begin(); it != ema_tracks.end();) {
                if (alive.find(it->first) == alive.end()) {
                    it = ema_tracks.erase(it);
                } else {
                    ++it;
                }
            }

            tracked.swap(smoothed);
        }
        
        // 12. Dessiner les détections
        for (const auto& [tid, det] : tracked) {
            cv::rectangle(display_frame, 
                        cv::Point(static_cast<int>(det.x1), static_cast<int>(det.y1)),
                        cv::Point(static_cast<int>(det.x2), static_cast<int>(det.y2)),
                        cv::Scalar(0, 255, 0), 2);
            
            std::string label = COCO_CLASSES[det.cls] + " " + std::to_string(det.conf).substr(0, 4);
            cv::putText(display_frame, label,
                       cv::Point(static_cast<int>(det.x1), static_cast<int>(det.y1) - 10),
                       cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);
        }
        
        // 13. Format fixe pour Mamba
        yolo_state = build_mamba_yolo_state(tracked, orig_w, orig_h);

        // 14. Préparer les données de sortie (legacy)
        for (const auto& [tid, det] : tracked) {
            selected.emplace_back(tid, det);
        }

        // 15. Logging blackbox
        if (bb_log_enabled) {
            std::lock_guard<std::mutex> lock(bb_mutex);
            if (bb_queue.size() < 100) {
                bb_queue.push({frame_id, tracked, yolo_state});
            }
        }
        
        // Log chaque frame
        if (frame_log_enabled && (frame_id % frame_log_every_n == 0)) {
            logger->info("🎯 YOLO Frame " + std::to_string(frame_id) + 
                       " traitée - " + std::to_string(tracked.size()) + 
                       " objets détectés");
        }
        
    } catch (const std::exception& e) {
        logger->error("Erreur traitement YOLO: " + std::string(e.what()));
    }
    
    return std::make_tuple(display_frame, tracked, yolo_state, selected);
}

std::future<std::pair<cv::Mat, std::vector<std::vector<float>>>>
YOLOTrackerProcessor::process_async(int frame_id, const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame_id, frame]() {
        auto [result_frame, tracked, yolo_state, selected] = process(frame_id, frame);
        
        // Convertir les détections au format attendu
        std::vector<std::vector<float>> detections_raw;
        for (const auto& [tid, det] : tracked) {
            detections_raw.push_back({
                static_cast<float>(tid),
                det.x1, det.y1, det.x2, det.y2,
                static_cast<float>(det.cls),
                det.conf
            });
        }
        
        return std::make_pair(result_frame, detections_raw);
    });
}

std::future<std::pair<cv::Mat, std::vector<float>>>
YOLOTrackerProcessor::process_async_mamba(int frame_id, const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame_id, frame]() {
        auto [result_frame, tracked, yolo_state, selected] = process(frame_id, frame);
        (void)tracked;
        (void)selected;
        return std::make_pair(result_frame, yolo_state);
    });
}

void YOLOTrackerProcessor::cleanup() {
    bb_running = false;
    if (bb_thread.joinable()) {
        bb_thread.join();
    }
    
    // Nettoyer TensorRT
    if (cuda_stream) {
        cudaStreamDestroy(cuda_stream);
        cuda_stream = nullptr;
    }
    
    for (auto buffer : buffers) {
        if (buffer) {
            cudaFree(buffer);
        }
    }
    buffers.clear();
    
    if (context) {
        delete context;
        context = nullptr;
    }
    
    if (engine) {
        delete engine;
        engine = nullptr;
    }
    
    if (runtime) {
        delete runtime;
        runtime = nullptr;
    }
}

} // namespace hesia
