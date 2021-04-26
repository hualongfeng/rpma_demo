#ifndef CLIENT_OP
#define CLIENT_OP

#include <librpma.h>
#include <limits.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct __io_buf_client {
  /* RPMA resources */
  struct rpma_peer *peer;
  struct rpma_conn *conn;
  /* resources - memory regions */
  size_t dst_offset;
  size_t dst_used_size;
  size_t dst_size;
  struct rpma_mr_remote* dst_mr;
  void*  send_ptr;
  struct rpma_mr_local* send_mr;
  void*  recv_ptr;
  struct rpma_mr_local* recv_mr;

  enum rpma_op op;
  enum rpma_flush_type flush_type;
  char *_tmpfname;
} RPMA_socket;

RPMA_socket* rpma_socket(const char *addr);

int rpma_socket_connect(RPMA_socket *socket, const char *addr, const char *port);

int rpma_socket_get_remote_descriptor(RPMA_socket *socket, size_t size,
                           enum rpma_op op, char* path);

int rpma_socket_mr_write(RPMA_socket *socket, struct rpma_mr_local *mr, size_t mr_size);
int rpma_socket_mr_write1(RPMA_socket *socket, struct rpma_mr_local *mr, size_t mr_size);

int rpma_socket_write(RPMA_socket *socket, void* mr_ptr, size_t mr_size);

int rpma_socket_mr_flush(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size);
int rpma_socket_mr_flush1(RPMA_socket *socket, struct rpma_mr_local* mr, size_t mr_size);

int rpma_socket_flush(RPMA_socket *socket, void* mr_ptr, size_t mr_size);

int rpma_socket_array_flush(RPMA_socket **sockets, int n, void* mr_ptr, size_t mr_size);
int rpma_socket_array_mr_flush(RPMA_socket **sockets, struct rpma_mr_local** mrs,
                               int n, size_t mr_size);

void rpma_socket_close(RPMA_socket *socket);

#endif //CLIENT_OP
