// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2020, Intel Corporation */

/*
 * client.c -- a client of the messages-ping-pong example
 *
 * Please see README.md for a detailed description of this example.
 */

#include <librpma.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <libpmem.h>
#include <string.h>

#include "common-conn.h"
#include "messages-ping-pong-common.h"
#include "client_op.h"

#define USAGE_STR "usage: %s <server_address> <port>"
#define FLUSH_ID	(void *)0xF01D


static uint64_t strtoul_noerror(const char *in)
{
	uint64_t out = strtoul(in, NULL, 10);
	if (out == ULONG_MAX && errno == ERANGE) {
		(void) fprintf(stderr, "strtoul(%s) overflowed\n", in);
		exit(-1);
	}
	return out;
}

int main(int argc, char *argv[])
{
	/* validate parameters */
	if (argc < 3) {
		fprintf(stderr, USAGE_STR, argv[0]);
		exit(-1);
	}

	/* configure logging thresholds to see more details */
	rpma_log_set_threshold(RPMA_LOG_THRESHOLD, RPMA_LOG_LEVEL_INFO);
	rpma_log_set_threshold(RPMA_LOG_THRESHOLD_AUX, RPMA_LOG_LEVEL_INFO);

	/* read common parameters */
	char *addr = argv[1];
	char *port = argv[2];

	int ret;

  RPMA_socket* socket = rpma_socket(addr);
  if(socket == NULL) return -1;

  ret = rpma_socket_connect(socket, addr, port);

  char *path = "/mnt/temp";
  //ret |= rpma_socket_get_remote_descriptor(socket, REQUIRE_SIZE, RPMA_OP_FLUSH, path);
  ret |= rpma_socket_get_remote_descriptor(socket, REQUIRE_SIZE, RPMA_OP_WRITE, path);

  size_t mr_size = 1024 * 1024 * 1024; //max size, can't extend 1G byte
  //void* mr_ptr = malloc(mr_size);
  void* mr_ptr = malloc_aligned(mr_size);
  if (mr_ptr == NULL) return -1;
  char data[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-\n";
  size_t data_size = strlen(data);
  for(int i = 0; i < mr_size; i+=data_size) {
  	memcpy(mr_ptr + i, data, data_size);	
  }
	
  struct rpma_mr_local *src_mr = NULL;
  if (ret = rpma_mr_reg(socket->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
    return ret;
  }

//  size_t dst_size;
//  int is_pmem;
//  size_t length = REQUIRE_SIZE;
//  size_t offset = 0;
//  char *ptr = pmem_map_file(path, length, PMEM_FILE_CREATE | PMEM_FILE_SPARSE, 0600, &dst_size, &is_pmem);
//  printf("is_pmem: %d; size: %ld; ptr: %p\n", is_pmem, dst_size, ptr);
//  for(int i = 0; i < 10; i++) {
//        memcpy(ptr + offset, mr_ptr, mr_size);
//        offset += mr_size;
//  }

  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
  ret |= rpma_socket_mr_write(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);
//  ret |= rpma_socket_mr_write1(socket, src_mr, mr_size);

  rpma_mr_dereg(&src_mr);

  rpma_socket_close(socket);

  return ret ? -1 : 0;
}
