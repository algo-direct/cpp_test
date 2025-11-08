cmake_minimum_required(VERSION 3.16)

project(cpp_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(myapp main.cpp)

add_executable(hello hello.cpp)

install(TARGETS myapp RUNTIME DESTINATION bin)
install(TARGETS hello
	RUNTIME DESTINATION bin)

# Optional DPDK example
option(BUILD_DPDK_EXAMPLE "Build the DPDK multicast receive example" OFF)
# Path to a local DPDK installation root (e.g. /opt/dpdk or /home/ashish/git/dpdk-25.03)
set(DPDK_ROOT "" CACHE PATH "Path to DPDK installation root (optional)")

if(BUILD_DPDK_EXAMPLE)
	# If the user supplied a DPDK root, add its pkgconfig locations to PKG_CONFIG_PATH so
	# pkg-config can find libdpdk. This is a helpful convenience for local builds.
	if(NOT DPDK_ROOT STREQUAL "")
		set(ENV{PKG_CONFIG_PATH} "${DPDK_ROOT}/lib64/pkgconfig:${DPDK_ROOT}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
	endif()

	find_package(PkgConfig REQUIRED)
	pkg_check_modules(RTE libdpdk)

	# If pkg-config didn't find DPDK, try a minimal manual search using DPDK_ROOT
	if(NOT RTE_FOUND)
		message(WARNING "libdpdk not found via pkg-config; attempting manual search using DPDK_ROOT and common locations")
		if(NOT DPDK_ROOT STREQUAL "")
			find_path(RTE_INCLUDE_DIRS rte_eal.h PATHS ${DPDK_ROOT}/include /usr/local/include /usr/include)
			find_library(RTE_LIBRARIES NAMES rte_eal PATHS ${DPDK_ROOT}/lib ${DPDK_ROOT}/lib64 /usr/local/lib /usr/lib)
			if(RTE_INCLUDE_DIRS AND RTE_LIBRARIES)
				set(RTE_FOUND TRUE)
				# pkg-config normally supplies extra cflags; leave RTE_CFLAGS_OTHER empty when falling back
				set(RTE_CFLAGS_OTHER "")
			endif()
		endif()
	endif()

	if(NOT RTE_FOUND)
		message(FATAL_ERROR "libdpdk not found via pkg-config and manual search failed; set DPDK_ROOT or PKG_CONFIG_PATH to point to your DPDK installation")
	endif()

	include_directories(${RTE_INCLUDE_DIRS})
	if(RTE_LIBRARY_DIRS)
		link_directories(${RTE_LIBRARY_DIRS})
	endif()
	add_definitions(${RTE_CFLAGS_OTHER})

	# Print helpful detection info
	message(STATUS "DPDK detected via pkg-config/manually")
	message(STATUS "  RTE include dirs: ${RTE_INCLUDE_DIRS}")
	if(RTE_LIBRARY_DIRS)
		message(STATUS "  RTE library dirs: ${RTE_LIBRARY_DIRS}")
	endif()
	message(STATUS "  RTE libraries: ${RTE_LIBRARIES}")

	add_executable(dpdk_recv dpdk/dpdk_recv.cpp)
	target_include_directories(dpdk_recv PRIVATE ${RTE_INCLUDE_DIRS})
	target_link_libraries(dpdk_recv ${RTE_LIBRARIES})

	install(TARGETS dpdk_recv RUNTIME DESTINATION bin)
endif()