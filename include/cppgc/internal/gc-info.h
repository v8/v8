// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_INTERNAL_GC_INFO_H_
#define INCLUDE_CPPGC_INTERNAL_GC_INFO_H_

#include <stdint.h>

#include "cppgc/internal/finalizer-trait.h"
#include "cppgc/internal/name-trait.h"
#include "cppgc/trace-trait.h"
#include "v8config.h"  // NOLINT(build/include_directory)

namespace cppgc {
namespace internal {

using GCInfoIndex = uint16_t;

class V8_EXPORT RegisteredGCInfoIndex final {
 public:
  RegisteredGCInfoIndex(FinalizationCallback finalization_callback,
                        TraceCallback trace_callback,
                        NameCallback name_callback, bool has_v_table);
  GCInfoIndex GetIndex() const { return index_; }

 private:
  const GCInfoIndex index_;
};

// Fold types based on finalizer behavior. Note that finalizer characteristics
// align with trace behavior, i.e., destructors are virtual when trace methods
// are and vice versa.
template <typename T, typename ParentMostGarbageCollectedType>
struct GCInfoFolding {
  static constexpr bool kHasVirtualDestructorAtBase =
      std::has_virtual_destructor<ParentMostGarbageCollectedType>::value;
  static constexpr bool kBothTypesAreTriviallyDestructible =
      std::is_trivially_destructible<ParentMostGarbageCollectedType>::value &&
      std::is_trivially_destructible<T>::value;
  static constexpr bool kHasCustomFinalizerDispatchAtBase =
      internal::HasFinalizeGarbageCollectedObject<
          ParentMostGarbageCollectedType>::value;
#ifdef CPPGC_SUPPORTS_OBJECT_NAMES
  static constexpr bool kWantsDetailedObjectNames = true;
#else   // !CPPGC_SUPPORTS_OBJECT_NAMES
  static constexpr bool kWantsDetailedObjectNames = false;
#endif  // !CPPGC_SUPPORTS_OBJECT_NAMES

  // Folding would regresses name resolution when deriving names from C++
  // class names as it would just folds a name to the base class name.
  using ResultType = std::conditional_t<(kHasVirtualDestructorAtBase ||
                                         kBothTypesAreTriviallyDestructible ||
                                         kHasCustomFinalizerDispatchAtBase) &&
                                            !kWantsDetailedObjectNames,
                                        ParentMostGarbageCollectedType, T>;
};

// Trait determines how the garbage collector treats objects wrt. to traversing,
// finalization, and naming.
template <typename T>
struct GCInfoTrait {
  static GCInfoIndex Index() {
    static_assert(sizeof(T), "T must be fully defined");
    static const RegisteredGCInfoIndex registered_index(
        FinalizerTrait<T>::kCallback, TraceTrait<T>::Trace,
        NameTrait<T>::GetName, std::is_polymorphic<T>::value);
    return registered_index.GetIndex();
  }
};

}  // namespace internal
}  // namespace cppgc

#endif  // INCLUDE_CPPGC_INTERNAL_GC_INFO_H_
