#include "clean_pipeline.hpp"
#include "config.hpp"
#include <iostream>
#include <cuda_runtime.h>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <string>
#include <cstdlib>

namespace hesia {

namespace {

std::filesystem::path canonicalize_existing_path_or_throw(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::file_status status = std::filesystem::symlink_status(path, ec);
    if (!ec && status.type() == std::filesystem::file_type::symlink) {
        throw std::runtime_error("Les chemins symboliques sont interdits pour les traces M2B");
    }

    try {
        return std::filesystem::canonical(path);
    } catch (...) {
        return std::filesystem::weakly_canonical(path);
    }
}

std::filesystem::path resolve_trace_dir_or_throw(const std::filesystem::path& configured_dir) {
    std::filesystem::path allowed_root;
    if (const char* allowed_root_env = std::getenv("HESIA_ALLOWED_TRACE_ROOT");
        allowed_root_env && allowed_root_env[0] != '\0') {
        allowed_root = allowed_root_env;
    } else {
        allowed_root = Config::BB_DIR;
    }
    if (allowed_root.is_relative()) {
        allowed_root = Config::BASE_DIR / allowed_root;
    }
    std::filesystem::create_directories(allowed_root);
    const std::filesystem::path canonical_allowed_root =
        canonicalize_existing_path_or_throw(allowed_root);

    std::filesystem::path candidate = configured_dir.empty()
        ? (canonical_allowed_root / "m2b_dataset")
        : configured_dir;
    if (candidate.is_relative()) {
        candidate = Config::BASE_DIR / candidate;
    }
    std::filesystem::create_directories(candidate);
    const std::filesystem::path canonical_candidate =
        canonicalize_existing_path_or_throw(candidate);

    const std::filesystem::path relative = canonical_candidate.lexically_relative(canonical_allowed_root);
    if (relative.empty() || relative == "." || (!relative.empty() && *relative.begin() == "..")) {
        throw std::runtime_error("HESIA_M2B_TRACE_DIR hors racine autorisee");
    }

    return canonical_candidate;
}

} // namespace

CleanPipeline::CleanPipeline()
    : yolo_gpu_id(0), midas_gpu_id(0), running(false), frame_counter(0) {

    logger = setup_logger("clean_pipeline", Config::LOG_DIR);
    logger->info("Initialisation du pipeline clean...");

    if (const char* mamba_env = std::getenv("HESIA_MAMBA_LOG");
        mamba_env && std::string(mamba_env) == "1") {
        mamba_log_enabled = true;
    }
    if (const char* trace_env = std::getenv("HESIA_M2B_TRACE");
        trace_env && std::string(trace_env) == "1") {
        m2b_trace_enabled = true;
    }
    if (const char* trace_every_env = std::getenv("HESIA_M2B_TRACE_EVERY_N");
        trace_every_env && trace_every_env[0] != '\0') {
        try {
            m2b_trace_every_n = std::max(1, std::stoi(trace_every_env));
        } catch (...) {
            m2b_trace_every_n = 15;
        }
    }
    if (const char* trace_dir_env = std::getenv("HESIA_M2B_TRACE_DIR");
        trace_dir_env && trace_dir_env[0] != '\0') {
        m2b_trace_dir = trace_dir_env;
    } else {
        m2b_trace_dir = Config::BB_DIR / "m2b_dataset";
    }
    try {
        m2b_trace_dir = resolve_trace_dir_or_throw(m2b_trace_dir);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Configuration trace M2B invalide: ") + e.what());
    }

    logger->info(std::string("Mamba log: ") + (mamba_log_enabled ? "ACTIF" : "INACTIF"));
    logger->info(std::string("M2B trace: ") + (m2b_trace_enabled ? "ACTIVE" : "INACTIVE"));
    if (m2b_trace_enabled) {
        logger->info("M2B trace every N frames: " + std::to_string(m2b_trace_every_n));
        logger->info("M2B trace dir: " + m2b_trace_dir.string());
    }
}

CleanPipeline::~CleanPipeline() {
    if (logger) {
        logger->info("Arrêt du pipeline clean...");
    }
    stop();
    if (logger) {
        logger->info("[CLEAN] Pipeline clean arrêté.");
    }
}

bool CleanPipeline::setup_gpu_context() {
    logger->info("[CLEAN] Configuration des contextes GPU...");

    int device_count = 0;
    cudaError_t error = cudaGetDeviceCount(&device_count);
    if (error != cudaSuccess) {
        logger->error("CUDA non disponible: " + std::string(cudaGetErrorString(error)));
        return false;
    }

    logger->info("[GPU] GPU CUDA détectés: " + std::to_string(device_count) + " dispositifs");

    if (yolo_gpu_id >= device_count) {
        logger->warning("[WARNING] YOLO GPU ID " + std::to_string(yolo_gpu_id) +
                        " invalide, utilisation du GPU 0");
        yolo_gpu_id = 0;
    }

    if (midas_gpu_id >= device_count) {
        logger->warning("[WARNING] MiDaS GPU ID " + std::to_string(midas_gpu_id) +
                        " invalide, utilisation du GPU 0");
        midas_gpu_id = 0;
    }

    logger->info("[YOLO] YOLO GPU configuré sur device " + std::to_string(yolo_gpu_id));
    logger->info("[MIDAS] MiDaS GPU configuré sur device " + std::to_string(midas_gpu_id));

    cudaDeviceProp prop{};
    cudaGetDeviceProperties(&prop, yolo_gpu_id);
    logger->info("[GPU] GPU " + std::to_string(yolo_gpu_id) + ": " + std::string(prop.name) +
                 " (" + std::to_string(prop.major) + "." + std::to_string(prop.minor) + ")");
    logger->info("[GPU] Mémoire GPU: " + std::to_string(prop.totalGlobalMem / 1024 / 1024) + " MB");

    return true;
}

void CleanPipeline::capture_worker() {
    logger->info("Thread capture démarré");

    if (!video_manager) {
        logger->error("Video manager non initialisé");
        return;
    }

    const int max_queue_size = 30;
    bool eof_backoff_logged = false;
    logger->info("[CAPTURE] capture_worker: Démarrage capture vidéo...");

    while (running.load()) {
        try {
            const int frame_id = frame_counter.fetch_add(1);

            auto [ret, frame] = video_manager->get_frame();

            if (!ret) {
                logger->warning("[CAPTURE] capture_worker: Fin du flux vidéo ou pas de frame");
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }

            if (frame.empty()) {
                logger->warning("[CAPTURE] capture_worker: Frame vide reçue");
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
                continue;
            }

            FrameData frame_data(frame_id, std::move(frame));

            {
                std::lock_guard<std::mutex> lock(yolo_mutex);
                if (yolo_queue.size() >= static_cast<size_t>(max_queue_size)) {
                    yolo_queue.pop();
                }
                yolo_queue.push(frame_data);
            }

            {
                std::lock_guard<std::mutex> lock(midas_mutex);
                if (midas_queue.size() >= static_cast<size_t>(max_queue_size)) {
                    midas_queue.pop();
                }
                midas_queue.push(frame_data);
            }

            yolo_cv.notify_one();
            midas_cv.notify_one();

            std::this_thread::sleep_for(std::chrono::milliseconds(33));

        } catch (const std::exception& e) {
            logger->error("Erreur capture worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    logger->info("Thread capture arrêté");
}

void CleanPipeline::yolo_worker() {
    logger->info("Thread YOLO démarré sur GPU " + std::to_string(yolo_gpu_id));

    if (yolo_gpu_id >= 0) {
        cudaSetDevice(yolo_gpu_id);
    }

    while (running.load()) {
        FrameData frame_data;
        bool has_data = false;

        {
            std::unique_lock<std::mutex> lock(yolo_mutex);
            if (yolo_cv.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return !yolo_queue.empty() || !running.load(); })) {
                if (!running.load()) break;

                if (!yolo_queue.empty()) {
                    frame_data = std::move(yolo_queue.front());
                    yolo_queue.pop();
                    has_data = true;
                }
            }
        }

        if (!has_data) {
            continue;
        }

        try {
            auto start_t = std::chrono::high_resolution_clock::now();

            if (!yolo_processor) {
                // Mode dégradé
                frame_data.yolo_result = frame_data.original_frame.clone();
                cv::putText(frame_data.yolo_result, "YOLO OFF", cv::Point(50, 100),
                            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
                frame_data.yolo_detections.clear();
                frame_data.yolo_state.clear();
            } else {
                auto [yolo_frame, tracked, yolo_state, selected] =
                    yolo_processor->process(frame_data.frame_id, frame_data.original_frame);

                frame_data.yolo_result = std::move(yolo_frame);
                frame_data.yolo_detections = std::move(tracked);
                frame_data.yolo_state = std::move(yolo_state);
            }

            auto end_t = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_t - start_t);

            total_yolo_time.store(total_yolo_time.load() + static_cast<double>(duration.count()));
            yolo_frames_processed.fetch_add(1);

            {
                std::lock_guard<std::mutex> send_lock(send_mutex);
                if (send_queue.size() < 30) {
                    send_queue.push(std::move(frame_data));
                } else {
                    send_queue.pop();
                    send_queue.push(std::move(frame_data));
                }
            }
            send_cv.notify_one();

        } catch (const std::exception& e) {
            logger->error("Erreur YOLO worker: " + std::string(e.what()));
        }
    }

    logger->info("Thread YOLO arrêté");
}

void CleanPipeline::midas_worker() {
    logger->info("Thread MiDaS démarré sur GPU " + std::to_string(midas_gpu_id));

    if (midas_gpu_id >= 0) {
        cudaSetDevice(midas_gpu_id);
    }

    while (running.load()) {
        FrameData frame_data;
        bool has_data = false;

        {
            std::unique_lock<std::mutex> lock(midas_mutex);
            if (midas_cv.wait_for(lock, std::chrono::milliseconds(100),
                                  [this] { return !midas_queue.empty() || !running.load(); })) {
                if (!running.load()) break;

                if (!midas_queue.empty()) {
                    frame_data = std::move(midas_queue.front());
                    midas_queue.pop();
                    has_data = true;
                }
            }
        }

        if (!has_data) {
            continue;
        }

        try {
            auto start_t = std::chrono::high_resolution_clock::now();

            if (!midas_processor) {
                // Mode dégradé
                frame_data.midas_result = cv::Mat::zeros(frame_data.original_frame.rows,
                                                        frame_data.original_frame.cols,
                                                        CV_8UC3);
                cv::putText(frame_data.midas_result, "MiDaS OFF", cv::Point(50, 100),
                            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
                frame_data.deep_skel_state.clear();
            } else {
                auto [midas_colored, midas_map, deep_skel] =
                    midas_processor->process(frame_data.frame_id, frame_data.original_frame);

                frame_data.midas_result = std::move(midas_colored);
                frame_data.midas_depth_map = std::move(midas_map);
                frame_data.deep_skel_state = std::move(deep_skel);
            }

            auto end_t = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_t - start_t);

            total_midas_time.store(total_midas_time.load() + static_cast<double>(duration.count()));
            midas_frames_processed.fetch_add(1);

            {
                std::lock_guard<std::mutex> send_lock(send_mutex);
                if (send_queue.size() < 30) {
                    send_queue.push(std::move(frame_data));
                } else {
                    send_queue.pop();
                    send_queue.push(std::move(frame_data));
                }
            }
            send_cv.notify_one();

        } catch (const std::exception& e) {
            logger->error("Erreur MiDaS worker: " + std::string(e.what()));
        }
    }

    logger->info("Thread MiDaS arrêté");
}

void CleanPipeline::send_worker() {
    logger->info("Thread envoi démarré");

    std::unordered_map<int, FrameData> frame_cache;
    const size_t max_cache_size = 30;

    while (running.load()) {
        FrameData frame_data;
        bool has_data = false;

        {
            std::unique_lock<std::mutex> lock(send_mutex);
            if (send_cv.wait_for(lock, std::chrono::milliseconds(100),
                                 [this] { return !send_queue.empty() || !running.load(); })) {
                if (!running.load()) break;

                if (!send_queue.empty()) {
                    frame_data = std::move(send_queue.front());
                    send_queue.pop();
                    has_data = true;
                }
            }
        }

        if (!has_data) {
            continue;
        }

        try {
            const int frame_id = frame_data.frame_id;

            auto it = frame_cache.find(frame_id);
            if (it != frame_cache.end()) {
                if (frame_data.yolo_result.empty() && !it->second.yolo_result.empty()) {
                    frame_data.yolo_result = it->second.yolo_result;
                    frame_data.yolo_detections = it->second.yolo_detections;
                }
                if (frame_data.yolo_state.empty() && !it->second.yolo_state.empty()) {
                    frame_data.yolo_state = it->second.yolo_state;
                }
                if (frame_data.midas_result.empty() && !it->second.midas_result.empty()) {
                    frame_data.midas_result = it->second.midas_result;
                    frame_data.deep_skel_state = it->second.deep_skel_state;
                }
                if (frame_data.midas_depth_map.empty() && !it->second.midas_depth_map.empty()) {
                    frame_data.midas_depth_map = it->second.midas_depth_map;
                }
                if (frame_data.original_frame.empty() && !it->second.original_frame.empty()) {
                    frame_data.original_frame = it->second.original_frame;
                }

                frame_cache.erase(it);
            } else {
                if (frame_cache.size() >= max_cache_size) {
                    auto oldest = std::min_element(frame_cache.begin(), frame_cache.end(),
                                                   [](const auto& a, const auto& b) { return a.first < b.first; });
                    if (oldest != frame_cache.end()) {
                        frame_cache.erase(oldest);
                    }
                }
                frame_cache.emplace(frame_id, frame_data);
                continue;
            }

            if (frame_data.yolo_result.empty()) {
                frame_data.yolo_result = frame_data.original_frame.clone();
                cv::putText(frame_data.yolo_result, "YOLO OFF", cv::Point(50, 100),
                            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
            }
            if (frame_data.midas_result.empty()) {
                frame_data.midas_result = cv::Mat::zeros(frame_data.original_frame.rows,
                                                        frame_data.original_frame.cols,
                                                        CV_8UC3);
                cv::putText(frame_data.midas_result, "MiDaS OFF", cv::Point(50, 100),
                            cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 0, 255), 2);
            }

            if (frame_data.yolo_result.size() != frame_data.midas_result.size()) {
                cv::resize(frame_data.midas_result, frame_data.midas_result, frame_data.yolo_result.size());
            }

            // Callback réseau
            if (frame_callback) {
                frame_callback(frame_data.yolo_result, frame_data.midas_result, frame_id);
            }

            // Queue logging (30 FPS)
            if (mamba_log_enabled || m2b_trace_enabled) {
                std::lock_guard<std::mutex> log_lock(logging_mutex);
                if (logging_queue.size() < 100) {
                    logging_queue.push(frame_data);
                } else {
                    logging_queue.pop();
                    logging_queue.push(frame_data);
                }
                logging_cv.notify_one();
            }

            auto start_t = std::chrono::high_resolution_clock::now();

            // Affichage local (non bloquant)
            if (video_manager) {
                std::vector<std::vector<float>> detections;
                detections.reserve(frame_data.yolo_detections.size());
                for (const auto& [tid, det] : frame_data.yolo_detections) {
                    detections.push_back({det.x1, det.y1, det.x2, det.y2,
                                          static_cast<float>(det.cls), det.conf});
                }
                video_manager->receive_yolo_result(frame_id, frame_data.yolo_result, detections);
                video_manager->receive_midas_result(frame_id, frame_data.midas_result);
            }

            auto end_t = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_t - start_t);
            total_send_time.store(total_send_time.load() + static_cast<double>(duration.count()));
            send_frames_processed.fetch_add(1);
            frames_processed.fetch_add(1);

        } catch (const std::exception& e) {
            logger->error("Erreur send worker: " + std::string(e.what()));
        }
    }

    logger->info("Thread envoi arrêté");
}

bool CleanPipeline::start() {
    logger->info("[PIPELINE] Démarrage du CleanPipeline...");

    if (running.load()) {
        logger->warning("Pipeline déjà démarré");
        return false;
    }

    // Init stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        frames_processed = 0;
        yolo_frames_processed = 0;
        midas_frames_processed = 0;
        send_frames_processed = 0;
        total_yolo_time = 0.0;
        total_midas_time = 0.0;
        total_send_time = 0.0;
        frame_counter = 0;
        start_time = std::chrono::high_resolution_clock::now();
    }

