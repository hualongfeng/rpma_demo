#include <inttypes.h>
#include <stdbool.h>
#include <librpma.h>
#include <libpmem.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "common-conn.h"
#include "server.h"


int get_descriptor(struct client_res* clnt);
int get_descriptor_for_write(struct client_res* clnt);
int get_descriptor_for_flush(struct client_res* clnt);
int register_mr_to_descriptor(struct client_res* clnt, enum rpma_op op);
int register_cfg_to_descriptor(struct client_res* clnt);


int deal_require(struct client_res* clnt) {
  get_descriptor(clnt);

  /* prepare a receive for the client's response */
  rpma_recv(clnt->conn, clnt->recv_mr, 0, MSG_SIZE, clnt->recv_ptr);

  /* send the common_data to the client */
  rpma_send(clnt->conn, clnt->send_mr, 0, MSG_SIZE,RPMA_F_COMPLETION_ALWAYS, NULL);

  return 0;
}

int get_descriptor(struct client_res* clnt) {
  struct require_data* data = (struct require_data*)(clnt->recv_ptr);
  int ret = 0;
  switch (data->op) {
    case RPMA_OP_WRITE:
          ret = get_descriptor_for_write(clnt);
          printf("RPMA_OP_WRITE\n");
          break; 
    case RPMA_OP_FLUSH:
          ret = get_descriptor_for_flush(clnt);
          printf("RPMA_OP_FLUSH\n");
          break;
    case RPMA_OP_READ:
         // ret = get_descriptor_for_read(clnt);
         // break;
    default:
          fprintf(stdout, "the op:%d isn't supported now", data->op);
          ret = -1;
  }
  return ret;
}
int get_memory_or_pmem(struct client_res* clnt) {
  const struct server_res *svr = clnt->svr;
  int ret = 0;
  struct response_data* rdata = clnt->send_ptr;

  /* print the received old value of the client's counter */
  struct require_data* data = (struct require_data*)(clnt->recv_ptr);
  uint64_t length = data->size;
  (void) printf("Alloc memory size: %" PRIu64 "\n", length);
  char *path = data->path;

  /* resources - memory region */
  size_t mr_size = 0;

  clnt->is_pmem = 0;
#ifdef USE_LIBPMEM
//  truncate(path, length);
//  clnt->dst_ptr = pmem_map_file(path, length, PMEM_FILE_CREATE | PMEM_FILE_SPARSE, 0600, &(clnt->dst_size), &(clnt->is_pmem));
  if (access(path, F_OK) != -1) {
    clnt->dst_ptr = pmem_map_file(path, 0, 0, 0600, &(clnt->dst_size), &(clnt->is_pmem));
  }
  else {
    clnt->dst_ptr = pmem_map_file(path, length, PMEM_FILE_CREATE, 0600, &(clnt->dst_size), &(clnt->is_pmem));
  }
  printf("is_pmem: %d; size: %ld; ptr: %p\n", clnt->is_pmem, clnt->dst_size, clnt->dst_ptr);
  printf("path: %s\n", path);

  if (!clnt->is_pmem || clnt->dst_size != length || clnt->dst_ptr == NULL) {
    printf("pmem_map failed");
    if (!clnt->is_pmem) {
      rdata->type |= RESPONSE_NOT_PMEM;
    }
    if (clnt->dst_size < length) {
      rdata->type |= RESPONSE_NOT_ENOUGH_SPACE;
    }
    if (clnt->dst_ptr == NULL) {
      rdata->type |= RESPONSE_ERROR;
    }

    if (clnt->dst_ptr) pmem_unmap(clnt->dst_ptr, length);
    clnt->dst_ptr  = NULL;
    clnt->dst_size = 0;
    return -1;
  } 
  printf("pmem_map success\n");
#else
  printf("using malloc\n");
  clnt->dst_ptr = malloc_aligned(length);
  if (clnt->dst_ptr == NULL) {
    rdata->type |= RESPONSE_MALLOC_FAILED;
    return -1;
  }
  clnt->dst_size = length;
#endif
  return ret;
}

