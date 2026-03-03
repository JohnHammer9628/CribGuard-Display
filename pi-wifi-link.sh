#!/usr/bin/env bash
set -euo pipefail

# pi-static-link-robust.sh
# Robust configuration for:
#  - "ap" role: static AP 192.168.50.1/24 (no DHCP)
#  - "client" role: static client 192.168.50.2/24
#
# Usage:
#   sudo SSID=Pi-Network PSK='MyPassw0rd' ./pi-static-link-robust.sh ap
#   sudo SSID=Pi-Network PSK='MyPassw0rd' ./pi-static-link-robust.sh client

# ---------------- User-configurable (override via env) ----------------
SSID="${SSID:-Pi-Network}"
PSK="${PSK:-StrongPasswordHere}"
# IFACE can be overridden; if empty script will detect the wifi iface
IFACE="${IFACE:-}"
AP_IP_CIDR="${AP_IP_CIDR:-192.168.50.1/24}"
CLIENT_IP_CIDR="${CLIENT_IP_CIDR:-192.168.50.2/24}"
AP_GW="${AP_GW:-192.168.50.1}"
AP_CON="${AP_CON:-pi-ap}"
CLIENT_CON="${CLIENT_CON:-pi-client}"
# ---------------------------------------------------------------------

die(){ echo "ERROR: $*" >&2; exit 1; }
info(){ echo "INFO: $*"; }

need_root(){ [[ $EUID -eq 0 ]] || die "Run with sudo"; }

have_cmd(){ command -v "$1" >/dev/null 2>&1; }

install_pkgs(){
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -y
  apt-get install -y network-manager openssh-server
  systemctl enable --now NetworkManager
  systemctl enable --now ssh
}

wait_nm(){
  for i in {1..30}; do
    if nmcli -t -f RUNNING general status 2>/dev/null | grep -qx running; then
      return 0
    fi
    sleep 0.5
  done
  die "NetworkManager failed to start."
}

detect_wifi_iface(){
  if [[ -n "$IFACE" ]]; then
    echo "$IFACE"
    return
  fi
  # prefer nmcli-discovered wifi devices (TYPE=wifi)
  IFACE_DETECTED="$(nmcli -t -f DEVICE,TYPE device status 2>/dev/null | awk -F: '$2=="wifi"{print $1; exit}')"
  if [[ -n "$IFACE_DETECTED" ]]; then
    echo "$IFACE_DETECTED"
    return
  fi
  # fallback to 'wlan0' if present in ip link
  if ip link show wlan0 >/dev/null 2>&1; then
    echo "wlan0"
    return
  fi
  die "Could not detect a Wi-Fi interface. Set IFACE explicitly (e.g. IFACE=wlp1s0)."
}

ensure_iface_up(){
  local iface="$1"
  # unblock if rfkill blocked
  rfkill list wifi 2>/dev/null | grep -qi 'Soft blocked: yes' && rfkill unblock wifi || true
  ip link set "$iface" up || true
  sleep 0.5
}

delete_con_if_exists(){
  local name="$1"
  nmcli -t -f NAME con show | grep -Fxq "$name" && nmcli connection delete "$name" >/dev/null 2>&1 || true
}

disable_wifi_powersave(){
  mkdir -p /etc/NetworkManager/conf.d
  cat >/etc/NetworkManager/conf.d/wifi-powersave.conf <<'EOF'
[connection]
wifi.powersave = 2
EOF
  systemctl restart NetworkManager
  wait_nm
}

ensure_ssh_and_firewall(){
  systemctl enable --now ssh || true
  if have_cmd ufw; then
    if ufw status | grep -qi inactive; then
      # do not enable ufw; just allow ssh if ufw is active later
      true
    else
      ufw allow ssh || true
    fi
  fi
}

