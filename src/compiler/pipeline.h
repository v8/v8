// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_H_
#define V8_COMPILER_PIPELINE_H_

#include <fstream>  // NOLINT(readability/streams)

#include "src/v8.h"

#include "src/compiler.h"

// Note: TODO(turbofan) implies a performance improvement opportunity,
//   and TODO(name) implies an incomplete implementation

namespace v8 {
namespace internal {
namespace compiler {

// Clients of this interface shouldn't depend on lots of compiler internals.
class Graph;
class InstructionSequence;
class Linkage;
class RegisterAllocator;
class Schedule;
class SourcePositionTable;
class ZonePool;

class Pipeline {
 public:
  explicit Pipeline(CompilationInfo* info) : info_(info) {}

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

 private:
  CompilationInfo* info_;

  CompilationInfo* info() const { return info_; }
  Isolate* isolate() { return info_->isolate(); }
  Zone* zone() { return info_->zone(); }

  Schedule* ComputeSchedule(ZonePool* zone_pool, Graph* graph);
  void OpenTurboCfgFile(std::ofstream* stream);
  void PrintCompilationStart();
  void PrintScheduleAndInstructions(const char* phase, const Schedule* schedule,
                                    const SourcePositionTable* positions,
                                    const InstructionSequence* instructions);
  void PrintAllocator(const char* phase, const RegisterAllocator* allocator);
  void VerifyAndPrintGraph(Graph* graph, const char* phase,
                           bool untyped = false);
  Handle<Code> GenerateCode(ZonePool* zone_pool, Linkage* linkage, Graph* graph,
                            Schedule* schedule,
                            SourcePositionTable* source_positions);
};
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_PIPELINE_H_
