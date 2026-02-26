#ifdef MT_HWP_ENABLE

#include "mt_hwp.h"
#include <cstring>
#include <cstdlib>

using namespace vortex;

///////////////////////////////////////////////////////////////////////////////
// PrefetchEngine
///////////////////////////////////////////////////////////////////////////////

PrefetchEngine::PrefetchEngine() {
  reset();
}

void PrefetchEngine::reset() {
  for (uint32_t i = 0; i < PWS_TABLE_SIZE; ++i) {
    pws_table_[i].valid = false;
    pws_table_[i].lru_age = 0;
  }
  for (uint32_t i = 0; i < GS_TABLE_SIZE; ++i) {
    gs_table_[i].valid = false;
    gs_table_[i].lru_age = 0;
  }
  for (uint32_t i = 0; i < IP_TABLE_SIZE; ++i) {
    ip_table_[i].valid = false;
    ip_table_[i].lru_age = 0;
    ip_table_[i].obs_count = 0;
  }
}

// GS table lookup (Cycle 1, higher priority)
// @mitul: working now
bool PrefetchEngine::gs_lookup(uint64_t pc, uint64_t addr, uint64_t &out_addr) {
  for (uint32_t i = 0; i < GS_TABLE_SIZE; ++i) {
    auto &e = gs_table_[i];
    if (e.valid && e.pc == pc) {
      out_addr = addr + e.stride;
      for (uint32_t j = 0; j < GS_TABLE_SIZE; ++j) {
        if (gs_table_[j].valid) gs_table_[j].lru_age++;
      }
      e.lru_age = 0;
      return true;
    }
  }
  return false;
}

// IP table lookup + training (Cycle 1, parallel with GS)
// @mitul: working now
bool PrefetchEngine::ip_lookup(uint64_t pc, uint32_t warp_id, uint64_t addr, uint64_t &out_addr) {
  for (uint32_t i = 0; i < IP_TABLE_SIZE; ++i) {
    auto &e = ip_table_[i];
    if (!e.valid || e.pc != pc)
      continue;

    if (e.trained) {
      out_addr = addr + e.stride;
      for (uint32_t j = 0; j < IP_TABLE_SIZE; ++j) {
        if (ip_table_[j].valid) ip_table_[j].lru_age++;
      }
      e.lru_age = 0;
      return true;
    }

    if (e.obs_count < 2) {
      e.wid[e.obs_count]  = warp_id;
      e.addr[e.obs_count] = addr;
      e.obs_count++;
    } else {
      int64_t stride_01 = (int64_t)(e.addr[1] - e.addr[0]);
      int64_t stride_1n = (int64_t)(addr - e.addr[1]);
      if (stride_01 == stride_1n
          && warp_id != e.wid[0]
          && warp_id != e.wid[1]
          && e.wid[0] != e.wid[1]) {
        e.stride  = stride_1n;
        e.trained = true;
      } else {
        e.addr[0] = e.addr[1];
        e.wid[0]  = e.wid[1];
        e.addr[1] = addr;
        e.wid[1]  = warp_id;
      }
    }
    return false;
  }

  // IP miss — allocate new entry
  int slot = -1;
  uint32_t max_age = 0;
  for (uint32_t i = 0; i < IP_TABLE_SIZE; ++i) {
    if (!ip_table_[i].valid) { slot = i; break; }
    if (ip_table_[i].lru_age >= max_age) { max_age = ip_table_[i].lru_age; slot = i; }
  }
  if (slot >= 0) {
    auto &e = ip_table_[slot];
    e.valid     = true;
    e.pc        = pc;
    e.trained   = false;
    e.obs_count = 1;
    e.wid[0]    = warp_id;
    e.addr[0]   = addr;
    e.stride    = 0;
    e.lru_age   = 0;
  }
  return false;
}

