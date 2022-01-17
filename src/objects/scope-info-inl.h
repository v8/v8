// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_SCOPE_INFO_INL_H_
#define V8_OBJECTS_SCOPE_INFO_INL_H_

#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/fixed-array-inl.h"
#include "src/objects/scope-info.h"
#include "src/objects/string.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

#include "torque-generated/src/objects/scope-info-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(ScopeInfo)

bool ScopeInfo::IsAsmModule() const { return IsAsmModuleBit::decode(Flags()); }

bool ScopeInfo::HasSimpleParameters() const {
  return HasSimpleParametersBit::decode(Flags());
}

int ScopeInfo::Flags() const { return flags(); }
int ScopeInfo::ParameterCount() const { return parameter_count(); }
int ScopeInfo::ContextLocalCount() const { return context_local_count(); }

ObjectSlot ScopeInfo::data_start() { return RawField(OffsetOfElementAt(0)); }

bool ScopeInfo::HasInlinedLocalNames() const {
  return ContextLocalCount() < kScopeInfoMaxInlinedLocalNamesSize;
}

template <typename ScopeInfoPtr>
class ScopeInfo::LocalNamesIterator {
 public:
  class Iterator {
   public:
    Iterator(ScopeInfoPtr scope_info, int index)
        : scope_info_(scope_info), index_(index) {}

    Iterator& operator++() {
      index_++;
      return *this;
    }

    friend bool operator==(const Iterator& a, const Iterator& b) {
      return *a.scope_info_ == *b.scope_info_ && a.index_ == b.index_;
    }
    friend bool operator!=(const Iterator& a, const Iterator& b) {
      return !(a == b);
    }

    String name() const {
      DCHECK_LT(index_, scope_info_->context_local_count());
      return scope_info_->context_local_names(index_);
    }

    const Iterator* operator*() const { return this; }

    int index() const { return index_; }

   private:
    ScopeInfoPtr scope_info_;
    int index_;
  };

  explicit LocalNamesIterator(ScopeInfoPtr scope_info)
      : scope_info_(scope_info) {}

  inline Iterator begin() const { return Iterator(scope_info_, 0); }

  inline Iterator end() const {
    return Iterator(scope_info_, scope_info_->ContextLocalCount());
  }

 private:
  ScopeInfoPtr scope_info_;
};

// static
ScopeInfo::LocalNamesIterator<Handle<ScopeInfo>> ScopeInfo::IterateLocalNames(
    Handle<ScopeInfo> scope_info) {
  return LocalNamesIterator<Handle<ScopeInfo>>(scope_info);
}

// static
ScopeInfo::LocalNamesIterator<ScopeInfo*> ScopeInfo::IterateLocalNames(
    ScopeInfo* scope_info, const DisallowGarbageCollection& no_gc) {
  USE(no_gc);
  return LocalNamesIterator<ScopeInfo*>(scope_info);
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_SCOPE_INFO_INL_H_
