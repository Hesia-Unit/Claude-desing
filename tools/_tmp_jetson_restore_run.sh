set -euo pipefail
systemctl stop hesia-drone.service hesia-server.service || true
chattr -i /opt/hesia/bin/hesia_drone /opt/hesia/bin/hesia_server_cpp 2>/dev/null || true
install -o root -g hesia -m 0750 /home/ajax/.cache/.hesia/build/drone-cloaked-cfi-20260420/hesia_drone /opt/hesia/bin/hesia_drone
install -o root -g hesia -m 0750 /home/ajax/.cache/.hesia/build/server-cloaked-tee-20260420/hesia_server_cpp /opt/hesia/bin/hesia_server_cpp
patchelf --replace-needed /home/ajax/.cache/.hesia/work/runtime_20260420/drone_source/Sentinel/lib/libhesia_sentinel.so libhesia_sentinel.so /opt/hesia/bin/hesia_drone || true
patchelf --set-rpath '$ORIGIN/../lib:/usr/local/cuda/lib64' /opt/hesia/bin/hesia_drone
install -o root -g root -m 0644 /tmp/allowlist.conf /etc/hesia/sentinel/allowlist.conf
install -o root -g root -m 0644 /tmp/allowlist.conf.sig /etc/hesia/sentinel/allowlist.conf.sig
bash /home/ajax/.cache/.hesia/work/runtime_20260420/tools/refresh_firmware_allowlist.sh /opt/hesia/bin/hesia_drone /etc/hesia/secure /etc/hesia/secure/allowlist_signing.key
chattr +i /opt/hesia/bin/hesia_drone /opt/hesia/bin/hesia_server_cpp 2>/dev/null || true
systemctl start hesia-server.service hesia-drone.service
sleep 10