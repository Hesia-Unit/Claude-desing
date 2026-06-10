# Fonctionnement

## Drone (C++)
Chemin: drone_source

Build Jetson (clang + CFI):
1. cmake -S <drone_source> -B <build_dir> -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DHESIA_ENABLE_CFI=ON -DHESIA_ENABLE_LTO=ON
2. cmake --build <build_dir> -j"$(nproc)"

Run:
- service: sudo systemctl restart hesia-drone.service
- direct: sudo <build_dir>/hesia_drone

## Serveur (C++)
Chemin: server_source

Build:
1. cmake -S <server_source> -B <build_dir> -DCMAKE_BUILD_TYPE=Release
2. cmake --build <build_dir> -j"$(nproc)"

Run:
- lancer le binaire serveur depuis <build_dir>
- UI dans server_source/ui

## Transition Jetson / OP-TEE
Chemin: drone_transition_source

- scripts/prepare_jetson.sh: preparation machine
- scripts/sync_to_jetson.sh: synchronisation
- optee_ta_skeleton/: base TA/host OP-TEE

## Verification rapide
- verifier les services systemd
- verifier les chemins relatifs (WorkingDirectory)
- verifier les droits policy/secure
