// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_H_
#define V8_COMPILER_PIPELINE_H_

#include "src/v8.h"

#include "src/compiler.h"

// Note: TODO(turbofan) implies a performance improvement opportunity,
//   and TODO(name) implies an incomplete implementation

namespace v8 {
namespace internal {
namespace compiler {

// Clients of this interface shouldn't depend on lots of compiler internals.
class Graph;
class Schedule;
class SourcePositionTable;
class Linkage;

class Pipeline {
 public:
  explicit Pipeline(CompilationInfo* info)
      : info_(info), context_specialization_(FLAG_context_specialization) {}

  // Run the entire pipeline and generate a handle to a code object.
  Handle<Code> GenerateCode();

  // Run the pipeline on a machine graph and generate code. If {schedule}
  // is {NULL}, then compute a new schedule for code generation.
  Handle<Code> GenerateCodeForMachineGraph(Linkage* linkage, Graph* graph,
                                           Schedule* schedule = NULL);

  static inline bool SupportedBackend() { return V8_TURBOFAN_BACKEND != 0; }
  static inline bool SupportedTarget() { return V8_TURBOFAN_TARGET != 0; }

  static void SetUp();
  static void TearDown();
  bool context_specialization() { return context_specialization_; }
  void set_context_specialization(bool context_specialization) {
    context_specialization_ = context_specialization;
  }

 private:
  CompilationInfo* info_;
  bool context_specialization_;

  CompilationInfo* info() const { return info_; }
  Isolate* isolate() { return info_->isolate(); }
  Zone* zone() { return info_->zone(); }

  Schedule* ComputeSchedule(Graph* graph);
  void VerifyAndPrintGraph(Graph* graph, const char* phase);
  Handle<Code> GenerateCode(Linkage* linkage, Graph* graph, Schedule* schedule,
                            SourcePositionTable* source_positions);
};
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_PIPELINE_H_
