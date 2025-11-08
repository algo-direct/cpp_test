#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_ether.h>

#include <getopt.h>
#include <arpa/inet.h>
#include <iostream>
#include <csignal>
#include <cstring>
#include <vector>

static volatile bool keep_running = true;

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
    // Default application options (these are parsed after EAL init)
    uint16_t app_port = 0; // default to first port
    std::string target_ip_str = "224.0.0.100";
    uint32_t target_ip = RTE_IPV4(224,0,0,100);
    uint16_t target_port = 40000;
    bool enable_promisc = true;

    // Initialize EAL first. Application-specific args should be passed after the "--" when running.
    int eal_ret = rte_eal_init(argc, argv);
    if (eal_ret < 0) {
        std::cerr << "Failed to init EAL" << std::endl;
        return 1;
    }

    argc -= eal_ret;
    argv += eal_ret;

    // Parse application args (after EAL args / --)
    const struct option longopts[] = {
        {"port", required_argument, nullptr, 'p'},
        {"target-ip", required_argument, nullptr, 'i'},
        {"target-port", required_argument, nullptr, 't'},
        {"no-promisc", no_argument, nullptr, 'n'},
        {"all-multicast", no_argument, nullptr, 'a'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:i:t:na", longopts, nullptr)) != -1) {
        switch (opt) {
            case 'p': app_port = (uint16_t)atoi(optarg); break;
            case 'i': target_ip_str = optarg; target_ip = parse_ipv4_addr(optarg); break;
            case 't': target_port = (uint16_t)atoi(optarg); break;
            case 'n': enable_promisc = false; break;
            case 'a': enable_promisc = true; break;
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

    // Configure device
    struct rte_eth_conf port_conf;
    std::memset(&port_conf, 0, sizeof(port_conf));

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

    if (rte_eth_rx_queue_setup(port_id, 0, nb_rx_desc, rte_eth_dev_socket_id(port_id), nullptr, mbuf_pool) != 0) {
        std::cerr << "Failed to setup RX queue" << std::endl;
        return 1;
    }

    if (rte_eth_dev_start(port_id) != 0) {
        std::cerr << "Failed to start port" << std::endl;
        return 1;
    }

    if (enable_promisc) {
        rte_eth_promiscuous_enable(port_id);
    } else {
        // optionally enable all-multicast if requested via other flags; here we can leave as default
    }

    // Program multicast MAC derived from target_ip so the NIC accepts that multicast address only
    rte_eth_promiscuous_disable(port_id);
    uint32_t ip_host = target_ip; // parse_ipv4_addr returned host-order
    if ((ip_host & 0xF0000000u) != 0xE0000000u) {
        std::cerr << "Warning: target IP " << target_ip_str << " is not an IPv4 multicast address (224.0.0.0/4)." << std::endl;
    } else {
        struct rte_ether_addr mc;
        uint32_t lower23 = ip_host & 0x7FFFFFu; // lower 23 bits
        mc.addr_bytes[0] = 0x01;
        mc.addr_bytes[1] = 0x00;
        mc.addr_bytes[2] = 0x5e;
        mc.addr_bytes[3] = (uint8_t)((lower23 >> 16) & 0x7Fu);
        mc.addr_bytes[4] = (uint8_t)((lower23 >> 8) & 0xFFu);
        mc.addr_bytes[5] = (uint8_t)(lower23 & 0xFFu);

        int rc = rte_eth_dev_set_mc_addr_list(port_id, &mc, 1);
        if (rc != 0) {
            std::cerr << "Warning: failed to set multicast MAC on port " << port_id << " (rc=" << rc << ")." << std::endl;
        } else {
            char macbuf[64];
            snprintf(macbuf, sizeof(macbuf), "%02x:%02x:%02x:%02x:%02x:%02x",
                mc.addr_bytes[0], mc.addr_bytes[1], mc.addr_bytes[2], mc.addr_bytes[3], mc.addr_bytes[4], mc.addr_bytes[5]);
            std::cout << "Programmed multicast MAC " << macbuf << " on port " << port_id << std::endl;
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "DPDK receiver started on port " << port_id << ", listening for IPv4 UDP dst " << target_ip_str << ":" << target_port << std::endl;

    const uint16_t BURST_SIZE = 32;
    struct rte_mbuf* bufs[BURST_SIZE];
    uint64_t total = 0;
    uint64_t matched = 0;

    while (keep_running) {
        const uint16_t nb_rx = rte_eth_rx_burst(port_id, 0, bufs, BURST_SIZE);
        if (nb_rx == 0) continue;

        for (uint16_t i = 0; i < nb_rx; ++i) {
            struct rte_mbuf* m = bufs[i];
            // minimal parsing: ethernet + IPv4 + UDP
            unsigned char* pkt = rte_pktmbuf_mtod(m, unsigned char*);
            uint16_t pkt_len = rte_pktmbuf_pkt_len(m);

            // check length for Ethernet(14) + IPv4(20) + UDP(8)
            if (pkt_len >= 14 + 20 + 8) {
                uint16_t eth_type = (pkt[12] << 8) | pkt[13];
                if (eth_type == 0x0800) { // IPv4
                    unsigned char* ip = pkt + 14;
                    uint8_t ihl = (ip[0] & 0x0f) * 4;
                    uint8_t proto = ip[9];
                    uint32_t dst = (ip[16] << 24) | (ip[17] << 16) | (ip[18] << 8) | ip[19];
                    if (proto == 17 && dst == rte_be_to_cpu_32(target_ip)) { // UDP
                        unsigned char* udp = ip + ihl;
                        uint16_t dst_port = rte_be_to_cpu_16(*(uint16_t*)(udp + 2)); // beware unaligned access on some CPUs
                        if (dst_port == target_port) {
                            ++matched;
                            std::cout << "matched pkt len=" << pkt_len << " total=" << total << " matched=" << matched << std::endl;
                        }
                    }
                }
            }

            ++total;
            rte_pktmbuf_free(m);
        }
    }

    rte_eth_dev_stop(port_id);
    rte_eth_dev_close(port_id);

    std::cout << "Exiting. total=" << total << " matched=" << matched << std::endl;
    return 0;
}
