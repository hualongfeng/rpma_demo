#include <librpma.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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
  LOG("connect... addr: %s:%s", addr, port);
  int ret = 0;

  ret = client_connect(socket->peer, addr, port, NULL, &socket->conn);

  ret && LOG("%s", rpma_err_2str(ret));
  return ret;
}

int rpma_socket_get_remote_descriptor(RPMA_socket *socket, size_t size,
                           enum rpma_op op, char* path) {
  int ret = 0;
  LOG("size: %ld, op: %d, path: %s", size, op, path);
  /* prepare a receive for the server's response */
  ret |= rpma_recv(socket->conn, socket->recv_mr, 0, MSG_SIZE, socket->recv_ptr);
  struct require_data* rdata = socket->send_ptr;
  rdata->size = size;
  rdata->op   = op;
  rdata->path_size = strlen(path);
  strncpy(rdata->path, path, MSG_SIZE - sizeof(struct require_data));
  if (ret |= rpma_send(socket->conn, socket->send_mr, 0, 
                       MSG_SIZE, RPMA_F_COMPLETION_ALWAYS, NULL)) {
    LOG("%s", rpma_err_2str(ret));
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

      LOG("unsuccessful completion of an operation.");
      return -1;
    }

    if (cmpl.op == RPMA_OP_SEND) {
      send_cmpl = 1;
    } else if (cmpl.op == RPMA_OP_RECV) {
      if (cmpl.op_context != socket->recv_ptr ||
          cmpl.byte_len != MSG_SIZE) {
        LOG("received completion is not as expected (%p != %p [cmpl.op_context] || %"
            PRIu32
            " != %d [cmpl.byte_len] )",
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
      LOG("%s", rpma_err_2str(ret));
    }
  }

  /*
   * Create a remote memory registration structure from the received
   * descriptor.
   */
  if (ret = rpma_mr_remote_from_descriptor(&dst_data->descriptors[0],
             dst_data->mr_desc_size, &socket->dst_mr)) {
    LOG("%s", rpma_err_2str(ret));
  }

  /* get the remote memory region size */
  if (ret = rpma_mr_remote_get_size(socket->dst_mr, &socket->dst_size)) {
    LOG("%s", rpma_err_2str(ret));
  }

  if (socket->dst_size < size) {
    LOG("%s:%d: Remote memory region size too small for writing the"
    " data of the assumed size (%zu < %ld)",
    __FILE__, __LINE__, socket->dst_size, size);
    return -1;
  }

  socket->dst_offset = dst_data->data_offset;
  socket->op = op;

  /* determine the flush type */
  if (direct_write_to_pmem) {
    LOG("RPMA_FLUSH_TYPE_PERSISTENT is supported");
    socket->flush_type = RPMA_FLUSH_TYPE_PERSISTENT;
  } else {
    LOG("RPMA_FLUSH_TYPE_PERSISTENT is NOT supported");
    socket->flush_type = RPMA_FLUSH_TYPE_VISIBILITY;
  }

  return ret;
}

int rpma_socket_mr_write(RPMA_socket *socket, struct rpma_mr_local *mr, size_t mr_size) {
  assert(socket);
  assert(mr);
  assert(mr_size);
  assert(socket->dst_offset + mr_size <= socket->dst_size);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
  do {
    write_size = remain_size > GIGABYTE ? GIGABYTE : remain_size;
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ALWAYS, NULL);

    ret |= rpma_conn_completion_wait(socket->conn);
    ret |= rpma_conn_completion_get(socket->conn, &cmpl);

    LOG("offset: %ld", data_offset);
    if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op != RPMA_OP_WRITE) {
      LOG("write failed, %s", rpma_err_2str(ret));
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

  LOG("");
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

    LOG("%ld", data_offset);
    socket->dst_offset += write_size;
    data_offset += write_size;
  };

  write_size = remain_size;
  ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                    data_offset, write_size, RPMA_F_COMPLETION_ALWAYS, NULL);

  ret |= rpma_conn_completion_wait(socket->conn);
  ret |= rpma_conn_completion_get(socket->conn, &cmpl);

  if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op != RPMA_OP_WRITE) {
    LOG("write failed: %s", rpma_err_2str(ret));
    return -1;
  }
  socket->dst_offset += write_size;

  return 0;
}