    // Purge queues
    {
        std::lock_guard<std::mutex> lock(yolo_mutex);
        std::queue<FrameData>().swap(yolo_queue);
    }
    {
        std::lock_guard<std::mutex> lock(midas_mutex);
        std::queue<FrameData>().swap(midas_queue);
    }
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        std::queue<FrameData>().swap(send_queue);
    }
    {
        std::lock_guard<std::mutex> lock(logging_mutex);
        std::queue<FrameData>().swap(logging_queue);
    }

    // GPU
    if (!setup_gpu_context()) {
        logger->error("Échec configuration GPU");
        return false;
    }

    // Processeurs
    try {
        logger->info("Initialisation YOLO...");
        yolo_processor = std::make_unique<YOLOTrackerProcessor>(
            yolo_gpu_id,
            16,
            0.25f,
            0.45f,
            0.35f,
            5
        );
        logger->info("✓ YOLO processeur initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation YOLO: " + std::string(e.what()));
        return false;
    }

    try {
        logger->info("Initialisation MiDaS...");
        midas_processor = std::make_unique<MiDaSProcessor>(midas_gpu_id);
        logger->info("✓ MiDaS processeur initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation MiDaS: " + std::string(e.what()));
        midas_processor.reset();
        logger->warning("MiDaS désactivé: le pipeline continuera en mode dégradé.");
    }

    try {
        logger->info("Initialisation Video Manager...");
        video_manager = std::make_unique<VideoManager>();
        video_manager->start_display();
        logger->info("✓ Video manager initialisé");
    } catch (const std::exception& e) {
        logger->error("Échec initialisation video manager: " + std::string(e.what()));
        return false;
    }

    // IMPORTANT: activer running AVANT de lancer les threads.
    running.store(true);

    capture_thread = std::thread(&CleanPipeline::capture_worker, this);
    yolo_thread = std::thread(&CleanPipeline::yolo_worker, this);
    midas_thread = std::thread(&CleanPipeline::midas_worker, this);
    send_thread = std::thread(&CleanPipeline::send_worker, this);
    if (mamba_log_enabled || m2b_trace_enabled) {
        logging_thread = std::thread(&CleanPipeline::frame_logging_worker, this);
    }

    logger->info("Pipeline clean d??marr?? - " +
                 std::to_string((mamba_log_enabled || m2b_trace_enabled) ? 5 : 4) + " threads actifs");
    logger->info("YOLO: ACTIF");
    logger->info("MiDaS: " + std::string(midas_processor ? "ACTIF" : "INACTIF (mode dégradé)"));

    return true;
}

