// config.hpp - FICHIER CORRIGÉ
#pragma once
#include <string>
#include <filesystem>
#include <iostream>
#include <cstdlib>

namespace hesia {
    struct Config {
        // Constantes de chemin (déclarations)
        static std::string LOG_DIR;
        static std::filesystem::path LOG_PATH;
        
        // Chemins des modèles TensorRT (.engine)
        static inline std::filesystem::path YOLO_ENGINE = "MODEL/ENGINE/yolov8n.engine";
        static inline std::filesystem::path MIDAS_ENGINE = "MODEL/ENGINE/midas_v21_small_256.engine";
        
        // Chemins des répertoires
        static inline std::filesystem::path BASE_DIR = ".";
        static inline std::filesystem::path BB_DIR = BASE_DIR / "blackbox";
        
        // Vidéo
        static inline std::string VIDEO_PATH = "videos/DRONE2.mp4";
        
        // Configuration threads
        static inline bool PIN_IA_THREADS = true;
        
        // Fonction d'initialisation dans la classe Config
        static void init() {
            const char* base_env = std::getenv("HESIA_BASE_DIR");
            if (base_env && base_env[0] != '\0') {
                BASE_DIR = base_env;
            } else {
                try {
                    BASE_DIR = std::filesystem::current_path();
                } catch (...) {
                    BASE_DIR = ".";
                }
            }

            if (YOLO_ENGINE.is_relative()) {
                YOLO_ENGINE = BASE_DIR / YOLO_ENGINE;
            }
            if (MIDAS_ENGINE.is_relative()) {
                MIDAS_ENGINE = BASE_DIR / MIDAS_ENGINE;
            }
            BB_DIR = BASE_DIR / "blackbox";

            try {
                std::filesystem::create_directories(LOG_DIR);
                std::filesystem::create_directories(BB_DIR);
                std::filesystem::create_directories(BASE_DIR / "MODEL");
                std::filesystem::create_directories(BASE_DIR / "videos");
                std::cout << "📁 Répertoires initialisés avec succès" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "❌ Erreur initialisation répertoires: " << e.what() << std::endl;
            }
        }
    };
    
    // Fonction d'initialisation globale (alternative)
    inline void initialize_system() {
        Config::init();
    }
} // namespace hesia
