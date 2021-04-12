#include <librpma.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <assert.h>

#include "common-conn.h"
#include "messages-ping-pong-common.h"
#include "client_op.h"
#include "log.h"

RPMA_socket* rpma_socket(const char *addr) {
  RPMA_socket *socket = malloc(sizeof(RPMA_socket));
  int ret = 0;
  ret = client_peer_via_address(addr, &socket->peer);
  if(ret != 0) {
    goto err_peer_delete;
  }

  socket->recv_ptr = malloc_aligned(MSG_SIZE);
  socket->send_ptr = malloc_aligned(MSG_SIZE);
  if(socket->recv_ptr == NULL || socket->send_ptr == NULL) {
    goto err_malloc_free;
  }

  if (ret = rpma_mr_reg(socket->peer, socket->recv_ptr,
                        MSG_SIZE, RPMA_MR_USAGE_RECV, &socket->recv_mr)) {
    goto err_malloc_free;
  }

  if ((ret = rpma_mr_reg(socket->peer, socket->send_ptr,
                         MSG_SIZE, RPMA_MR_USAGE_SEND, &socket->send_mr))) {
    rpma_mr_dereg(&socket->recv_mr);
    goto err_malloc_free;
  }

  socket->dst_offset    = 0;
  socket->dst_used_size = 0;
  socket->dst_size      = 0;
  socket->dst_mr        = NULL;
  socket->_tmpfname     = NULL;
  return socket;

err_malloc_free:
    if(socket->recv_ptr) free(socket->recv_ptr);
    if(socket->send_ptr) free(socket->send_ptr);
    rpma_peer_delete(&socket->peer);
err_peer_delete:
    free(socket);
    return NULL;

}

int rpma_socket_connect(RPMA_socket *socket, const char *addr, const char *port) {
  int ret = 0;
  ret = client_connect(socket->peer, addr, port, NULL, &socket->conn);
  
  ret && fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
  return ret;
}

int rpma_socket_get_remote_descriptor(RPMA_socket *socket, size_t size,
                           enum rpma_op op, char* path) {
  int ret = 0;
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);
  /* prepare a receive for the server's response */
  ret |= rpma_recv(socket->conn, socket->recv_mr, 0, MSG_SIZE, socket->recv_ptr);
  struct require_data* rdata = socket->send_ptr;
  rdata->size = size;
  rdata->op   = op;
  rdata->path_size = strlen(path);
  strncpy(rdata->path, path, MSG_SIZE - sizeof(struct require_data));
  if (ret |= rpma_send(socket->conn, socket->send_mr, 0, 
                       MSG_SIZE, RPMA_F_COMPLETION_ALWAYS, NULL)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
    return -1;
  }

  socket->_tmpfname = (char*)malloc(rdata->path_size + 1);
  if (socket->_tmpfname) {
    strcpy(socket->_tmpfname, path);
  }

  struct rpma_completion cmpl;
  int send_cmpl = 0;
  int recv_cmpl = 0;

  do {
    /* prepare completions, get one and validate it */
    if ((ret = rpma_conn_completion_wait(socket->conn))) {
      break;
    } else if ((ret = rpma_conn_completion_get(socket->conn,
        &cmpl))) {
      break;
    } else if (cmpl.op_status != IBV_WC_SUCCESS) {

      fprintf(stderr, "unsuccessful completion of an operation.\n");
      return -1;
    }

    if (cmpl.op == RPMA_OP_SEND) {
      send_cmpl = 1;
    } else if (cmpl.op == RPMA_OP_RECV) {
      if (cmpl.op_context != socket->recv_ptr ||
          cmpl.byte_len != MSG_SIZE) {
        (void) fprintf(stderr,
          "received completion is not as expected (%p != %p [cmpl.op_context] || %"
          PRIu32
          " != %d [cmpl.byte_len] )\n",
          cmpl.op_context, socket->recv_ptr,
          cmpl.byte_len, MSG_SIZE);
        return -1;
      }
      recv_cmpl = 1;
    }
  } while (!send_cmpl || !recv_cmpl);

  /* copy the new value of the counter and print it out */
  struct response_data* resp_data = socket->recv_ptr;
  if (resp_data->type != RESPONSE_NORMAL) return -resp_data->type;
  struct common_data *dst_data = &resp_data->data;

  /*
   * Create a remote peer configuration structure from the received
   * descriptor and apply it to the current connection.
   */
  bool direct_write_to_pmem = false;
  struct rpma_peer_cfg *pcfg = NULL;
  if (dst_data->pcfg_desc_size) {
    ret = rpma_peer_cfg_from_descriptor(&dst_data->descriptors[dst_data->mr_desc_size],
      dst_data->pcfg_desc_size, &pcfg);
    ret = rpma_peer_cfg_get_direct_write_to_pmem(pcfg, &direct_write_to_pmem);
    ret |= rpma_conn_apply_remote_peer_cfg(socket->conn, pcfg);
    (void) rpma_peer_cfg_delete(&pcfg);
    if (ret) {
      fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
    }
  }

  /*
   * Create a remote memory registration structure from the received
   * descriptor.
   */
  if (ret = rpma_mr_remote_from_descriptor(&dst_data->descriptors[0],
             dst_data->mr_desc_size, &socket->dst_mr)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
  }

  /* get the remote memory region size */
  if (ret = rpma_mr_remote_get_size(socket->dst_mr, &socket->dst_size)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
  }

  if (socket->dst_size < size) {
    fprintf(stderr, 
    "%s:%d: Remote memory region size too small for writing the"
    " data of the assumed size (%zu < %ld)\n",
    __FILE__, __LINE__, socket->dst_size, size);
    return -1;
  }

  socket->dst_offset = dst_data->data_offset;
  socket->op = op;

  /* determine the flush type */
  if (direct_write_to_pmem) {
    printf("RPMA_FLUSH_TYPE_PERSISTENT is supported\n");
    socket->flush_type = RPMA_FLUSH_TYPE_PERSISTENT;
  } else {
    printf("RPMA_FLUSH_TYPE_PERSISTENT is NOT supported\n");
    socket->flush_type = RPMA_FLUSH_TYPE_VISIBILITY;
  }

  return ret;
}