int get_descriptor_for_write(struct client_res* clnt) {
  if (clnt->dst_ptr == NULL)
    get_memory_or_pmem(clnt);
  return register_mr_to_descriptor(clnt, RPMA_OP_WRITE);
}

int get_descriptor_for_flush(struct client_res* clnt) {
  int ret = 0;
  if (clnt->dst_ptr == NULL)
    ret = get_memory_or_pmem(clnt);
  ret |= register_mr_to_descriptor(clnt, RPMA_OP_FLUSH);

  ret |= register_cfg_to_descriptor(clnt);
  return ret;
}


int register_mr_to_descriptor(struct client_res* clnt, enum rpma_op op) {
  const struct server_res *svr = clnt->svr;
  int ret = 0;

  int usage = 0;
  switch (op) {
    case RPMA_OP_FLUSH:
      usage |= (clnt->is_pmem ? RPMA_MR_USAGE_FLUSH_TYPE_PERSISTENT : RPMA_MR_USAGE_FLUSH_TYPE_VISIBILITY);
      // don't have break
    case RPMA_OP_WRITE:
      usage |= RPMA_MR_USAGE_WRITE_DST;
      break;
    case RPMA_OP_READ:
      usage |= RPMA_MR_USAGE_READ_SRC;
      break;
    default:
      printf("%s:%d: Warn: Don't step in this\n", __FILE__, __LINE__);
      break;
  }

  /* register the memory */
  if ((ret = rpma_mr_reg(svr->peer, clnt->dst_ptr, clnt->dst_size,
        usage, &clnt->dst_mr))) {
    free(clnt->dst_ptr);
    clnt->dst_ptr = NULL;
    return ret;
  }

  /* get size of the memory region's descriptor */
  size_t mr_desc_size;
  ret = rpma_mr_get_descriptor_size(clnt->dst_mr, &mr_desc_size);

  /* calculate data for the client write */
  struct response_data* rdata = clnt->send_ptr;
  rdata->type = RESPONSE_NORMAL;
  struct common_data *data = &rdata->data;
  data->data_offset = 0;
  data->mr_desc_size = mr_desc_size;
  data->pcfg_desc_size = 0;

  /* get the memory region's descriptor */
  rpma_mr_get_descriptor(clnt->dst_mr, &data->descriptors[0]);
  return 0;
}

// only used to flush
int register_cfg_to_descriptor(struct client_res* clnt) {
  const struct server_res *svr = clnt->svr;
  int ret = 0;

  /* resources - memory region */
  struct rpma_peer_cfg *pcfg = NULL;

  /* create a peer configuration structure */
  ret |= rpma_peer_cfg_new(&pcfg);

#ifdef USE_LIBPMEM
  /* configure peer's direct write to pmem support */
  ret = rpma_peer_cfg_set_direct_write_to_pmem(pcfg, true);
  if (ret) {
    (void) rpma_peer_cfg_delete(&pcfg);
    return ret;
  }
#endif /* USE_LIBPMEM */

  /* get size of the peer config descriptor */
  size_t pcfg_desc_size;
  ret |= rpma_peer_cfg_get_descriptor_size(pcfg, &pcfg_desc_size);

  /* calculate data for the client write */
  struct response_data* rdata = clnt->send_ptr;
  struct common_data *data = &rdata->data;
  data->pcfg_desc_size = pcfg_desc_size;

  /*
   * Get the peer's configuration descriptor.
   * The pcfg_desc descriptor is saved in the `descriptors[]` array
   * just after the mr_desc descriptor.
   */
  rpma_peer_cfg_get_descriptor(pcfg, &data->descriptors[data->mr_desc_size]);

  (void) rpma_peer_cfg_delete(&pcfg);

  return 0;
}
