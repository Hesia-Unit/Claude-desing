set -euo pipefail
cat > /etc/hesia/sentinel/allowlist.conf <<'EOF'
cpu_hash=b108296a567431c4251abd68e508384fadf8a86ea1edd3c490d2d62775d30322
storage_hash=85931d531d1e512133f66c8d3132401c17f456aae1f79f000b4ba4ea4c154870
modules_required=
EOF
openssl pkeyutl -sign -rawin -inkey /etc/hesia/secure/allowlist_signing.key -in /etc/hesia/sentinel/allowlist.conf | base64 -w0 > /etc/hesia/sentinel/allowlist.conf.sig
printf '\n' >> /etc/hesia/sentinel/allowlist.conf.sig
chown root:root /etc/hesia/sentinel/allowlist.conf /etc/hesia/sentinel/allowlist.conf.sig
chmod 0644 /etc/hesia/sentinel/allowlist.conf /etc/hesia/sentinel/allowlist.conf.sig
systemctl restart hesia-drone.service
sleep 8