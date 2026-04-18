#include "pipeline.hpp"
#include "drone_network.hpp"

namespace hesia {

void HighPerformancePipeline::start() {
    if (running.load()) {
        logger->warning("Pipeline déjà démarré");
        return;
    }
    
    running.store(true);
    frame_counter.store(0);
    start_time = std::chrono::steady_clock::now();
    
    // Démarrer les threads
    capture_thread = std::thread(&HighPerformancePipeline::capture_worker, this);
    yolo_thread = std::thread(&HighPerformancePipeline::yolo_worker, this);
    send_thread = std::thread(&HighPerformancePipeline::send_worker, this);
    
    logger->info("Pipeline démarré - 3 threads actifs");
}

void HighPerformancePipeline::stop() {
    if (!running.load()) return;
    
    running.store(false);
    
    // Notifier tous les threads
    cv.notify_all();
    
    // Attendre la fin des threads
    if (capture_thread.joinable()) capture_thread.join();
    if (yolo_thread.joinable()) yolo_thread.join();
    if (send_thread.joinable()) send_thread.join();
    
    logger->info("Pipeline arrêté");
    log_stats();
}

HighPerformancePipeline::PipelineStats HighPerformancePipeline::get_stats() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
    
    return {
        frames_captured.load(),
        frames_processed.load(),
        frames_sent.load(),
        frames_dropped.load(),
        elapsed > 0 ? frames_captured.load() / static_cast<double>(elapsed) : 0.0,
        elapsed > 0 ? frames_processed.load() / static_cast<double>(elapsed) : 0.0,
        elapsed > 0 ? frames_sent.load() / static_cast<double>(elapsed) : 0.0,
        capture_queue.size(),
        yolo_queue.size(),
        send_queue.size()
    };
}

void HighPerformancePipeline::log_stats() {
    auto stats = get_stats();
    logger->info("=== PIPELINE STATS ===");
    logger->info("Frames captured: " + std::to_string(stats.captured));
    logger->info("Frames processed: " + std::to_string(stats.processed));
    logger->info("Frames sent: " + std::to_string(stats.sent));
    logger->info("Frames dropped: " + std::to_string(stats.dropped));
    logger->info("Capture FPS: " + std::to_string(stats.capture_fps));
    logger->info("Processing FPS: " + std::to_string(stats.processing_fps));
    logger->info("Send FPS: " + std::to_string(stats.send_fps));
    logger->info("====================");
}

void HighPerformancePipeline::capture_worker() {
    logger->info("Capture thread démarré");
    
    while (running.load()) {
        try {
            // Capturer frame
            auto [ret, frame] = video_manager->get_frame();
            if (!ret) {
                logger->info("Fin de la vidéo");
                break;
            }
            
            // Créer frame de pipeline
            int frame_id = frame_counter.fetch_add(1);
            auto pipeline_frame = std::make_shared<PipelineFrame>(frame_id);
            
            // Redimensionner vers 480x270 (réduction 75%)
            cv::resize(frame, pipeline_frame->image, cv::Size(480, 270), 0, 0, cv::INTER_LINEAR);
            
            // Mettre dans queue YOLO
            if (!yolo_queue.push(pipeline_frame, 10)) {
                frames_dropped.fetch_add(1);
                logger->warning("Queue YOLO pleine - frame " + std::to_string(frame_id) + " dropped");
                continue;
            }
            
            frames_captured.fetch_add(1);
            
            // Log toutes les 30 frames
            if (frame_id % 30 == 0) {
                auto stats = get_stats();
                logger->info("[PIPELINE] Captured: " + std::to_string(stats.captured) + 
                            " FPS: " + std::to_string(stats.capture_fps));
            }
            
        } catch (const std::exception& e) {
            logger->error("Capture worker error: " + std::string(e.what()));
        }
    }
}

void HighPerformancePipeline::yolo_worker() {
    logger->info("YOLO thread démarré");
    
    while (running.load()) {
        try {
            std::shared_ptr<PipelineFrame> frame;
            if (!yolo_queue.pop(frame, 50)) {
                continue; // Timeout normal
            }
            
            // Traitement YOLO (async)
            auto yolo_future = yolo_processor->process_async(frame->frame_id, frame->image);
            
            // Attendre résultat avec timeout court
            auto status = yolo_future.wait_for(std::chrono::milliseconds(200));
            if (status == std::future_status::ready) {
                auto [yolo_processed, detections] = yolo_future.get();
                frame->yolo_output = yolo_processed.clone();
                frame->yolo_detections = detections;
                frame->yolo_processed = true;
            } else {
                // Timeout - utiliser frame originale
                frame->yolo_output = frame->image.clone();
                frame->yolo_processed = true;
                logger->debug("YOLO timeout pour frame " + std::to_string(frame->frame_id));
            }
            
            // Mettre dans queue send
            if (!send_queue.push(frame, 10)) {
                frames_dropped.fetch_add(1);
                logger->warning("Queue send pleine - frame " + std::to_string(frame->frame_id) + " dropped");
                continue;
            }
            
            frames_processed.fetch_add(1);
            
        } catch (const std::exception& e) {
            logger->error("YOLO worker error: " + std::string(e.what()));
        }
    }
}

void HighPerformancePipeline::send_worker() {
    logger->info("Send thread démarré");
    
    while (running.load()) {
        try {
            std::shared_ptr<PipelineFrame> frame;
            if (!send_queue.pop(frame, 50)) {
                continue; // Timeout normal
            }
            
            // Envoyer frame
            video_manager->receive_yolo_result(frame->frame_id, frame->yolo_output, frame->yolo_detections);
            video_manager->receive_midas_result(frame->frame_id, frame->midas_output);
            
            // Envoyer via réseau (asynchrone)
            if (drone_client && drone_client->get_drone() && drone_client->get_drone()->get_video_channel()) {
                drone_client->send_video_frame_async(frame->yolo_output, frame->midas_output, frame->frame_id);
            }
            
            frames_sent.fetch_add(1);
            
        } catch (const std::exception& e) {
            logger->error("Send worker error: " + std::string(e.what()));
        }
    }
}

} // namespace hesia