int rpma_socket_write(RPMA_socket *socket, void* mr_ptr, size_t mr_size) {
  assert(socket && mr_ptr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);
  LOG("");

  int ret = 0;
  struct rpma_mr_local *src_mr = NULL;
  /* register the memory RDMA write */
  if (ret = rpma_mr_reg(socket->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr)) {
    LOG("%s", rpma_err_2str(ret));
    return ret;
  }

  ret = rpma_socket_mr_write(socket, src_mr, mr_size);

  rpma_mr_dereg(&src_mr);
  return ret;
}

int rpma_socket_array_mr_pflush(RPMA_socket **sockets, struct rpma_mr_local** mrs, int n,
                                size_t mr_size, size_t offset) {
  assert(sockets
         && n > 0
         && mrs
         && mr_size
         && offset + mr_size <= sockets[0]->dst_size);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
  do {
    write_size = remain_size > GIGABYTE ? GIGABYTE : remain_size;
    remain_size -= write_size;
    for(int i = 0; i < n; i++) {
      ret = rpma_write(sockets[i]->conn, sockets[i]->dst_mr, offset, mrs[i],
                      data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

      ret = rpma_flush(sockets[i]->conn, sockets[i]->dst_mr, offset, write_size,
                     sockets[i]->flush_type, RPMA_F_COMPLETION_ALWAYS, mrs[i]);
    }
    for(int i = 0; i < n; i++) {
      ret |= rpma_conn_completion_wait(sockets[i]->conn);
      ret |= rpma_conn_completion_get(sockets[i]->conn, &cmpl);

      if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op_context != mrs[i]) {
        LOG("write failed, %s\n"
            "unexpected cmpl.op_context value "
            "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
            rpma_err_2str(ret),
            (uintptr_t)cmpl.op_context,
            (uintptr_t)mrs[i]
           );
        return -1;
      }
    }
    offset += write_size;
    data_offset += write_size;
    LOG("%ld", data_offset);
  } while(remain_size != 0);

  LOG("end: %ld", data_offset);
  return 0;
}


int rpma_socket_array_mr_flush(RPMA_socket **sockets, struct rpma_mr_local** mrs,
                               int n, size_t mr_size) {
  assert(sockets
         && n > 0
         && mrs
         && mr_size
         && sockets[0]->dst_offset + mr_size <= sockets[0]->dst_size);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
  do {
    write_size = remain_size > GIGABYTE ? GIGABYTE : remain_size;
    remain_size -= write_size;
    //initiates the write operation
    for (int i = 0; i < n; i++) {
      ret = rpma_write(sockets[i]->conn, sockets[i]->dst_mr, sockets[i]->dst_offset, mrs[i],
                      data_offset, write_size, RPMA_F_COMPLETION_ALWAYS, NULL);
      ret |= rpma_conn_completion_wait(sockets[i]->conn);
      ret |= rpma_conn_completion_get(sockets[i]->conn, &cmpl);
    }
    //wait the write operation is successful
    for (int i = 0; i < 0; i++) {
      ret |= rpma_conn_completion_wait(sockets[i]->conn);
      ret |= rpma_conn_completion_get(sockets[i]->conn, &cmpl);

      if (ret || cmpl.op != RPMA_OP_WRITE || cmpl.op_status != IBV_WC_SUCCESS) {
        LOG("write failed, %s\n"
            "unexpected cmpl.op value (%d != %d)\n"
            "or status with %d\n",
            rpma_err_2str(ret),
            cmpl.op, RPMA_OP_WRITE,
            cmpl.op_status
           );
        return -1;
      }
    }
    LOG("write finished");
    //initiates the flush operation
    for (int i = 0; i < n; i++) {
      ret = rpma_flush(sockets[i]->conn, sockets[i]->dst_mr, sockets[i]->dst_offset, write_size,
                     sockets[i]->flush_type, RPMA_F_COMPLETION_ALWAYS, mrs[i]);
      ret |= rpma_conn_completion_wait(sockets[i]->conn);
      ret |= rpma_conn_completion_get(sockets[i]->conn, &cmpl);
    }
    //wait the flush operation is successful
    for (int i = 0; i < 0; i++) {
      ret |= rpma_conn_completion_wait(sockets[i]->conn);
      ret |= rpma_conn_completion_get(sockets[i]->conn, &cmpl);

      if (ret || cmpl.op_status != IBV_WC_SUCCESS || cmpl.op_context != mrs[i]) {
        LOG("write failed, %s\n"
            "unexpected cmpl.op_context value "
            "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
            rpma_err_2str(ret),
            (uintptr_t)cmpl.op_context,
            (uintptr_t)mrs[i]
           );
        return -1;
      }
    }
    for (int i = 0; i < n; i++) {
      sockets[i]->dst_offset += write_size;
    }
    data_offset += write_size;
    LOG("offset: %ld", data_offset);
  } while(remain_size != 0);

  LOG("end: %ld", data_offset);
  return 0;
}

