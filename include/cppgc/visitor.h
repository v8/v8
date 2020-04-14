// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_VISITOR_H_
#define INCLUDE_CPPGC_VISITOR_H_

#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/internal/pointer-policies.h"
#include "include/cppgc/liveness-broker.h"
#include "include/cppgc/member.h"
#include "include/cppgc/trace-trait.h"

namespace cppgc {
namespace internal {
class VisitorBase;
}  // namespace internal

class Visitor;

using WeakCallback = void (*)(const LivenessBroker&, const void*);

/**
 * Visitor passed to trace methods. All managed pointers must have called the
 * visitor's trace method on them.
 */
class Visitor {
 public:
  template <typename T>
  void Trace(const Member<T>& member) {
    const T* value = member.GetRawAtomic();
    // TODO(chromium:1056170): DCHECK (or similar) for deleted values as they
    // should come in at a different path.
    Trace(value);
  }

  template <typename T>
  void Trace(const WeakMember<T>& weak_member) {
    static_assert(sizeof(T), "T must be fully defined");
    static_assert(internal::IsGarbageCollectedType<T>::value,
                  "T must be GarabgeCollected or GarbageCollectedMixin type");

    const T* value = weak_member.GetRawAtomic();

    // Bailout assumes that WeakMember emits write barrier.
    if (!value) {
      return;
    }

    // TODO(chromium:1056170): DCHECK (or similar) for deleted values as they
    // should come in at a different path.
    VisitWeak(value, TraceTrait<T>::GetTraceDescriptor(value),
              &HandleWeakMember<T>, &weak_member);
  }

 protected:
  virtual void Visit(const void* self, TraceDescriptor) {}
  virtual void VisitWeak(const void* self, TraceDescriptor, WeakCallback,
                         const void* weak_member) {}

 private:
  template <typename T>
  static void HandleWeakMember(const LivenessBroker& info, const void* object) {
    const WeakMember<T>* weak_member =
        reinterpret_cast<const WeakMember<T>*>(object);
    if (!info.IsHeapObjectAlive(*weak_member)) {
      // Object is passed down through the marker as const. Alternatives are
      // - non-const Trace method;
      // - mutable pointer in MemberBase;
      const_cast<WeakMember<T>*>(weak_member)->Clear();
    }
  }

  Visitor() = default;

  template <typename T>
  void Trace(const T* t) {
    static_assert(sizeof(T), "T must be fully defined");
    static_assert(internal::IsGarbageCollectedType<T>::value,
                  "T must be GarabgeCollected or GarbageCollectedMixin type");
    if (!t) {
      return;
    }
    Visit(t, TraceTrait<T>::GetTraceDescriptor(t));
  }

  friend class internal::VisitorBase;
};

}  // namespace cppgc

#endif  // INCLUDE_CPPGC_VISITOR_H_
