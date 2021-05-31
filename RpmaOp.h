#ifndef _RPMA_OP_H_
#define _RPMA_OP_H_

#include <functional>
#include <librpma.h>
#include <memory>


class RpmaOp {
  std::function<void()> func;
public:
  RpmaOp(std::function<void()> f): func(f) {}
  void do_callback() { func(); }
  virtual ~RpmaOp() { std::cout << "I'm in Rpma::~Rpma()" << std::endl; };
};

class RpmaRecv : public RpmaOp {
  std::function<void()> func;
public:
  RpmaRecv(std::function<void()> f): RpmaOp(f) {}
  ~RpmaRecv() {}

  int operator() (struct rpma_conn *conn, struct rpma_mr_local *dst, size_t offset, size_t len, const void *op_context) {
    return rpma_recv(conn, dst, offset, len, op_context);
  }
};

class RpmaReqRecv : public RpmaOp{
  std::function<void()> func;
public:
  RpmaReqRecv(std::function<void()> f): RpmaOp(f) {}
  ~RpmaReqRecv() {}

  int operator() (struct rpma_conn_req *req, struct rpma_mr_local *dst, size_t offset, size_t len, const void *op_context) {
    return rpma_conn_req_recv(req, dst, offset, len, op_context);
  }
};


#endif //_RPMA_OP_H_