#!/bin/sh
# Schreibt Qt-Settings mit der Redirect-IP bevor CaptiveDNS startet.
# DNS_REDIRECT_IP kommt aus der docker-compose Umgebungsvariable.

CONFIG_DIR="$HOME/.config/BrowseDNS"
mkdir -p "$CONFIG_DIR"

cat > "$CONFIG_DIR/CaptiveDNS.conf" << EOF
[General]
redirectIp=${DNS_REDIRECT_IP}
captiveIp=${DNS_REDIRECT_IP}
ip=${DNS_REDIRECT_IP}
EOF

echo "[captive-dns] Redirect-IP = ${DNS_REDIRECT_IP}"
echo "[captive-dns] Alle DNS-Anfragen → ${DNS_REDIRECT_IP} (Frontend auf Port 80)"
exec /usr/local/bin/CaptiveDNS