int rpma_socket_mr_write(RPMA_socket *socket, struct rpma_mr_local *mr, size_t mr_size) {
  assert(socket);
  assert(mr);
  assert(mr_size);
  assert(socket->dst_offset + mr_size <= socket->dst_size);
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
  do {
//    write_size = remain_size > GIGABYTE ? GIGABYTE : remain_size;
    write_size = remain_size > (MEGABYTE << 2) ? (MEGABYTE << 2) : remain_size;
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ALWAYS, NULL);

    ret |= rpma_conn_completion_wait(socket->conn);
    ret |= rpma_conn_completion_get(socket->conn, &cmpl);

    LOG("%ld", data_offset);
    if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op != RPMA_OP_WRITE) {
      fprintf(stderr, "%s:%d: write failed, %s", __FILE__, __LINE__, rpma_err_2str(ret));
      return -1;
    }
    socket->dst_offset += write_size;
    data_offset += write_size;
  } while(remain_size != 0);

  LOG("end: %ld", data_offset);
  return 0;
}


int rpma_socket_mr_write1(RPMA_socket *socket, struct rpma_mr_local *mr, size_t mr_size) {
  assert(socket && mr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);

  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);
  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;

//  while(remain_size > GIGABYTE) {
  while(remain_size > (MEGABYTE << 2)) {
//    write_size = GIGABYTE;
    write_size = (MEGABYTE << 2);
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

    printf("%s:%d: %s %ld\n", __FILE__, __LINE__, __FUNCTION__, data_offset);
    socket->dst_offset += write_size;
    data_offset += write_size;
  };

  write_size = remain_size;
  ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                    data_offset, write_size, RPMA_F_COMPLETION_ALWAYS, NULL);

  ret |= rpma_conn_completion_wait(socket->conn);
  ret |= rpma_conn_completion_get(socket->conn, &cmpl);

  if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op != RPMA_OP_WRITE) {
    fprintf(stderr, "%s:%d: write failed, %s", __FILE__, __LINE__, rpma_err_2str(ret));
    return -1;
  }
  socket->dst_offset += write_size;

  return 0;
}

