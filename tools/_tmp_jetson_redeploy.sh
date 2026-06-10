set -euo pipefail
systemctl stop hesia-drone.service hesia-server.service || true
chattr -i /opt/hesia/bin/hesia_drone /opt/hesia/bin/hesia_server_cpp /lib/optee_armtz/a17de805-9dc1-43ef-932b-91f107cad57b.ta /usr/lib/optee_armtz/a17de805-9dc1-43ef-932b-91f107cad57b.ta 2>/dev/null || true
install -o root -g hesia -m 0750 /home/ajax/.cache/.hesia/build/drone-cloaked-cfi-20260420/hesia_drone /opt/hesia/bin/hesia_drone
install -o root -g hesia -m 0750 /home/ajax/.cache/.hesia/build/server-cloaked-tee-20260420/hesia_server_cpp /opt/hesia/bin/hesia_server_cpp
install -o root -g root -m 0644 /home/ajax/.cache/.hesia/optee/hesia_ta/ta/a17de805-9dc1-43ef-932b-91f107cad57b.ta /lib/optee_armtz/a17de805-9dc1-43ef-932b-91f107cad57b.ta
install -o root -g root -m 0644 /home/ajax/.cache/.hesia/optee/hesia_ta/ta/a17de805-9dc1-43ef-932b-91f107cad57b.ta /usr/lib/optee_armtz/a17de805-9dc1-43ef-932b-91f107cad57b.ta
install -o root -g hesia -m 0750 /home/ajax/.cache/.hesia/work/runtime_20260420/server_source/tools/ui_server.py /opt/hesia/bin/hesia_ui_server.py
patchelf --replace-needed /home/ajax/.cache/.hesia/work/runtime_20260420/drone_source/Sentinel/lib/libhesia_sentinel.so libhesia_sentinel.so /opt/hesia/bin/hesia_drone || true
patchelf --set-rpath '$ORIGIN/../lib:/usr/local/cuda/lib64' /opt/hesia/bin/hesia_drone
bash /home/ajax/.cache/.hesia/work/runtime_20260420/tools/refresh_firmware_allowlist.sh /opt/hesia/bin/hesia_drone /etc/hesia/secure /etc/hesia/secure/allowlist_signing.key
chattr +i /opt/hesia/bin/hesia_drone /opt/hesia/bin/hesia_server_cpp 2>/dev/null || true
systemctl start hesia-server.service hesia-drone.service
sleep 10