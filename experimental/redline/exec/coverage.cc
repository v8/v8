// Copyright 2026 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "experimental/redline/exec/coverage.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "experimental/redline/base/log.h"

namespace redline {

namespace {

inline bool GetBit(const uint8_t* bits, uint32_t index) {
  return (bits[index / 8] >> (index % 8)) & 1;
}

inline void SetBit(uint8_t* bits, uint32_t index) {
  bits[index / 8] |= static_cast<uint8_t>(1u << (index % 8));
}

inline void ClearBit(uint8_t* bits, uint32_t index) {
  bits[index / 8] &= static_cast<uint8_t>(~(1u << (index % 8)));
}

}  // namespace

// static
std::unique_ptr<CoverageMap> CoverageMap::Create(int id, std::string* error) {
  std::string name =
      StringPrintf("/shm_id_%d_%d", static_cast<int>(getpid()), id);
  shm_unlink(name.c_str());
  int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC,
                    S_IRUSR | S_IWUSR);
  if (fd < 0) {
    *error =
        StringPrintf("shm_open(%s) failed: %s", name.c_str(), strerror(errno));
    return nullptr;
  }
  if (ftruncate(fd, static_cast<off_t>(kShmSize)) != 0) {
    *error =
        StringPrintf("ftruncate(%s) failed: %s", name.c_str(), strerror(errno));
    close(fd);
    shm_unlink(name.c_str());
    return nullptr;
  }
  void* mapping =
      mmap(nullptr, kShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (mapping == MAP_FAILED) {
    *error = StringPrintf("mmap(%s) failed: %s", name.c_str(), strerror(errno));
    shm_unlink(name.c_str());
    return nullptr;
  }
  memset(mapping, 0, kShmSize);
  return std::unique_ptr<CoverageMap>(new CoverageMap(id, name, mapping));
}

CoverageMap::CoverageMap(int id, std::string shm_name, void* mapping)
    : id_(id),
      shm_name_(std::move(shm_name)),
      shmem_(static_cast<uint8_t*>(mapping)) {}

CoverageMap::~CoverageMap() {
  munmap(shmem_, kShmSize);
  shm_unlink(shm_name_.c_str());
}

namespace {

// Checks the header that the target wrote. Returns an empty string if it is
// valid, else the reason.
std::string CheckHeader(const cov::Header& header) {
  if (header.magic != cov::kMagic) {
    return "The target did not write the coverage header of Redline; is it "
           "built with v8_redline=true and coverage instrumentation "
           "(sanitizer_coverage_flags)?";
  }
  if (header.version != cov::kVersion) {
    return StringPrintf(
        "The target writes version %u of the coverage header, but Redline "
        "reads version %u; rebuild Redline or the target",
        header.version, cov::kVersion);
  }
  const auto mode = static_cast<cov::Mode>(header.mode);
  if (mode != cov::Mode::kTracePcGuard && mode != cov::Mode::kInlineBoolFlag &&
      mode != cov::Mode::kInline8bitCounters) {
    return "The target has no coverage instrumentation "
           "(sanitizer_coverage_flags)";
  }
  if (header.header_size != cov::kHeaderSize ||
      header.shm_size != CoverageMap::kShmSize ||
      header.bits_per_edge != cov::BitsPerEdge(mode) ||
      header.max_edges !=
          cov::MaxEdges(header.shm_size, header.bits_per_edge) ||
      header.num_edges == 0 || header.num_edges > header.max_edges ||
      header.num_regions > cov::kMaxRegions ||
      header.build_id_size > cov::kMaxBuildIdSize) {
    return "The target wrote an invalid coverage header";
  }
  // The regions follow each other, from edge 1 to the last edge.
  uint32_t next = 1;
  for (uint32_t i = 0; i < header.num_regions; i++) {
    const cov::Region& region = header.regions[i];
    if (region.kind < static_cast<uint32_t>(cov::RegionKind::kCode) ||
        region.kind > static_cast<uint32_t>(cov::RegionKind::kOptimized) ||
        region.first != next || region.count > header.num_edges - next + 1) {
      return "The target wrote an invalid coverage region";
    }
    next += region.count;
  }
  if (next != header.num_edges + 1) {
    return "The coverage regions of the target do not hold all edges";
  }
  return "";
}

}  // namespace

bool CoverageMap::FinishInitialization(std::string* error) {
  cov::Header header;
  memcpy(&header, shmem_, sizeof(header));
  std::string problem = CheckHeader(header);
  if (!problem.empty()) {
    *error = problem;
    return false;
  }
  if (header.bits_per_edge != 1) {
    *error =
        "The target has coverage counters (inline-8bit-counters). This "
        "Redline reads only one bit per edge (trace-pc-guard or "
        "inline-bool-flag)";
    return false;
  }
  if (num_edges_ != 0) {
    // A new process of the same target must write the same header.
    if (memcmp(&header, &header_, sizeof(header)) != 0) {
      *error = StringPrintf(
          "The target changed its coverage header (%u -> %u edges); mixing "
          "binaries?",
          num_edges_, header.num_edges);
      return false;
    }
    return true;
  }

  header_ = header;
  num_edges_ = header.num_edges;
  uint32_t bitmap_size = (num_edges_ + 1 + 7) / 8;
  // Round up to a multiple of 8 bytes so that the scans can use 64-bit
  // words.
  bitmap_size += 7 - ((bitmap_size - 1) % 8);
  bitmap_size_ = bitmap_size;
  virgin_bits_.assign(bitmap_size_, 0xff);
  crash_bits_.assign(bitmap_size_, 0xff);
  ResetState();
  return true;
}

const char* CoverageMap::mode_name() const {
  switch (static_cast<cov::Mode>(header_.mode)) {
    case cov::Mode::kTracePcGuard:
      return "trace-pc-guard";
    case cov::Mode::kInlineBoolFlag:
      return "inline-bool-flag";
    case cov::Mode::kInline8bitCounters:
      return "inline-8bit-counters";
    case cov::Mode::kNone:
      break;
  }
  return "none";
}

std::string CoverageMap::build_id() const {
  std::string hex;
  for (uint32_t i = 0; i < header_.build_id_size; i++) {
    hex += StringPrintf("%02x", header_.build_id[i]);
  }
  return hex;
}

double CoverageMap::coverage_fraction() const {
  if (num_edges_ == 0) {
    return 0.0;
  }
  return static_cast<double>(found_edges_) /
         static_cast<double>(num_edges_ + 1);
}

void CoverageMap::ClearBitmap() {
  if (bitmap_size_ == 0) {
    return;
  }
  memset(shm_edges(), 0, bitmap_size_);
}

uint32_t CoverageMap::ScanNewEdges(uint8_t* virgin_bits,
                                   std::vector<uint32_t>* out) {
  uint32_t count = 0;
  const uint8_t* edges = shm_edges();
  for (uint32_t offset = 0; offset < bitmap_size_; offset += 8) {
    uint64_t current, virgin;
    memcpy(&current, edges + offset, 8);
    memcpy(&virgin, virgin_bits + offset, 8);
    uint64_t fresh = current & virgin;
    if (fresh == 0) {
      continue;
    }
    while (fresh != 0) {
      uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(fresh));
      uint32_t index = offset * 8 + bit;
      ClearBit(virgin_bits, index);
      if (out != nullptr) {
        out->push_back(index);
      }
      count++;
      fresh &= fresh - 1;
    }
  }
  return count;
}

