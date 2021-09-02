#include <inttypes.h>
#include "include/ceph_assert.h"
#include "include/encoding.h"
#include <rados/librados.hpp>
#include "Types.h"
#include "include/utime.h"

struct cls_rbd_rwlcache_map {
  epoch_t current_cache_id;

  struct Daemon {
    uint64_t id;

    std::string rdma_address;
    int32_t rdma_port;

    uint64_t total_size;
    uint64_t free_size;

    // Primary or other replicated already flush data into osd.
    // So those caches can directly deleted.
    std::set<epoch_t> need_free_caches;

    utime_t expiration;

    struct entity_addr_t daemon_addr;

    Daemon() {
    }

    Daemon(const cls::rbd::RwlCacheDaemonInfo &info,
	   const entity_inst_t &inst) :
      id(info.id),
      rdma_address(info.rdma_address),
      rdma_port(info.rdma_port),
      total_size(info.total_size),
      free_size(info.total_size),
      expiration(ceph_clock_now()),
      daemon_addr(inst.addr) {
    }

    void encode(ceph::buffer::list &bl, uint64_t features) const {
      ENCODE_START(1, 1, bl);
      encode(id, bl);
      encode(rdma_address, bl);
      encode(rdma_port, bl);
      encode(total_size, bl);
      encode(free_size, bl);
      encode(need_free_caches, bl);
      encode(expiration, bl);
      encode(daemon_addr, bl, features);
      ENCODE_FINISH(bl);
    }

    void decode(ceph::buffer::list::const_iterator &it) {
      DECODE_START(1, it);
      decode(id, it);
      decode(rdma_address, it);
      decode(rdma_port, it);
      decode(total_size, it);
      decode(free_size, it);
      decode(need_free_caches, it);
      decode(expiration, it);
      decode(daemon_addr, it);
      DECODE_FINISH(it);
    }
  };

  std::map<uint64_t, struct Daemon> daemons;

  struct Cache {
    epoch_t cache_id;

    // infos of primary
    uint64_t primary_id;
    struct entity_addr_t primary_addr;

    uint64_t cache_size;
    uint32_t copies;

    // unfree space daemons. Before free, daemons.size() == copies
    std::set<uint64_t> daemons;

    Cache() {
    }

    Cache(epoch_t cache_id,
	  const cls::rbd::RwlCacheRequest &req,
	  const struct entity_addr_t &addr,
	  const std::set<uint64_t> &daemons) :
      cache_id(cache_id),
      primary_id(req.id),
      primary_addr(addr),
      cache_size(req.size),
      copies(req.copies),
      daemons(daemons) {
    }

    void encode(ceph::buffer::list &bl, uint64_t features) const {
      ENCODE_START(1, 1, bl);
      encode(cache_id, bl);
      encode(primary_id, bl);
      encode(primary_addr, bl, features);
      encode(cache_size, bl);
      encode(copies, bl);
      encode(daemons, bl);
      ENCODE_FINISH(bl);
    }

    void decode(ceph::buffer::list::const_iterator &it) {
      DECODE_START(1, it);
      decode(cache_id, it);
      decode(primary_id, it);
      decode(primary_addr, it);
      decode(cache_size, it);
      decode(copies, it);
      decode(daemons, it);
      DECODE_FINISH(it);
    }
  };

  // cache allocated but not receive ack in uncommitted_caches
  std::map<epoch_t, std::pair<utime_t, struct Cache>> uncommitted_caches;

  // uncommitted_cache recive ack in time and result is ok. Move cahe from uncommitted_cache
  // to caches.
  std::map<epoch_t, struct Cache> caches;

  // Primary can't free related daemon's cache-file, etc rdma-disconnect.
  // So cache move uncommitted_caches or caches to free_daemon_space_caches;
  std::map<epoch_t, struct Cache> free_daemon_space_caches;


  cls_rbd_rwlcache_map() {
    current_cache_id = 0;
  }

  void encode(ceph::buffer::list& bl, uint64_t features) const {
    ENCODE_START(1, 1, bl);
    encode(current_cache_id, bl);
    encode(daemons, bl, features);
    encode(uncommitted_caches, bl, features);
    encode(caches, bl, features);
    encode(free_daemon_space_caches, bl, features);
    ENCODE_FINISH(bl);
  }

  void decode(ceph::buffer::list::const_iterator &it) {
    DECODE_START(1, it);
    decode(current_cache_id, it);
    decode(daemons, it);
    decode(uncommitted_caches, it);
    decode(caches, it);
    decode(free_daemon_space_caches, it);
    DECODE_FINISH(it);
  }

  void dump(ceph::Formatter *f) const {
    f->open_array_section("librbd rwlcache infos");
    f->close_section();
  }
};
WRITE_CLASS_ENCODER_FEATURES(cls_rbd_rwlcache_map)
WRITE_CLASS_ENCODER_FEATURES(cls_rbd_rwlcache_map::Daemon)
WRITE_CLASS_ENCODER_FEATURES(cls_rbd_rwlcache_map::Cache)

using namespace std;
int main(int argc, char* argv[]) {
  if (argc < 2) {
    exit(0);
  }

  string path = argv[1];


  return 0;
  
}
