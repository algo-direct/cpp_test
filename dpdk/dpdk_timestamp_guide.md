# DPDK Packet Timestamping Guide

## Methods for Timestamping Incoming Packets in DPDK

### 1. **Software Timestamping (Most Common)**
Use CPU clock to timestamp packets immediately after receiving them.

```cpp
#include <rte_cycles.h>

// Get TSC (Time Stamp Counter) - highest resolution
uint64_t timestamp = rte_rdtsc();

// Or get TSC with precision cycles
uint64_t precise_timestamp = rte_rdtsc_precise();

// Or use HPET (High Precision Event Timer) if available
uint64_t hpet_timestamp = rte_get_hpet_cycles();
```

**Advantages:**
- Simple, works on all NICs
- Nanosecond precision using TSC
- No hardware dependencies

**Disadvantages:**
- Software overhead (~50-100ns)
- Not synchronized across multiple servers without PTP/NTP

---

### 2. **Hardware Timestamping (NIC-based)**
Use NIC hardware to timestamp packets at wire arrival time.

**Prerequisites:**
- NIC must support hardware timestamping (Intel X710, Mellanox, etc.)
- Enable Rx timestamp offload

```cpp
// Enable hardware timestamping during port configuration
struct rte_eth_conf port_conf;
memset(&port_conf, 0, sizeof(port_conf));
port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;

// Configure port with timestamping
rte_eth_dev_configure(port_id, rx_rings, tx_rings, &port_conf);

// Enable timestamp offload in queue configuration
struct rte_eth_rxconf rx_conf;
memset(&rx_conf, 0, sizeof(rx_conf));
rx_conf.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;
rte_eth_rx_queue_setup(port_id, queue_id, nb_rx_desc, 
                        rte_eth_dev_socket_id(port_id), 
                        &rx_conf, mbuf_pool);
```

**Reading hardware timestamp from mbuf:**
```cpp
// After rte_eth_rx_burst()
struct rte_mbuf *m = bufs[i];

// Check if timestamp is valid
if (m->ol_flags & RTE_MBUF_F_RX_TIMESTAMP) {
    uint64_t hw_timestamp = m->timestamp;
    printf("HW timestamp: %lu\n", hw_timestamp);
}
```

**Advantages:**
- Sub-microsecond precision (~10ns)
- Minimal CPU overhead
- Timestamp at wire arrival (before DMA)

**Disadvantages:**
- NIC-specific, not all NICs support it
- Requires device capability verification

---

### 3. **PTP (Precision Time Protocol) Synchronized Timestamps**
For multi-server synchronization, use IEEE 1588 PTP.

```cpp
// Enable timesync (PTP) on device
struct rte_eth_dev_info dev_info;
rte_eth_dev_info_get(port_id, &dev_info);

if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TIMESTAMP) {
    // Read PTP synchronized clock
    struct timespec ts;
    rte_eth_timesync_read_rx_timestamp(port_id, &ts, 0);
    
    uint64_t ns = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    printf("PTP timestamp: %lu ns\n", ns);
}
```

---

### 4. **Hybrid Approach (Recommended for Low Latency)**

Combine hardware and software timestamps for best results:

```cpp
uint64_t sw_timestamp_rx_start = rte_rdtsc();  // Before rx_burst

const uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);

uint64_t sw_timestamp_rx_end = rte_rdtsc();    // After rx_burst

for (uint16_t i = 0; i < nb_rx; i++) {
    struct rte_mbuf *m = bufs[i];
    
    uint64_t pkt_timestamp;
    
    // Prefer hardware timestamp if available
    if (m->ol_flags & RTE_MBUF_F_RX_TIMESTAMP) {
        pkt_timestamp = m->timestamp;  // NIC timestamp
    } else {
        // Fallback to software timestamp
        pkt_timestamp = sw_timestamp_rx_start;
    }
    
    // Store timestamp in packet metadata or private area
    // Option 1: Use mbuf's udata64
    m->udata64 = pkt_timestamp;
    
    // Option 2: Use mbuf's timestamp field (if not used by HW)
    // m->timestamp = pkt_timestamp;
    
    // Process packet...
}
```

---

## Timestamp Resolution Comparison

| Method | Resolution | Overhead | Cross-Server Sync |
|--------|-----------|----------|-------------------|
| rte_rdtsc() | ~1ns | ~10-30 cycles | No |
| rte_rdtsc_precise() | ~1ns | ~100 cycles | No |
| Hardware (NIC) | ~10ns | 0 cycles | No |
| PTP Synchronized | ~10-100ns | Minimal | Yes |

---

## Converting TSC to Nanoseconds

```cpp
// Get TSC frequency (cycles per second)
uint64_t tsc_hz = rte_get_tsc_hz();

// Convert TSC cycles to nanoseconds
uint64_t tsc_cycles = rte_rdtsc();
uint64_t nanoseconds = (tsc_cycles * 1000000000ULL) / tsc_hz;

// Or use helper function
double ns_per_cycle = 1000000000.0 / (double)tsc_hz;
uint64_t ns = (uint64_t)(tsc_cycles * ns_per_cycle);
```

---

## Calculating Packet Latency

```cpp
// Example: Measure time from packet arrival to processing completion

// 1. Capture arrival timestamp
uint64_t arrival_ts = rte_rdtsc();
m->timestamp = arrival_ts;

// 2. After processing, calculate latency
uint64_t processing_done_ts = rte_rdtsc();
uint64_t latency_cycles = processing_done_ts - m->timestamp;

// Convert to nanoseconds
uint64_t tsc_hz = rte_get_tsc_hz();
double latency_ns = ((double)latency_cycles * 1000000000.0) / (double)tsc_hz;

printf("Packet latency: %.2f ns\n", latency_ns);
```

---

## Best Practices

1. **Choose the right method:**
   - High-frequency trading: Hardware timestamping + PTP
   - General networking: Software TSC timestamps
   - Debugging/monitoring: Software timestamps (simpler)

2. **Minimize jitter:**
   - Use CPU core isolation (isolcpus kernel parameter)
   - Disable power management (C-states, P-states)
   - Pin DPDK threads to specific cores
   - Use huge pages

3. **Timestamp placement:**
   - Timestamp as early as possible after rx_burst
   - Store in mbuf metadata (udata64 or dynfield)
   - Don't timestamp in slow path

4. **Clock synchronization:**
   - For single-server: TSC is sufficient
   - For multi-server: Use PTP or NTP
   - Verify TSC is stable (check /proc/cpuinfo for constant_tsc)

5. **Performance:**
   - Hardware timestamping has zero CPU overhead
   - Software TSC: ~10-30 CPU cycles
   - Batch timestamp multiple packets for efficiency

---

## Example: Latency Histogram

```cpp
#define NUM_BUCKETS 100
uint64_t latency_histogram[NUM_BUCKETS] = {0};

// After processing each packet
uint64_t latency_ns = calculate_latency(m);
uint32_t bucket = latency_ns / 100;  // 100ns buckets
if (bucket < NUM_BUCKETS) {
    latency_histogram[bucket]++;
}

// Periodically print histogram
void print_latency_histogram() {
    for (int i = 0; i < NUM_BUCKETS; i++) {
        if (latency_histogram[i] > 0) {
            printf("%d-%dns: %lu packets\n", 
                   i*100, (i+1)*100, latency_histogram[i]);
        }
    }
}
```
