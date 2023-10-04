// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LINEAR_ALLOCATION_AREA_H_
#define V8_HEAP_LINEAR_ALLOCATION_AREA_H_

// This header file is included outside of src/heap/.
// Avoid including src/heap/ internals.
#include "include/v8-internal.h"
#include "src/base/platform/mutex.h"
#include "src/common/checks.h"

namespace v8 {
namespace internal {

// A linear allocation area to allocate objects from.
//
// Invariant that must hold at all times:
//   start <= top <= limit
class LinearAllocationArea final {
 public:
  LinearAllocationArea() = default;
  LinearAllocationArea(Address top, Address limit)
      : start_(top), top_(top), limit_(limit) {
    Verify();
  }

  void Reset(Address top, Address limit) {
    start_ = top;
    top_ = top;
    limit_ = limit;
    Verify();
  }

  void ResetStart() { start_ = top_; }

  V8_INLINE bool CanIncrementTop(size_t bytes) const {
    Verify();
    return (top_ + bytes) <= limit_;
  }

  V8_INLINE Address IncrementTop(size_t bytes) {
    Address old_top = top_;
    top_ += bytes;
    Verify();
    return old_top;
  }

  V8_INLINE bool DecrementTopIfAdjacent(Address new_top, size_t bytes) {
    Verify();
    if ((new_top + bytes) == top_) {
      top_ = new_top;
      if (start_ > top_) {
        ResetStart();
      }
      Verify();
      return true;
    }
    return false;
  }

  V8_INLINE bool MergeIfAdjacent(LinearAllocationArea& other) {
    Verify();
    other.Verify();
    if (top_ == other.limit_) {
      top_ = other.top_;
      start_ = other.start_;
      other.Reset(kNullAddress, kNullAddress);
      Verify();
      return true;
    }
    return false;
  }

  V8_INLINE void SetLimit(Address limit) {
    limit_ = limit;
    Verify();
  }

  V8_INLINE Address start() const {
    Verify();
    return start_;
  }
  V8_INLINE Address top() const {
    Verify();
    return top_;
  }
  V8_INLINE Address limit() const {
    Verify();
    return limit_;
  }
  const Address* top_address() const { return &top_; }
  Address* top_address() { return &top_; }
  const Address* limit_address() const { return &limit_; }
  Address* limit_address() { return &limit_; }

  void Verify() const {
#ifdef DEBUG
    SLOW_DCHECK(start_ <= top_);
    SLOW_DCHECK(top_ <= limit_);
    if (V8_COMPRESS_POINTERS_8GB_BOOL) {
      SLOW_DCHECK(IsAligned(top_, kObjectAlignment8GbHeap));
    } else {
      SLOW_DCHECK(IsAligned(top_, kObjectAlignment));
    }
#endif  // DEBUG
  }

  static constexpr int kSize = 3 * kSystemPointerSize;

 private:
  // The start of the LAB. Initially coincides with `top_`. As top is moved
  // ahead, the area [start_, top_[ denotes a range of new objects. This range
  // is reset with `ResetStart()`.
  Address start_ = kNullAddress;
  // The top of the LAB that is used for allocation.
  Address top_ = kNullAddress;
  // Limit of the LAB the denotes the end of the valid range for allocation.
  Address limit_ = kNullAddress;
};

static_assert(sizeof(LinearAllocationArea) == LinearAllocationArea::kSize,
              "LinearAllocationArea's size must be small because it "
              "is included in IsolateData.");

// LabOriginalLimits keeps track of all allocated labs for local-heaps and
// allows to take a snapshot of them.
class V8_EXPORT_PRIVATE LabOriginalLimits final {
 public:
  class BaseHandle;

 private:
  struct Lab {
    Address top = 0;
    Address limit = 0;
  };

  struct Node {
    Lab lab;
    // Back reference to the handle, needed for compaction.
    BaseHandle* handle = nullptr;
  };

