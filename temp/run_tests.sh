#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "Building tests..."
g++ -std=gnu++23 -O2 spsc_ring.cpp -o spsc_ring
g++ -std=gnu++23 -O2 token_bucket.cpp -o token_bucket
g++ -std=gnu++23 -O2 reservoir_sampling.cpp -o reservoir_sampling
g++ -std=gnu++23 -O2 topk_stream.cpp -o topk_stream
g++ -std=gnu++23 -O2 lru_cache.cpp -o lru_cache
g++ -std=gnu++23 -O2 slab_allocator.cpp -o slab_allocator
g++ -std=gnu++23 -O2 radix_sort.cpp -o radix_sort
g++ -std=gnu++23 -O2 csv_parser.cpp -o csv_parser
echo "Running tests..."
./spsc_ring
./token_bucket
./reservoir_sampling
./topk_stream
./lru_cache
./slab_allocator
./radix_sort
./csv_parser
echo "All temp tests passed"
