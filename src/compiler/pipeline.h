// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PIPELINE_H_
#define V8_COMPILER_PIPELINE_H_

// Clients of this interface shouldn't depend on lots of compiler internals.
// Do not include anything from src/compiler here!
#include "src/objects.h"

namespace v8 {
namespace internal {

class CompilationInfo;
class CompilationJob;
class RegisterConfiguration;

namespace compiler {

class CallDescriptor;
class Graph;
class InstructionSequence;
class Linkage;
class PipelineData;
class Schedule;
class SourcePositionTable;

class Pipeline {
 public:
  explicit Pipeline(PipelineData* data) : data_(data) {}

  // Run the graph creation and initial optimization passes.
  bool CreateGraph();

  // Run the concurrent optimization passes.
  bool OptimizeGraph(Linkage* linkage);

  // Perform the actual code generation and return handle to a code object.
  Handle<Code> GenerateCode(Linkage* linkage);

  // Run the entire pipeline and generate a handle to a code object.
  Handle<Code> GenerateCode();

  // Run the pipeline on a machine graph and generate code. The {schedule} must
  // be valid, hence the given {graph} does not need to be schedulable.
  static Handle<Code> GenerateCodeForCodeStub(Isolate* isolate,
                                              CallDescriptor* call_descriptor,
                                              Graph* graph, Schedule* schedule,
                                              Code::Flags flags,
                                              const char* debug_name);

  // Run the entire pipeline and generate a handle to a code object suitable for
  // testing.
  static Handle<Code> GenerateCodeForTesting(CompilationInfo* info);

  // Run the pipeline on a machine graph and generate code. If {schedule} is
  // {nullptr}, then compute a new schedule for code generation.
  static Handle<Code> GenerateCodeForTesting(CompilationInfo* info,
                                             Graph* graph,
                                             Schedule* schedule = nullptr);

  // Run just the register allocator phases.
  static bool AllocateRegistersForTesting(const RegisterConfiguration* config,
                                          InstructionSequence* sequence,
                                          bool run_verifier);

  // Run the pipeline on a machine graph and generate code. If {schedule} is
  // {nullptr}, then compute a new schedule for code generation.
  static Handle<Code> GenerateCodeForTesting(CompilationInfo* info,
                                             CallDescriptor* call_descriptor,
                                             Graph* graph,
                                             Schedule* schedule = nullptr);

  // Returns a new compilation job for the given function.
  static CompilationJob* NewCompilationJob(Handle<JSFunction> function);

  // Returns a new compilation job for the WebAssembly compilation info.
  static CompilationJob* NewWasmCompilationJob(
      CompilationInfo* info, Graph* graph, CallDescriptor* descriptor,
      SourcePositionTable* source_positions);

 private:
  // The wasm compilation job calls ScheduleAndSelectInstructions and
  // RunPrintAndVerify, so we make it a member class.
  friend class PipelineWasmCompilationJob;

  // Helpers for executing pipeline phases.
  template <typename Phase>
  void Run();
  template <typename Phase, typename Arg0>
  void Run(Arg0 arg_0);
  template <typename Phase, typename Arg0, typename Arg1>
  void Run(Arg0 arg_0, Arg1 arg_1);

  void BeginPhaseKind(const char* phase_kind);
  void EndPhaseKind();
  bool ScheduleAndSelectInstructions(Linkage* linkage);
  void RunPrintAndVerify(const char* phase, bool untyped = false);
  Handle<Code> ScheduleAndGenerateCode(CallDescriptor* call_descriptor);
  void AllocateRegisters(const RegisterConfiguration* config,
                         CallDescriptor* descriptor, bool run_verifier);

  CompilationInfo* info() const;
  Isolate* isolate() const;

  PipelineData* const data_;

  DISALLOW_COPY_AND_ASSIGN(Pipeline);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PIPELINE_H_
