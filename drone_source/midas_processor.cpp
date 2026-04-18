#include "midas_processor.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <thread>
#include <cuda_runtime.h>

// Logger TensorRT pour MiDaS
class MiDaSLogger : public nvinfer1::ILogger {
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kERROR) {
            std::cerr << "[TensorRT-MiDaS-ERROR] " << msg << std::endl;
        } else if (severity <= Severity::kWARNING) {
            std::cout << "[TensorRT-MiDaS-WARN] " << msg << std::endl;
        }
    }
};

static MiDaSLogger gMidasLogger;

namespace hesia {

cv::Mat MiDaSProcessor::normalize_and_clip_depth(const cv::Mat& depth) {
    if (depth.empty() || depth.total() == 0) {
        return cv::Mat();
    }
    
    double min_val, max_val;
    cv::minMaxLoc(depth, &min_val, &max_val);
    
    if (std::abs(max_val - min_val) < 1e-6) {
        logger->warning("Depth map plate");
        return cv::Mat::zeros(depth.size(), CV_32F);
    }
    
    cv::Mat normalized;
    depth.convertTo(normalized, CV_32F, 1.0 / (max_val - min_val), 
                   -min_val / (max_val - min_val));
    
    // Clip entre 0 et 1
    cv::threshold(normalized, normalized, 1.0, 1.0, cv::THRESH_TRUNC);
    cv::threshold(normalized, normalized, 0.0, 0.0, cv::THRESH_TOZERO);
    
    return normalized;
}

std::vector<cv::Mat> MiDaSProcessor::build_polar_masks(int h, int w, int sectors) {
    std::vector<cv::Mat> masks;
    float cx = w / 2.0f;
    float cy = h / 2.0f;
    float max_radius = std::sqrt(cx*cx + cy*cy);
    float sector_angle = 2.0f * M_PI / sectors;
    
    for (int i = 0; i < sectors; ++i) {
        cv::Mat mask = cv::Mat::zeros(h, w, CV_8UC1);
        float start_angle = i * sector_angle - M_PI;
        float end_angle = start_angle + sector_angle;
        
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dx = x - cx;
                float dy = y - cy;
                float distance = std::sqrt(dx*dx + dy*dy);
                float angle = std::atan2(dy, dx);
                
                // Normaliser l'angle
                if (angle < start_angle) angle += 2.0f * M_PI;
                if (angle > end_angle) angle -= 2.0f * M_PI;
                
                if (angle >= start_angle && angle <= end_angle && distance <= max_radius) {
                    mask.at<uchar>(y, x) = 255;
                }
            }
        }
        masks.push_back(mask);
    }
    
    return masks;
}

std::vector<float> MiDaSProcessor::extract_semantic(const cv::Mat& depth) {
    if (depth.empty()) {
        return std::vector<float>(SECTORS * 3 + 2, 0.0f);
    }
    
    std::vector<float> features;
    features.reserve(SECTORS * 3 + 2);
    
    // Caractéristiques par secteur
    for (const auto& mask : polar_masks) {
        cv::Scalar mean_val, stddev_val;
        cv::meanStdDev(depth, mean_val, stddev_val, mask);
        
        double min_val, max_val;
        cv::minMaxLoc(depth, &min_val, &max_val, nullptr, nullptr, mask);
        
        features.push_back(static_cast<float>(min_val));
        features.push_back(static_cast<float>(mean_val[0]));
        features.push_back(static_cast<float>(stddev_val[0]));
    }
    
    // Gradient vertical (haut - bas)
    if (depth.rows >= 3) {
        int third = depth.rows / 3;
        cv::Rect top_rect(0, 0, depth.cols, third);
        cv::Rect bottom_rect(0, depth.rows - third, depth.cols, third);
        
        double top_mean = cv::mean(depth(top_rect))[0];
        double bottom_mean = cv::mean(depth(bottom_rect))[0];
        float gradient = static_cast<float>(top_mean - bottom_mean);
        
        features.push_back(std::tanh(gradient * 5.0f));
    } else {
        features.push_back(0.0f);
    }
    
    // Asymétrie gauche/droite
    if (depth.cols >= 2) {
        cv::Rect left_rect(0, 0, depth.cols / 2, depth.rows);
        cv::Rect right_rect(depth.cols / 2, 0, depth.cols - depth.cols / 2, depth.rows);
        
        double left_mean = cv::mean(depth(left_rect))[0];
        double right_mean = cv::mean(depth(right_rect))[0];
        float asymmetry = static_cast<float>(left_mean - right_mean);
        
        features.push_back(std::tanh(asymmetry * 5.0f));
    } else {
        features.push_back(0.0f);
    }
    
    return features;
}