// PWS table lookup + stride promotion (Cycle 2)
// @mitul: working now
bool PrefetchEngine::pws_lookup(uint64_t pc, uint32_t warp_id, uint64_t addr, uint64_t &out_addr) {
  for (uint32_t i = 0; i < PWS_TABLE_SIZE; ++i) {
    auto &e = pws_table_[i];
    if (!e.valid || e.pc != pc || e.wid != warp_id)
      continue;

    int64_t delta = (int64_t)(addr - e.last_addr);
    bool result = false;

    if (delta == e.stride && e.stride != 0) {
      e.trained = true;
      out_addr = addr + delta;
      result = true;
    } else {
      e.stride  = delta;
      e.trained = false;
    }
    e.last_addr = addr;

    for (uint32_t j = 0; j < PWS_TABLE_SIZE; ++j) {
      if (pws_table_[j].valid) pws_table_[j].lru_age++;
    }
    e.lru_age = 0;

    if (e.trained) {
      try_stride_promotion(pc, e.stride);
    }

    return result;
  }

  // PWS miss — allocate new entry
  int slot = -1;
  uint32_t max_age = 0;
  for (uint32_t i = 0; i < PWS_TABLE_SIZE; ++i) {
    if (!pws_table_[i].valid) { slot = i; break; }
    if (pws_table_[i].lru_age >= max_age) { max_age = pws_table_[i].lru_age; slot = i; }
  }
  if (slot >= 0) {
    auto &e = pws_table_[slot];
    e.valid     = true;
    e.pc        = pc;
    e.wid       = warp_id;
    e.trained   = false;
    e.last_addr = addr;
    e.stride    = 0;
    e.lru_age   = 0;
  }

  return false;
}

// Stride promotion: if >= GS_PROMOTION_THRESH trained PWS entries
// share the same (PC, stride), promote to GS and remove them
// @mitul: working now
void PrefetchEngine::try_stride_promotion(uint64_t pc, int64_t stride) {
  uint32_t count = 0;
  for (uint32_t i = 0; i < PWS_TABLE_SIZE; ++i) {
    auto &e = pws_table_[i];
    if (e.valid && e.pc == pc && e.trained && e.stride == stride) {
      ++count;
    }
  }
  if (count < GS_PROMOTION_THRESH)
    return;

  int slot = -1;
  uint32_t max_age = 0;
  for (uint32_t i = 0; i < GS_TABLE_SIZE; ++i) {
    if (!gs_table_[i].valid) { slot = i; break; }
    if (gs_table_[i].lru_age >= max_age) { max_age = gs_table_[i].lru_age; slot = i; }
  }
  if (slot >= 0) {
    gs_table_[slot].valid   = true;
    gs_table_[slot].pc      = pc;
    gs_table_[slot].stride  = stride;
    gs_table_[slot].lru_age = 0;
  }

  for (uint32_t i = 0; i < PWS_TABLE_SIZE; ++i) {
    auto &e = pws_table_[i];
    if (e.valid && e.pc == pc && e.trained && e.stride == stride) {
      e.valid = false;
    }
  }
}

// Main entry point
// @mitul: working now
std::vector<uint64_t> PrefetchEngine::on_memory_access(uint64_t pc, uint32_t warp_id, uint64_t addr) {
  uint64_t pf_addr = 0;

  // Cycle 1: GS and IP checked in parallel; GS has higher priority
  bool gs_hit = gs_lookup(pc, addr, pf_addr);
  if (gs_hit) return {pf_addr};

  uint64_t ip_addr = 0;
  bool ip_hit = ip_lookup(pc, warp_id, addr, ip_addr);
  if (ip_hit) return {ip_addr};

  // Cycle 2: PWS lookup (only if GS and IP both missed)
  uint64_t pws_addr = 0;
  bool pws_hit = pws_lookup(pc, warp_id, addr, pws_addr);
  if (pws_hit) return {pws_addr};

  return {};
}

///////////////////////////////////////////////////////////////////////////////
// PrefetchCache
///////////////////////////////////////////////////////////////////////////////

PrefetchCache::PrefetchCache() {
  reset();
}

void PrefetchCache::reset() {
  useful_prefetches = 0;
  early_evictions   = 0;
  total_insertions  = 0;
  inflight_.clear();
  for (auto &set : sets_) {
    for (auto &line : set) {
      line.valid   = false;
      line.tag     = 0;
      line.used    = false;
      line.lru_age = 0;
    }
  }
}

uint32_t PrefetchCache::get_set(uint64_t addr) const {
  return (addr / PREFETCH_CACHE_LINE) % NUM_SETS;
}

uint64_t PrefetchCache::get_tag(uint64_t addr) const {
  return addr / (PREFETCH_CACHE_LINE * NUM_SETS);
}

