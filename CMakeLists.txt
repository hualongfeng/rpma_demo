#
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020, Intel Corporation
#

cmake_minimum_required(VERSION 3.3)
project(rpma_demo VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
#set(CMAKE_CXX_FLAGS "-std=c++17 ${CMAKE_CXX_FLAGS}")

set(LIBRPMA_LIBRARIES rpma)
#set(LIBRPMA_LIBRARY_DIRS ${CMAKE_BINARY_DIR}/lib/)
set(LIBRPMA_LIBRARY_DIRS ${CMAKE_SOURCE_DIR}/lib/)
set(LIBRPMA_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include/)

link_directories(${LIBRPMA_LIBRARY_DIRS})

FIND_LIBRARY(LIBIBVERBS_LIBRARIES ibverbs)
FIND_LIBRARY(LIBRT_LIBRARIES rt)
FIND_LIBRARY(LIBPMEM_LIBRARIES pmem)

function(add_example name)
	set(srcs ${ARGN})
	add_executable(${name} ${srcs})
	target_include_directories(${name} PRIVATE ${LIBRPMA_INCLUDE_DIRS})
	target_link_libraries(${name} rpma ${LIBPMEM_LIBRARIES} ${LIBIBVERBS_LIBRARIES} ${LIBRT_LIBRARIES})
endfunction()

add_example(server server.c
                   common-conn.c
                   common-epoll.c
                   deal_require.c
                   )
add_example(client client.c
                   common-conn.c
                   client_op.c
                   )
add_example(server_cpp server.cc
                   common-conn.c
                   Reactor.cc
                   EventOp.cc
                   )

