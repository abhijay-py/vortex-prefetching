// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <VX_config.h>
#include <bitmanip.h>

#ifndef RAM_PAGE_SIZE
#define RAM_PAGE_SIZE     4096
#endif

#ifndef MEM_CLOCK_RATIO
#define MEM_CLOCK_RATIO   1
#endif

namespace vortex {

inline constexpr uint32_t XLENB           = (XLEN / 8);
inline constexpr uint32_t VLENB           = (VLEN / 8);

inline constexpr uint32_t MAX_NUM_CORES   = 1024;
inline constexpr uint32_t MAX_NUM_WARPS   = 64;
inline constexpr uint32_t MAX_NUM_REGS    = 32;
inline constexpr uint32_t LOG_NUM_REGS    = 5;
inline constexpr uint32_t NUM_SRC_REGS    = 3;

inline constexpr uint32_t LSU_WORD_SIZE   = (XLEN / 8);
inline constexpr uint32_t LSU_CHANNELS    = NUM_LSU_LANES;
inline constexpr uint32_t LSU_NUM_REQS	  = (NUM_LSU_BLOCKS * LSU_CHANNELS);

// The dcache uses coalesced memory blocks
inline constexpr uint32_t DCACHE_WORD_SIZE= LSU_LINE_SIZE;
inline constexpr uint32_t DCACHE_CHANNELS = UP((NUM_LSU_LANES * XLENB) / DCACHE_WORD_SIZE);
inline constexpr uint32_t DCACHE_NUM_REQS	= (NUM_LSU_BLOCKS * DCACHE_CHANNELS);

inline constexpr uint32_t NUM_SOCKETS     = UP(NUM_CORES / SOCKET_SIZE);

inline constexpr uint32_t L2_NUM_REQS     = NUM_SOCKETS * L1_MEM_PORTS;
inline constexpr uint32_t L3_NUM_REQS     = NUM_CLUSTERS * L2_MEM_PORTS;

inline constexpr uint32_t PER_ISSUE_WARPS = NUM_WARPS / ISSUE_WIDTH;
inline constexpr uint32_t ISSUE_WIS_BITS  = log2ceil(PER_ISSUE_WARPS);

#ifdef ORCHESTRATED_PREFETCH_ENABLE
// PA Scheduler
inline constexpr uint32_t PA_FETCH_GROUP_SIZE   = 8;

// SLD Prefetcher — adapted for Vortex's 64B cache lines
inline constexpr uint32_t SLD_TABLE_SIZE        = 64;  // fully-associative entries
inline constexpr uint32_t SLD_LINE_SHIFT        = 6;   // 64B cache line (2^6)
inline constexpr uint32_t SLD_LINES_PER_MACRO   = 4;   // 4 lines per macro-block
inline constexpr uint32_t SLD_MACROBLOCK_SHIFT  = 8;   // 256B macro-block (4 × 64B = 2^8)
inline constexpr uint32_t SLD_COVERAGE_THRESH   = 2;   // prefetch when >= C lines accessed
#endif

#ifdef SNAKE_PREFETCH_ENABLE
// Snake prefetcher — variable-length chain-based inter-thread stride detection
inline constexpr uint32_t SNAKE_HEAD_ENTRIES   = 64;  // one slot per warp (matches MAX_NUM_WARPS)
inline constexpr uint32_t SNAKE_TAIL_ENTRIES   = 64;  // fully-associative stride-chain table
inline constexpr uint32_t SNAKE_TRAIN_THRESH   = 3;   // warps required to confirm a chain
inline constexpr uint32_t SNAKE_MAX_WIDS       = 8;   // warp IDs tracked per tail entry
#endif

#ifdef MT_HWP_ENABLE
// MT-HWP table sizes
inline constexpr uint32_t PWS_TABLE_SIZE       = 32;
inline constexpr uint32_t GS_TABLE_SIZE        = 8;
inline constexpr uint32_t IP_TABLE_SIZE        = 8;
inline constexpr uint32_t GS_PROMOTION_THRESH  = 3;
inline constexpr uint32_t IP_TRAINING_THRESH   = 3;

// Prefetch cache (16 KB, 8-way, 64-byte lines => 32 sets)
inline constexpr uint32_t PREFETCH_CACHE_SIZE  = 16384;
inline constexpr uint32_t PREFETCH_CACHE_WAYS  = 8;
inline constexpr uint32_t PREFETCH_CACHE_LINE  = 64;
inline constexpr uint32_t PREFETCH_CACHE_SETS  = PREFETCH_CACHE_SIZE / (PREFETCH_CACHE_WAYS * PREFETCH_CACHE_LINE);

// Prefetch degree/distance
inline constexpr uint32_t PREFETCH_DEGREE      = 1;
inline constexpr uint32_t PREFETCH_DISTANCE    = 1;

// Throttling
inline constexpr uint32_t THROTTLE_PERIOD      = 100000;
inline constexpr uint32_t THROTTLE_DEGREE_INIT = 2;
#endif

} // namespace vortex
