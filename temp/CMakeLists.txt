cmake_minimum_required(VERSION 3.16)

project(temp_examples NONE)

find_package(Threads REQUIRED)

set(TEMP_SRCS
    spsc_ring.cpp
    token_bucket.cpp
    reservoir_sampling.cpp
    topk_stream.cpp
    lru_cache.cpp
    slab_allocator.cpp
    radix_sort.cpp
    csv_parser.cpp
    mpmc_queue_ms.cpp
)

set(TEMP_TARGETS)
foreach(src IN LISTS TEMP_SRCS)
    get_filename_component(tgt ${src} NAME_WE)
    add_executable(${tgt} ${src})
    target_link_libraries(${tgt} PRIVATE Threads::Threads)
    target_compile_features(${tgt} PRIVATE cxx_std_23)
    list(APPEND TEMP_TARGETS ${tgt})
endforeach()

add_custom_target(run_temp_tests
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/temp bash run_tests.sh
    DEPENDS ${TEMP_TARGETS}
)