 public:
  class V8_EXPORT_PRIVATE BaseHandle {
   public:
    ~BaseHandle();

    // The current design with compaction and back references requires the
    // handles to be non-movable. If ever movability is needed, this can be
    // solved by introducing an indirection into the handles.
    BaseHandle(const BaseHandle&) = delete;
    BaseHandle& operator=(const BaseHandle&) = delete;

   protected:
    friend LabOriginalLimits;
    explicit BaseHandle(LabOriginalLimits& limits) : limits_(limits) {
      limits_.SetUpHandle(*this);
    }

    LabOriginalLimits& limits_;
    size_t node_index_ = 0;
  };

  // RAII based handle for lab limits. Automatically destroys the corresponding
  // lab entry.
  class V8_EXPORT_PRIVATE LabHandle final : public BaseHandle {
   public:
    void UpdateLimits(Address top, Address limit);
    void AdvanceTop(Address top);
    void SetTop(Address top);

    std::pair<Address, Address> top_and_limit() const;

   private:
    friend LabOriginalLimits;
    explicit LabHandle(LabOriginalLimits& limits) : BaseHandle(limits) {}
  };

  // RAII based handle for pending objects. Automatically destroys the
  // corresponding entry.
  class V8_EXPORT_PRIVATE PendingObjectHandle final : public BaseHandle {
   public:
    void UpdateAddress(Address base);
    void Reset();

    Address address() const;

   private:
    friend LabOriginalLimits;

    explicit PendingObjectHandle(LabOriginalLimits& limits)
        : BaseHandle(limits) {}
    // For fast checks to avoid mutex locking.
    bool reset_ = true;
  };

  // Represents a snapshot of all allocated lab limits.
  class Snapshot final {
   public:
    bool IsAddressInAnyLab(Address address) const {
      for (const auto& lab : labs_) {
        if (lab.top <= address && address < lab.limit) return true;
      }
      for (Address object : objects_) {
        if (object == address) return true;
      }
      return false;
    }

   private:
    friend LabOriginalLimits;

    void AddIfNeeded(Node& node);

    std::vector<Lab> labs_;
    std::vector<Address> objects_;
    size_t version_ = std::numeric_limits<size_t>::max();
  };

  LabOriginalLimits() = default;

  LabOriginalLimits(const LabOriginalLimits&) = delete;
  LabOriginalLimits& operator=(const LabOriginalLimits&) = delete;

  // Initializes an empty snapshot.
  Snapshot CreateEmptySnapshot();

  // Update the snapshot for all registered labs. Returns false if the passed
  // snapshot is already up-to-date.
  bool UpdateSnapshotIfNeeded(Snapshot&);

  // Update the snapshot for the specified handles. Returns false if the passed
  // snapshot is already up-to-date.
  bool UpdatePartialSnapshotIfNeeded(std::initializer_list<BaseHandle*>,
                                     Snapshot&);

  // Allocate a lab-limit instance and return a handle to it.
  LabHandle AllocateLabHandle();
  PendingObjectHandle AllocateObjectHandle();

 private:
  size_t AllocateNode();
  void FreeNode(BaseHandle& handle);

  void UpdateLabLimits(BaseHandle&, Address top, Address limit);
  void AdvanceTop(BaseHandle&, Address top);
  void SetTop(BaseHandle&, Address top);

  Lab ExtractLab(const BaseHandle&) const;

  void SetUpHandle(BaseHandle& handle);

  void BumpVersion() {
    // Use sequential consistency to have the guarantee that the most current
    // update is read by the snapshot. This allows us to stay on the safe side.
    version_.fetch_add(1, std::memory_order_seq_cst);
  }

  std::vector<Node> nodes_;
  mutable base::SharedMutex mutex_;

  // Version to fast check if the snapshot is up-to-date.
  std::atomic_size_t version_{0};
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LINEAR_ALLOCATION_AREA_H_