void CleanPipeline::stop() {
    if (!running.load()) {
        return;
    }

    if (logger) {
        logger->info("Arrêt du pipeline...");
    }

    running = false;

    yolo_cv.notify_all();
    midas_cv.notify_all();
    send_cv.notify_all();
    logging_cv.notify_all();

    if (capture_thread.joinable()) capture_thread.join();
    if (yolo_thread.joinable()) yolo_thread.join();
    if (midas_thread.joinable()) midas_thread.join();
    if (send_thread.joinable()) send_thread.join();
    if (logging_thread.joinable()) logging_thread.join();

    if (video_manager) {
        video_manager->cleanup();
    }

    if (yolo_processor) {
        yolo_processor->cleanup();
    }

    if (midas_processor) {
        midas_processor->cleanup();
    }

    {
        std::lock_guard<std::mutex> lock(yolo_mutex);
        std::queue<FrameData>().swap(yolo_queue);
    }
    {
        std::lock_guard<std::mutex> lock(midas_mutex);
        std::queue<FrameData>().swap(midas_queue);
    }
    {
        std::lock_guard<std::mutex> lock(send_mutex);
        std::queue<FrameData>().swap(send_queue);
    }
    {
        std::lock_guard<std::mutex> lock(logging_mutex);
        std::queue<FrameData>().swap(logging_queue);
    }

    if (logger) {
        logger->info("Pipeline arrêté");
    }
}

