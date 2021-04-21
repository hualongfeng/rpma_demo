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

#include "common-conn.h"
#include "messages-ping-pong-common.h"

#define USAGE_STR "usage: %s <server_address> <port>"
#define FLUSH_ID	(void *)0xF01D


static uint64_t
strtoul_noerror(const char *in)
{
	uint64_t out = strtoul(in, NULL, 10);
	if (out == ULONG_MAX && errno == ERANGE) {
		(void) fprintf(stderr, "strtoul(%s) overflowed\n", in);
		exit(-1);
	}
	return out;
}

int
main(int argc, char *argv[])
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

	/* RPMA resources - general */
	struct rpma_peer *peer = NULL;
	struct rpma_conn *conn = NULL;
	struct rpma_completion cmpl;

	/* resources - memory region */
	struct rpma_peer_cfg *pcfg = NULL;
	void *mr_ptr = NULL;
	size_t mr_size = 0;
	size_t data_offset = 0;
	struct rpma_mr_remote *dst_mr = NULL;
	size_t dst_size = 0;
	size_t dst_offset = 0;
	struct rpma_mr_local *src_mr = NULL;


	/* prepare memory */
	struct rpma_mr_local *recv_mr, *send_mr;
	void *recv = malloc_aligned(MSG_SIZE);
	if (recv == NULL)
		return -1;
	void *send = malloc_aligned(MSG_SIZE);
	if (send == NULL) {
		free(recv);
		return -1;
	}

	/*
	 * lookup an ibv_context via the address and create a new peer using it
	 */
	if ((ret = client_peer_via_address(addr, &peer)))
		goto err_mr_free;

	/* register the memory */
	if ((ret = rpma_mr_reg(peer, recv, MSG_SIZE, RPMA_MR_USAGE_RECV,
				&recv_mr)))
		goto err_peer_delete;
	if ((ret = rpma_mr_reg(peer, send, MSG_SIZE, RPMA_MR_USAGE_SEND,
				&send_mr))) {
		(void) rpma_mr_dereg(&recv_mr);
		goto err_peer_delete;
	}

	/* establish a new connection to a server listening at addr:port */
	if ((ret = client_connect(peer, addr, port, NULL, &conn)))
		goto err_mr_dereg;

		/* prepare a receive for the server's response */
		ret |= rpma_recv(conn, recv_mr, 0, MSG_SIZE, recv);

		/* send a message to the server */
		(void) printf("Value sent: %" PRIu64 "\n", REQUIRE_SIZE);
		//uint64_t* send64 = send;
		//*send64 = cntr;
    struct require_data* rdata = send;
    rdata->size = REQUIRE_SIZE;
    rdata->op   = RPMA_OP_FLUSH;
    //rdata->op   = RPMA_OP_WRITE;
    char *path = "/mnt/temp";
    rdata->path_size = strlen(path);
    strncpy(rdata->path, path, MSG_SIZE - sizeof(struct require_data));
		ret |= rpma_send(conn, send_mr, 0, MSG_SIZE, RPMA_F_COMPLETION_ALWAYS, NULL);

		int send_cmpl = 0;
		int recv_cmpl = 0;

		do {
			/* prepare completions, get one and validate it */
			if ((ret = rpma_conn_completion_wait(conn))) {
				break;
			} else if ((ret = rpma_conn_completion_get(conn,
					&cmpl))) {
				break;
			} else if (cmpl.op_status != IBV_WC_SUCCESS) {

				(void) fprintf(stderr,
					"Shutting down the client due to the unsuccessful completion of an operation.\n");
				ret = -1;
				break;
			}

			if (cmpl.op == RPMA_OP_SEND) {
				send_cmpl = 1;
			} else if (cmpl.op == RPMA_OP_RECV) {
				if (cmpl.op_context != recv ||
						cmpl.byte_len != MSG_SIZE) {
					(void) fprintf(stderr,
						"received completion is not as expected (%p != %p [cmpl.op_context] || %"
						PRIu32
						" != %d [cmpl.byte_len] )\n",
						cmpl.op_context, recv,
						cmpl.byte_len, MSG_SIZE);
					ret = -1;
					break;
				}

				recv_cmpl = 1;
			}
		} while (!send_cmpl || !recv_cmpl);


		/* copy the new value of the counter and print it out */
    struct response_data* resp_data = recv;
    if(resp_data->type != RESPONSE_NORMAL) {
      goto err_conn_disconnect;
    }
		struct common_data dst_data;
		memcpy(&dst_data, &resp_data->data, sizeof(dst_data));
		printf("Value received: %" PRIu64 "\n", dst_data.data_offset);
		printf("received\n");


		mr_size = 1024 * 1024 * 1024; //REQUIRE_SIZE;
	//	mr_size = REQUIRE_SIZE;
		//mr_ptr = malloc_aligned(mr_size);
		mr_ptr = malloc(mr_size);
    printf("mr_size: %lx; mr_ptr: %p\n", mr_size, mr_ptr);
		if (mr_ptr == NULL) return -1;
		char data[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890-\n";
		size_t data_size = strlen(data);
		for(int i = 0; i < mr_size; i+=data_size) {
			memcpy(mr_ptr + i, data, data_size);	
		}
	
        	/* register the memory RDMA write */
        	ret = rpma_mr_reg(peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr);
        	if (ret)
                	goto err_conn_disconnect;

	        /*
	         * Create a remote peer configuration structure from the received
	         * descriptor and apply it to the current connection.
	         */
    bool direct_write_to_pmem = false;
		enum rpma_flush_type flush_type = RPMA_FLUSH_TYPE_VISIBILITY;

    if(dst_data.pcfg_desc_size) {
		  ret = rpma_peer_cfg_from_descriptor(
				&dst_data.descriptors[dst_data.mr_desc_size],
				dst_data.pcfg_desc_size, &pcfg);
		  ret = rpma_peer_cfg_get_direct_write_to_pmem(pcfg, &direct_write_to_pmem);
		  ret |= rpma_conn_apply_remote_peer_cfg(conn, pcfg);
		  (void) rpma_peer_cfg_delete(&pcfg);
		  if (ret)
			  goto err_mr_dereg;
    }

	  /*
    * Create a remote memory registration structure from the received
    * descriptor.
    */
	  ret = rpma_mr_remote_from_descriptor(&dst_data.descriptors[0],
               dst_data.mr_desc_size, &dst_mr);
    if (ret)
      goto err_mr_dereg;

		/* get the remote memory region size */
		ret = rpma_mr_remote_get_size(dst_mr, &dst_size);
	        if (ret) {
        	        goto err_mr_dereg;
          } else if (dst_size < mr_size) {
                	fprintf(stderr,
                                "Remote memory region size too small for writing the data of the assumed size (%zu < %ld)\n",
                        dst_size, mr_size);
                	goto err_conn_disconnect;
        	}

		dst_offset = dst_data.data_offset;
		ret = rpma_write(conn, dst_mr, dst_offset, src_mr,
//                      data_offset, mr_size, RPMA_F_COMPLETION_ALWAYS, NULL);
                	 	data_offset, mr_size, RPMA_F_COMPLETION_ON_ERROR, NULL);


  /* determine the flush type */
  if (direct_write_to_pmem) {
    printf("RPMA_FLUSH_TYPE_PERSISTENT is supported\n");
    flush_type = RPMA_FLUSH_TYPE_PERSISTENT;
  } else {
    printf("RPMA_FLUSH_TYPE_PERSISTENT is NOT supported\n");
    flush_type = RPMA_FLUSH_TYPE_VISIBILITY;
  }


		ret = rpma_flush(conn, dst_mr, dst_offset, mr_size, flush_type, RPMA_F_COMPLETION_ALWAYS, FLUSH_ID);
//		if (ret)
//			goto err_conn_disconnect;

		/* wait for the completion to be ready */
		ret |= rpma_conn_completion_wait(conn);

		ret |= rpma_conn_completion_get(conn, &cmpl);
    printf("%s:%d: op:%d\n", __FILE__, __LINE__, cmpl.op);


	  if (cmpl.op_context != FLUSH_ID) {
	    (void) fprintf(stderr,
	                   "unexpected cmpl.op_context value "
	                   "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
	                   (uintptr_t)cmpl.op_context,
	                   (uintptr_t)FLUSH_ID);
	  }
    if (cmpl.op_status != IBV_WC_SUCCESS)
      printf("failed\n");
    else
      printf("success\n");



		ret = rpma_write(conn, dst_mr, dst_offset+mr_size, src_mr,
                      data_offset, mr_size, RPMA_F_COMPLETION_ON_ERROR, NULL);
		ret = rpma_flush(conn, dst_mr, dst_offset+mr_size, mr_size, flush_type, RPMA_F_COMPLETION_ALWAYS, FLUSH_ID);

		ret |= rpma_conn_completion_wait(conn);
		ret |= rpma_conn_completion_get(conn, &cmpl);
    printf("%s:%d: op:%d\n offset: %ld\n", __FILE__, __LINE__, cmpl.op, mr_size);



	  if (cmpl.op_context != FLUSH_ID) {
	    (void) fprintf(stderr,
	                   "unexpected cmpl.op_context value "
	                   "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
	                   (uintptr_t)cmpl.op_context,
	                   (uintptr_t)FLUSH_ID);
	  }
    if (cmpl.op_status != IBV_WC_SUCCESS)
      printf("failed\n");
    else
      printf("success\n");
  for(int i = 2; i < 10; i++) {
		ret = rpma_write(conn, dst_mr, dst_offset + mr_size * i, src_mr,
                      data_offset, mr_size, RPMA_F_COMPLETION_ALWAYS, NULL);
		ret |= rpma_conn_completion_wait(conn);
		ret |= rpma_conn_completion_get(conn, &cmpl);
    if (cmpl.op_status != IBV_WC_SUCCESS)
      printf("failed\n");
    else
      printf("success\n");
  }



//	}
err_conn_disconnect:
	ret |= common_disconnect_and_wait_for_conn_close(&conn);

err_mr_dereg:
	/* deregister the memory regions */
	ret |= rpma_mr_dereg(&send_mr);
	ret |= rpma_mr_dereg(&recv_mr);
	ret |= rpma_mr_dereg(&src_mr);

err_peer_delete:
	/* delete the peer object */
	ret |= rpma_peer_delete(&peer);

err_mr_free:
	/* free the memory */
	free(send);
	free(recv);
  if(mr_ptr != NULL) free(mr_ptr);

	return ret ? -1 : 0;
}
