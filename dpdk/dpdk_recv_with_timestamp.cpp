#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_cycles.h>

#include <getopt.h>
#include <arpa/inet.h>
#include <iostream>
#include <csignal>
#include <cstring>
#include <vector>

static volatile bool keep_running = true;

// Latency statistics
struct latency_stats {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t total_ns;
    uint64_t count;
    uint64_t histogram[100];  // 100ns buckets up to 10us
    
    latency_stats() : min_ns(UINT64_MAX), max_ns(0), total_ns(0), count(0) {
        memset(histogram, 0, sizeof(histogram));
    }
    
    void update(uint64_t latency_ns) {
        if (latency_ns < min_ns) min_ns = latency_ns;
        if (latency_ns > max_ns) max_ns = latency_ns;
        total_ns += latency_ns;
        count++;
        
        uint32_t bucket = latency_ns / 100;
        if (bucket < 100) histogram[bucket]++;
    }
    
    void print() {
        if (count == 0) return;
        std::cout << "\n=== Latency Statistics ===" << std::endl;
        std::cout << "Packets: " << count << std::endl;
        std::cout << "Min: " << min_ns << " ns" << std::endl;
        std::cout << "Avg: " << (total_ns / count) << " ns" << std::endl;
        std::cout << "Max: " << max_ns << " ns" << std::endl;
        
        std::cout << "\nHistogram (100ns buckets):" << std::endl;
        for (int i = 0; i < 100; i++) {
            if (histogram[i] > 0) {
                std::cout << i*100 << "-" << (i+1)*100 << "ns: " << histogram[i] << std::endl;
            }
        }
    }
};

static void
signal_handler(int signum)
{
    (void)signum;
    keep_running = false;
}

static uint32_t parse_ipv4_addr(const char* s)
{
    struct in_addr a;
    if (inet_aton(s, &a) == 0) return 0;
    return ntohl(a.s_addr);
}

