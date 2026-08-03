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
#include <set>
#include <vector>

void fuzzilli_cov_enable();
void sanitizer_cov_reset_edgeguards();
uint32_t sanitizer_cov_count_discovered_edges();
void cov_init_builtins_edges(uint32_t num_edges);
void cov_update_builtins_basic_block_coverage(const std::vector<bool>& cov_map);

constexpr uint32_t kFuzzilliNumOptimizedCodeEdges = 256 * 1024;

void cov_init_optimized_code_edges(uint32_t num_edges);
void cov_add_optimized_code_coverage_edges(const std::set<uint32_t>& hashes);
std::vector<uint32_t> cov_get_and_reset_optimized_code_coverage_edges();
void cov_update_optimized_code_coverage(const std::vector<uint32_t>& hashes);

#endif  // V8_FUZZILLI_COV_H_
