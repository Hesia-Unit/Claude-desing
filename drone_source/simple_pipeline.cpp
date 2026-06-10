#include "simple_pipeline.hpp"
#include "config.hpp"
#include <iostream>
#include <cuda_runtime.h>

namespace hesia {

SimplePipeline::SimplePipeline() 
    : running(false), yolo_gpu_id(1), midas_gpu_id(1) {
    
    logger = setup_logger("pipeline", Config::LOG_DIR);
    logger->info("Initialisation du pipeline simple...");
}

SimplePipeline::~SimplePipeline() {
    stop();
}

bool SimplePipeline::setup_gpu_context() {
    logger->info("Configuration des contextes GPU...");
    
    // Lister les GPU disponibles
    int device_count = 0;
    cudaError_t cuda_status = cudaGetDeviceCount(&device_count);
    if (cuda_status != cudaSuccess) {
        logger->error("Impossible d'obtenir le nombre de GPU: " + std::to_string(cuda_status));
        return false;
    }
    
    logger->info("Nombre de GPU détectés: " + std::to_string(device_count));
    
    // Lister les GPU
    for (int i = 0; i < device_count; ++i) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, i);
        logger->info("GPU " + std::to_string(i) + ": " + std::string(prop.name) + 
                    " (Compute: " + std::to_string(prop.major) + "." + std::to_string(prop.minor) + ")");
    }
    
    // Valider les GPU IDs
    if (yolo_gpu_id >= device_count) {
        logger->warning("YOLO GPU ID " + std::to_string(yolo_gpu_id) + 
                    " invalide, utilisation du GPU 0");
        yolo_gpu_id = 0;
    }
    
    if (midas_gpu_id >= device_count) {
        logger->warning("MiDaS GPU ID " + std::to_string(midas_gpu_id) + 
                    " invalide, utilisation du GPU 0");
        midas_gpu_id = 0;
    }
    
    // Initialiser CUDA pour YOLO
    cuda_status = cudaSetDevice(yolo_gpu_id);
    if (cuda_status != cudaSuccess) {
        logger->error("Impossible de définir le GPU YOLO " + std::to_string(yolo_gpu_id) + 
                    ": " + std::to_string(cuda_status));
        return false;
    }
    
    logger->info("YOLO GPU configuré sur device " + std::to_string(yolo_gpu_id));
    logger->info("MiDaS GPU configuré sur device " + std::to_string(midas_gpu_id));
    
    return true;
}

void SimplePipeline::capture_worker() {
    logger->info("Thread capture démarré");
    
    if (!video_manager) {
        logger->error("Video manager non initialisé");
        return;
    }
    
    while (running.load()) {
        try {
            auto [ret, frame] = video_manager->get_frame();
            if (!ret) {
                logger->info("Fin du flux vidéo");
                break;
            }
            
            static int frame_counter = 0;
            FrameData frame_data(frame_counter++, std::move(frame));
            
            // Envoyer aux workers YOLO et MiDaS
            {
                std::lock_guard<std::mutex> lock(yolo_mutex);
                yolo_queue.push(frame_data);
            }
            {
                std::lock_guard<std::mutex> lock(midas_mutex);
                midas_queue.push(frame_data);
            }
            
            yolo_cv.notify_one();
            midas_cv.notify_one();
            
        } catch (const std::exception& e) {
            logger->error("Erreur capture worker: " + std::string(e.what()));
        }
    }
    
    logger->info("Thread capture arrêté");
}

void SimplePipeline::yolo_worker() {
    logger->info("Thread YOLO démarré sur GPU " + std::to_string(yolo_gpu_id));
    
    // Configurer GPU pour YOLO
    cudaSetDevice(yolo_gpu_id);
    
    while (running.load()) {
        std::unique_lock<std::mutex> lock(yolo_mutex);
        yolo_cv.wait(lock, [this] { return !yolo_queue.empty() || !running.load(); });
        
        if (!running.load()) break;
        
        FrameData frame_data = std::move(yolo_queue.front());
        yolo_queue.pop();
        lock.unlock();
        
        try {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Traitement YOLO synchrone
            auto [yolo_frame, tracked, depthstate, selected] = 
                yolo_processor->process(frame_data.frame_id, frame_data.original_frame);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            // Vérifier que les résultats ne sont pas vides
            if (yolo_frame.empty()) {
                logger->warning("YOLO frame vide pour frame " + std::to_string(frame_data.frame_id) + 
                                ", utilisation frame originale");
                yolo_frame = frame_data.original_frame.clone();
            }
            
            // Mettre à jour les données
            frame_data.yolo_result = std::move(yolo_frame);
            frame_data.yolo_detections = std::move(tracked);
            
            logger->debug("YOLO Frame " + std::to_string(frame_data.frame_id) + 
                        " traitée - " + std::to_string(tracked.size()) + " objets détectés");
            
            // Envoyer au thread d'envoi
            {
                std::lock_guard<std::mutex> send_lock(send_mutex);
                send_queue.push(std::move(frame_data));
            }
            send_cv.notify_one();
            
        } catch (const std::exception& e) {
            logger->error("Erreur YOLO worker: " + std::string(e.what()));
        }
    }
    
    logger->info("Thread YOLO arrêté");
}

