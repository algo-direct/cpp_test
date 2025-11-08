NVMe / File latency micro-benchmark

Usage

Build (from repo root):

cmake --build build -j

Run:

# quick mode (smaller sample count)
sudo ./build/myapp --quick

# run only write benchmark without O_DIRECT and keep output file
./build/myapp --mode=write --no-odirect --keep-write-file --write-path=/tmp/mybench.dat

# run only read benchmark on a given device/file (may need sudo)
sudo ./build/myapp --mode=read --read-path=/dev/nvme0n1p1

Options

--read-path <path>       Path to read from (default: /media/ashish/nvme9100/data.txt)
--write-path <path>      Path to write to (default: /tmp/nvme_write_bench.dat)
--mode <read|write|both> Choose which benchmark(s) to run (default: both)
--num-tests <N>          Number of random reads/writes to perform (default: 10000)
--no-odirect             Do not use O_DIRECT (useful for testing regular files)
--quick                  Run smaller number of samples (100..1000) for quick testing
--keep-write-file        Do not delete the temporary write file after the test
--buffer-size <bytes>    Size of each IO in bytes (default: 4096)

Notes

- Percentiles are computed using linear interpolation between sorted samples for more accurate fractional percentiles (p99.9, p99.95, etc.).
- The read benchmark still uses O_DIRECT by default (unless --no-odirect). Running reads on raw block devices typically requires sudo.
- The write benchmark defaults to a safe temporary file in /tmp and cleans it up unless --keep-write-file is passed.

Safety

Be careful when pointing the read or write paths at real devices or important files. The defaults are configured to be reasonably safe for development, but always double-check paths before running with elevated privileges.