void MiDaSProcessor::log_deep_skel_header() {
    std::ofstream f(deep_skel_log_file, std::ios::app);
    if (f.is_open()) {
        f << "{\"version\":\"TensorRT\",\"sectors\":" << SECTORS 
          << ",\"features\":" << (SECTORS * 3 + 2) << "}\n";
        f << "#" << std::string(78, '-') << "#\n";
        f.close();
    }
}

void MiDaSProcessor::log_deep_skel_data(int frame_id, const std::vector<float>& state) {
    std::ofstream f(deep_skel_log_file, std::ios::app);
    if (f.is_open()) {
        f << "{\"frame_id\":" << frame_id << ",\"features\":[";
        for (size_t i = 0; i < state.size(); ++i) {
            if (i > 0) f << ",";
            f << std::fixed << std::setprecision(6) << state[i];
        }
        f << "]}\n";
        f.flush();
    }
}

void MiDaSProcessor::bb_writer() {
    if (!bb_log_enabled) {
        return;
    }
    std::filesystem::path out_dir = Config::BB_DIR / "midas";
    std::filesystem::create_directories(out_dir);
    std::filesystem::path meta_file = out_dir / "meta.log";
    
    while (bb_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        std::lock_guard<std::mutex> lock(bb_mutex);
        if (!bb_queue.empty()) {
            auto [frame_id, payload] = bb_queue.front();
            bb_queue.pop();
            
            std::ofstream f(meta_file, std::ios::app);
            if (f.is_open()) {
                f << "{\"frame_id\":" << frame_id << "}\n";
                f.flush();
            }
        }
    }
}