void CleanPipeline::set_gpu_ids(int yolo_gpu, int midas_gpu) {
    yolo_gpu_id = yolo_gpu;
    midas_gpu_id = midas_gpu;
    if (logger) {
        logger->info("GPU IDs configurés - YOLO: " + std::to_string(yolo_gpu_id) +
                     ", MiDaS: " + std::to_string(midas_gpu_id));
    }
}

CleanPipeline::PipelineStats CleanPipeline::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex);

    PipelineStats stats;

    auto now = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    double elapsed_seconds = duration.count() / 1000.0;

    if (elapsed_seconds > 0) {
        int processed_frames = frames_processed.load();
        stats.avg_fps = (processed_frames > 0) ? (processed_frames / elapsed_seconds) : 0.0;

        int yolo_count = yolo_frames_processed.load();
        int midas_count = midas_frames_processed.load();
        int send_count = send_frames_processed.load();

        stats.avg_yolo_ms = (yolo_count > 0) ? (total_yolo_time.load() / yolo_count) : 0.0;
        stats.avg_midas_ms = (midas_count > 0) ? (total_midas_time.load() / midas_count) : 0.0;
        stats.avg_send_ms = (send_count > 0) ? (total_send_time.load() / send_count) : 0.0;
    }

    return stats;
}

