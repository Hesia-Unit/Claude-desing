# Arborescence

## Racine
- drone_source/
- server_source/
- drone_transition_source/
- README_00_OVERVIEW.md
- README_01_ARBORESCENCE.md
- README_02_FONCTIONNEMENT.md
- README_03_UTILITE_PAR_PARTIE.md

## drone_source
- CMakeLists.txt
- main.cpp, hesia_drone.cpp/hpp
- drone_network.cpp/hpp, tls_channel.cpp/hpp, secure_channel.cpp/hpp
- serialization.cpp/hpp, protocole.hpp
- clean_pipeline.cpp/hpp, video_manager.cpp/hpp, video_channel.cpp/hpp
- yolo_processor.cpp/hpp, midas_processor.cpp/hpp
- system_security.cpp/hpp, runtime_aslr.cpp/hpp, stack_protection.cpp/hpp
- cfi_protection.cpp/hpp, sandbox_linux.cpp, seccomp_bpf.cpp/hpp
- optee_client.cpp/hpp, sentinel_bridge.cpp/hpp
- Sentinel/ (module Ada)
- tools/ (outils)

## server_source
- CMakeLists.txt
- include/ (headers)
- src/ (implementation)
- ui/ (front statique)
- tools/ (scripts)
- liboqs/ (sources PQC vendorees)

## drone_transition_source
- allowlist.conf, allowlist.sig, allowlist_pub.pem
- scripts/ (prepare/sync Jetson)
- optee_ta_skeleton/ (host + TA)
- AIR/IMPLEMENTATION/Jetson/GPT/drone/tools/hesia-validate.sh
