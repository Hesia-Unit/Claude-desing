#include "video_manager.hpp"
#include <stdexcept>
#include <chrono>
#include <thread>
#include <filesystem>
#include <cstdlib>
#include <cctype>

namespace hesia {

namespace {

static bool env_flag_enabled(const char* name) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return false;
    }

    std::string normalized(value);
    for (char& c : normalized) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

static bool env_flag_enabled_or_default(const char* name, bool default_value) {
    const char* value = std::getenv(name);
    if (!value || value[0] == '\0') {
        return default_value;
    }
    return env_flag_enabled(name);
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    return -1;
}

static std::string percent_decode_uri_component(const std::string& value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int hi = hex_value(value[i + 1]);
            const int lo = hex_value(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(value[i]);
    }

    return decoded;
}

static std::string normalize_file_uri(const std::string& configured_source) {
    std::string raw_path;
    if (configured_source.rfind("file://localhost/", 0) == 0) {
        raw_path = configured_source.substr(sizeof("file://localhost") - 1);
    } else if (configured_source.rfind("file://", 0) == 0) {
        raw_path = configured_source.substr(sizeof("file://") - 1);
    } else if (configured_source.rfind("file:", 0) == 0) {
        raw_path = configured_source.substr(sizeof("file:") - 1);
    } else {
        return configured_source;
    }

    if (raw_path.empty()) {
        throw std::runtime_error("URI file: vide dans HESIA_VIDEO_SOURCE");
    }

    return percent_decode_uri_component(raw_path);
}

static std::string resolve_video_source(const std::string& configured_source) {
    if (configured_source.empty()) {
        throw std::runtime_error("Aucune source video reelle configuree (HESIA_VIDEO_SOURCE)");
    }

    if (configured_source.rfind("camera:", 0) == 0) {
        return configured_source;
    }

    const std::string normalized_source = normalize_file_uri(configured_source);
    std::filesystem::path path(normalized_source);
    if (path.is_relative()) {
        try {
            path = std::filesystem::weakly_canonical(std::filesystem::current_path() / path);
        } catch (...) {
            path = std::filesystem::absolute(std::filesystem::current_path() / path);
        }
    }
    return path.string();
}

static std::filesystem::path canonical_regular_video_path_or_throw(const std::string& source) {
    std::filesystem::path requested(source);
    std::error_code ec;
    const std::filesystem::file_status requested_status = std::filesystem::symlink_status(requested, ec);
    if (!ec && requested_status.type() == std::filesystem::file_type::symlink) {
        throw std::runtime_error("Les sources video symboliques sont interdites");
    }

    std::filesystem::path canonical_path;
    try {
        canonical_path = std::filesystem::canonical(requested);
    } catch (const std::exception&) {
        throw std::runtime_error("Source video non canonique ou introuvable: " + source);
    }

    if (!std::filesystem::is_regular_file(canonical_path)) {
        throw std::runtime_error("La source video doit etre un fichier regulier");
    }

    const char* root_env = std::getenv("HESIA_ALLOWED_VIDEO_ROOT");
    if (root_env && root_env[0] != '\0') {
        const std::filesystem::path allowed_root = std::filesystem::canonical(root_env);
        std::filesystem::path relative;
        try {
            relative = std::filesystem::relative(canonical_path, allowed_root);
        } catch (const std::exception&) {
            throw std::runtime_error("Source video hors racine autorisee");
        }
        if (relative.empty() || relative.begin() == relative.end() ||
            *relative.begin() == std::filesystem::path("..")) {
            throw std::runtime_error("Source video hors racine autorisee");
        }
    }

    return canonical_path;
}

} // namespace

