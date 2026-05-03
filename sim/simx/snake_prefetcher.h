#pragma once

#ifdef SNAKE_PREFETCH_ENABLE

#include <cstdint>
#include <vector>
#include "constants.h"

namespace vortex {

// Snake: Variable-Length Chain-Based Prefetching for GPUs (MICRO '23, Mostofi et al.)
//
// Head Table (per-warp): stores the last (PC, addr) seen by each warp.
// Tail Table (fully-associative): stores (PC1→PC2, stride) chains with a set of
//   confirming warp IDs. When SNAKE_TRAIN_THRESH distinct warps confirm the same
//   chain, it is promoted to "trained" and prefetches are issued on every match.
class SnakePrefetcher {
public:
  SnakePrefetcher();
  void reset();

  // Called on every demand load. Returns cache-line-aligned prefetch addresses.
  std::vector<uint64_t> on_memory_access(uint64_t pc, uint32_t warp_id, uint64_t addr);

  uint64_t prefetches_issued;

private:
  struct HeadEntry {
    bool     valid;
    uint64_t pc;
    uint64_t addr;
  };

  struct TailEntry {
    bool     valid;
    uint64_t pc1;                   // previous load PC (chain head)
    uint64_t pc2;                   // current load PC (chain tail)
    int64_t  stride;                // addr2 - addr1 (inter-thread stride)
    bool     trained;
    uint32_t wid_count;             // number of distinct warps that confirmed this chain
    uint32_t wids[SNAKE_MAX_WIDS];  // warp IDs that confirmed this chain
    uint32_t lru_age;
  };

  HeadEntry head_table_[SNAKE_HEAD_ENTRIES];
  TailEntry tail_table_[SNAKE_TAIL_ENTRIES];

  int  tail_lookup(uint64_t pc1, uint64_t pc2, int64_t stride) const;
  int  tail_find_victim() const;
  void tail_lru_update(int used_slot);
};

} // namespace vortex

#endif // SNAKE_PREFETCH_ENABLE