int rpma_socket_array_flush(RPMA_socket **sockets, int n, void* mr_ptr, size_t mr_size) {
  assert(sockets);
  assert(n > 0);
  assert(mr_ptr);
  assert(mr_size);
  assert(sockets[0]->dst_offset + mr_size <= sockets[0]->dst_size);
  LOG("");

  int ret = 0;
  struct rpma_mr_local **src_mrs = (struct rpma_mr_local **)malloc(sizeof(struct rpma_mr_local*) * n);
  memset(src_mrs, 0, sizeof(struct rpma_mr_local*) * n);
  for(int i = 0; i < n; i++) {

    /* register the memory RDMA write */
    if (ret = rpma_mr_reg(sockets[0]->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mrs[i])) {
      LOG("%s", rpma_err_2str(ret));
      for (int j = 0; j < i; j++) {
        rpma_mr_dereg(&src_mrs[j]);
      }
      return ret;
    }
  }

  ret = rpma_socket_array_mr_flush(sockets, src_mrs, n, mr_size);
  for (int j = 0; j < n; j++) {
    rpma_mr_dereg(&src_mrs[j]);
  }
  free(src_mrs); src_mrs = NULL;

  return ret;
}

int rpma_socket_mr_flush(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size) {
  assert(socket && mr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
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
      LOG("write failed, %s\n"
          "unexpected cmpl.op_context value "
          "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
          rpma_err_2str(ret),
          (uintptr_t)cmpl.op_context,
          (uintptr_t)mr
         );
      return -1;
    }
    socket->dst_offset += write_size;
    data_offset += write_size;
    LOG("%ld", data_offset);
  } while(remain_size != 0);

  LOG("end: %ld", data_offset);
  return 0;
}


int rpma_socket_mr_flush1(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size) {
  assert(socket && mr && mr_size && socket->dst_offset + mr_size <= socket->dst_size);

  int ret = 0;
  struct rpma_completion cmpl;
  size_t remain_size = mr_size;
  size_t write_size = mr_size;
  size_t data_offset = 0;
  LOG("start: %ld", data_offset);
  while(remain_size > GIGABYTE) {
    write_size = GIGABYTE;
    remain_size -= write_size;
    ret = rpma_write(socket->conn, socket->dst_mr, socket->dst_offset, mr,
                      data_offset, write_size, RPMA_F_COMPLETION_ON_ERROR, NULL);

//    ret = rpma_flush(socket->conn, socket->dst_mr, socket->dst_offset, write_size,
//                     socket->flush_type, RPMA_F_COMPLETION_ON_ERROR, mr);
    
    socket->dst_offset += write_size;
    data_offset += write_size;
    LOG("%ld", data_offset);
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
    LOG("write failed, %s\n"
        "unexpected cmpl.op_context value "
        "(0x%" PRIXPTR " != 0x%" PRIXPTR ")\n",
        rpma_err_2str(ret),
        (uintptr_t)cmpl.op_context,
        (uintptr_t)mr
       );
    return -1;
  }
  socket->dst_offset += write_size;
  data_offset += write_size;

  LOG("end: %ld", data_offset);
  return 0;
}


int rpma_socket_flush(RPMA_socket *socket, void* mr_ptr, size_t mr_size) {
  assert(socket);
  assert(mr_ptr);
  assert(mr_size);
  assert(socket->dst_offset + mr_size <= socket->dst_size);
  LOG("");

  int ret = 0;
  struct rpma_mr_local *src_mr = NULL;
  /* register the memory RDMA write */
  if (ret = rpma_mr_reg(socket->peer, mr_ptr, mr_size, RPMA_MR_USAGE_WRITE_SRC,
                                &src_mr)) {
    LOG("%s", rpma_err_2str(ret));
    return ret;
  }

  ret = rpma_socket_mr_flush(socket, src_mr, mr_size);

  rpma_mr_dereg(&src_mr);
  return ret;
}



void rpma_socket_close(RPMA_socket *socket) {
  LOG("");
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
