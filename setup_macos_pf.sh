#!/usr/bin/env bash
# =============================================================================
# setup_macos_pf.sh — Configure macOS pf to redirect TCP traffic to the proxy.
#
# Usage:
#   sudo ./setup_macos_pf.sh enable  [PROXY_PORT] [TARGET_PORT]
#   sudo ./setup_macos_pf.sh disable
#
# Defaults: PROXY_PORT=8080  TARGET_PORT=80
# To test: curl -v http://httpbin.org/get  (with proxy running on PROXY_PORT)
# To edit just this pf rules: edit the isolated anchor file and run
# sudo pfctl -a tcp_proxy -f /etc/pf.anchors/tcp_proxy
# =============================================================================

set -euo pipefail

PROXY_PORT="${2:-8080}"
TARGET_PORT="${3:-80}"
PF_CONF="/etc/pf.conf"
PF_ANCHOR="tcp_proxy"
ANCHOR_FILE="/etc/pf.anchors/${PF_ANCHOR}"

if [[ $EUID -ne 0 ]]; then
    echo "❌  This script must be run as root (sudo)."
    exit 1
fi

enable_pf() {
    echo "→ Setting up pf anchor for TCP proxy..."
    echo "  PROXY_PORT : ${PROXY_PORT}"
    echo "  TARGET_PORT: ${TARGET_PORT}"

    # Create anchor directory if needed
    mkdir -p /etc/pf.anchors

    # Write anchor rules
    cat > "${ANCHOR_FILE}" <<EOF
# tcp_proxy anchor — redirect TARGET_PORT → PROXY_PORT on loopback
rdr pass on lo0 proto tcp from any to any port ${TARGET_PORT} -> 127.0.0.1 port ${PROXY_PORT}
EOF

    # Backup pf.conf if not already backed up
    if [[ ! -f "${PF_CONF}.tcp_proxy_backup" ]]; then
        cp "${PF_CONF}" "${PF_CONF}.tcp_proxy_backup"
        echo "→ Backed up pf.conf to ${PF_CONF}.tcp_proxy_backup"
    fi

    # Safely inject the rules in the exact required order using awk
    if ! grep -q "${PF_ANCHOR}" "${PF_CONF}"; then
        echo "→ Injecting anchor references into ${PF_CONF} in the correct order..."
        
        awk -v anchor="${PF_ANCHOR}" -v anchor_file="${ANCHOR_FILE}" '
        { print }
        $0 == "rdr-anchor \"com.apple/*\"" {
            print "rdr-anchor \"" anchor "\""
        }
        $0 == "anchor \"com.apple/*\"" {
            print "anchor \"" anchor "\""
        }
        $0 == "load anchor \"com.apple\" from \"/etc/pf.anchors/com.apple\"" {
            print "load anchor \"" anchor "\" from \"" anchor_file "\""
        }' "${PF_CONF}.tcp_proxy_backup" > "${PF_CONF}.tmp"
        
        mv "${PF_CONF}.tmp" "${PF_CONF}"
        echo "→ Successfully updated ${PF_CONF}"
    fi

    # Enable pf
    pfctl -e 2>/dev/null || true
    
    # Reload full ruleset and catch syntax errors safely
    echo "→ Loading pf rules..."
    if ! pfctl -f "${PF_CONF}"; then
        echo "❌ Syntax error detected! Restoring backup pf.conf to prevent network lockout..."
        cp "${PF_CONF}.tcp_proxy_backup" "${PF_CONF}"
        exit 1
    fi

    echo ""
    echo "✅  pf is now active. Rules in memory:"
    pfctl -s nat 2>/dev/null | grep -i "${PF_ANCHOR}" || pfctl -a "${PF_ANCHOR}" -s nat 2>/dev/null || true

    echo ""
    echo "Start the proxy:"
    echo "  ./tcp_proxy --mode trans --listen-port ${PROXY_PORT} --log-level debug"
    echo ""
    echo "Test (in another terminal):"
    echo "  curl -v http://httpbin.org/get"
    echo ""
    echo "Disable when done:"
    echo "  sudo ./setup_macos_pf.sh disable"
}

disable_pf() {
    echo "→ Removing tcp_proxy pf rules..."

    # Restore backup pf.conf
    if [[ -f "${PF_CONF}.tcp_proxy_backup" ]]; then
        cp "${PF_CONF}.tcp_proxy_backup" "${PF_CONF}"
        echo "→ Restored ${PF_CONF} from backup"
    fi

    # Remove anchor file
    rm -f "${ANCHOR_FILE}"

    # Reload pf (will flush our anchor safely)
    pfctl -f "${PF_CONF}" 2>/dev/null || true

    echo "✅  pf tcp_proxy rules removed."
}

case "${1:-}" in
    enable)  enable_pf  ;;
    disable) disable_pf ;;
    *)
        echo "Usage: sudo $0 enable [PROXY_PORT] [TARGET_PORT]"
        echo "       sudo $0 disable"
        exit 1
        ;;
esac
