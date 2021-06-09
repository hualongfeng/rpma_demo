#
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020, Intel Corporation
#

cmake_minimum_required(VERSION 3.3)
project(rpma_demo VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
#set(CMAKE_CXX_FLAGS "-std=c++17 ${CMAKE_CXX_FLAGS}")

set(LIBRPMA_LIBRARIES rpma)
#set(LIBRPMA_LIBRARY_DIRS ${CMAKE_BINARY_DIR}/lib/)
set(LIBRPMA_LIBRARY_DIRS ${CMAKE_SOURCE_DIR}/lib/)
set(LIBRPMA_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/include/)
set(LIBRADOS_INCLUDE_DIRS /home/ssp/hualong/ceph/build/include /home/ssp/hualong/ceph/src)
set(LIBRADOS_LIBRARY_DIRS /home/ssp/hualong/ceph/build/lib)
set(LIBBOOST_INCLUDE_DIRS /home/ssp/hualong/ceph/build/boost/include)
set(LIBBOOST_LIBRARY_DIRS /home/ssp/hualong/ceph/build/boost/lib)

link_directories(${LIBRPMA_LIBRARY_DIRS})

FIND_LIBRARY(LIBIBVERBS_LIBRARIES ibverbs)
FIND_LIBRARY(LIBRT_LIBRARIES rt)
FIND_LIBRARY(LIBPMEM_LIBRARIES pmem)
FIND_LIBRARY(LIBRADOS_LIBRARIES rados ceph-common denc-mod-common denc-mod-rbd denc-mod-osd )

function(add_example name)
	set(srcs ${ARGN})
	add_executable(${name} ${srcs})
	target_include_directories(${name} PRIVATE ${LIBRPMA_INCLUDE_DIRS} ${LIBRADOS_INCLUDE_DIRS} ${LIBBOOST_INCLUDE_DIRS})
	target_link_libraries(${name} rados rpma ${LIBPMEM_LIBRARIES} ${LIBIBVERBS_LIBRARIES} ${LIBRT_LIBRARIES})
endfunction()

add_example(server_cpp server.cc
                   Reactor.cc
                   EventOp.cc
                   MemoryManager.cc
                   Types.cc
                   )
add_example(client_cpp client.cc
                   Reactor.cc
                   EventOp.cc
                   MemoryManager.cc
                   Types.cc
                   )
add_example(test_cpp test_demo.cc
                     Types.cc)