#include "sld_prefetcher.h"

#ifdef ORCHESTRATED_PREFETCH_ENABLE

#include <cassert>

namespace vortex {

SLDPrefetcher::SLDPrefetcher()
  : prefetches_issued(0)
  , macroblock_hits(0)
  , table_(SLD_TABLE_SIZE)
  , lru_counter_(0)
{
  // reset() will also zero the counters...
  for (auto& e : table_) {
    e.valid          = false;
    e.bitvector      = 0;
    e.lru_age        = 0;
    e.macroblock_tag = 0;
  }
}

void SLDPrefetcher::reset() {
  for (auto& e : table_) {
    e.valid         = false;
    e.bitvector     = 0;
    e.lru_age       = 0;
    e.macroblock_tag = 0;
  }
  lru_counter_      = 0;
  prefetches_issued = 0;
  macroblock_hits   = 0;
}

int SLDPrefetcher::lookup(uint64_t macroblock_tag) const {
  for (uint32_t i = 0; i < SLD_TABLE_SIZE; ++i) {
    if (table_[i].valid && table_[i].macroblock_tag == macroblock_tag)
      return (int)i;
  }
  return -1;
}

int SLDPrefetcher::find_victim() const {
  // Prefer invalid entries first
  for (uint32_t i = 0; i < SLD_TABLE_SIZE; ++i) {
    if (!table_[i].valid)
      return (int)i;
  }
  // Otherwise evict the LRU entry
  int victim = 0;
  uint32_t oldest = table_[0].lru_age;
  for (uint32_t i = 1; i < SLD_TABLE_SIZE; ++i) {
    if (table_[i].lru_age < oldest) {
      oldest = table_[i].lru_age;
      victim = (int)i;
    }
  }
  return victim;
}

std::vector<uint64_t> SLDPrefetcher::on_cache_miss(uint64_t addr) {
  uint64_t macroblock_tag = addr >> SLD_MACROBLOCK_SHIFT;
  uint32_t line_index     = (addr >> SLD_LINE_SHIFT) & (SLD_LINES_PER_MACRO - 1);

  int idx = lookup(macroblock_tag);
  if (idx == -1) {
    // Allocate a new entry (evicting LRU if necessary)
    idx = find_victim();
    table_[idx].valid         = true;
    table_[idx].macroblock_tag = macroblock_tag;
    table_[idx].bitvector     = 0;
  } else {
    ++macroblock_hits;
  }

  // Record this cache-line access
  table_[idx].bitvector |= (1u << line_index);
  table_[idx].lru_age    = ++lru_counter_;

  // Issue prefetches once the coverage threshold is reached
  std::vector<uint64_t> result;
  uint32_t bv = table_[idx].bitvector;
  if (__builtin_popcount(bv) >= (int)SLD_COVERAGE_THRESH) {
    uint64_t macro_base = macroblock_tag << SLD_MACROBLOCK_SHIFT;
    for (uint32_t bit = 0; bit < SLD_LINES_PER_MACRO; ++bit) {
      if (!(bv & (1u << bit))) {
        uint64_t pf_addr = macro_base | (static_cast<uint64_t>(bit) << SLD_LINE_SHIFT);
        result.push_back(pf_addr);
        ++prefetches_issued;
        // @mitul: added mark bit... so we don't re-prefetch the same line for this macro-block
        table_[idx].bitvector |= (1u << bit);
      }
    }
  }

  return result;
}

} // namespace vortex

#endif // ORCHESTRATED_PREFETCH_ENABLE