std::vector<uint32_t> CoverageMap::EvaluateNewEdges() {
  std::vector<uint32_t> new_edges;
  if (bitmap_size_ == 0) {
    return new_edges;
  }
  found_edges_ += ScanNewEdges(virgin_bits_.data(), &new_edges);
  return new_edges;
}

bool CoverageMap::EvaluateCrash() {
  if (bitmap_size_ == 0) {
    return false;
  }
  return ScanNewEdges(crash_bits_.data(), nullptr) > 0;
}

bool CoverageMap::AllEdgesSet(const std::vector<uint32_t>& edges) const {
  for (uint32_t index : edges) {
    if (index == 0 || index > num_edges_ || !GetBit(shm_edges(), index)) {
      return false;
    }
  }
  return true;
}

std::vector<uint32_t> CoverageMap::MissingEdges(
    const std::vector<uint32_t>& edges) const {
  std::vector<uint32_t> missing;
  for (uint32_t index : edges) {
    if (index == 0 || index > num_edges_ || !GetBit(shm_edges(), index)) {
      missing.push_back(index);
    }
  }
  return missing;
}

std::vector<uint32_t> CoverageMap::CurrentEdges() const {
  std::vector<uint32_t> result;
  const uint8_t* edges = shm_edges();
  for (uint32_t offset = 0; offset < bitmap_size_; offset += 8) {
    uint64_t current;
    memcpy(&current, edges + offset, 8);
    while (current != 0) {
      uint32_t bit = static_cast<uint32_t>(__builtin_ctzll(current));
      uint32_t index = offset * 8 + bit;
      if (index != 0 && index <= num_edges_) {
        result.push_back(index);
      }
      current &= current - 1;
    }
  }
  return result;
}

uint32_t CoverageMap::CountCurrentEdges() const {
  uint32_t count = 0;
  const uint8_t* edges = shm_edges();
  for (uint32_t offset = 0; offset < bitmap_size_; offset += 8) {
    uint64_t current;
    memcpy(&current, edges + offset, 8);
    count += static_cast<uint32_t>(__builtin_popcountll(current));
  }
  if (count > 0 && GetBit(edges, 0)) {
    count--;
  }
  return count;
}

void CoverageMap::ForgetEdges(const std::vector<uint32_t>& edges) {
  for (uint32_t index : edges) {
    if (index == 0 || index > num_edges_) {
      continue;
    }
    if (!GetBit(virgin_bits_.data(), index)) {
      SetBit(virgin_bits_.data(), index);
      found_edges_--;
    }
  }
}

void CoverageMap::MarkSeen(const std::vector<uint32_t>& edges) {
  for (uint32_t index : edges) {
    if (index == 0 || index > num_edges_) {
      continue;
    }
    if (GetBit(virgin_bits_.data(), index)) {
      ClearBit(virgin_bits_.data(), index);
      found_edges_++;
    }
  }
}

void CoverageMap::ResetState() {
  if (bitmap_size_ == 0) {
    return;
  }
  std::fill(virgin_bits_.begin(), virgin_bits_.end(), 0xff);
  std::fill(crash_bits_.begin(), crash_bits_.end(), 0xff);
  ClearBit(virgin_bits_.data(), 0);
  ClearBit(crash_bits_.data(), 0);
  for (uint32_t index = num_edges_ + 1; index < bitmap_size_ * 8; index++) {
    ClearBit(virgin_bits_.data(), index);
    ClearBit(crash_bits_.data(), index);
  }
  found_edges_ = 0;
}

}  // namespace redline
