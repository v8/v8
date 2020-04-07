// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_CPPGC_VISITOR_H_
#define INCLUDE_CPPGC_VISITOR_H_

#include "include/cppgc/garbage-collected.h"
#include "include/cppgc/member.h"
#include "include/cppgc/trace-trait.h"

namespace cppgc {
namespace internal {
class VisitorBase;
}  // namespace internal

class Visitor {
 public:
  template <typename T>
  void Trace(const Member<T>& member) {
    const T* value = member.GetRawAtomic();
    // TODO(chromium:1056170): DCHECK (or similar) for deleted values as they
    // should come in at a different path.
    Trace(value);
  }

 protected:
  virtual void Visit(const void*, TraceDescriptor) {}

 private:
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