int rpma_socket_write(RPMA_socket *socket, void* mr_ptr, size_t mr_size) {
  assert(socket && mr_ptr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);

  int ret = 0;
  struct rpma_mr_local *src_mr = NULL;
  /* register the memory RDMA write */
  if (ret = rpma_mr_reg(socket->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
    return ret;
  }

  ret = rpma_socket_mr_write1(socket, src_mr, mr_size);

  rpma_mr_dereg(&src_mr);
  return ret;
}

int rpma_socket_mr_flush(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size) {
  assert(socket && mr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  do {
    write_size = remain_size > GIGABYTE ? GIGABYTE : remain_size;
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

    ret = rpma_flush(socket->conn, socket->dst_mr, socket->dst_offset, write_size,
                     socket->flush_type, RPMA_F_COMPLETION_ALWAYS, mr);
    ret |= rpma_conn_completion_wait(socket->conn);
    ret |= rpma_conn_completion_get(socket->conn, &cmpl);

    if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op_context != mr) {
      fprintf(stderr, "%s:%d: write failed, %s\n"
                      "unexpected cmpl.op_context value "
                      "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
                      __FILE__, __LINE__, rpma_err_2str(ret),
                      (uintptr_t)cmpl.op_context,
                      (uintptr_t)mr
                      );
      return -1;
    }
    socket->dst_offset += write_size;
    data_offset += write_size;
  } while(remain_size != 0);

  return 0;
}


int rpma_socket_mr_flush1(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size) {
  assert(socket && mr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  while(remain_size > GIGABYTE) {
    write_size = GIGABYTE;
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

    ret = rpma_flush(socket->conn, socket->dst_mr, socket->dst_offset, write_size,
                     socket->flush_type, RPMA_F_COMPLETION_ON_ERROR, mr);
    
    socket->dst_offset += write_size;
    data_offset += write_size;
  };

  write_size = remain_size;
//  remain_size = 0;
  ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                    data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

  ret = rpma_flush(socket->conn, socket->dst_mr, socket->dst_offset, write_size,
                   socket->flush_type, RPMA_F_COMPLETION_ALWAYS, mr);
  ret |= rpma_conn_completion_wait(socket->conn);
  ret |= rpma_conn_completion_get(socket->conn, &cmpl);

  if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op_context != mr) {
    fprintf(stderr, "%s:%d: write failed, %s\n"
                    "unexpected cmpl.op_context value "
                    "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
                    __FILE__, __LINE__, rpma_err_2str(ret),
                    (uintptr_t)cmpl.op_context,
                    (uintptr_t)mr
                    );
    return -1;
  }
  socket->dst_offset += write_size;
//  data_offset += write_size;

  return 0;
}


int rpma_socket_flush(RPMA_socket *socket, void* mr_ptr, size_t mr_size) {
  assert(socket);
  assert(mr_ptr);
  assert(mr_size);
  assert(socket->dst_offset + mr_size <= socket->dst_size);
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);

  int ret = 0;
  struct rpma_mr_local *src_mr = NULL;
  /* register the memory RDMA write */
  if (ret = rpma_mr_reg(socket->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr)) {
    fprintf(stderr, "%s:%d: %s", __FILE__, __LINE__, rpma_err_2str(ret));
    return ret;
  }

  ret = rpma_socket_mr_flush1(socket, src_mr, mr_size);

  rpma_mr_dereg(&src_mr);
  return ret;
}



void rpma_socket_close(RPMA_socket *socket) {
  printf("%s:%d: %s\n", __FILE__, __LINE__, __FUNCTION__);
  if (socket->recv_ptr) {
    rpma_mr_dereg(&socket->recv_mr);
    free(socket->recv_ptr);
  }
  if (socket->send_ptr) {
    rpma_mr_dereg(&socket->send_mr);
    free(socket->send_ptr);
  }

  if (socket->_tmpfname) {
    free(socket->_tmpfname);
    socket->_tmpfname = NULL;
  }

  common_disconnect_and_wait_for_conn_close(&socket->conn);
  rpma_peer_delete(&socket->peer);

  free(socket);
}
