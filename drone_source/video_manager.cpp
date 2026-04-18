#include "video_manager.hpp"
#include <stdexcept>
#include <chrono>
#include <thread>
#include <filesystem>

namespace hesia {

VideoManager::VideoManager() : running(false) {
    logger = setup_logger("video_manager", Config::LOG_DIR);
    display_enabled = false;
    logger->info("Display disabled (config fixe)");
    
    // Essayer d'abord la vidéo configurée
    std::string video_path = Config::VIDEO_PATH;
    
    // Vérifier si le chemin est relatif et ajuster
    if (video_path.find("/") == 0 || video_path.find("\\") == 0 || video_path.find(":") != std::string::npos) {
        // Chemin absolu, utiliser tel quel
    } else {
        // Chemin relatif, essayer depuis le répertoire courant
        std::filesystem::path current_path = std::filesystem::current_path();
        std::filesystem::path full_path = current_path / video_path;
        
        // Si ça n'existe pas, essayer depuis le répertoire parent
        if (!std::filesystem::exists(full_path)) {
            full_path = current_path.parent_path() / video_path;
        }
        
        video_path = full_path.string();
    }
    
    logger->info("Tentative d'ouverture de la vidéo: " + video_path);
    logger->info("Chemin de travail actuel: " + std::filesystem::current_path().string());
    
    cap.open(video_path);
    
    if (!cap.isOpened()) {
        logger->warning("Impossible d'ouvrir la vidéo: " + video_path + ", tentative avec webcam...");
        
        // Essayer la webcam par défaut
        cap.open(0); // Webcam par défaut
        
        if (!cap.isOpened()) {
            throw std::runtime_error("Impossible d'ouvrir la webcam");
        }
        
        logger->info("Webcam ouverte avec succès");
        is_file_source = false;
    } else {
        logger->info("Vidéo ouverte: " + video_path);
        is_file_source = true;
    }

    video_path_ = video_path;
    
    frame_count = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    fps = cap.get(cv::CAP_PROP_FPS);
    width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    
    logger->info("Video Manager initialisé: " + std::to_string(width) + 
                "x" + std::to_string(height) + " @ " + std::to_string(fps) + "fps");
}

VideoManager::~VideoManager() {
    cleanup();
}

void VideoManager::start_display() {
    if (!display_enabled) {
        logger->info("Display thread skipped (display disabled)");
        return;
    }
    running = true;
    display_thread = std::thread(&VideoManager::display_loop, this);
    logger->info("Affichage démarré");
}

std::pair<bool, cv::Mat> VideoManager::get_frame() {
    cv::Mat frame;
    bool ret = cap.read(frame);
    if (!ret && is_file_source && frame_count > 0 && !video_path_.empty()) {
        // End of file: loop by rewinding or reopening.
        if (!cap.set(cv::CAP_PROP_POS_FRAMES, 0)) {
            cap.release();
            cap.open(video_path_);
        }
        ret = cap.read(frame);
        if (ret) {
            loop_count++;
            logger->info("Fin de vidéo - redémarrage (loop=" + std::to_string(loop_count) + ")");
        }
    }
    return {ret, frame};
}

void VideoManager::send_to_yolo(int frame_id, const cv::Mat& frame) {
    (void)frame_id;
    (void)frame;
    // Queue YOLO non utilisée dans cette version simplifiée
}

void VideoManager::send_to_midas(int frame_id, const cv::Mat& frame) {
    (void)frame_id;
    (void)frame;
    // Queue MiDaS non utilisée dans cette version simplifiée
}

void VideoManager::receive_yolo_result(int frame_id, const cv::Mat& frame, const std::vector<std::vector<float>>& detections) {
    if (!display_enabled) {
        return;
    }
    (void)detections;
    std::lock_guard<std::mutex> lock(display_queue_mutex);
    if (display_queue.size() < 10) {
        display_queue.push(std::make_tuple("yolo", frame_id, frame.clone()));
    }
}

void VideoManager::receive_midas_result(int frame_id, const cv::Mat& frame) {
    if (!display_enabled) {
        return;
    }
    std::lock_guard<std::mutex> lock(display_queue_mutex);
    if (display_queue.size() < 10) {
        display_queue.push(std::make_tuple("midas", frame_id, frame.clone()));
    }
}

void VideoManager::display_loop() {
    logger->info("Thread d'affichage démarré (mode transmission uniquement)");

    int processed = 0;
    while (running) {
        std::tuple<std::string, int, cv::Mat> item;
        bool has_item = false;

        {
            std::lock_guard<std::mutex> lock(display_queue_mutex);
            if (!display_queue.empty()) {
                item = std::move(display_queue.front());
                display_queue.pop();
                has_item = true;
            }
        }

        if (has_item) {
            auto& [source, frame_id, frame] = item;
            (void)source;
            (void)frame;

            processed++;
            // Réduire le bruit: log 1 frame sur 30.
            if (processed % 30 == 0) {
                logger->info("📹 Frame " + std::to_string(frame_id) + " traitée (transmission uniquement)");
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void VideoManager::cleanup() {
    running = false;
    if (display_thread.joinable()) {
        display_thread.join();
    }
    cap.release();
    logger->info("Video Manager nettoyé");
}

} // namespace hesia
