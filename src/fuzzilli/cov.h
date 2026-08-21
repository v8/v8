// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_FUZZILLI_COV_H_
#define V8_FUZZILLI_COV_H_

// This file is defining functions to handle coverage which are needed for
// fuzzilli fuzzer It communicates coverage bitmap with fuzzilli through shared
// memory
// https://clang.llvm.org/docs/SanitizerCoverage.html

#include <cstdint>
#include <vector>

#include "src/diagnostics/basic-block-profiler.h"

inline void cov_set_edge(uint8_t* edges, uint32_t index) {
  v8::internal::BasicBlockProfiler::SetCoverageBit(edges, index);
}

inline bool cov_is_edge_set(const uint8_t* edges, uint32_t index) {
  return v8::internal::BasicBlockProfiler::GetCoverageBit(edges, index);
}

void fuzzilli_cov_enable();
void sanitizer_cov_reset_edgeguards();
uint32_t sanitizer_cov_count_discovered_edges();
void cov_init_builtins_edges(uint32_t num_edges);
bool cov_has_builtins_edges();
uint32_t cov_get_builtins_start();
uint8_t* cov_get_shmem_edges();
void cov_update_builtins_basic_block_coverage(const std::vector<bool>& cov_map);

#endif  // V8_FUZZILLI_COV_H_