int main(int argc, char** argv)
{
    // Default application options
    uint16_t app_port = 0;
    std::string target_ip_str = "224.0.0.100";
    uint32_t target_ip = RTE_IPV4(224,0,0,100);
    uint16_t target_port = 40000;
    bool enable_promisc = true;
    bool enable_hw_timestamp = false;
    bool show_latency_stats = false;

    // Initialize EAL first
    int eal_ret = rte_eal_init(argc, argv);
    if (eal_ret < 0) {
        std::cerr << "Failed to init EAL" << std::endl;
        return 1;
    }

    argc -= eal_ret;
    argv += eal_ret;

    // Parse application args
    const struct option longopts[] = {
        {"port", required_argument, nullptr, 'p'},
        {"target-ip", required_argument, nullptr, 'i'},
        {"target-port", required_argument, nullptr, 't'},
        {"no-promisc", no_argument, nullptr, 'n'},
        {"hw-timestamp", no_argument, nullptr, 'H'},
        {"latency-stats", no_argument, nullptr, 'L'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:i:t:nHL", longopts, nullptr)) != -1) {
        switch (opt) {
            case 'p': app_port = (uint16_t)atoi(optarg); break;
            case 'i': target_ip_str = optarg; target_ip = parse_ipv4_addr(optarg); break;
            case 't': target_port = (uint16_t)atoi(optarg); break;
            case 'n': enable_promisc = false; break;
            case 'H': enable_hw_timestamp = true; break;
            case 'L': show_latency_stats = true; break;
            default: break;
        }
    }

    unsigned nb_ports = rte_eth_dev_count_avail();
    if (nb_ports == 0) {
        std::cerr << "No Ethernet ports - bye" << std::endl;
        return 1;
    }

    if (app_port >= nb_ports) {
        std::cerr << "Requested port " << app_port << " >= available ports (" << nb_ports << ")" << std::endl;
        return 1;
    }

    uint16_t port_id = app_port;
    uint16_t rx_rings = 1;
    uint16_t tx_rings = 0;
    const uint16_t nb_rx_desc = 1024;

    // Get TSC frequency for timestamp conversion
    uint64_t tsc_hz = rte_get_tsc_hz();
    double ns_per_cycle = 1000000000.0 / (double)tsc_hz;
    std::cout << "TSC frequency: " << tsc_hz << " Hz" << std::endl;
    std::cout << "TSC resolution: " << ns_per_cycle << " ns/cycle" << std::endl;

    // Configure device with optional hardware timestamping
    struct rte_eth_conf port_conf;
    std::memset(&port_conf, 0, sizeof(port_conf));
    
    if (enable_hw_timestamp) {
        // Check if device supports hardware timestamping
        struct rte_eth_dev_info dev_info;
        rte_eth_dev_info_get(port_id, &dev_info);
        
        if (dev_info.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TIMESTAMP) {
            port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;
            std::cout << "Hardware timestamping enabled on port " << port_id << std::endl;
        } else {
            std::cerr << "Warning: Hardware timestamping not supported on port " << port_id << std::endl;
            enable_hw_timestamp = false;
        }
    }

    if (rte_eth_dev_configure(port_id, rx_rings, tx_rings, &port_conf) != 0) {
        std::cerr << "Failed to configure port " << port_id << std::endl;
        return 1;
    }

    // Allocate and set up one RX queue
    struct rte_mempool* mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL",
                                8192, 250, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == nullptr) {
        std::cerr << "Failed to create mbuf pool" << std::endl;
        return 1;
    }

    // Setup RX queue with timestamp offload if enabled
    struct rte_eth_rxconf rx_conf;
    std::memset(&rx_conf, 0, sizeof(rx_conf));
    if (enable_hw_timestamp) {
        rx_conf.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;
    }

    if (rte_eth_rx_queue_setup(port_id, 0, nb_rx_desc, rte_eth_dev_socket_id(port_id), 
                                &rx_conf, mbuf_pool) != 0) {
        std::cerr << "Failed to setup RX queue" << std::endl;
        return 1;
    }

    if (rte_eth_dev_start(port_id) != 0) {
        std::cerr << "Failed to start port" << std::endl;
        return 1;
    }

    if (enable_promisc) {
        rte_eth_promiscuous_enable(port_id);
    }

    // Program multicast MAC
    rte_eth_promiscuous_disable(port_id);
    uint32_t ip_host = target_ip;
    if ((ip_host & 0xF0000000u) != 0xE0000000u) {
        std::cerr << "Warning: target IP " << target_ip_str << " is not an IPv4 multicast address" << std::endl;
    } else {
        struct rte_ether_addr mc;
        uint32_t lower23 = ip_host & 0x7FFFFFu;
        mc.addr_bytes[0] = 0x01;
        mc.addr_bytes[1] = 0x00;
        mc.addr_bytes[2] = 0x5e;
        mc.addr_bytes[3] = (uint8_t)((lower23 >> 16) & 0x7Fu);
        mc.addr_bytes[4] = (uint8_t)((lower23 >> 8) & 0xFFu);
        mc.addr_bytes[5] = (uint8_t)(lower23 & 0xFFu);

        int rc = rte_eth_dev_set_mc_addr_list(port_id, &mc, 1);
        if (rc != 0) {
            std::cerr << "Warning: failed to set multicast MAC (rc=" << rc << ")" << std::endl;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "DPDK receiver started on port " << port_id 
              << ", listening for IPv4 UDP dst " << target_ip_str << ":" << target_port << std::endl;
    if (enable_hw_timestamp) {
        std::cout << "Using hardware timestamps" << std::endl;
    } else {
        std::cout << "Using software TSC timestamps" << std::endl;
    }

    const uint16_t BURST_SIZE = 32;
    struct rte_mbuf* bufs[BURST_SIZE];
    uint64_t total = 0;
    uint64_t matched = 0;
    
    latency_stats lstats;
    uint64_t last_stats_print = rte_rdtsc();
    const uint64_t stats_interval_cycles = tsc_hz;  // Print stats every 1 second

    while (keep_running) {
        // Capture timestamp BEFORE rx_burst for latency measurement
        uint64_t rx_start_tsc = rte_rdtsc();
        
        const uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);
        if (nb_rx == 0) continue;
        
        uint64_t rx_end_tsc = rte_rdtsc();

        for (uint16_t i = 0; i < nb_rx; ++i) {
            struct rte_mbuf* m = bufs[i];
            
            // Get packet timestamp
            uint64_t pkt_timestamp_tsc;
            const char* timestamp_source;
            
            if (enable_hw_timestamp && (m->ol_flags & RTE_MBUF_F_RX_TIMESTAMP)) {
                // Hardware timestamp available
                pkt_timestamp_tsc = m->timestamp;
                timestamp_source = "HW";
            } else {
                // Use software timestamp (approximation - actual arrival is between rx_start and rx_end)
                pkt_timestamp_tsc = rx_start_tsc;
                timestamp_source = "SW";
            }
            
            // Store timestamp in mbuf for later use
            m->udata64 = pkt_timestamp_tsc;
            
            // Parse packet
            unsigned char* pkt = rte_pktmbuf_mtod(m, unsigned char*);
            uint16_t pkt_len = rte_pktmbuf_pkt_len(m);

            if (pkt_len >= 14 + 20 + 8) {
                uint16_t eth_type = (pkt[12] << 8) | pkt[13];
                if (eth_type == 0x0800) { // IPv4
                    unsigned char* ip = pkt + 14;
                    uint8_t ihl = (ip[0] & 0x0f) * 4;
                    uint8_t proto = ip[9];
                    uint32_t dst = (ip[16] << 24) | (ip[17] << 16) | (ip[18] << 8) | ip[19];
                    if (proto == 17 && dst == rte_be_to_cpu_32(target_ip)) { // UDP
                        unsigned char* udp = ip + ihl;
                        uint16_t dst_port = rte_be_to_cpu_16(*(uint16_t*)(udp + 2));
                        if (dst_port == target_port) {
                            ++matched;
                            
                            // Calculate processing latency (from arrival to now)
                            uint64_t processing_done_tsc = rte_rdtsc();
                            uint64_t latency_cycles = processing_done_tsc - pkt_timestamp_tsc;
                            uint64_t latency_ns = (uint64_t)(latency_cycles * ns_per_cycle);
                            
                            if (show_latency_stats) {
                                lstats.update(latency_ns);
                            }
                            
                            // Convert timestamp to nanoseconds for display
                            uint64_t timestamp_ns = (uint64_t)(pkt_timestamp_tsc * ns_per_cycle);
                            
                            std::cout << "[" << timestamp_source << "] "
                                      << "matched pkt len=" << pkt_len 
                                      << " timestamp=" << timestamp_ns << "ns"
                                      << " latency=" << latency_ns << "ns"
                                      << " total=" << total 
                                      << " matched=" << matched << std::endl;
                        }
                    }
                }
            }

            ++total;
            rte_pktmbuf_free(m);
        }
        
        // Print periodic statistics
        if (show_latency_stats) {
            uint64_t now = rte_rdtsc();
            if (now - last_stats_print > stats_interval_cycles) {
                lstats.print();
                last_stats_print = now;
            }
        }
    }

    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);

    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Total packets: " << total << std::endl;
    std::cout << "Matched packets: " << matched << std::endl;
    
    if (show_latency_stats && matched > 0) {
        lstats.print();
    }

    return 0;
}