MiDaSProcessor::MiDaSProcessor(int gpu_id) 
    : runtime(nullptr),
      engine(nullptr),
      context(nullptr),
      input_buffer(nullptr),
      output_buffer(nullptr),
      cpu_output_buffer(),
      cuda_stream(nullptr),
      input_dims(),
      output_dims(),
      input_width(384), 
      input_height(384), 
      gpu_id(gpu_id),
      SECTORS(12), 
      EMA_ALPHA(0.3f),
      polar_masks(),
      ema_state(),
      deep_skel_log_file(),
      bb_queue(),
      bb_thread(),
      bb_mutex(),
      bb_running(true),
      logger(nullptr) {
    
    logger = setup_logger("midas_processor", Config::LOG_DIR);
    logger->info("🚀 Initialisation MiDaS TensorRT sur GPU " + std::to_string(gpu_id));

    bool ai_log_to_file = false;
    if (!ai_log_to_file) {
        logger->disable_file_output();
        deep_skel_log_enabled = false;
        bb_log_enabled = false;
    }
    
    try {
        // 1. Initialiser CUDA
        logger->info("🔧 Initialisation CUDA pour MiDaS...");
        cudaSetDevice(gpu_id);
        
        // Afficher les informations GPU
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, gpu_id);
        logger->info("📊 GPU MiDaS: " + std::string(prop.name) + 
                    " (Compute " + std::to_string(prop.major) + "." + std::to_string(prop.minor) + ")");
        logger->info("💾 Mémoire GPU: " + std::to_string(prop.totalGlobalMem / 1024 / 1024) + " MB");
        logger->info("⚡ Multiprocessors: " + std::to_string(prop.multiProcessorCount));
        
        // 2. Créer le runtime TensorRT
        logger->info("🔧 Création TensorRT runtime pour MiDaS...");
        runtime = nvinfer1::createInferRuntime(gMidasLogger);
        if (!runtime) {
            throw std::runtime_error("Impossible de créer TensorRT runtime");
        }
        
        // 3. Charger le fichier .engine
        std::string engine_path = Config::MIDAS_ENGINE;
        logger->info("Chargement engine MiDaS: " + engine_path);
        
        std::ifstream engine_file(engine_path, std::ios::binary);
        if (!engine_file) {
            throw std::runtime_error("Fichier engine MiDaS non trouvé: " + engine_path);
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
            throw std::runtime_error("Impossible de désérialiser le moteur MiDaS");
        }
        
        // 5. Créer le contexte d'exécution
        context = engine->createExecutionContext();
        if (!context) {
            throw std::runtime_error("Impossible de créer contexte d'exécution MiDaS");
        }

        // 6. Allouer les buffers CUDA
        int nbIOTensors = engine->getNbIOTensors();
        logger->info("Nombre de IO tensors MiDaS: " + std::to_string(nbIOTensors));

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
                logger->info("Input buffer MiDaS: " + std::to_string(binding_size) + " bytes");

                // Mettre à jour la taille d'entrée
                if (dims.nbDims == 4) {
                    input_height = dims.d[2];
                    input_width = dims.d[3];
                } else if (dims.nbDims == 3) {
                    input_height = dims.d[1];
                    input_width = dims.d[2];
                }
            } else { // Output
                output_dims = dims;
                output_buffer = d_buffer;
                logger->info("Output buffer MiDaS: " + std::to_string(binding_size) + " bytes");

                // Allouer aussi le buffer CPU pour la sortie
                cpu_output_buffer.resize(binding_size / sizeof(float));
            }
        }

        // 7. Créer le stream CUDA
        cudaStreamCreate(&cuda_stream);

        logger->info("✓ MiDaS TensorRT initialisé avec succès");
        logger->info("Résolution d'entrée: " + std::to_string(input_width) + 
                    "x" + std::to_string(input_height));
        
    } catch (const std::exception& e) {
        logger->error("Erreur initialisation TensorRT MiDaS: " + std::string(e.what()));
        throw;
    }
    
    // Initialiser l'état EMA
    ema_state.resize(SECTORS * 3 + 2, 0.0f);
    
    // Construire les masques polaires
    polar_masks = build_polar_masks(input_height, input_width, SECTORS);
    
    if (deep_skel_log_enabled) {
        std::filesystem::path deep_skel_dir = Config::BASE_DIR / "deep_skel_logs";
        std::filesystem::create_directories(deep_skel_dir);
        auto now = std::time(nullptr);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&now), "%Y%m%d_%H%M%S");
        deep_skel_log_file = deep_skel_dir / ("midas_trt_" + ss.str() + ".log");
        log_deep_skel_header();
    }

    if (bb_log_enabled) {
        bb_thread = std::thread(&MiDaSProcessor::bb_writer, this);
    }
    
    logger->info("MiDaS Processor TensorRT initialisé");
}

MiDaSProcessor::~MiDaSProcessor() {
    cleanup();
}

