// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2020, Intel Corporation */

/*
 * server.c -- a server of the messages-ping-pong example
 *
 * Please see README.md for a detailed description of this example.
 */

#include <inttypes.h>
#include <librpma.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <unistd.h>

#define USAGE_STR "usage: %s <server_address> <port>\n"

#include "common-conn.h"
#include "messages-ping-pong-common.h"

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

	/* RPMA resources */
	struct rpma_peer *peer = NULL;
	struct rpma_ep *ep = NULL;
	struct rpma_conn_req *req = NULL;
	enum rpma_conn_event conn_event = RPMA_CONN_UNDEFINED;
	struct rpma_conn *conn = NULL;
	struct rpma_completion cmpl;

	/*
	 * lookup an ibv_context via the address and create a new peer using it
	 */
	if ((ret = server_peer_via_address(addr, &peer)))
		goto err_free;

	/* start a listening endpoint at addr:port */
	if ((ret = rpma_ep_listen(peer, addr, port, &ep)))
		goto err_peer_delete;

	/* register the memory */
	if ((ret = rpma_mr_reg(peer, recv, MSG_SIZE, RPMA_MR_USAGE_RECV,
				&recv_mr)))
		goto err_ep_shutdown;
	if ((ret = rpma_mr_reg(peer, send, MSG_SIZE, RPMA_MR_USAGE_SEND,
				&send_mr))) {
		(void) rpma_mr_dereg(&recv_mr);
		goto err_ep_shutdown;
	}

	/* receive an incoming connection request */
	if ((ret = rpma_ep_next_conn_req(ep, NULL, &req)))
		goto err_mr_dereg;

	/*
	 * Put an initial receive to be prepared for the first message of
	 * the client's ping-pong.
	 */
	if ((ret = rpma_conn_req_recv(req, recv_mr, 0, MSG_SIZE, recv))) {
		(void) rpma_conn_req_delete(&req);
		goto err_mr_dereg;
	}

	/* accept the connection request and obtain the connection object */
	if ((ret = rpma_conn_req_connect(&req, NULL, &conn)))
		goto err_mr_dereg;

	/* wait for the connection to be established */
	if ((ret = rpma_conn_next_event(conn, &conn_event)))
		goto err_conn_disconnect;
	if (conn_event != RPMA_CONN_ESTABLISHED) {
		fprintf(stderr,
				"rpma_conn_next_event returned an unexptected event\n");
		goto err_conn_disconnect;
	}

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
				"operation %d failed: %s\n",
				cmpl.op,
				ibv_wc_status_str(cmpl.op_status));

			ret = -1;
			break;
		}

		if (cmpl.op == RPMA_OP_RECV) {
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
	} while (!recv_cmpl);


	/* print the received old value of the client's counter */
	uint64_t* length = (uint64_t*)recv;
	(void) printf("Alloc memory size: %" PRIu64 "\n", *length);

	/* resources - memory region */
	struct rpma_peer_cfg *pcfg = NULL;
        void *mr_ptr = NULL;
        size_t mr_size = 0;
        size_t data_offset = 0;
        struct rpma_mr_local *mr = NULL;

	mr_ptr = malloc_aligned(*length);
	if (mr_ptr == NULL) return -1;
	mr_size = *length;

	/* register the memory */
	ret = rpma_mr_reg(peer, mr_ptr, mr_size,
                        RPMA_MR_USAGE_WRITE_DST | RPMA_MR_USAGE_FLUSH_TYPE_VISIBILITY, &mr);
	if(ret) {
		printf("register failed");
	} else {
		printf("register success\n");
	}

        /* get size of the memory region's descriptor */
        size_t mr_desc_size;
        ret = rpma_mr_get_descriptor_size(mr, &mr_desc_size);
        if (ret)
                goto err_mr_dereg;
	printf("mr_desc_size: %ld\n", mr_desc_size);

	/* create a peer configuration structure */
	ret = rpma_peer_cfg_new(&pcfg);

	/* get size of the peer config descriptor */
	size_t pcfg_desc_size;
	ret = rpma_peer_cfg_get_descriptor_size(pcfg, &pcfg_desc_size);
        if (ret)
                goto err_mr_dereg;

	printf("pcfg_desc_size: %ld\n", pcfg_desc_size);
	/* calculate data for the client write */
        struct common_data data;
        data.data_offset = data_offset;
        data.mr_desc_size = mr_desc_size;
	data.pcfg_desc_size = pcfg_desc_size;

        /* get the memory region's descriptor */
        ret = rpma_mr_get_descriptor(mr, &data.descriptors[0]);
        if (ret)
                goto err_mr_dereg;

	/* create a peer configuration structure */
	ret = rpma_peer_cfg_new(&pcfg);
	if (ret)
                goto err_mr_dereg;
	/*
         * Get the peer's configuration descriptor.
         * The pcfg_desc descriptor is saved in the `descriptors[]` array
         * just after the mr_desc descriptor.
         */
	ret = rpma_peer_cfg_get_descriptor(pcfg,
                        &data.descriptors[mr_desc_size]);
	if (ret)
		goto err_mr_dereg;

	//copy common_data to send buffer
	memcpy(send, &data, sizeof(data));

	/* send the common_data to the client */
	if ((ret = rpma_send(conn, send_mr, 0, MSG_SIZE,RPMA_F_COMPLETION_ALWAYS, NULL)))
	ret = rpma_conn_completion_wait(conn);
	ret = rpma_conn_completion_get(conn,&cmpl);
	if(!ret) printf("Have finished to send\n");

err_conn_disconnect:
	ret |= common_disconnect_and_wait_for_conn_close(&conn);
	char str[65] = {0};
	str[64] = '\0';
        for(int i = 0; i < (*length); i+=64) {
	    memset(str, 0, 64);
            memcpy(str, mr_ptr+i, 64);
	    printf("%02d:%s\n", i/64, str);
        }

err_mr_dereg:
	/* deregister the memory regions */
	ret |= rpma_mr_dereg(&send_mr);
	ret |= rpma_mr_dereg(&recv_mr);
	ret |= rpma_mr_dereg(&mr);

err_ep_shutdown:
	ret |= rpma_ep_shutdown(&ep);

err_peer_delete:
	/* delete the peer object */
	ret |= rpma_peer_delete(&peer);

err_free:
	free(send);
	free(recv);
	free(mr_ptr);

	return ret ? -1 : 0;
}