void PrefetchCache::insert(uint64_t addr) {
  clear_inflight(addr);

  uint32_t s   = get_set(addr);
  uint64_t tag = get_tag(addr);
  auto &set    = sets_[s];

  int slot = -1;
  uint32_t max_age = 0;
  for (uint32_t i = 0; i < PREFETCH_CACHE_WAYS; ++i) {
    if (!set[i].valid) { slot = i; break; }
    if (set[i].lru_age >= max_age) { max_age = set[i].lru_age; slot = i; }
  }

  if (slot >= 0) {
    if (set[slot].valid && !set[slot].used) {
      ++early_evictions;
    }
    set[slot].valid   = true;
    set[slot].tag     = tag;
    set[slot].used    = false;
    set[slot].lru_age = 0;
    for (uint32_t i = 0; i < PREFETCH_CACHE_WAYS; ++i) {
      if ((int)i != slot && set[i].valid) set[i].lru_age++;
    }
  }

  ++total_insertions;
}

bool PrefetchCache::lookup_and_consume(uint64_t addr) {
  uint32_t s   = get_set(addr);
  uint64_t tag = get_tag(addr);
  auto &set    = sets_[s];

  for (uint32_t i = 0; i < PREFETCH_CACHE_WAYS; ++i) {
    if (set[i].valid && set[i].tag == tag) {
      set[i].used = true;
      set[i].valid = false;
      ++useful_prefetches;
      return true;
    }
  }
  return false;
}

bool PrefetchCache::is_inflight(uint64_t addr) const {
  uint64_t line_addr = addr >> 6;
  return inflight_.count(line_addr) > 0;
}

void PrefetchCache::mark_inflight(uint64_t addr) {
  uint64_t line_addr = addr >> 6;
  inflight_.insert(line_addr);
}

void PrefetchCache::clear_inflight(uint64_t addr) {
  uint64_t line_addr = addr >> 6;
  inflight_.erase(line_addr);
}

///////////////////////////////////////////////////////////////////////////////
// ThrottleEngine
///////////////////////////////////////////////////////////////////////////////

ThrottleEngine::ThrottleEngine(const PrefetchCache* pcache)
    : pcache_(pcache) {
  reset();
}

void ThrottleEngine::reset() {
  degree_            = THROTTLE_DEGREE_INIT;
  last_update_cycle_ = 0;
  period_merges_     = 0;
  period_requests_   = 0;
  merge_ratio_prev_  = 0.0;
}

void ThrottleEngine::tick(uint64_t cycle) {
  if (cycle - last_update_cycle_ >= THROTTLE_PERIOD) {
    update_degree();
    last_update_cycle_ = cycle;
  }
}

bool ThrottleEngine::should_prefetch() {
  if (degree_ >= 5) return false;
  if (degree_ == 0) return true;

  double prob = 1.0 - (double)degree_ / 5.0;
  double r = (double)(rand() % 1000) / 1000.0;
  return r < prob;
}

void ThrottleEngine::record_intra_core_merge() {
  ++period_merges_;
}

void ThrottleEngine::record_request() {
  ++period_requests_;
}


// @mitul: working now
void ThrottleEngine::update_degree() {
  if (!pcache_)
    return;

  // Eq. 5 — early eviction rate
  double early_eviction_rate = 0.0;
  if (pcache_->useful_prefetches > 0) {
    early_eviction_rate = (double)pcache_->early_evictions / (double)pcache_->useful_prefetches;
  }

  // Eq. 6 — merge ratio
  double merge_ratio_monitored = 0.0;
  if (period_requests_ > 0) {
    merge_ratio_monitored = (double)period_merges_ / (double)period_requests_;
  }

  // Eq. 8 — smoothed merge ratio
  double merge_ratio_current = (merge_ratio_prev_ + merge_ratio_monitored) / 2.0;

  // Table I decision
  if (early_eviction_rate > 0.02) {
    degree_ = 5;
  } else if (early_eviction_rate >= 0.01) {
    if (degree_ < 5) degree_++;
  } else {
    if (merge_ratio_current > 0.15) {
      if (degree_ > 0) degree_--;
    } else {
      degree_ = 5;
    }
  }

  period_merges_    = 0;
  period_requests_  = 0;
  merge_ratio_prev_ = merge_ratio_current;
}

#endif // MT_HWP_ENABLE