# Create AP safely (address + method in one step) and disable WPS, ap-isolation
configure_ap(){
  local iface="$1"
  info "Creating AP connection '$AP_CON' on interface $iface with $AP_IP_CIDR"
  delete_con_if_exists "$AP_CON"

  nmcli connection add type wifi ifname "$iface" con-name "$AP_CON" ssid "$SSID" \
    ipv4.method manual ipv4.addresses "$AP_IP_CIDR" ipv6.method ignore connection.autoconnect yes

  nmcli connection modify "$AP_CON" 802-11-wireless.mode ap
  nmcli connection modify "$AP_CON" 802-11-wireless.band bg
  nmcli connection modify "$AP_CON" 802-11-wireless.channel 6

  # security: WPA2-PSK only, disable WPS
  nmcli connection modify "$AP_CON" wifi-sec.key-mgmt wpa-psk
  nmcli connection modify "$AP_CON" wifi-sec.psk "$PSK"

  # ensure pure local network
  nmcli connection modify "$AP_CON" ipv4.gateway ""
  nmcli connection modify "$AP_CON" ipv4.dns ""
  nmcli connection modify "$AP_CON" ipv4.never-default yes

  # explicitly turn off AP isolation (allow client<->client)
  # two possible keys for different NM versions:
  nmcli connection modify "$AP_CON" 802-11-wireless.ap-isolation no 2>/dev/null || true
  nmcli connection modify "$AP_CON" wifi-sec.wps-method disabled 2>/dev/null || true
  nmcli connection modify "$AP_CON" 802-11-wireless-security.wps-method disabled 2>/dev/null || true

  nmcli connection up "$AP_CON" || die "Failed to bring up AP connection '$AP_CON'"

  # verify IP present on iface
  for i in {1..8}; do
    ip -4 addr show dev "$iface" | grep -q "${AP_IP_CIDR%/*}" && break || sleep 0.5
    if [[ $i -eq 8 ]]; then
      die "AP is up but the interface $iface does not have the IP ${AP_IP_CIDR%/*}."
    fi
  done

  info "AP created & active. SSID=$SSID IP=${AP_IP_CIDR%/*}"
}

# Create client profile with static IP and ensure it connects
configure_client(){
  local iface="$1"
  info "Creating client connection '$CLIENT_CON' on interface $iface with $CLIENT_IP_CIDR"
  delete_con_if_exists "$CLIENT_CON"

  # create in one step (address + method), forcing WPA2-PSK
  nmcli device wifi rescan || true
  nmcli connection add type wifi ifname "$iface" con-name "$CLIENT_CON" ssid "$SSID" \
    wifi-sec.key-mgmt wpa-psk wifi-sec.psk "$PSK" \
    ipv4.method manual ipv4.addresses "$CLIENT_IP_CIDR" ipv4.gateway "$AP_GW" \
    ipv4.dns "" ipv4.never-default yes ipv6.method ignore connection.autoconnect yes

  # bring it up and wait for ip
  nmcli connection up "$CLIENT_CON" || true

  # retry activation up to several times (some drivers take a moment)
  local tries=0
  while :; do
    ((tries++))
    # check active on iface
    ACTIVE=$(nmcli -t -f NAME,DEVICE connection show --active | awk -F: -v d="$iface" '$2==d{print $1; exit}')
    ADDR=$(nmcli -g ipv4.addresses connection show "$CLIENT_CON" 2>/dev/null || true)
    if [[ -n "$ACTIVE" && -n "$ADDR" ]]; then
      info "Client connected (CON=$ACTIVE IP=${ADDR%%/*})"
      break
    fi
    if [[ $tries -ge 10 ]]; then
      nmcli -f NAME,DEVICE,STATE connection show --active || true
      ip -4 addr show dev "$iface" || true
      die "Client failed to connect or get IP after multiple attempts. Last ACTIVE='$ACTIVE' ADDR='$ADDR'"
    fi
    sleep 1
    nmcli connection up "$CLIENT_CON" >/dev/null 2>&1 || true
  done
}

main(){
  need_root
  install_pkgs
  wait_nm
  disable_wifi_powersave
  ensure_ssh_and_firewall

  local role="${1:-}"
  [[ "$role" == "ap" || "$role" == "client" ]] || die "Usage: $0 ap|client"

  local iface
  iface="$(detect_wifi_iface)"
  info "Using Wi-Fi interface: $iface"
  ensure_iface_up "$iface"

  if [[ "$role" == "ap" ]]; then
    configure_ap "$iface"
    info "AP ready. Try: ssh pi@${AP_IP_CIDR%/*}"
  else
    configure_client "$iface"
    info "Client ready. Try: ssh pi@${CLIENT_IP_CIDR%/*}"
  fi
}

main "$@"
