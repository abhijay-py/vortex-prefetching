#pragma once

#ifdef ORCHESTRATED_PREFETCH_ENABLE

#include <cstdint>
#include <vector>
#include "constants.h"

namespace vortex {

// Tracks cache line accesses within 256B macro-blocks (4 x 64B lines).
// When >= SLD_COVERAGE_THRESH lines in a macro-block are accessed,
// prefetches the remaining unaccessed lines into the L1 dcache.
class SLDPrefetcher {
public:
  SLDPrefetcher();

  void reset();

  // Called on every demand load address.
  // Returns a vector of cache-line-aligned prefetch addresses.
  std::vector<uint64_t> on_cache_miss(uint64_t addr);

  uint64_t prefetches_issued;
  uint64_t macroblock_hits;

private:
  struct SLDEntry {
    uint64_t macroblock_tag;
    uint32_t bitvector;   // one bit per cache line in the macro-block
    uint32_t lru_age;
    bool     valid;
  };

  std::vector<SLDEntry> table_;
  uint32_t lru_counter_;

  int lookup(uint64_t macroblock_tag) const;
  int find_victim() const;
};

} // namespace vortex

#endif // ORCHESTRATED_PREFETCH_ENABLE