void CleanPipeline::log_stats() const {
    auto stats = get_stats();
    if (logger) {
        int processed_frames = frames_processed.load();
        int yolo_count = yolo_frames_processed.load();
        int midas_count = midas_frames_processed.load();
        int send_count = send_frames_processed.load();

        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
        double elapsed_seconds = duration.count() / 1000.0;

        logger->info("Pipeline Stats - FPS: " + std::to_string(stats.avg_fps) +
                     " (frames: " + std::to_string(processed_frames) +
                     ", temps: " + std::to_string(elapsed_seconds) + "s)" +
                     ", YOLO: " + std::to_string(stats.avg_yolo_ms) + "ms" +
                     " (" + std::to_string(yolo_count) + " frames)" +
                     ", MiDaS: " + std::to_string(stats.avg_midas_ms) + "ms" +
                     " (" + std::to_string(midas_count) + " frames)" +
                     ", Send: " + std::to_string(stats.avg_send_ms) + "ms" +
                     " (" + std::to_string(send_count) + " frames)");
    }
}

void CleanPipeline::log_combined_frame_to_disk(const cv::Mat& combined_frame, int frame_id) {
    (void)combined_frame;
    (void)frame_id;
    return;
}

void CleanPipeline::frame_logging_worker() {
    const int target_logging_ms = 33;

    // Logs dataset pour modele sequentiel (Mamba)
    const std::filesystem::path mamba_dir = Config::BB_DIR / "mamba_dataset";
    const std::filesystem::path state_file = mamba_dir / "states.log";
    if (mamba_log_enabled) {
        std::filesystem::create_directories(mamba_dir);
    }

    const std::filesystem::path trace_rgb_dir = m2b_trace_dir / "rgb";
    const std::filesystem::path trace_depth_dir = m2b_trace_dir / "depth";
    const std::filesystem::path trace_preview_dir = m2b_trace_dir / "preview";
    const std::filesystem::path trace_meta_file = m2b_trace_dir / "metadata.jsonl";
    if (m2b_trace_enabled) {
        std::filesystem::create_directories(trace_rgb_dir);
        std::filesystem::create_directories(trace_depth_dir);
        std::filesystem::create_directories(trace_preview_dir);
    }
    const int YOLO_K = 16;

    std::ofstream state_f(state_file, std::ios::app);
    if (mamba_log_enabled && !state_f.is_open()) {
        if (logger) {
            logger->warning("Impossible d'ouvrir le log Mamba: " + state_file.string());
        }
    }
    std::ofstream trace_meta_f(trace_meta_file, std::ios::app);
    if (m2b_trace_enabled && !trace_meta_f.is_open()) {
        if (logger) {
            logger->warning("Impossible d'ouvrir le log M2B: " + trace_meta_file.string());
        }
    }

    std::string buffer;
    buffer.reserve(4096);
    int batch_count = 0;
    auto last_flush = std::chrono::steady_clock::now();
    const int flush_ms = 2000;

    while (running.load()) {
        auto loop_start = std::chrono::high_resolution_clock::now();

        FrameData frame_data;
        bool has_data = false;

        {
            std::unique_lock<std::mutex> lock(logging_mutex);
            if (logging_cv.wait_for(lock, std::chrono::milliseconds(10),
                                    [this] { return !logging_queue.empty() || !running.load(); })) {
                if (!running.load()) break;

                if (!logging_queue.empty()) {
                    frame_data = std::move(logging_queue.front());
                    logging_queue.pop();
                    has_data = true;
                }
            }
        }

        if (has_data) {
            try {
                if (frame_data.yolo_result.size() != frame_data.midas_result.size()) {
                    cv::resize(frame_data.midas_result, frame_data.midas_result, frame_data.yolo_result.size());
                }

                const double ts = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();

                std::vector<float> y_state = frame_data.yolo_state;
                if (y_state.empty()) {
                    y_state.assign(static_cast<size_t>(YOLO_K) * 6, 0.0f);
                    for (int i = 0; i < YOLO_K; ++i) {
                        y_state[static_cast<size_t>(i) * 6 + 5] = -1.0f;
                    }
                }

                std::ostringstream json;
                json.setf(std::ios::fixed);
                json << std::setprecision(6);
                json << "{\"ts\":" << ts << ",\"frame_id\":" << frame_data.frame_id;
                json << ",\"yolo_state\":[";
                for (size_t i = 0; i < y_state.size(); ++i) {
                    if (i) json << ",";
                    json << y_state[i];
                }
                json << "],\"midas_state\":[";
                for (size_t i = 0; i < frame_data.deep_skel_state.size(); ++i) {
                    if (i) json << ",";
                    json << frame_data.deep_skel_state[i];
                }
                json << "]}";

                if (mamba_log_enabled) {
                    buffer += json.str();
                    buffer += "\n";
                    batch_count++;

                    auto now = std::chrono::steady_clock::now();
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_flush).count();

                    if (batch_count >= mamba_log_batch || elapsed_ms >= flush_ms) {
                        if (state_f.is_open()) {
                            state_f << buffer;
                            state_f.flush();
                        }
                        buffer.clear();
                        batch_count = 0;
                        last_flush = now;
                    }
                }

                if (m2b_trace_enabled && (frame_data.frame_id % m2b_trace_every_n == 0)) {
                    const std::string frame_base = "frame_" + std::to_string(frame_data.frame_id);
                    const auto rgb_path = trace_rgb_dir / (frame_base + ".jpg");
                    const auto depth_path = trace_depth_dir / (frame_base + ".png");
                    const auto preview_path = trace_preview_dir / (frame_base + ".jpg");

                    if (!frame_data.original_frame.empty()) {
                        cv::imwrite(rgb_path.string(), frame_data.original_frame);
                    }

                    double depth_min = 0.0;
                    double depth_max = 0.0;
                    if (!frame_data.midas_depth_map.empty()) {
                        cv::minMaxLoc(frame_data.midas_depth_map, &depth_min, &depth_max);
                        cv::Mat depth_u16(frame_data.midas_depth_map.size(), CV_16UC1, cv::Scalar(0));
                        if (std::abs(depth_max - depth_min) > 1e-9) {
                            frame_data.midas_depth_map.convertTo(
                                depth_u16,
                                CV_16UC1,
                                65535.0 / (depth_max - depth_min),
                                -65535.0 * depth_min / (depth_max - depth_min));
                        }
                        cv::imwrite(depth_path.string(), depth_u16);
                    }

                    if (!frame_data.original_frame.empty() && !frame_data.midas_result.empty()) {
                        cv::Mat preview;
                        cv::hconcat(frame_data.original_frame, frame_data.midas_result, preview);
                        cv::imwrite(preview_path.string(), preview);
                    }

                    if (trace_meta_f.is_open()) {
                        std::ostringstream meta;
                        meta.setf(std::ios::fixed);
                        meta << std::setprecision(6);
                        meta << "{\"frame_id\":" << frame_data.frame_id
                             << ",\"rgb\":\"" << rgb_path.filename().string() << "\""
                             << ",\"depth\":\"" << depth_path.filename().string() << "\""
                             << ",\"preview\":\"" << preview_path.filename().string() << "\""
                             << ",\"depth_min\":" << depth_min
                             << ",\"depth_max\":" << depth_max
                             << ",\"yolo_state\":[";
                        for (size_t i = 0; i < y_state.size(); ++i) {
                            if (i) meta << ",";
                            meta << y_state[i];
                        }
                        meta << "],\"midas_state\":[";
                        for (size_t i = 0; i < frame_data.deep_skel_state.size(); ++i) {
                            if (i) meta << ",";
                            meta << frame_data.deep_skel_state[i];
                        }
                        meta << "]}\n";
                        trace_meta_f << meta.str();
                        trace_meta_f.flush();
                    }
                }

            } catch (const std::exception& e) {
                if (logger) {
                    logger->error("Erreur logging worker: " + std::string(e.what()));
                }
            }
        }

        auto loop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::high_resolution_clock::now() - loop_start)
                           .count();

        if (loop_ms < target_logging_ms) {
            std::this_thread::sleep_for(std::chrono::milliseconds(target_logging_ms - loop_ms));
        }
    }

    if (!buffer.empty() && state_f.is_open()) {
        state_f << buffer;
        state_f.flush();
    }

    if (logger) {
        logger->info("Thread logging arr??t??");
    }
}

} // namespace hesia
