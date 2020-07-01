// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_MARKING_VISITOR_H_
#define V8_HEAP_CPPGC_MARKING_VISITOR_H_

#include "include/cppgc/trace-trait.h"
#include "src/base/macros.h"
#include "src/heap/base/stack.h"
#include "src/heap/cppgc/visitor.h"

namespace cppgc {
namespace internal {

class HeapBase;
class HeapObjectHeader;
class Marker;
class MarkingState;

class MarkingVisitor : public ConservativeTracingVisitor,
                       public heap::base::StackVisitor {
 public:
  MarkingVisitor(HeapBase&, MarkingState&);
  virtual ~MarkingVisitor() = default;

  MarkingVisitor(const MarkingVisitor&) = delete;
  MarkingVisitor& operator=(const MarkingVisitor&) = delete;

 private:
  void Visit(const void*, TraceDescriptor) final;
  void VisitWeak(const void*, TraceDescriptor, WeakCallback, const void*) final;
  void VisitRoot(const void*, TraceDescriptor) final;
  void VisitWeakRoot(const void*, TraceDescriptor, WeakCallback,
                     const void*) final;
  void VisitConservatively(HeapObjectHeader&,
                           TraceConservativelyCallback) final;
  void RegisterWeakCallback(WeakCallback, const void*) final;

  // StackMarker interface.
  void VisitPointer(const void*) override;

  MarkingState& marking_state_;
};

class V8_EXPORT_PRIVATE MutatorThreadMarkingVisitor : public MarkingVisitor {
 public:
  explicit MutatorThreadMarkingVisitor(Marker*);
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_MARKING_VISITOR_H_
