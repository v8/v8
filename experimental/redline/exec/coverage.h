// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXPERIMENTAL_REDLINE_EXEC_COVERAGE_H_
#define V8_EXPERIMENTAL_REDLINE_EXEC_COVERAGE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "experimental/redline/d8/cov.h"

namespace redline {

// The edge coverage bitmap shared with one target process, plus the
// bookkeeping of which edges have been seen so far ("virgin bits").
//
// The layout is that of experimental/redline/d8/cov.h: a POSIX shared memory
// object named "shm_id_<pid>_<id>" of kShmSize bytes. The target writes a
// header at startup: the mode, the number of edges and the regions. One bit
// per edge follows. Edge indices start at 1. Index 0 is never reported. The
// parent must clear the bitmap before each execution and read it afterwards.
//
// Not thread-safe; one instance per ReprlChild.
class CoverageMap {
 public:
  // The size of the shared memory. The target maps the size that it gets.
  static constexpr size_t kShmSize = cov::kShmSize;

  // Creates the shared memory object; `id` must be unique within this
  // process (one per target child).
  static std::unique_ptr<CoverageMap> Create(int id, std::string* error);
  ~CoverageMap();
  CoverageMap(const CoverageMap&) = delete;
  CoverageMap& operator=(const CoverageMap&) = delete;

  // Value for the SHM_ID environment variable of the target.
  const std::string& shm_name() const { return shm_name_; }
  int id() const { return id_; }

  // Reads the header written by the target. Must be called after the target
  // has started (i.e. after the REPRL handshake) and before any evaluation.
  // Returns false if the header is missing or invalid, for example because
  // the target is not instrumented.
  bool FinishInitialization(std::string* error);
  bool initialized() const { return num_edges_ != 0; }

  // Number of instrumented edges reported by the target.
  uint32_t num_edges() const { return num_edges_; }
  // The header that the target wrote (valid after FinishInitialization()).
  const cov::Header& header() const { return header_; }
  // The mode of the coverage instrumentation of the target, for example
  // "inline-bool-flag".
  const char* mode_name() const;
  // The GNU build ID of the target in hex, or an empty string.
  std::string build_id() const;
  // Number of distinct edges discovered so far (through EvaluateNewEdges).
  uint32_t found_edges() const { return found_edges_; }
  // found_edges() / (num_edges() + 1): the same ratio Fuzzilli reports.
  double coverage_fraction() const;

  // Zeroes the bitmap. Call before every execution.
  void ClearBitmap();

  // Returns the edges set in the bitmap that have never been seen before
  // and marks them as seen. Indices are strictly increasing.
  std::vector<uint32_t> EvaluateNewEdges();

  // Like EvaluateNewEdges but against a separate set of bits used for
  // crashing executions, so that crash triage does not pollute the regular
  // coverage state. Returns true if the crash reached unseen crash edges.
  bool EvaluateCrash();

  // Whether every edge in `edges` is set in the current bitmap.
  bool AllEdgesSet(const std::vector<uint32_t>& edges) const;
  // The subset of `edges` that is not set in the current bitmap.
  std::vector<uint32_t> MissingEdges(const std::vector<uint32_t>& edges) const;

  // All edges currently set in the bitmap (index 0 excluded).
  std::vector<uint32_t> CurrentEdges() const;
  // Number of edges currently set in the bitmap.
  uint32_t CountCurrentEdges() const;

  // Marks `edges` as unseen again (used when edges found by a program turn
  // out to be non-deterministic and the program is discarded).
  void ForgetEdges(const std::vector<uint32_t>& edges);

  // Imports a set of edges as already seen (e.g. when loading a state).
  void MarkSeen(const std::vector<uint32_t>& edges);

  // Resets all bookkeeping to the initial state.
  void ResetState();

  // Raw access to the bitmap written by the target (bitmap_size() bytes, a
  // multiple of 8), for coverage bookkeeping shared between several targets
  // (see engine/shared_coverage.h).
  const uint8_t* bitmap() const { return shm_edges(); }
  uint32_t bitmap_size() const { return bitmap_size_; }

 private:
  CoverageMap(int id, std::string shm_name, void* mapping);

  // The bitmap starts after the header (cov::kHeaderSize).
  uint8_t* shm_edges() { return shmem_ + cov::kHeaderSize; }
  const uint8_t* shm_edges() const { return shmem_ + cov::kHeaderSize; }

  uint32_t ScanNewEdges(uint8_t* virgin_bits, std::vector<uint32_t>* out);

  int id_;
  std::string shm_name_;
  uint8_t* shmem_;
  cov::Header header_ = {};
  uint32_t num_edges_ = 0;
  uint32_t bitmap_size_ = 0;  // bytes, multiple of 8
  uint32_t found_edges_ = 0;
  std::vector<uint8_t> virgin_bits_;
  std::vector<uint8_t> crash_bits_;
};

}  // namespace redline

#endif  // V8_EXPERIMENTAL_REDLINE_EXEC_COVERAGE_H_