VideoManager::VideoManager() : running(false) {
    logger = setup_logger("video_manager", Config::LOG_DIR);
    display_enabled = false;
    logger->info("Display disabled (config fixe)");

    const std::string source = resolve_video_source(Config::VIDEO_PATH);
    logger->info("Tentative d'ouverture de la source video: " + source);
    logger->info("Chemin de travail actuel: " + std::filesystem::current_path().string());

    if (source.rfind("camera:", 0) == 0) {
        const std::string index_text = source.substr(7);
        size_t consumed = 0;
        int camera_index = -1;
        try {
            camera_index = std::stoi(index_text, &consumed);
        } catch (...) {
            throw std::runtime_error("Indice camera invalide dans HESIA_VIDEO_SOURCE");
        }
        if (consumed != index_text.size() || camera_index < 0) {
            throw std::runtime_error("Indice camera invalide dans HESIA_VIDEO_SOURCE");
        }
        if (!cap.open(camera_index, cv::CAP_V4L2)) {
            throw std::runtime_error("Impossible d'ouvrir la camera V4L2 demandee");
        }
        is_file_source = false;
        video_path_ = source;
        logger->info("Camera V4L2 ouverte: index=" + std::to_string(camera_index));
    } else {
        const std::filesystem::path source_path(source);
        if (!std::filesystem::exists(source_path)) {
            throw std::runtime_error("Source video introuvable: " + source);
        }

        const bool is_regular_file = std::filesystem::is_regular_file(source_path);
        const bool allow_file_source = env_flag_enabled("HESIA_ALLOW_FILE_VIDEO_SOURCE");
        if (is_regular_file && !allow_file_source) {
            throw std::runtime_error("Les replays video sur fichier sont interdits sans HESIA_ALLOW_FILE_VIDEO_SOURCE=1");
        }

        const std::filesystem::path effective_source_path =
            is_regular_file ? canonical_regular_video_path_or_throw(source) : source_path;

        if (!cap.open(effective_source_path.string())) {
            throw std::runtime_error("Impossible d'ouvrir la source video configuree");
        }

        is_file_source = is_regular_file;
        file_loop_enabled_ = is_file_source &&
                             env_flag_enabled_or_default("HESIA_FILE_VIDEO_LOOP", false);
        video_path_ = effective_source_path.string();
        logger->info(std::string("Source video ouverte: ") + video_path_ +
                     (is_file_source ? " (replay fichier explicite)" : " (device)"));
        if (is_file_source) {
            logger->info(std::string("Bouclage replay fichier: ") +
                         (file_loop_enabled_ ? "ACTIF" : "INACTIF"));
        }
    }

    frame_count = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    fps = cap.get(cv::CAP_PROP_FPS);
    width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));

    logger->info("Video Manager initialise: " + std::to_string(width) +
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
    logger->info("Affichage demarre");
}

std::pair<bool, cv::Mat> VideoManager::get_frame() {
    cv::Mat frame;
    const bool ret = cap.read(frame);
    if (ret) {
        end_of_stream_.store(false);
        eof_warning_emitted_.store(false);
        return {true, std::move(frame)};
    }

    if (is_file_source) {
        if (file_loop_enabled_ && rewind_file_source()) {
            if (!eof_warning_emitted_.exchange(true)) {
                logger->warning("Fin de source video fichier - repositionnement au debut du replay");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            cv::Mat replay_frame;
            const bool replay_ret = cap.read(replay_frame);
            if (replay_ret && !replay_frame.empty()) {
                end_of_stream_.store(false);
                eof_warning_emitted_.store(false);
                return {true, std::move(replay_frame)};
            }
            logger->error("Relecture du replay video impossible apres repositionnement");
        }

        end_of_stream_.store(true);
        if (!eof_warning_emitted_.exchange(true)) {
            logger->warning("Fin de source video fichier - aucun bouclage automatique autorise");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return {false, cv::Mat{}};
}

bool VideoManager::rewind_file_source() {
    if (!is_file_source) {
        return false;
    }

    if (!cap.set(cv::CAP_PROP_POS_FRAMES, 0)) {
        logger->warning("Echec repositionnement CAP_PROP_POS_FRAMES=0 - tentative de reouverture");
        cap.release();
        if (!cap.open(video_path_)) {
            logger->error("Impossible de rouvrir la source video: " + video_path_);
            return false;
        }
    }

    ++loop_count;
    logger->info("Replay video boucle - iteration #" + std::to_string(loop_count));
    return true;
}

void VideoManager::send_to_yolo(int frame_id, const cv::Mat& frame) {
    (void)frame_id;
    (void)frame;
}

void VideoManager::send_to_midas(int frame_id, const cv::Mat& frame) {
    (void)frame_id;
    (void)frame;
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
    logger->info("Thread d'affichage demarre (mode transmission uniquement)");

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
            if (processed % 30 == 0) {
                logger->info("Frame " + std::to_string(frame_id) + " traitee (transmission uniquement)");
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
    logger->info("Video Manager nettoye");
}

} // namespace hesia
