#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstring>
#include <cerrno>
#include <stdlib.h>
#include <vector>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <getopt.h>
#include <string>
#include <sys/stat.h>
#include <cstdio>

// Helper function to align memory
void* aligned_malloc(size_t size, size_t align) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, align, size) != 0) {
        return nullptr;
    }
    return ptr;
}

void aligned_free(void* ptr) {
    free(ptr);
}

int main(int argc, char** argv) {
    // Defaults
    std::string read_path = "/media/ashish/nvme9100/data.txt"; // change as needed
    std::string write_path = "/tmp/nvme_write_bench.dat";
    size_t buffer_size = 4096; // bytes
    size_t alignment = 4096;
    int num_tests = 10000;
    bool use_odirect = true;
    bool do_read = true;
    bool do_write = true;
    bool keep_write_file = false;
    bool quick = false;

    const struct option longopts[] = {
        {"read-path", required_argument, nullptr, 'r'},
        {"write-path", required_argument, nullptr, 'w'},
        {"mode", required_argument, nullptr, 'm'}, // read, write, both
        {"num-tests", required_argument, nullptr, 'n'},
        {"no-odirect", no_argument, nullptr, 'N'},
        {"quick", no_argument, nullptr, 'q'},
        {"keep-write-file", no_argument, nullptr, 'k'},
        {"buffer-size", required_argument, nullptr, 'b'},
        {0,0,0,0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "r:w:m:n:Nqkb:", longopts, nullptr)) != -1) {
        switch (opt) {
            case 'r': read_path = optarg; break;
            case 'w': write_path = optarg; break;
            case 'm': {
                std::string m = optarg;
                if (m == "read") { do_read = true; do_write = false; }
                else if (m == "write") { do_read = false; do_write = true; }
                else { do_read = true; do_write = true; }
                break;
            }
            case 'n': num_tests = atoi(optarg); break;
            case 'N': use_odirect = false; break;
            case 'q': quick = true; break;
            case 'k': keep_write_file = true; break;
            case 'b': buffer_size = (size_t)atoi(optarg); break;
            default: break;
        }
    }

    if (quick) {
        num_tests = std::max(100, std::min(1000, num_tests));
    }

    // allocate aligned buffer
    void* buffer = aligned_malloc(buffer_size, alignment);
    if (!buffer) {
        std::cerr << "Error allocating aligned memory" << std::endl;
        return 1;
    }

    // Helper percentile (interpolated) lambda
    auto interpolated_percentile = [](const std::vector<double>& sorted, double p) -> double {
        if (sorted.empty()) return 0.0;
        if (p <= 0.0) return sorted.front();
        if (p >= 100.0) return sorted.back();
        double pos = (p/100.0) * (sorted.size() - 1);
        size_t idx = (size_t)floor(pos);
        double frac = pos - idx;
        if (idx + 1 < sorted.size()) {
            return sorted[idx] + frac * (sorted[idx+1] - sorted[idx]);
        } else {
            return sorted[idx];
        }
    };

    // Reusable stats printer lambda
    auto print_stats = [&](const std::vector<double>& samples, const std::string& title) {
        if (samples.empty()) {
            std::cout << title << ": no samples" << std::endl;
            return;
        }
        std::vector<double> sorted = samples;
        std::sort(sorted.begin(), sorted.end());
        int n = (int)sorted.size();
        double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / n;
        double minv = sorted.front();
        double maxv = sorted.back();
        double p90 = interpolated_percentile(sorted, 90.0);
        double p95 = interpolated_percentile(sorted, 95.0);
        double p99 = interpolated_percentile(sorted, 99.0);
        double p999 = interpolated_percentile(sorted, 99.9);
        double p9995 = interpolated_percentile(sorted, 99.95);
        double p9999 = interpolated_percentile(sorted, 99.99);
        double p99999 = interpolated_percentile(sorted, 99.999);

        std::cout << title << " over " << n << " samples:\n";
        std::cout << "  mean: " << mean << " us\n";
        std::cout << "  min: " << minv << " us\n";
        std::cout << "  max: " << maxv << " us\n";
        std::cout << "  p90: " << p90 << " us\n";
        std::cout << "  p95: " << p95 << " us\n";
        std::cout << "  p99: " << p99 << " us\n";
        std::cout << "  p99.9: " << p999 << " us\n";
        std::cout << "  p99.95: " << p9995 << " us\n";
        std::cout << "  p99.99: " << p9999 << " us\n";
        std::cout << "  p99.999: " << p99999 << " us" << std::endl;
    };

    // Seed rand
    srand((unsigned)time(nullptr));

    // --- Read benchmark (if requested) ---
    if (do_read) {
        int rflags = O_RDONLY | (use_odirect ? O_DIRECT : 0);
        int fd = open(read_path.c_str(), rflags);
        if (fd == -1) {
            std::cerr << "Error opening read path '" << read_path << "': " << strerror(errno) << std::endl;
            std::cerr << "Skipping read benchmark." << std::endl;
        } else {
            std::vector<double> read_samples;
            read_samples.reserve(num_tests);

            // pick a reasonable range for random offsets (e.g., up to 1GB by default)
            const off_t max_blocks = 1024 * 1024; // number of 4KiB blocks

            for (int i = 0; i < num_tests; ++i) {
                off_t offset = (rand() % max_blocks) * alignment;
                if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                    std::cerr << "lseek failed: " << strerror(errno) << std::endl;
                    break;
                }
                auto start = std::chrono::steady_clock::now();
                ssize_t bytes_read = read(fd, buffer, buffer_size);
                auto end = std::chrono::steady_clock::now();
                if (bytes_read != (ssize_t)buffer_size) {
                    std::cerr << "Error reading data or short read: " << strerror(errno) << std::endl;
                    break;
                }
                std::chrono::duration<double, std::micro> elapsed = end - start;
                read_samples.push_back(elapsed.count());
            }

            print_stats(read_samples, std::string("NVMe read access time"));
            close(fd);
        }
    }

    // --- Write benchmark (if requested) ---
    if (do_write) {
        int wflags = O_WRONLY | O_CREAT | (use_odirect ? O_DIRECT : 0);
        int wfd = open(write_path.c_str(), wflags, 0644);
        if (wfd == -1) {
            if (use_odirect) {
                std::cerr << "Opening write path with O_DIRECT failed, retrying without O_DIRECT: " << strerror(errno) << std::endl;
                wflags = O_WRONLY | O_CREAT;
                wfd = open(write_path.c_str(), wflags, 0644);
            }
            if (wfd == -1) {
                std::cerr << "Error opening write path '" << write_path << "': " << strerror(errno) << std::endl;
                std::cerr << "Skipping write benchmark." << std::endl;
            }
        }

        if (wfd != -1) {
            // Prepare buffer contents once
            std::memset(buffer, 'A', buffer_size);
            std::vector<double> write_samples;
            write_samples.reserve(num_tests);

            const off_t max_write_blocks = 1024 * 64; // ~256MB window

            for (int i = 0; i < num_tests; ++i) {
                off_t offset = (rand() % max_write_blocks) * alignment;
                auto start = std::chrono::steady_clock::now();
                ssize_t bytes_written = pwrite(wfd, buffer, buffer_size, offset);
                auto end = std::chrono::steady_clock::now();
                if (bytes_written != (ssize_t)buffer_size) {
                    std::cerr << "Error writing data or short write: " << strerror(errno) << std::endl;
                    break;
                }
                std::chrono::duration<double, std::micro> elapsed = end - start;
                write_samples.push_back(elapsed.count());
            }

            print_stats(write_samples, std::string("File write access time to '" + write_path + "'"));

            close(wfd);
            if (!keep_write_file) unlink(write_path.c_str());
        }
    }

    aligned_free(buffer);
    return 0;
}