void SimplePipeline::midas_worker() {
    logger->info("Thread MiDaS démarré sur GPU " + std::to_string(midas_gpu_id));
    
    // Configurer GPU pour MiDaS
    cudaSetDevice(midas_gpu_id);
    
    while (running.load()) {
        std::unique_lock<std::mutex> lock(midas_mutex);
        midas_cv.wait(lock, [this] { return !midas_queue.empty() || !running.load(); });
        
        if (!running.load()) break;
        
        FrameData frame_data = std::move(midas_queue.front());
        midas_queue.pop();
        lock.unlock();
        
        try {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Traitement MiDaS synchrone avec Deep-Skel
            auto [midas_colored, midas_map, deep_skel] = 
                midas_processor->process(frame_data.frame_id, frame_data.original_frame);
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
            // Vérifier que les résultats ne sont pas vides
            if (midas_colored.empty()) {
                logger->warning("MiDaS frame vide pour frame " + std::to_string(frame_data.frame_id) + 
                                ", utilisation frame noire");
                midas_colored = cv::Mat::zeros(frame_data.original_frame.rows, 
                                                 frame_data.original_frame.cols, 
                                                 frame_data.original_frame.type());
            }
            
            // Mettre à jour les données
            frame_data.midas_result = std::move(midas_colored);
            frame_data.deep_skel_state = std::move(deep_skel);
            
            logger->debug("MiDaS Frame " + std::to_string(frame_data.frame_id) + 
                        " traitée - gradient=" + std::to_string(deep_skel[deep_skel.size()-2]) + 
                            " asymmetry=" + std::to_string(deep_skel[deep_skel.size()-1]));
            
            // Envoyer au thread d'envoi
            {
                std::lock_guard<std::mutex> send_lock(send_mutex);
                send_queue.push(std::move(frame_data));
            }
            send_cv.notify_one();
            
        } catch (const std::exception& e) {
            logger->error("Erreur MiDaS worker: " + std::string(e.what()));
        }
    }
    
    logger->info("Thread MiDaS arrêté");
}

