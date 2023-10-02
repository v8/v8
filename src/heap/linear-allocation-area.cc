// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/linear-allocation-area.h"

#include "src/base/iterator.h"
#include "src/base/platform/mutex.h"

namespace v8 {
namespace internal {

LabOriginalLimits::LabOriginalLimits() {
  Node* prev = nullptr;
  for (auto& heap_data : base::Reversed(labs_)) {
    heap_data.next_free = prev;
    prev = &heap_data;
  }
  freelist_head_ = prev;
}

LabOriginalLimits::LabHandle LabOriginalLimits::AllocateLabHandle() {
  return LabHandle{*this, AllocateNode()};
}

LabOriginalLimits::PendingObjectHandle
LabOriginalLimits::AllocateObjectHandle() {
  return PendingObjectHandle{*this, AllocateNode()};
}

LabOriginalLimits::Node& LabOriginalLimits::AllocateNode() {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);

  CHECK_WITH_MSG(freelist_head_,
                 "Ran out of LAB entries. Too many local heaps?");

  // Allocate from the free-list.
  auto* new_limits = freelist_head_;
  freelist_head_ = freelist_head_->next_free;

  DCHECK(!new_limits->prev_allocated);
  DCHECK(!new_limits->next_allocated);

  // Link into the allocated-list head.
  if (allocated_list_head_) {
    allocated_list_head_->prev_allocated = new_limits;
  }
  new_limits->next_allocated = allocated_list_head_;
  allocated_list_head_ = new_limits;

  return *new_limits;
}

void LabOriginalLimits::FreeNode(LabOriginalLimits::Node& node) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);

  // Move into the free-list.
  node.next_free = freelist_head_;
  freelist_head_ = &node;

  // Unlink from the allocated-list.
  if (node.prev_allocated) {
    node.prev_allocated->next_allocated = node.next_allocated;
  } else {
    DCHECK_EQ(allocated_list_head_, &node);
    allocated_list_head_ = node.next_allocated;
  }
  if (node.next_allocated) {
    node.next_allocated->prev_allocated = node.prev_allocated;
  }

  node.prev_allocated = nullptr;
  node.next_allocated = nullptr;

  node.lab.top = kNullAddress;
  node.lab.limit = kNullAddress;

  BumpVersion();
}

LabOriginalLimits::Snapshot LabOriginalLimits::CreateEmptySnapshot() {
  return {};
}

bool LabOriginalLimits::UpdateSnapshotIfNeeded(Snapshot& snapshot) {
  auto current_version = version_.load(std::memory_order_seq_cst);
  if (current_version == snapshot.version_) return false;

  snapshot.labs_.clear();
  snapshot.objects_.clear();

  base::SharedMutexGuard<base::MutexSharedType::kShared> lock(&mutex_);
  for (auto* allocated_lab = allocated_list_head_; allocated_lab;
       allocated_lab = allocated_lab->next_allocated) {
    snapshot.AddIfNeeded(*allocated_lab);
  }

  snapshot.version_ = current_version;
  return true;
}

bool LabOriginalLimits::UpdatePartialSnapshotIfNeeded(
    std::initializer_list<BaseHandle*> handles, Snapshot& snapshot) {
  auto current_version = version_.load(std::memory_order_seq_cst);
  if (current_version == snapshot.version_) return false;

  snapshot.labs_.clear();
  snapshot.objects_.clear();

  base::SharedMutexGuard<base::MutexSharedType::kShared> lock(&mutex_);
  for (auto* handle : handles) {
    DCHECK_NOT_NULL(handle);
    snapshot.AddIfNeeded(handle->node_);
  }

  snapshot.version_ = current_version;
  return true;
}

void LabOriginalLimits::UpdateLabLimits(Node& node, Address top,
                                        Address limit) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  node.lab.top = top;
  node.lab.limit = limit;
  BumpVersion();
}

void LabOriginalLimits::AdvanceTop(Node& node, Address top) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  DCHECK_LE(node.lab.top, top);
  DCHECK_GE(node.lab.limit, top);
  node.lab.top = top;
  BumpVersion();
}

void LabOriginalLimits::SetTop(Node& node, Address top) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  node.lab.top = top;
  BumpVersion();
}

LabOriginalLimits::Lab LabOriginalLimits::ExtractLab(Node& node) const {
  base::SharedMutexGuard<base::MutexSharedType::kShared> lock(&mutex_);
  return node.lab;
}

LabOriginalLimits::BaseHandle::~BaseHandle() { limits_.FreeNode(node_); }

void LabOriginalLimits::LabHandle::UpdateLimits(Address top, Address limit) {
  limits_.UpdateLabLimits(node_, top, limit);
}

void LabOriginalLimits::LabHandle::AdvanceTop(Address top) {
  limits_.AdvanceTop(node_, top);
}

void LabOriginalLimits::LabHandle::SetTop(Address top) {
  limits_.SetTop(node_, top);
}

std::pair<Address, Address> LabOriginalLimits::LabHandle::top_and_limit()
    const {
  auto lab = limits_.ExtractLab(node_);
  return {lab.top, lab.limit};
}

void LabOriginalLimits::PendingObjectHandle::UpdateAddress(Address base) {
  reset_ = false;
  limits_.UpdateLabLimits(node_, base, base);
}

void LabOriginalLimits::PendingObjectHandle::Reset() {
  if (reset_) return;
  limits_.UpdateLabLimits(node_, kNullAddress, kNullAddress);
  reset_ = true;
}

Address LabOriginalLimits::PendingObjectHandle::address() const {
  if (reset_) return kNullAddress;
  auto [top, limit] = limits_.ExtractLab(node_);
  DCHECK_EQ(top, limit);
  return top;
}

void LabOriginalLimits::Snapshot::AddIfNeeded(Node& node) {
  // Don't take a snapshot of empty labs.
  if (!node.lab.top) {
    DCHECK(!node.lab.limit);
    return;
  }
  // TODO(v8:14158): Differentiate Labs from objects to be safe.
  if (node.lab.top == node.lab.limit) {
    objects_.emplace_back(node.lab.top);
  } else {
    labs_.emplace_back(Lab{node.lab.top, node.lab.limit});
  }
}

}  // namespace internal
}  // namespace v8
