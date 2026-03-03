#!/usr/bin/env bash
set -euo pipefail

# pi-static-link.sh (Bookworm / Pi 5)
# Creates a persistent, no-internet Wi-Fi link:
#  - "ap" role: Pi #1 becomes a WPA2 AP with STATIC IP 192.168.50.1/24 (no DHCP)
#  - "client" role: Pi #2 joins that AP with STATIC IP 192.168.50.2/24
#  - SSH enabled on both
#
# IMPORTANT: With no DHCP, your laptop must use a manual IP like 192.168.50.10/24
# (or use the DHCP variant later).

# ========= USER CONFIG (override via env vars) =========
SSID="${SSID:-Pi-Network}"
PSK="${PSK:-StrongPasswordHere}"
IFACE="${IFACE:-wlan0}"

AP_IP_CIDR="${AP_IP_CIDR:-192.168.50.1/24}"
CLIENT_IP_CIDR="${CLIENT_IP_CIDR:-192.168.50.2/24}"
AP_GW="${AP_GW:-192.168.50.1}"

AP_CON="${AP_CON:-pi-ap}"
CLIENT_CON="${CLIENT_CON:-pi-client}"
# =======================================================

die() { echo "ERROR: $*" >&2; exit 1; }

need_root() {
  [[ "${EUID}" -eq 0 ]] || die "Run as root: sudo $0 ap|client"
}

have_cmd() { command -v "$1" >/dev/null 2>&1; }

install_pkgs() {
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  apt-get install -y network-manager openssh-server
  systemctl enable --now NetworkManager
  systemctl enable --now ssh
}

wait_nm() {
  for _ in {1..30}; do
    if nmcli -t -f RUNNING general status 2>/dev/null | grep -qx running; then
      return 0
    fi
    sleep 0.5
  done
  die "NetworkManager is not running."
}

disable_wifi_powersave() {
  # Helps Wi-Fi stability on some setups
  mkdir -p /etc/NetworkManager/conf.d
  cat >/etc/NetworkManager/conf.d/wifi-powersave.conf <<'EOF'
[connection]
wifi.powersave = 2
EOF
  systemctl restart NetworkManager
  wait_nm
}

validate_config() {
  [[ -n "$SSID" ]] || die "SSID is empty"
  [[ -n "$PSK" ]] || die "PSK is empty"
  [[ ${#PSK} -ge 8 ]] || die "PSK must be at least 8 characters"
  [[ -n "$IFACE" ]] || die "IFACE is empty"
  [[ -n "$AP_IP_CIDR" ]] || die "AP_IP_CIDR is empty (e.g. 192.168.50.1/24)"
  [[ -n "$CLIENT_IP_CIDR" ]] || die "CLIENT_IP_CIDR is empty (e.g. 192.168.50.2/24)"
  [[ -n "$AP_GW" ]] || die "AP_GW is empty (e.g. 192.168.50.1)"
}

ensure_iface_exists() {
  nmcli -t -f DEVICE device status | cut -d: -f1 | grep -Fxq "$IFACE" || {
    echo "Known devices:"
    nmcli device status || true
    die "Interface '$IFACE' not found."
  }
}

delete_con_if_exists() {
  local name="$1"
  nmcli -t -f NAME con show | grep -Fxq "$name" && nmcli con delete "$name" >/dev/null
}

configure_ap() {
  echo "==> Configuring AP on $IFACE (static $AP_IP_CIDR) ..."
  delete_con_if_exists "$AP_CON"

  # Create connection with address + manual method IN ONE STEP (prevents the 'manual requires address' error)
  nmcli con add type wifi ifname "$IFACE" con-name "$AP_CON" ssid "$SSID" \
    ipv4.method manual ipv4.addresses "$AP_IP_CIDR" ipv6.method ignore connection.autoconnect yes

  # AP mode + security
  nmcli con modify "$AP_CON" 802-11-wireless.mode ap
  nmcli con modify "$AP_CON" 802-11-wireless.band bg
  nmcli con modify "$AP_CON" 802-11-wireless.channel 6
  nmcli con modify "$AP_CON" wifi-sec.key-mgmt wpa-psk
  nmcli con modify "$AP_CON" wifi-sec.psk "$PSK"

  # Ensure no gateway/DNS set (pure local)
  nmcli con modify "$AP_CON" ipv4.gateway ""
  nmcli con modify "$AP_CON" ipv4.dns ""
  nmcli con modify "$AP_CON" ipv4.never-default yes

  nmcli con up "$AP_CON"

  echo ""
  echo "✅ AP READY"
  echo "  SSID: $SSID"
  echo "  IP:   ${AP_IP_CIDR%/*}"
  echo "  CON:  $AP_CON"
  echo ""
  echo "Next:"
  echo "  - On Pi #2: sudo SSID='$SSID' PSK='$PSK' $0 client"
  echo "  - On laptop: connect to '$SSID' and set manual IP 192.168.50.10/24"
  echo "SSH:"
  echo "  ssh pi@${AP_IP_CIDR%/*}"
  echo ""
}

configure_client() {
  echo "==> Configuring CLIENT on $IFACE (static $CLIENT_IP_CIDR) ..."
  delete_con_if_exists "$CLIENT_CON"

  nmcli dev wifi rescan || true

  # Create the client profile explicitly (more reliable than renaming auto-created profiles)
  nmcli con add type wifi ifname "$IFACE" con-name "$CLIENT_CON" ssid "$SSID" \
    wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PSK" \
    ipv4.method manual ipv4.addresses "$CLIENT_IP_CIDR" ipv4.gateway "$AP_GW" \
    ipv4.dns "" ipv4.never-default yes ipv6.method ignore connection.autoconnect yes

  nmcli con up "$CLIENT_CON"

  echo ""
  echo "✅ CLIENT READY"
  echo "  IP:  ${CLIENT_IP_CIDR%/*}"
  echo "  CON: $CLIENT_CON"
  echo ""
  echo "SSH (from laptop while connected to '$SSID'):"
  echo "  ssh pi@${CLIENT_IP_CIDR%/*}"
  echo ""
}

usage() {
  cat <<EOF
Usage:
  sudo $0 ap
  sudo $0 client

Optional env overrides:
  SSID=Pi-Network
  PSK=StrongPasswordHere
  IFACE=wlan0
  AP_IP_CIDR=192.168.50.1/24
  CLIENT_IP_CIDR=192.168.50.2/24
  AP_GW=192.168.50.1
EOF
  exit 1
}

main() {
  need_root
  validate_config
  install_pkgs
  wait_nm
  disable_wifi_powersave
  ensure_iface_exists

  case "${1:-}" in
    ap) configure_ap ;;
    client) configure_client ;;
    *) usage ;;
  esac
}

main "$@"