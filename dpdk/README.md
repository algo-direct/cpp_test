DPDK multicast receive example

This folder contains a minimal DPDK example (`dpdk_recv`) which initializes the EAL, starts the first available port in promiscuous mode and prints a simple message when it receives IPv4/UDP packets destined to 226.0.0.100:40000.

Building

This project integrates the example into the top-level `CMakeLists.txt` under the option `BUILD_DPDK_EXAMPLE`.

From the project root:

```bash
# configure and enable the example (use -DDPDK_ROOT if DPDK is installed in a non-standard location)
cmake -S . -B build -DBUILD_DPDK_EXAMPLE=ON -DDPDK_ROOT=/home/ashish/git/dpdk-25.03
cmake --build build -j
```

Note: Building requires DPDK and its `pkg-config` entry (libdpdk) to be available. If your DPDK installation doesn't register a pkg-config file, pass `-DDPDK_ROOT=` pointing at the DPDK install root (the script will try to use that to find headers and libraries). Alternatively you can set PKG_CONFIG_PATH to include the DPDK pkgconfig directory:

```bash
export PKG_CONFIG_PATH=/home/ashish/git/dpdk-25.03/lib64/pkgconfig:$PKG_CONFIG_PATH
```

Running

DPDK apps usually need root privileges, hugepages mounted, and NICs bound to a DPDK-compatible driver (e.g., vfio-pci, uio_pci_generic). Example run (adjust EAL args for your environment):

```bash
# example: use 1 core, no hugepage setup in this doc (user must prepare hugepages)
sudo ./build/dpdk_recv -- -l 0
```

If you haven't bound your NIC to a DPDK driver, you can still try running with `--vdev=net_pcap0,iface=eth0` (pcap PMD) for testing but performance will differ and pcap must be available.

- If you don't have a DPDK driver bound NIC for testing, you can still try running with `--vdev=net_pcap0,iface=eth0` (pcap PMD) for testing but performance will differ and pcap must be available.
- A small helper script `dpdk/bind_nic.sh` is provided to bind a NIC to a DPDK driver using DPDK's `dpdk-devbind.py` script. Example:

```bash
# bind PCI device 0000:02:00.0 to vfio-pci (requires sudo)
./dpdk/bind_nic.sh 0000:02:00.0 vfio-pci

# or bind by interface name (eth0 -> pci address will be resolved)
./dpdk/bind_nic.sh eth0 vfio-pci
```

The script will try to locate `dpdk-devbind.py` under the DPDK root (for example `/home/ashish/git/dpdk-25.03/usertools/dpdk-devbind.py`) or common system locations.
Notes

- The example enables promiscuous mode so multicast packets will be received even if the NIC is not explicitly configured for the multicast MAC.
- This code is intentionally minimal and focuses on packet receive and simple parsing; in production you should add proper error handling, port selection, multi-queue support, and NUMA-aware mempool placement.
- To receive real multicast traffic, ensure the sender is transmitting to 226.0.0.100:40000 and the NIC/network path allows multicast.

Troubleshooting

- "libdpdk not found": set PKG_CONFIG_PATH to include your DPDK pkgconfig directory, for example:

```bash
export PKG_CONFIG_PATH=/opt/dpdk/lib64/pkgconfig:$PKG_CONFIG_PATH
```

- Hugepages / driver binding: follow the DPDK Quick Start for your version to reserve hugepages and bind NICs.
