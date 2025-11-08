#!/usr/bin/env bash
# Helper script to bind a NIC to a DPDK driver using dpdk-devbind.py
# Usage: ./bind_nic.sh <PCI_ADDR|IFACE> <driver>
# Example: ./bind_nic.sh 0000:02:00.0 vfio-pci
#          ./bind_nic.sh eth0 vfio-pci

set -euo pipefail

if [ "$#" -lt 2 ]; then
  echo "Usage: $0 <PCI_ADDR|IFACE> <DRIVER>"
  echo "Example: $0 0000:02:00.0 vfio-pci or $0 eth0 vfio-pci"
  exit 2
fi

TARGET="$1"
DRIVER="$2"
DPDK_ROOT_DEFAULT="/home/ashish/git/dpdk-25.03"
DPDK_ROOT="${DPDK_ROOT:-$DPDK_ROOT_DEFAULT}"

# Try to find dpdk-devbind.py in common places
CANDIDATES=(
  "$DPDK_ROOT/usertools/dpdk-devbind.py"
  "$DPDK_ROOT/tools/dpdk-devbind.py"
  "/usr/local/bin/dpdk-devbind.py"
  "/usr/bin/dpdk-devbind.py"
)

DPDK_DEVBIND=""
for p in "${CANDIDATES[@]}"; do
  if [ -x "$p" ]; then
    DPDK_DEVBIND="$p"
    break
  fi
done

if [ -z "$DPDK_DEVBIND" ]; then
  echo "dpdk-devbind.py not found. Please set DPDK_ROOT to your DPDK install root or install dpdk-devbind.py in PATH." >&2
  exit 1
fi

echo "Using dpdk-devbind: $DPDK_DEVBIND"

# If the target looks like an interface name, map to its PCI address using ethtool or ip
if [[ "$TARGET" =~ ^[a-zA-Z] ]]; then
  # try to find PCI address via ethtool -i
  if command -v ethtool >/dev/null 2>&1; then
    PCI_ADDR=$(ethtool -i "$TARGET" 2>/dev/null | awk -F': ' '/bus-info/ {print $2}') || true
  fi
  if [ -z "${PCI_ADDR:-}" ]; then
    # fallback: use ip link to find ifindex, then sysfs
    if [ -d "/sys/class/net/$TARGET/device" ]; then
      PCI_ADDR=$(readlink -f "/sys/class/net/$TARGET/device" | awk -F'/' '{print $(NF)}')
    fi
  fi
  if [ -z "${PCI_ADDR:-}" ]; then
    echo "Unable to determine PCI address for interface $TARGET" >&2
    exit 1
  fi
  TARGET="$PCI_ADDR"
fi

# Bind
echo "Binding $TARGET to driver $DRIVER"
sudo "$DPDK_DEVBIND" -b "$DRIVER" "$TARGET"

echo "Done."
