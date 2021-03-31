#
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020, Intel Corporation
#

cmake_minimum_required(VERSION 3.3)
project(rpma_demo C)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)
set(VERSION_PATCH 0)

set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})

set(LIBRPMA_LIBRARIES rpma)
#set(LIBRPMA_LIBRARY_DIRS ${CMAKE_BINARY_DIR}/lib/)
set(LIBRPMA_LIBRARY_DIRS ${CMAKE_SOURCE_DIR}/lib/)
set(LIBRPMA_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include/)

link_directories(${LIBRPMA_LIBRARY_DIRS})

FIND_LIBRARY(LIBIBVERBS_LIBRARIES ibverbs)
FIND_LIBRARY(LIBRT_LIBRARIES rt)

function(add_example name)
	set(srcs ${ARGN})
	add_executable(example-${name} ${srcs})
	target_include_directories(example-${name} PRIVATE ${LIBRPMA_INCLUDE_DIRS})
	target_link_libraries(example-${name} rpma  ${LIBIBVERBS_LIBRARIES} ${LIBRT_LIBRARIES})
endfunction()

add_example(server server.c common-conn.c common-epoll.c)
add_example(client client.c common-conn.c)
