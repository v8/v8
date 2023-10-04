// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/linear-allocation-area.h"

#include "src/base/iterator.h"
#include "src/base/platform/mutex.h"

namespace v8 {
namespace internal {

LabOriginalLimits::LabHandle LabOriginalLimits::AllocateLabHandle() {
  return LabHandle{*this};
}

LabOriginalLimits::PendingObjectHandle
LabOriginalLimits::AllocateObjectHandle() {
  return PendingObjectHandle{*this};
}

size_t LabOriginalLimits::AllocateNode() {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);

  nodes_.emplace_back();
  return nodes_.size() - 1;
}

void LabOriginalLimits::FreeNode(BaseHandle& handle) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);

  DCHECK_LT(handle.node_index_, nodes_.size());

  const auto index = handle.node_index_;
  auto node_it = std::next(nodes_.begin(), index);

  std::swap(nodes_.back(), *node_it);
  node_it->handle->node_index_ = index;

  nodes_.pop_back();

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
  for (auto& node : nodes_) {
    snapshot.AddIfNeeded(node);
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
    snapshot.AddIfNeeded(nodes_[handle->node_index_]);
  }

  snapshot.version_ = current_version;
  return true;
}

void LabOriginalLimits::UpdateLabLimits(BaseHandle& handle, Address top,
                                        Address limit) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  DCHECK_LT(handle.node_index_, nodes_.size());
  auto& node = nodes_[handle.node_index_];
  node.lab.top = top;
  node.lab.limit = limit;
  BumpVersion();
}

void LabOriginalLimits::AdvanceTop(BaseHandle& handle, Address top) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  DCHECK_LT(handle.node_index_, nodes_.size());
  auto& node = nodes_[handle.node_index_];
  DCHECK_LE(node.lab.top, top);
  DCHECK_GE(node.lab.limit, top);
  node.lab.top = top;
  BumpVersion();
}

void LabOriginalLimits::SetTop(BaseHandle& handle, Address top) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);
  DCHECK_LT(handle.node_index_, nodes_.size());
  auto& node = nodes_[handle.node_index_];
  node.lab.top = top;
  BumpVersion();
}

LabOriginalLimits::Lab LabOriginalLimits::ExtractLab(
    const BaseHandle& handle) const {
  base::SharedMutexGuard<base::MutexSharedType::kShared> lock(&mutex_);
  DCHECK_LT(handle.node_index_, nodes_.size());
  auto& node = nodes_[handle.node_index_];
  return node.lab;
}

void LabOriginalLimits::SetUpHandle(BaseHandle& handle) {
  base::SharedMutexGuard<base::MutexSharedType::kExclusive> lock(&mutex_);

  nodes_.emplace_back();
  handle.node_index_ = nodes_.size() - 1;

  nodes_.back().handle = &handle;
}

LabOriginalLimits::BaseHandle::~BaseHandle() { limits_.FreeNode(*this); }

void LabOriginalLimits::LabHandle::UpdateLimits(Address top, Address limit) {
  limits_.UpdateLabLimits(*this, top, limit);
}

void LabOriginalLimits::LabHandle::AdvanceTop(Address top) {
  limits_.AdvanceTop(*this, top);
}

void LabOriginalLimits::LabHandle::SetTop(Address top) {
  limits_.SetTop(*this, top);
}

std::pair<Address, Address> LabOriginalLimits::LabHandle::top_and_limit()
    const {
  auto lab = limits_.ExtractLab(*this);
  return {lab.top, lab.limit};
}

void LabOriginalLimits::PendingObjectHandle::UpdateAddress(Address base) {
  reset_ = false;
  limits_.UpdateLabLimits(*this, base, base);
}

void LabOriginalLimits::PendingObjectHandle::Reset() {
  if (reset_) return;
  limits_.UpdateLabLimits(*this, kNullAddress, kNullAddress);
  reset_ = true;
}

Address LabOriginalLimits::PendingObjectHandle::address() const {
  if (reset_) return kNullAddress;
  auto [top, limit] = limits_.ExtractLab(*this);
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
