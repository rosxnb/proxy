#!/usr/bin/env bash
# =============================================================================
# setup_macos_pf.sh — Configure macOS pf to redirect TCP traffic to the proxy.
#
# Usage:
#   sudo ./setup_macos_pf.sh enable  [PROXY_PORT] [TARGET_PORT]
#   sudo ./setup_macos_pf.sh disable
#
# Defaults: PROXY_PORT=8080  TARGET_PORT=80
#
# What this does:
#   1. Saves your existing pf.conf as a backup.
#   2. Adds a rdr-to rule that redirects TCP on TARGET_PORT to the proxy.
#   3. Enables pf.
#
# To test: curl -v http://httpbin.org/get  (with proxy running on PROXY_PORT)
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
#
# rdr-to on lo0: intercept connections on the loopback interface.
# For traffic from other machines, replace lo0 with your interface (e.g. en0).
rdr pass on lo0 proto tcp from any to any port ${TARGET_PORT} -> 127.0.0.1 port ${PROXY_PORT}
EOF

    # Backup pf.conf if not already backed up
    if [[ ! -f "${PF_CONF}.tcp_proxy_backup" ]]; then
        cp "${PF_CONF}" "${PF_CONF}.tcp_proxy_backup"
        echo "→ Backed up pf.conf to ${PF_CONF}.tcp_proxy_backup"
    fi

    # Add anchor reference to pf.conf if not already there
    if ! grep -q "tcp_proxy" "${PF_CONF}"; then
        cat >> "${PF_CONF}" <<EOF

# Added by setup_macos_pf.sh
rdr-anchor "${PF_ANCHOR}"
anchor "${PF_ANCHOR}"
load anchor "${PF_ANCHOR}" from "${ANCHOR_FILE}"
EOF
        echo "→ Added anchor reference to ${PF_CONF}"
    fi

    # Enable pf and load rules
    pfctl -e 2>/dev/null || true          # enable (ignore if already enabled)
    pfctl -f "${PF_CONF}"                  # reload full ruleset

    echo ""
    echo "✅  pf is now active. Rules:"
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

    # Reload pf (will flush our anchor)
    pfctl -f "${PF_CONF}" 2>/dev/null || true

    # Optionally disable pf entirely (commented out — other apps may use it)
    # pfctl -d

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