std::tuple<cv::Mat, cv::Mat, std::vector<float>> MiDaSProcessor::process(int frame_id, const cv::Mat& frame) {
    cv::Mat depth_colored;
    cv::Mat depth_map;
    std::vector<float> skel_state;
    
    try {
        logger->debug("MiDaS TensorRT frame " + std::to_string(frame_id) + 
                     " (" + std::to_string(frame.cols) + "x" + std::to_string(frame.rows) + ")");
        
        if (frame.empty()) {
            throw std::runtime_error("Frame vide");
        }
        
        // 1. Préprocessing: resize + RGB + normalisation
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(input_width, input_height));
        
        cv::Mat rgb;
        cv::cvtColor(resized, rgb, cv::COLOR_BGR2RGB);
        
        // 2. Normalisation MiDaS (0-255 -> 0-1)
        cv::Mat rgb_float;
        rgb.convertTo(rgb_float, CV_32F, 1.0 / 255.0);
        
        // 3. Réorganiser en format CHW pour TensorRT
        std::vector<float> input_data(input_width * input_height * 3);
        for (int c = 0; c < 3; ++c) {
            for (int y = 0; y < input_height; ++y) {
                const float* row = rgb_float.ptr<float>(y);
                for (int x = 0; x < input_width; ++x) {
                    size_t idx = c * input_height * input_width + y * input_width + x;
                    input_data[idx] = row[x * 3 + (2 - c)]; // BGR to RGB
                }
            }
        }
        
        // 4. Copier les données vers GPU
        size_t input_size = input_data.size() * sizeof(float);
        cudaMemcpyAsync(input_buffer, input_data.data(), input_size, 
                       cudaMemcpyHostToDevice, cuda_stream);
        
        // 5. Lier les buffers aux tensors
        context->setTensorAddress("input", input_buffer);
        context->setTensorAddress("output", output_buffer);
        
        // 6. Exécuter l'inférence TensorRT
        bool success = context->enqueueV3(cuda_stream);
        
        if (!success) {
            throw std::runtime_error("Échec execution TensorRT MiDaS");
        }
        
        // 7. Copier les résultats vers CPU
        // 6. Copier les résultats vers CPU
        size_t output_size = cpu_output_buffer.size() * sizeof(float);
        cudaMemcpyAsync(cpu_output_buffer.data(), output_buffer, output_size,
                       cudaMemcpyDeviceToHost, cuda_stream);
        
        cudaStreamSynchronize(cuda_stream);
        
        // 7. Récupérer les dimensions de sortie
        int output_height = input_height;
        int output_width = input_width;
        
        if (output_dims.nbDims == 4) {
            output_height = output_dims.d[2];
            output_width = output_dims.d[3];
        } else if (output_dims.nbDims == 3) {
            output_height = output_dims.d[1];
            output_width = output_dims.d[2];
        } else if (output_dims.nbDims == 2) {
            output_height = output_dims.d[0];
            output_width = output_dims.d[1];
        }
        
        // 8. Créer la carte de profondeur
        if (cpu_output_buffer.empty()) {
            throw std::runtime_error("Buffer de sortie MiDaS vide");
        }
        
        cv::Mat depth_small;
        if (output_dims.nbDims == 3) {
            // Format [1, H, W]
            depth_small = cv::Mat(output_height, output_width, CV_32F, cpu_output_buffer.data());
        } else if (output_dims.nbDims == 2) {
            // Format [H, W]
            depth_small = cv::Mat(output_height, output_width, CV_32F, cpu_output_buffer.data());
        } else {
            throw std::runtime_error("Format de sortie MiDaS non supporté: " + std::to_string(output_dims.nbDims) + " dimensions");
        }
        
        if (depth_small.empty()) {
            throw std::runtime_error("Carte de profondeur MiDaS vide après création");
        }
        
        // 9. Redimensionner à la taille originale
        if (frame.empty()) {
            throw std::runtime_error("Frame d'entrée vide");
        }
        
        cv::resize(depth_small, depth_map, frame.size(), 0, 0, cv::INTER_CUBIC);
        
        // 10. Normaliser et coloriser
        cv::Mat depth_norm;
        double min_val, max_val;
        cv::minMaxLoc(depth_map, &min_val, &max_val);
        
        logger->info("📏 Depth range: min=" + std::to_string(min_val) + 
                 ", max=" + std::to_string(max_val));
        
        if (std::abs(max_val - min_val) > 1e-6) {
            depth_map.convertTo(depth_norm, CV_8U, 255.0 / (max_val - min_val), 
                               -255.0 * min_val / (max_val - min_val));
        } else {
            depth_norm = cv::Mat(depth_map.size(), CV_8U, cv::Scalar(128));
        }
        
        cv::applyColorMap(depth_norm, depth_colored, cv::COLORMAP_MAGMA);
        
        // 11. Deep-Skel: redimensionner pour l'analyse
        cv::Mat depth_for_skel;
        cv::resize(depth_map, depth_for_skel, cv::Size(input_width, input_height));
        
        cv::Mat depth_normalized = normalize_and_clip_depth(depth_for_skel);
        
        if (!depth_normalized.empty()) {
            // Extraire les caractéristiques
            skel_state = extract_semantic(depth_normalized);
            
            // Appliquer EMA
            if (ema_state.empty()) {
                ema_state = skel_state;
            } else {
                for (size_t i = 0; i < skel_state.size(); ++i) {
                    ema_state[i] = EMA_ALPHA * skel_state[i] + (1.0f - EMA_ALPHA) * ema_state[i];
                }
            }
            
            // Logging
            if (deep_skel_log_enabled && (frame_id % deep_skel_log_every_n == 0)) {
                log_deep_skel_data(frame_id, ema_state);
            }
            
            if (ema_state.size() >= 2) {
                float gradient = ema_state[ema_state.size() - 2];
                float asymmetry = ema_state[ema_state.size() - 1];
                
                // Seulement logger si significatif
                if (frame_log_enabled && (frame_id % frame_log_every_n == 0)) {
                    if (std::abs(gradient) > 0.01f || std::abs(asymmetry) > 0.01f) {
                        logger->info("🔍 MiDaS Frame " + std::to_string(frame_id) + " - Gradient: " + std::to_string(gradient) + ", Asymétrie: " + std::to_string(asymmetry));
                    }
                }
            }
        } else {
            logger->warning("Depth map vide pour Deep-Skel");
            skel_state = std::vector<float>(SECTORS * 3 + 2, 0.0f);
            ema_state = skel_state;
        }
        
        // Ajouter un overlay TensorRT
        cv::putText(depth_colored, "TensorRT", cv::Point(10, 30), 
                   cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
        
        // Log chaque frame
        if (frame_log_enabled && (frame_id % frame_log_every_n == 0)) {
            logger->info("🎯 MiDaS TensorRT Frame " + std::to_string(frame_id) + " traité");
        }
        
    } catch (const std::exception& e) {
        logger->error("Erreur MiDaS TensorRT: " + std::string(e.what()));
        
        // Retourner des valeurs par défaut
        depth_colored = cv::Mat::zeros(360, 640, CV_8UC3);
        depth_map = cv::Mat::zeros(360, 640, CV_32F);
        cv::putText(depth_colored, "MiDaS ERROR", cv::Point(50, 100),
                   cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
        
        skel_state = std::vector<float>(SECTORS * 3 + 2, 0.0f);
        ema_state = skel_state;
    }
    
    return std::make_tuple(depth_colored, depth_map, ema_state);
}

std::vector<float> MiDaSProcessor::get_deep_skel_state() const {
    return ema_state;
}

std::future<std::tuple<cv::Mat, cv::Mat, std::vector<float>>> 
MiDaSProcessor::process_async(int frame_id, const cv::Mat& frame) {
    return std::async(std::launch::async, [this, frame_id, frame]() {
        return process(frame_id, frame);
    });
}

void MiDaSProcessor::cleanup() {
    bb_running = false;
    if (bb_thread.joinable()) {
        bb_thread.join();
    }
    
    // Nettoyer TensorRT
    if (cuda_stream) {
        cudaStreamDestroy(cuda_stream);
        cuda_stream = nullptr;
    }
    
    for (void* buffer : buffers) {
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
    
    logger->info("MiDaS TensorRT nettoyé");
}

} // namespace hesia
