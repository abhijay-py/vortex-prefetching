#ifdef SNAKE_PREFETCH_ENABLE

#include "snake_prefetcher.h"
#include <cstring>

using namespace vortex;

SnakePrefetcher::SnakePrefetcher() {
  reset();
}

void SnakePrefetcher::reset() {
  prefetches_issued = 0;
  for (uint32_t i = 0; i < SNAKE_HEAD_ENTRIES; ++i) {
    head_table_[i].valid = false;
    head_table_[i].pc    = 0;
    head_table_[i].addr  = 0;
  }
  for (uint32_t i = 0; i < SNAKE_TAIL_ENTRIES; ++i) {
    tail_table_[i].valid     = false;
    tail_table_[i].wid_count = 0;
    tail_table_[i].lru_age   = 0;
  }
}

// Returns the tail table slot matching (pc1, pc2, stride), or -1 if not found.
int SnakePrefetcher::tail_lookup(uint64_t pc1, uint64_t pc2, int64_t stride) const {
  for (uint32_t i = 0; i < SNAKE_TAIL_ENTRIES; ++i) {
    const auto& e = tail_table_[i];
    if (e.valid && e.pc1 == pc1 && e.pc2 == pc2 && e.stride == stride)
      return (int)i;
  }
  return -1;
}

// Returns an invalid slot if one exists, otherwise the LRU valid slot.
int SnakePrefetcher::tail_find_victim() const {
  for (uint32_t i = 0; i < SNAKE_TAIL_ENTRIES; ++i) {
    if (!tail_table_[i].valid) return (int)i;
  }
  int oldest = 0;
  uint32_t max_age = 0;
  for (uint32_t i = 0; i < SNAKE_TAIL_ENTRIES; ++i) {
    if (tail_table_[i].lru_age >= max_age) {
      max_age = tail_table_[i].lru_age;
      oldest  = (int)i;
    }
  }
  return oldest;
}

// Ages all valid entries except used_slot; resets used_slot to age 0.
void SnakePrefetcher::tail_lru_update(int used_slot) {
  for (uint32_t i = 0; i < SNAKE_TAIL_ENTRIES; ++i) {
    if (tail_table_[i].valid && (int)i != used_slot)
      tail_table_[i].lru_age++;
  }
  tail_table_[used_slot].lru_age = 0;
}

std::vector<uint64_t> SnakePrefetcher::on_memory_access(uint64_t pc, uint32_t warp_id, uint64_t addr) {
  std::vector<uint64_t> results;

  uint32_t head_idx = warp_id % SNAKE_HEAD_ENTRIES;
  auto& head = head_table_[head_idx];

  if (head.valid) {
    int64_t stride = (int64_t)(addr - head.addr);

    int slot = tail_lookup(head.pc, pc, stride);
    if (slot >= 0) {
      auto& te = tail_table_[slot];

      if (!te.trained) {
        // Record this warp if it hasn't confirmed the pattern yet.
        bool seen = false;
        for (uint32_t i = 0; i < te.wid_count; ++i) {
          if (te.wids[i] == warp_id) { seen = true; break; }
        }
        if (!seen && te.wid_count < SNAKE_MAX_WIDS) {
          te.wids[te.wid_count++] = warp_id;
          if (te.wid_count >= SNAKE_TRAIN_THRESH)
            te.trained = true;
        }
      }

      if (te.trained) {
        // Prefetch the next line in the chain.
        uint64_t pf_addr = (uint64_t)((int64_t)addr + stride) & ~uint64_t(63);
        results.push_back(pf_addr);
      }

      tail_lru_update(slot);
    } else {
      // No matching chain — allocate a new tail entry for this (PC1→PC2, stride) pair.
      int victim = tail_find_victim();
      auto& te = tail_table_[victim];
      te.valid     = true;
      te.pc1       = head.pc;
      te.pc2       = pc;
      te.stride    = stride;
      te.trained   = false;
      te.wid_count = 1;
      te.wids[0]   = warp_id;
      tail_lru_update(victim);
    }
  }

  // Advance the head entry for this warp.
  head.valid = true;
  head.pc    = pc;
  head.addr  = addr;

  return results;
}

#endif // SNAKE_PREFETCH_ENABLE
