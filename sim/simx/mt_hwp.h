#pragma once

#ifdef MT_HWP_ENABLE

#include <cstdint>
#include <type_traits>
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <array>
#include "constants.h"

namespace vortex {

///////////////////////////////////////////////////////////////////////////////
// PrefetchEngine: PWS + GS + IP tables
///////////////////////////////////////////////////////////////////////////////

// @mitul: corrected
class PrefetchEngine {
public:
  PrefetchEngine();
  void reset();

  /// Called on every demand load. Returns prefetch addresses (0 or 1).
  std::vector<uint64_t> on_memory_access(uint64_t pc, uint32_t warp_id, uint64_t addr);

private:
  struct PWSEntry {
    uint64_t pc;
    uint32_t wid;
    bool     trained;
    uint64_t last_addr;
    int64_t  stride;
    uint32_t lru_age;
    bool     valid;
  };

  struct GSEntry {
    uint64_t pc;
    int64_t  stride;
    uint32_t lru_age;
    bool     valid;
  };

  struct IPEntry {
    uint64_t pc;
    int64_t  stride;
    bool     trained;
    uint32_t wid[2];
    uint64_t addr[2];
    uint32_t obs_count;
    uint32_t lru_age;
    bool     valid;
  };

  bool gs_lookup(uint64_t pc, uint64_t addr, uint64_t &out_addr);
  bool ip_lookup(uint64_t pc, uint32_t warp_id, uint64_t addr, uint64_t &out_addr);
  bool pws_lookup(uint64_t pc, uint32_t warp_id, uint64_t addr, uint64_t &out_addr);
  void try_stride_promotion(uint64_t pc, int64_t stride);

  PWSEntry pws_table_[PWS_TABLE_SIZE];
  GSEntry  gs_table_[GS_TABLE_SIZE];
  IPEntry  ip_table_[IP_TABLE_SIZE];
};

///////////////////////////////////////////////////////////////////////////////
// PrefetchCache: 16 KB, 8-way set-associative
///////////////////////////////////////////////////////////////////////////////

class PrefetchCache {
public:
  PrefetchCache();
  void reset();

  void insert(uint64_t addr);
  bool lookup_and_consume(uint64_t addr);

  bool is_inflight(uint64_t addr) const;
  void mark_inflight(uint64_t addr);
  void clear_inflight(uint64_t addr);

  // Counters for throttle engine
  uint64_t useful_prefetches;
  uint64_t early_evictions;
  uint64_t total_insertions;

private:
  struct Line {
    bool     valid;
    uint64_t tag;
    bool     used;
    uint32_t lru_age;
  };

  static constexpr uint32_t NUM_SETS = PREFETCH_CACHE_SETS;

  std::array<std::array<Line, PREFETCH_CACHE_WAYS>, NUM_SETS> sets_;
  std::unordered_set<uint64_t> inflight_;

  uint32_t get_set(uint64_t addr) const;
  uint64_t get_tag(uint64_t addr) const;
};

///////////////////////////////////////////////////////////////////////////////
// ThrottleEngine: Adaptive prefetch throttling
///////////////////////////////////////////////////////////////////////////////

class ThrottleEngine {
public:
  explicit ThrottleEngine(const PrefetchCache* pcache);
  void reset();

  void tick(uint64_t cycle);
  bool should_prefetch();
  void record_intra_core_merge();
  void record_request();

private:
  void update_degree();

  const PrefetchCache* pcache_;
  uint32_t degree_;
  uint64_t last_update_cycle_;
  uint64_t period_merges_;
  uint64_t period_requests_;
  double merge_ratio_prev_;
};

} // namespace vortex

#endif // MT_HWP_ENABLE
