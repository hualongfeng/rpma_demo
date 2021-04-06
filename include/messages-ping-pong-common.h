/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright 2020, Intel Corporation */

/*
 * messages-ping-pong-common.h -- a common declarations for the 08 example
 */

#ifndef EXAMPLES_MSG_PING_PONG_COMMON
#define EXAMPLES_MSG_PING_PONG_COMMON

#define MSG_SIZE 4096

/* Both buffers are allocated one after another. */
#define RECV_OFFSET	0
#define SEND_OFFSET	MSG_SIZE

#endif /* EXAMPLES_MSG_PING_PONG_COMMON */