void SimplePipeline::send_worker() {
    logger->info("Thread envoi démarré");
    
    while (running.load()) {
        std::unique_lock<std::mutex> lock(send_mutex);
        send_cv.wait(lock, [this] { return !send_queue.empty() || !running.load(); });
        
        if (!running.load()) break;
        
        FrameData frame_data = std::move(send_queue.front());
        send_queue.pop();
        lock.unlock();
        
        try {
            auto start_time = std::chrono::high_resolution_clock::now();
            
            // Traitement simple et direct
            logger->debug("Traitement frame " + std::to_string(frame_data.frame_id) + 
                        " - YOLO: " + std::to_string(frame_data.yolo_result.empty()) + 
                        ", MiDaS: " + std::to_string(frame_data.midas_result.empty()));
            
            // Envoyer si on a les deux résultats
            if (!frame_data.yolo_result.empty() && !frame_data.midas_result.empty()) {
                
                // Vérifier la compatibilité des tailles
                if (frame_data.yolo_result.size() != frame_data.midas_result.size()) {
                    cv::Mat resized_midas;
                    cv::resize(frame_data.midas_result, resized_midas, frame_data.yolo_result.size());
                    
                    // Envoyer au video manager
                    std::vector<std::vector<float>> detections;
                    for (const auto& [tid, det] : frame_data.yolo_detections) {
                        detections.push_back({det.x1, det.y1, det.x2, det.y2, 
                                                 static_cast<float>(det.cls), det.conf});
                    }
                    
                    if (video_manager) {
                        video_manager->receive_yolo_result(frame_data.frame_id, 
                                                             frame_data.yolo_result, detections);
                        video_manager->receive_midas_result(frame_data.frame_id, 
                                                             resized_midas);
                    }
                    
                    
                    logger->info("Frame " + std::to_string(frame_data.frame_id) + 
                                        " envoyée - YOLO: " + std::to_string(frame_data.yolo_detections.size()) + 
                                        " objets");
                } else {
                    logger->warning("Frame " + std::to_string(frame_data.frame_id) + 
                                " incomplète - YOLO: " + std::to_string(frame_data.yolo_result.empty()) + 
                                ", MiDaS: " + std::to_string(frame_data.midas_result.empty()));
                }
            
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            
        } catch (const std::exception& e) {
            logger->error("Erreur send worker: " + std::string(e.what()));
        }
    }
    
    logger->info("Thread envoi arrêté");
}

bool SimplePipeline::start() {
    if (running.load()) {
        logger->warning("Pipeline déjà démarré");
        return false;
    }
    
    logger->info("Démarrage du pipeline simple...");
    
    // Configuration GPU
    if (!setup_gpu_context()) {
        logger->error("Échec configuration GPU");
        return false;
    }
    
    // Initialisation des processeurs
    try {
        yolo_processor = std::make_unique<YOLOTrackerProcessor>(0, 6, 0.5f, 0.45f, 0.35f, 3);
        logger->info("✓ YOLO processeur initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation YOLO: " + std::string(e.what()));
        return false;
    }
    
    try {
        midas_processor = std::make_unique<MiDaSProcessor>(0);
        logger->info("✓ MiDaS processeur initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation MiDaS: " + std::string(e.what()));
        return false;
    }
    
    try {
        video_manager = std::make_unique<VideoManager>();
        video_manager->start_display();
        logger->info("✓ Video manager initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation video manager: " + std::string(e.what()));
        return false;
    }
    
    // Démarrage des threads
    running = true;
    
    capture_thread = std::thread(&SimplePipeline::capture_worker, this);
    yolo_thread = std::thread(&SimplePipeline::yolo_worker, this);
    midas_thread = std::thread(&SimplePipeline::midas_worker, this);
    send_thread = std::thread(&SimplePipeline::send_worker, this);
    
    logger->info("Pipeline démarré - 4 threads actifs");
    return true;
}

void SimplePipeline::stop() {
    if (!running.load()) {
        return;
    }
    
    logger->info("Arrêt du pipeline...");
    
    running = false;
    
    // Réveiller tous les threads
    capture_cv.notify_all();
    yolo_cv.notify_all();
    midas_cv.notify_all();
    send_cv.notify_all();
    
    // Joindre les threads
    if (capture_thread.joinable()) capture_thread.join();
    if (yolo_thread.joinable()) yolo_thread.join();
    if (midas_thread.joinable()) midas_thread.join();
    if (send_thread.joinable()) send_thread.join();
    
    // Nettoyage
    if (video_manager) {
        video_manager->cleanup();
    }
    
    if (yolo_processor) {
        yolo_processor->cleanup();
    }
    
    if (midas_processor) {
        midas_processor->cleanup();
    }
    
    logger->info("Pipeline arrêté");
}

void SimplePipeline::set_gpu_ids(int yolo_gpu, int midas_gpu) {
    yolo_gpu_id = yolo_gpu;
    midas_gpu_id = midas_gpu;
    logger->info("GPU IDs configurés - YOLO: " + std::to_string(yolo_gpu_id) + 
                ", MiDaS: " + std::to_string(midas_gpu_id));
}

SimplePipeline::PipelineStats SimplePipeline::get_stats() const {
    // PipelineStats stays zeroed until explicit runtime accounting is wired in.
    return PipelineStats{};
}

void SimplePipeline::log_stats() const {
    auto stats = get_stats();
    logger->info("Pipeline Stats - FPS: " + std::to_string(stats.avg_fps) + 
                ", YOLO: " + std::to_string(stats.avg_yolo_ms) + "ms" +
                ", MiDaS: " + std::to_string(stats.avg_midas_ms) + "ms" +
                ", Send: " + std::to_string(stats.avg_send_ms) + "ms");
}

} // namespace hesia
