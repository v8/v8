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
class InstructionSequence;
class Linkage;
class PipelineData;
class RegisterConfiguration;
class Schedule;

class Pipeline {
 public:
  explicit Pipeline(CompilationInfo* info) : info_(info) {}

  // Run the entire pipeline and generate a handle to a code object.
  Handle<Code> GenerateCode();

  // Run the pipeline on a machine graph and generate code. If {schedule}
  // is {NULL}, then compute a new schedule for code generation.
  Handle<Code> GenerateCodeForMachineGraph(Linkage* linkage, Graph* graph,
                                           Schedule* schedule = NULL);

  static bool AllocateRegisters(const RegisterConfiguration* config,
                                InstructionSequence* sequence,
                                bool run_verifier);

  static inline bool SupportedBackend() { return V8_TURBOFAN_BACKEND != 0; }
  static inline bool SupportedTarget() { return V8_TURBOFAN_TARGET != 0; }

  static void SetUp();
  static void TearDown();

 private:
  CompilationInfo* info_;
  PipelineData* data_;

  // Helpers for executing pipeline phases.
  template <typename Phase>
  void Run();
  template <typename Phase, typename Arg0>
  void Run(Arg0 arg_0);

  CompilationInfo* info() const { return info_; }
  Isolate* isolate() { return info_->isolate(); }

  void BeginPhaseKind(const char* phase_kind);
  void RunPrintAndVerify(const char* phase, bool untyped = false);
  void GenerateCode(Linkage* linkage);
  void AllocateRegisters(const RegisterConfiguration* config,
                         bool run_verifier);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PIPELINE_H_
