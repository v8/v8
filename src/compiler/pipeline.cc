// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/pipeline.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>

#include "src/base/macros.h"
#include "src/builtins/builtins.h"
#include "src/builtins/profile-data-reader.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/compiler.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/reloc-info-inl.h"
#include "src/codegen/reloc-info.h"
#include "src/common/globals.h"
#include "src/common/high-allocation-throughput-scope.h"
#include "src/compiler/add-type-assertions-reducer.h"
#include "src/compiler/backend/bitcast-elider.h"
#include "src/compiler/backend/code-generator.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/backend/register-allocator-verifier.h"
#include "src/compiler/backend/register-allocator.h"
#include "src/compiler/basic-block-instrumentor.h"
#include "src/compiler/branch-elimination.h"
#include "src/compiler/bytecode-graph-builder.h"
#include "src/compiler/checkpoint-elimination.h"
#include "src/compiler/code-assembler-compilation-job.h"
#include "src/compiler/code-assembler.h"
#include "src/compiler/common-operator-reducer.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/constant-folding-reducer.h"
#include "src/compiler/csa-load-elimination.h"
#include "src/compiler/dead-code-elimination.h"
#include "src/compiler/escape-analysis-reducer.h"
#include "src/compiler/escape-analysis.h"
#include "src/compiler/graph-trimmer.h"
#include "src/compiler/js-call-reducer.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-create-lowering.h"
#include "src/compiler/js-generic-lowering.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/js-inlining-heuristic.h"
#include "src/compiler/js-intrinsic-lowering.h"
#include "src/compiler/js-native-context-specialization.h"
#include "src/compiler/js-typed-lowering.h"
#include "src/compiler/late-escape-analysis.h"
#include "src/compiler/linkage.h"
#include "src/compiler/load-elimination.h"
#include "src/compiler/loop-analysis.h"
#include "src/compiler/loop-peeling.h"
#include "src/compiler/loop-variable-optimizer.h"
#include "src/compiler/machine-graph-verifier.h"
#include "src/compiler/machine-operator-reducer.h"
#include "src/compiler/memory-optimizer.h"
#include "src/compiler/node-observer.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/osr.h"
#include "src/compiler/pair-load-store-reducer.h"
#include "src/compiler/phase.h"
#include "src/compiler/pipeline-data-inl.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/raw-machine-assembler.h"
#include "src/compiler/redundancy-elimination.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/simplified-lowering.h"
#include "src/compiler/simplified-operator-reducer.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/turbofan-graph-visualizer.h"
#include "src/compiler/turbofan-typer.h"
#include "src/compiler/turboshaft/build-graph-phase.h"
#include "src/compiler/turboshaft/debug-feature-lowering-phase.h"
#include "src/compiler/turboshaft/instruction-selection-phase.h"
#include "src/compiler/turboshaft/loop-peeling-phase.h"
#include "src/compiler/turboshaft/loop-unrolling-phase.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/pipelines.h"
#include "src/compiler/turboshaft/simplify-tf-loops.h"
#include "src/compiler/turboshaft/tracing.h"
#include "src/compiler/turboshaft/zone-with-name.h"
#include "src/compiler/type-narrowing-reducer.h"
#include "src/compiler/typed-optimization.h"
#include "src/compiler/value-numbering-reducer.h"
#include "src/compiler/verifier.h"
#include "src/compiler/zone-stats.h"
#include "src/diagnostics/code-tracer.h"
#include "src/diagnostics/disassembler.h"
#include "src/flags/flags.h"
#include "src/handles/handles-inl.h"
#include "src/heap/local-heap.h"
#include "src/heap/parked-scope.h"
#include "src/logging/code-events.h"
#include "src/logging/counters.h"
#include "src/logging/runtime-call-stats-scope.h"
#include "src/logging/runtime-call-stats.h"
#include "src/objects/code-kind.h"
#include "src/objects/shared-function-info.h"
#include "src/objects/string-inl.h"
#include "src/tracing/trace-event.h"
#include "src/utils/ostreams.h"
#include "src/utils/utils.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/int64-lowering.h"
#include "src/compiler/turboshaft/int64-lowering-phase.h"
#include "src/compiler/turboshaft/wasm-dead-code-elimination-phase.h"
#include "src/compiler/turboshaft/wasm-debug-memory-lowering-phase.h"
#include "src/compiler/turboshaft/wasm-gc-optimize-phase.h"
#include "src/compiler/turboshaft/wasm-lowering-phase.h"
#include "src/compiler/turboshaft/wasm-optimize-phase.h"
#include "src/compiler/turboshaft/wasm-turboshaft-compiler.h"
#include "src/compiler/wasm-compiler.h"
#include "src/compiler/wasm-escape-analysis.h"
#include "src/compiler/wasm-gc-lowering.h"
#include "src/compiler/wasm-gc-operator-reducer.h"
#include "src/compiler/wasm-load-elimination.h"
#include "src/compiler/wasm-typer.h"
#include "src/wasm/basic-block-calculator.h"
#include "src/wasm/function-body-decoder.h"
#include "src/wasm/function-compiler.h"
#include "src/wasm/turboshaft-graph-interface.h"
#include "src/wasm/wasm-builtin-list.h"
#include "src/wasm/wasm-code-coverage.h"
#include "src/wasm/wasm-disassembler.h"
#include "src/wasm/wasm-engine.h"

#if V8_TARGET_ARCH_ARM64
#include "src/compiler/turboshaft/wasm-simd-phase.h"
#endif  // V8_TARGET_ARCH_ARM64

#endif  // V8_ENABLE_WEBASSEMBLY

#if V8_ENABLE_WASM_SIMD256_REVEC
#include "src/compiler/revectorizer.h"
#include "src/compiler/turboshaft/wasm-revec-phase.h"
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

namespace v8 {
namespace internal {
namespace compiler {

static constexpr char kPipelineCompilationJobZoneName[] =
    "pipeline-compilation-job-zone";

#define RUN_MAYBE_ABORT(phase, ...) \
  if (V8_UNLIKELY(!Run<phase>(__VA_ARGS__))) return {};

#define PIPELINE_RUN_MAYBE_ABORT(pipeline, phase, ...) \
  if (V8_UNLIKELY(!(pipeline).Run<phase>(__VA_ARGS__))) return {};

class PipelineImpl final {
 public:
  explicit PipelineImpl(TFPipelineData* data) : data_(data) {}

  // Helpers for executing pipeline phases.
  template <turboshaft::TurbofanPhase Phase, typename... Args>
  V8_WARN_UNUSED_RESULT bool Run(Args&&... args);

  // Step A.1. Initialize the heap broker.
  V8_WARN_UNUSED_RESULT bool InitializeHeapBroker();

  // Step A.2. Run the graph creation and initial optimization passes.
  V8_WARN_UNUSED_RESULT bool CreateGraph(Linkage* linkage);

  // Step B. Run the concurrent optimization passes.
  bool OptimizeTurbofanGraph(Linkage* linkage);

  // Substep B.1. Produce a scheduled graph.
  V8_WARN_UNUSED_RESULT bool ComputeScheduledGraph();

#if V8_ENABLE_WASM_SIMD256_REVEC
  V8_WARN_UNUSED_RESULT bool Revectorize();
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

  // Substep B.3. Run register allocation on the instruction sequence.
  V8_WARN_UNUSED_RESULT bool AllocateRegisters(CallDescriptor* call_descriptor,
                                               bool has_dummy_end_block);

  // Step C. Run the code finalization pass.
  MaybeDirectHandle<Code> FinalizeCode(bool retire_broker = true);

  // Step D. Install any code dependencies.
  bool CommitDependencies(Handle<Code> code);

  void RunPrintAndVerify(const char* phase, bool untyped = false);

  TFPipelineData* data() const { return data_; }
  OptimizedCompilationInfo* info() const;
  Isolate* isolate() const;
  CodeGenerator* code_generator() const;

  ObserveNodeManager* observe_node_manager() const;

 private:
  TFPipelineData* const data_;
};

namespace {

class SourcePositionWrapper final : public Reducer {
 public:
  SourcePositionWrapper(Reducer* reducer, SourcePositionTable* table)
      : reducer_(reducer), table_(table) {}
  ~SourcePositionWrapper() final = default;
  SourcePositionWrapper(const SourcePositionWrapper&) = delete;
  SourcePositionWrapper& operator=(const SourcePositionWrapper&) = delete;

  const char* reducer_name() const override { return reducer_->reducer_name(); }

  Reduction Reduce(Node* node) final {
    SourcePosition const pos = table_->GetSourcePosition(node);
    SourcePositionTable::Scope position(table_, pos);
    return reducer_->Reduce(node, nullptr);
  }

  void Finalize() final { reducer_->Finalize(); }

 private:
  Reducer* const reducer_;
  SourcePositionTable* const table_;
};

class NodeOriginsWrapper final : public Reducer {
 public:
  NodeOriginsWrapper(Reducer* reducer, NodeOriginTable* table)
      : reducer_(reducer), table_(table) {}
  ~NodeOriginsWrapper() final = default;
  NodeOriginsWrapper(const NodeOriginsWrapper&) = delete;
  NodeOriginsWrapper& operator=(const NodeOriginsWrapper&) = delete;

  const char* reducer_name() const override { return reducer_->reducer_name(); }

  Reduction Reduce(Node* node) final {
    NodeOriginTable::Scope position(table_, reducer_name(), node);
    return reducer_->Reduce(node, nullptr);
  }

  void Finalize() final { reducer_->Finalize(); }

 private:
  Reducer* const reducer_;
  NodeOriginTable* const table_;
};

class V8_NODISCARD PipelineRunScope {
 public:
#ifdef V8_RUNTIME_CALL_STATS
  PipelineRunScope(
      TFPipelineData* data, const char* phase_name,
      RuntimeCallCounterId runtime_call_counter_id,
      RuntimeCallStats::CounterMode counter_mode = RuntimeCallStats::kExact)
      : phase_scope_(data->pipeline_statistics(), phase_name),
        zone_scope_(data->zone_stats(), phase_name),
        origin_scope_(data->node_origins(), phase_name),
        runtime_call_timer_scope(data->runtime_call_stats(),
                                 runtime_call_counter_id, counter_mode) {
    DCHECK_NOT_NULL(phase_name);
  }
#else   // V8_RUNTIME_CALL_STATS
  PipelineRunScope(TFPipelineData* data, const char* phase_name)
      : phase_scope_(data->pipeline_statistics(), phase_name),
        zone_scope_(data->zone_stats(), phase_name),
        origin_scope_(data->node_origins(), phase_name) {
    DCHECK_NOT_NULL(phase_name);
  }
#endif  // V8_RUNTIME_CALL_STATS

  Zone* zone() { return zone_scope_.zone(); }

 private:
  PhaseScope phase_scope_;
  ZoneStats::Scope zone_scope_;
  NodeOriginTable::PhaseScope origin_scope_;
#ifdef V8_RUNTIME_CALL_STATS
  RuntimeCallTimerScope runtime_call_timer_scope;
#endif  // V8_RUNTIME_CALL_STATS
};

// LocalIsolateScope encapsulates the phase where persistent handles are
// attached to the LocalHeap inside {local_isolate}.
class V8_NODISCARD LocalIsolateScope {
 public:
  explicit LocalIsolateScope(JSHeapBroker* broker,
                             OptimizedCompilationInfo* info,
                             LocalIsolate* local_isolate)
      : broker_(broker), info_(info) {
    broker_->AttachLocalIsolate(info_, local_isolate);
    info_->tick_counter().AttachLocalHeap(local_isolate->heap());
  }

  ~LocalIsolateScope() {
    info_->tick_counter().DetachLocalHeap();
    broker_->DetachLocalIsolate(info_);
  }

 private:
  JSHeapBroker* broker_;
  OptimizedCompilationInfo* info_;
};

void PrintFunctionSource(OptimizedCompilationInfo* info, Isolate* isolate,
                         int source_id,
                         DirectHandle<SharedFunctionInfo> shared) {
  if (!IsUndefined(shared->script(), isolate)) {
    DirectHandle<Script> script(Cast<Script>(shared->script()), isolate);

    if (!IsUndefined(script->source(), isolate)) {
      CodeTracer::StreamScope tracing_scope(isolate->GetCodeTracer());
      Tagged<Object> source_name = script->name();
      auto& os = tracing_scope.stream();
      os << "--- FUNCTION SOURCE (";
      if (IsString(source_name)) {
        os << Cast<String>(source_name)->ToCString().get() << ":";
      }
      os << shared->DebugNameCStr().get() << ") id{";
      os << info->optimization_id() << "," << source_id << "} start{";
      os << shared->StartPosition() << "} ---\n";
      {
        DisallowGarbageCollection no_gc;
        int start = shared->StartPosition();
        int len = shared->EndPosition() - start;
        SubStringRange source(Cast<String>(script->source()), no_gc, start,
                              len);
        for (auto c : source) {
          os << AsReversiblyEscapedUC16(c);
        }
      }

      os << "\n--- END ---\n";
    }
  }
}

// Print information for the given inlining: which function was inlined and
// where the inlining occurred.
void PrintInlinedFunctionInfo(
    OptimizedCompilationInfo* info, Isolate* isolate, int source_id,
    int inlining_id, const OptimizedCompilationInfo::InlinedFunctionHolder& h) {
  CodeTracer::StreamScope tracing_scope(isolate->GetCodeTracer());
  auto& os = tracing_scope.stream();
  os << "INLINE (" << h.shared_info->DebugNameCStr().get() << ") id{"
     << info->optimization_id() << "," << source_id << "} AS " << inlining_id
     << " AT ";
  const SourcePosition position = h.position.position;
  if (position.IsKnown()) {
    os << "<" << position.InliningId() << ":" << position.ScriptOffset() << ">";
  } else {
    os << "<?>";
  }
  os << std::endl;
}

// Print the source of all functions that participated in this optimizing
// compilation. For inlined functions print source position of their inlining.
void PrintParticipatingSource(OptimizedCompilationInfo* info,
                              Isolate* isolate) {
  SourceIdAssigner id_assigner(info->inlined_functions().size());
  PrintFunctionSource(info, isolate, -1, info->shared_info());
  const auto& inlined = info->inlined_functions();
  for (unsigned id = 0; id < inlined.size(); id++) {
    const int source_id = id_assigner.GetIdFor(inlined[id].shared_info);
    PrintFunctionSource(info, isolate, source_id, inlined[id].shared_info);
    PrintInlinedFunctionInfo(info, isolate, source_id, id, inlined[id]);
  }
}

void TraceScheduleAndVerify(OptimizedCompilationInfo* info,
                            TFPipelineData* data, Schedule* schedule,
                            const char* phase_name) {
  RCS_SCOPE(data->runtime_call_stats(),
            RuntimeCallCounterId::kOptimizeTraceScheduleAndVerify,
            RuntimeCallStats::kThreadSpecific);
  TRACE_EVENT0(TurbofanPipelineStatistics::kTraceCategory,
               "V8.TraceScheduleAndVerify");

  TraceSchedule(info, data, schedule, phase_name);

  if (v8_flags.turbo_verify) ScheduleVerifier::Run(schedule);
}

void AddReducer(TFPipelineData* data, GraphReducer* graph_reducer,
                Reducer* reducer) {
  if (data->info()->source_positions()) {
    SourcePositionWrapper* const wrapper =
        data->graph_zone()->New<SourcePositionWrapper>(
            reducer, data->source_positions());
    reducer = wrapper;
  }
  if (data->info()->trace_turbo_json()) {
    NodeOriginsWrapper* const wrapper =
        data->graph_zone()->New<NodeOriginsWrapper>(reducer,
                                                    data->node_origins());
    reducer = wrapper;
  }

  graph_reducer->AddReducer(reducer);
}

TurbofanPipelineStatistics* CreatePipelineStatistics(
    DirectHandle<Script> script, OptimizedCompilationInfo* info,
    Isolate* isolate, ZoneStats* zone_stats) {
  TurbofanPipelineStatistics* pipeline_statistics = nullptr;

  bool tracing_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(TRACE_DISABLED_BY_DEFAULT("v8.turbofan"),
                                     &tracing_enabled);
  if (tracing_enabled || v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics = new TurbofanPipelineStatistics(
        info, isolate->GetTurboStatistics(), zone_stats);
    pipeline_statistics->BeginPhaseKind("V8.TFInitializing");
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    json_of << "{\"function\" : ";
    JsonPrintFunctionSource(json_of, -1, info->GetDebugName(), script, isolate,
                            info->shared_info());
    json_of << ",\n\"phases\":[";
  }

  return pipeline_statistics;
}

#if V8_ENABLE_WEBASSEMBLY
TurbofanPipelineStatistics* CreatePipelineStatistics(
    WasmCompilationData& compilation_data, const wasm::WasmModule* wasm_module,
    OptimizedCompilationInfo* info, ZoneStats* zone_stats) {
  TurbofanPipelineStatistics* pipeline_statistics = nullptr;

  bool tracing_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("v8.wasm.turbofan"), &tracing_enabled);
  if (tracing_enabled || v8_flags.turbo_stats_wasm) {
    pipeline_statistics = new TurbofanPipelineStatistics(
        info, wasm::GetWasmEngine()->GetOrCreateTurboStatistics(), zone_stats);
    pipeline_statistics->BeginPhaseKind("V8.WasmInitializing");
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    std::unique_ptr<char[]> function_name = info->GetDebugName();
    json_of << "{\"function\":\"" << function_name.get() << "\", \"source\":\"";
    std::ostringstream disassembly;
    std::vector<uint32_t> source_positions;
    base::Vector<const uint8_t> function_bytes{compilation_data.func_body.start,
                                               compilation_data.body_size()};
    base::Vector<const uint8_t> module_bytes{nullptr, 0};
    std::optional<wasm::ModuleWireBytes> maybe_wire_bytes =
        compilation_data.wire_bytes_storage->GetModuleBytes();
    if (maybe_wire_bytes) module_bytes = maybe_wire_bytes->module_bytes();

    wasm::DisassembleFunction(
        wasm_module, compilation_data.func_index, function_bytes, module_bytes,
        compilation_data.func_body.offset, disassembly, &source_positions);
    for (const auto& c : disassembly.str()) {
      json_of << AsEscapedUC16ForJSON(c);
    }
    json_of << "\",\n\"sourceLineToBytecodePosition\" : [";
    bool insert_comma = false;
    for (auto val : source_positions) {
      if (insert_comma) {
        json_of << ", ";
      }
      json_of << val;
      insert_comma = true;
    }
    json_of << "],\n\"phases\":[";
  }

  return pipeline_statistics;
}
#endif  // V8_ENABLE_WEBASSEMBLY

// This runs instruction selection, register allocation and code generation.
[[nodiscard]] bool GenerateCodeFromTurboshaftGraph(
    Linkage* linkage, turboshaft::Pipeline& turboshaft_pipeline,
    PipelineImpl* turbofan_pipeline = nullptr,
    std::shared_ptr<OsrHelper> osr_helper = {}) {
  turboshaft::PipelineData* turboshaft_data = turboshaft_pipeline.data();
  turboshaft_data->InitializeCodegenComponent(osr_helper);
  // Run Turboshaft instruction selection.
  if (!turboshaft_pipeline.PrepareForInstructionSelection()) return false;
  if (!turboshaft_pipeline.SelectInstructions(linkage)) return false;
  // We can release the graph now.
  turboshaft_data->ClearGraphComponent();

  if (!turboshaft_pipeline.AllocateRegisters(
          linkage->GetIncomingDescriptor())) {
    return false;
  }
  if (!turboshaft_pipeline.AssembleCode(linkage)) {
    return false;
  }
  return true;
}

}  // namespace

class PipelineCompilationJob final : public TurbofanCompilationJob {
 public:
  PipelineCompilationJob(Isolate* isolate,
                         Handle<SharedFunctionInfo> shared_info,
                         Handle<JSFunction> function, BytecodeOffset osr_offset,
                         CodeKind code_kind);
  ~PipelineCompilationJob() final;
  PipelineCompilationJob(const PipelineCompilationJob&) = delete;
  PipelineCompilationJob& operator=(const PipelineCompilationJob&) = delete;

 protected:
  Status PrepareJobImpl(Isolate* isolate) final;
  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) final;
  Status FinalizeJobImpl(Isolate* isolate) final;

 private:
  Zone zone_;
  ZoneStats zone_stats_;
  OptimizedCompilationInfo compilation_info_;
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics_;
  TFPipelineData data_;
  turboshaft::PipelineData turboshaft_data_;
  PipelineImpl pipeline_;
  Linkage* linkage_;
};

PipelineCompilationJob::PipelineCompilationJob(
    Isolate* isolate, Handle<SharedFunctionInfo> shared_info,
    Handle<JSFunction> function, BytecodeOffset osr_offset, CodeKind code_kind)
    // Note that the OptimizedCompilationInfo is not initialized at the time
    // we pass it to the CompilationJob constructor, but it is not
    // dereferenced there.
    : TurbofanCompilationJob(isolate, &compilation_info_,
                             CompilationJob::State::kReadyToPrepare),
      zone_(isolate->allocator(), kPipelineCompilationJobZoneName),
      zone_stats_(isolate->allocator()),
      compilation_info_(&zone_, isolate, shared_info, function, code_kind,
                        osr_offset),
      pipeline_statistics_(CreatePipelineStatistics(
          direct_handle(Cast<Script>(shared_info->script()), isolate),
          compilation_info(), isolate, &zone_stats_)),
      data_(&zone_stats_, isolate, compilation_info(),
            pipeline_statistics_.get()),
      turboshaft_data_(&zone_stats_, turboshaft::TurboshaftPipelineKind::kJS,
                       isolate, compilation_info(),
                       AssemblerOptions::Default(isolate)),
      pipeline_(&data_),
      linkage_(nullptr) {
  turboshaft_data_.set_pipeline_statistics(pipeline_statistics_.get());
}

PipelineCompilationJob::~PipelineCompilationJob() = default;

void TraceSchedule(OptimizedCompilationInfo* info, TFPipelineData* data,
                   Schedule* schedule, const char* phase_name) {
  if (info->trace_turbo_json()) {
    UnparkedScopeIfNeeded scope(data->broker());
    AllowHandleDereference allow_deref;

    TurboJsonFile json_of(info, std::ios_base::app);
    json_of << "{\"name\":\"" << phase_name << "\",\"type\":\"schedule\""
            << ",\"data\":\"";
    std::stringstream schedule_stream;
    schedule_stream << *schedule;
    std::string schedule_string(schedule_stream.str());
    for (const auto& c : schedule_string) {
      json_of << AsEscapedUC16ForJSON(c);
    }
    json_of << "\"},\n";
  }

  if (info->trace_turbo_graph() || v8_flags.trace_turbo_scheduler) {
    UnparkedScopeIfNeeded scope(data->broker());
    AllowHandleDereference allow_deref;

    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
    tracing_scope.stream() << "----- " << phase_name << " -----\n" << *schedule;
  }
}

// Print the code after compiling it.
void PrintCode(Isolate* isolate, DirectHandle<Code> code,
               OptimizedCompilationInfo* info) {
  if (v8_flags.print_opt_source && info->IsOptimizing()) {
    PrintParticipatingSource(info, isolate);
  }

#ifdef ENABLE_DISASSEMBLER
  const bool print_code =
      v8_flags.print_code ||
      (info->IsOptimizing() && v8_flags.print_opt_code &&
       info->shared_info()->PassesFilter(v8_flags.print_opt_code_filter));
  if (print_code) {
    std::unique_ptr<char[]> debug_name = info->GetDebugName();
    CodeTracer::StreamScope tracing_scope(isolate->GetCodeTracer());
    std::ostream& os = tracing_scope.stream();

    // Print the source code if available.
    const bool print_source = info->IsOptimizing();
    if (print_source) {
      DirectHandle<SharedFunctionInfo> shared = info->shared_info();
      if (IsScript(shared->script()) &&
          !IsUndefined(Cast<Script>(shared->script())->source(), isolate)) {
        os << "--- Raw source ---\n";
        StringCharacterStream stream(
            Cast<String>(Cast<Script>(shared->script())->source()),
            shared->StartPosition());
        // fun->end_position() points to the last character in the stream. We
        // need to compensate by adding one to calculate the length.
        int source_len = shared->EndPosition() - shared->StartPosition() + 1;
        for (int i = 0; i < source_len; i++) {
          if (stream.HasMore()) {
            os << AsReversiblyEscapedUC16(stream.GetNext());
          }
        }
        os << "\n\n";
      }
    }
    if (info->IsOptimizing()) {
      os << "--- Optimized code ---\n"
         << "optimization_id = " << info->optimization_id() << "\n";
    } else {
      os << "--- Code ---\n";
    }
    if (print_source) {
      DirectHandle<SharedFunctionInfo> shared = info->shared_info();
      os << "source_position = " << shared->StartPosition() << "\n";
    }
    code->Disassemble(debug_name.get(), os, isolate);
    os << "--- End code ---\n";
  }
#endif  // ENABLE_DISASSEMBLER
}

// The CheckMaps node can migrate objects with deprecated maps. Afterwards, we
// check the resulting object against a fixed list of maps known at compile
// time. This is problematic if we made any assumptions about an object with the
// deprecated map, as it now changed shape. Therefore, we want to avoid
// embedding deprecated maps, as objects with these maps can be changed by
// CheckMaps.
// The following code only checks for deprecated maps at the end of compilation,
// but doesn't protect us against the embedded maps becoming deprecated later.
// However, this is enough, since if the map becomes deprecated later, it will
// migrate to a new map not yet known at compile time, so if we migrate to it as
// part of a CheckMaps, this check will always fail afterwards and deoptimize.
// This in turn relies on a runtime invariant that map migrations always target
// newly allocated maps.
bool CheckNoDeprecatedMaps(DirectHandle<Code> code, Isolate* isolate) {
  int mode_mask = RelocInfo::EmbeddedObjectModeMask();
  for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
    DCHECK(RelocInfo::IsEmbeddedObjectMode(it.rinfo()->rmode()));
    Tagged<HeapObject> obj = it.rinfo()->target_object(isolate);
    if (IsMap(obj) && Cast<Map>(obj)->is_deprecated()) {
      return false;
    }
  }
  return true;
}

namespace {
// Ensure that the RuntimeStats table is set on the PipelineData for
// duration of the job phase and unset immediately afterwards. Each job
// needs to set the correct RuntimeCallStats table depending on whether it
// is running on a background or foreground thread.
class V8_NODISCARD PipelineJobScope {
 public:
  PipelineJobScope(TFPipelineData* data, RuntimeCallStats* stats)
      : data_(data), current_broker_(data_->broker()) {
    data_->set_runtime_call_stats(stats);
  }
  PipelineJobScope(turboshaft::PipelineData* turboshaft_data,
                   RuntimeCallStats* stats)
      : turboshaft_data_(turboshaft_data),
        current_broker_(turboshaft_data_->broker()) {
    turboshaft_data_->set_runtime_call_stats(stats);
  }

  ~PipelineJobScope() {
    if (data_) data_->set_runtime_call_stats(nullptr);
    if (turboshaft_data_) turboshaft_data_->set_runtime_call_stats(nullptr);
  }

 private:
  HighAllocationThroughputScope high_throughput_scope_{
      V8::GetCurrentPlatform()};
  TFPipelineData* data_ = nullptr;
  turboshaft::PipelineData* turboshaft_data_ = nullptr;
  CurrentHeapBrokerScope current_broker_;
};
}  // namespace

PipelineCompilationJob::Status PipelineCompilationJob::PrepareJobImpl(
    Isolate* isolate) {
  // Ensure that the RuntimeCallStats table of main thread is available for
  // phases happening during PrepareJob.
  PipelineJobScope scope(&data_, isolate->counters()->runtime_call_stats());

  if (compilation_info()->bytecode_array()->length() >
      v8_flags.max_optimized_bytecode_size) {
    return AbortOptimization(BailoutReason::kFunctionTooBig);
  }
  // TODO(victorgomes): We can remove this compilation option. This should
  // always be true.
  compilation_info()->set_bailout_on_uninitialized();
  if (v8_flags.turbo_loop_peeling) {
    compilation_info()->set_loop_peeling();
  }
  if (v8_flags.turbo_inlining) {
    compilation_info()->set_inlining();
  }
  if (v8_flags.turbo_allocation_folding) {
    compilation_info()->set_allocation_folding();
  }

  // Determine whether to specialize the code for the function's context.
  // We can't do this in the case of OSR, because we want to cache the
  // generated code on the native context keyed on SharedFunctionInfo.
  // TODO(mythria): Check if it is better to key the OSR cache on JSFunction and
  // allow context specialization for OSR code.
  if (!compilation_info()
           ->shared_info()
           ->function_context_independent_compiled() &&
      compilation_info()->closure()->raw_feedback_cell()->map() ==
          ReadOnlyRoots(isolate).one_closure_cell_map() &&
      !compilation_info()->is_osr()) {
    compilation_info()->set_function_context_specializing();
    data_.ChooseSpecializationContext();
  }

  if (compilation_info()->source_positions()) {
    SharedFunctionInfo::EnsureSourcePositionsAvailable(
        isolate, compilation_info()->shared_info());
  }

  data_.set_start_source_position(
      compilation_info()->shared_info()->StartPosition());

  linkage_ = compilation_info()->zone()->New<Linkage>(
      Linkage::ComputeIncoming(compilation_info()->zone(), compilation_info()));

  if (compilation_info()->is_osr()) data_.InitializeOsrHelper();

  // InitializeHeapBroker() and CreateGraph() may already use
  // IsPendingAllocation.
  isolate->heap()->PublishMainThreadPendingAllocations();

  if (!pipeline_.InitializeHeapBroker()) {
    return FAILED;
  }

  // Serialization may have allocated.
  isolate->heap()->PublishMainThreadPendingAllocations();

  return SUCCEEDED;
}

PipelineCompilationJob::Status PipelineCompilationJob::ExecuteJobImpl(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  // Ensure that the RuntimeCallStats table is only available during execution
  // and not during finalization as that might be on a different thread.
  PipelineJobScope scope(&data_, stats);
  LocalIsolateScope local_isolate_scope(data_.broker(), data_.info(),
                                        local_isolate);

  turboshaft_data_.InitializeBrokerAndDependencies(data_.broker_ptr(),
                                                   data_.dependencies());
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data_);
  if (compilation_info_.was_cancelled()) {
    return AbortOptimization(BailoutReason::kCancelled);
  }

  if (V8_UNLIKELY(v8_flags.turbolev)) {
    if (!turboshaft_pipeline.CreateGraphWithMaglev(linkage_)) {
      return AbortOptimization(BailoutReason::kTurbofanGraphBuildingFailed);
    }
  } else {
    if (!pipeline_.CreateGraph(linkage_)) {
      return AbortOptimization(BailoutReason::kTurbofanGraphBuildingFailed);
    }

    // We selectively Unpark inside OptimizeTurbofanGraph.
    if (!pipeline_.OptimizeTurbofanGraph(linkage_)) return FAILED;

    // We convert the turbofan graph to turboshaft.
    if (!turboshaft_pipeline.CreateGraphFromTurbofan(&data_, linkage_)) {
      data_.EndPhaseKind();
      return FAILED;
    }
  }

  if (!turboshaft_pipeline.OptimizeTurboshaftGraph(linkage_)) {
    return FAILED;
  }

  const bool success = GenerateCodeFromTurboshaftGraph(
      linkage_, turboshaft_pipeline, &pipeline_, data_.osr_helper_ptr());
  return success ? SUCCEEDED : FAILED;
}

PipelineCompilationJob::Status PipelineCompilationJob::FinalizeJobImpl(
    Isolate* isolate) {
  // Ensure that the RuntimeCallStats table of main thread is available for
  // phases happening during PrepareJob.
  PipelineJobScope scope(&data_, isolate->counters()->runtime_call_stats());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeFinalizePipelineJob);
  Handle<Code> code;
  DirectHandle<NativeContext> context;
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data_);
  MaybeHandle<Code> maybe_code = turboshaft_pipeline.FinalizeCode();
  if (!maybe_code.ToHandle(&code)) {
    if (compilation_info()->bailout_reason() == BailoutReason::kNoReason) {
      return AbortOptimization(BailoutReason::kTurbofanCodeGenerationFailed);
    }
    return FAILED;
  }
  context = DirectHandle<NativeContext>(compilation_info()->native_context(),
                                        isolate);
  if (context->IsDetached(isolate)) {
    return AbortOptimization(BailoutReason::kDetachedNativeContext);
  }
  if (!CheckNoDeprecatedMaps(code, isolate)) {
    return RetryOptimization(BailoutReason::kConcurrentMapDeprecation);
  }
  if (!turboshaft_pipeline.CommitDependencies(code)) {
    return RetryOptimization(BailoutReason::kBailedOutDueToDependencyChange);
  }
  compilation_info()->SetCode(code);
  GlobalHandleVector<Map> maps = CollectRetainedMaps(isolate, code);
  RegisterWeakObjectsInOptimizedCode(isolate, context, code, std::move(maps));
  return SUCCEEDED;
}

template <turboshaft::TurbofanPhase Phase, typename... Args>
bool PipelineImpl::Run(Args&&... args) {
#ifdef V8_RUNTIME_CALL_STATS
  PipelineRunScope scope(this->data_, Phase::phase_name(),
                         Phase::kRuntimeCallCounterId, Phase::kCounterMode);
#else
  PipelineRunScope scope(this->data_, Phase::phase_name());
#endif
  Phase phase;
  static_assert(Phase::kKind == PhaseKind::kTurbofan);
  phase.Run(this->data_, scope.zone(), std::forward<Args>(args)...);
  return !info()->was_cancelled();
}

struct GraphBuilderPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(BytecodeGraphBuilder)

  void Run(TFPipelineData* data, Zone* temp_zone, Linkage* linkage) {
    BytecodeGraphBuilderFlags flags;
    if (data->info()->analyze_environment_liveness()) {
      flags |= BytecodeGraphBuilderFlag::kAnalyzeEnvironmentLiveness;
    }
    if (data->info()->bailout_on_uninitialized()) {
      flags |= BytecodeGraphBuilderFlag::kBailoutOnUninitialized;
    }

    JSHeapBroker* broker = data->broker();
    UnparkedScopeIfNeeded scope(broker);
    SharedFunctionInfoRef sfi_ref =
        MakeRef(broker, data->info()->closure()->shared());
    FeedbackCellRef feedback_cell_ref =
        MakeRef(broker, data->info()->closure()->raw_feedback_cell());
    BytecodeArrayRef bytecode = MakeRef(broker, data->info()->bytecode_array());
    CallFrequency frequency(1.0f);
    BuildGraphFromBytecode(broker, temp_zone, sfi_ref, bytecode,
                           feedback_cell_ref, data->info()->osr_offset(),
                           data->jsgraph(), frequency, data->source_positions(),
                           data->node_origins(), SourcePosition::kNotInlined,
                           data->info()->code_kind(), flags,
                           &data->info()->tick_counter(),
                           ObserveNodeInfo{data->observe_node_manager(),
                                           data->info()->node_observer()});

    // We need to be certain that the parameter count reported by our output
    // Code object matches what the code we compile expects. Otherwise, this
    // may lead to effectively signature mismatches during function calls. This
    // CHECK is a defense-in-depth measure to ensure this doesn't happen.
    SBXCHECK_EQ(
        StartNode(data->jsgraph()->graph()->start()).FormalParameterCount(),
        linkage->GetIncomingDescriptor()->ParameterSlotCount());
  }
};

struct InliningPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(Inlining)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    OptimizedCompilationInfo* info = data->info();
    GraphReducer graph_reducer(temp_zone, data->graph(), &info->tick_counter(),
                               data->broker(), data->jsgraph()->Dead(),
                               data->observe_node_manager());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    CheckpointElimination checkpoint_elimination(&graph_reducer);
    CommonOperatorReducer common_reducer(
        &graph_reducer, data->graph(), data->broker(), data->common(),
        data->machine(), temp_zone, BranchSemantics::kJS);
    JSCallReducer::Flags call_reducer_flags = JSCallReducer::kNoFlags;
    if (data->info()->bailout_on_uninitialized()) {
      call_reducer_flags |= JSCallReducer::kBailoutOnUninitialized;
    }
    if (data->info()->inline_js_wasm_calls() && data->info()->inlining()) {
      call_reducer_flags |= JSCallReducer::kInlineJSToWasmCalls;
    }
    JSCallReducer call_reducer(&graph_reducer, data->jsgraph(), data->broker(),
                               temp_zone, call_reducer_flags);
    JSContextSpecialization context_specialization(
        &graph_reducer, data->jsgraph(), data->broker(),
        data->specialization_context(),
        data->info()->function_context_specializing()
            ? data->info()->closure()
            : MaybeHandle<JSFunction>());
    JSNativeContextSpecialization::Flags flags =
        JSNativeContextSpecialization::kNoFlags;
    if (data->info()->bailout_on_uninitialized()) {
      flags |= JSNativeContextSpecialization::kBailoutOnUninitialized;
    }
    // Passing the OptimizedCompilationInfo's shared zone here as
    // JSNativeContextSpecialization allocates out-of-heap objects
    // that need to live until code generation.
    JSNativeContextSpecialization native_context_specialization(
        &graph_reducer, data->jsgraph(), data->broker(), flags, temp_zone,
        info->zone());
    JSInliningHeuristic inlining(
        &graph_reducer, temp_zone, data->info(), data->jsgraph(),
        data->broker(), data->source_positions(), data->node_origins(),
        JSInliningHeuristic::kJSOnly, nullptr, nullptr);

    JSIntrinsicLowering intrinsic_lowering(&graph_reducer, data->jsgraph(),
                                           data->broker());
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    AddReducer(data, &graph_reducer, &checkpoint_elimination);
    AddReducer(data, &graph_reducer, &common_reducer);
    AddReducer(data, &graph_reducer, &native_context_specialization);
    AddReducer(data, &graph_reducer, &context_specialization);
    AddReducer(data, &graph_reducer, &intrinsic_lowering);
    AddReducer(data, &graph_reducer, &call_reducer);
    if (data->info()->inlining()) {
      AddReducer(data, &graph_reducer, &inlining);
    }
    graph_reducer.ReduceGraph();
    info->set_inlined_bytecode_size(inlining.total_inlined_bytecode_size());

#if V8_ENABLE_WEBASSEMBLY
    // Not forwarding this information to the TurboFan pipeline data here later
    // skips `JSWasmInliningPhase` if there are no JS-to-Wasm functions calls.
    if (call_reducer.has_js_wasm_calls()) {
      const wasm::NativeModule* native_module =
          call_reducer.wasm_native_module_for_inlining();
      DCHECK_NOT_NULL(native_module);
      data->set_wasm_native_module_for_inlining(native_module);
      // Enable source positions if not enabled yet. While JS only uses the
      // source position table for tracing, profiling, ..., wasm needs it at
      // compile time for keeping track of source locations for wasm traps.
      // Note: By not setting data->info()->set_source_positions(), even with
      // wasm inlining, source positions shouldn't be kept alive after
      // compilation is finished (if not for tracing, ...)
      if (!data->source_positions()->IsEnabled()) {
        data->source_positions()->Enable();
        data->source_positions()->AddDecorator();
      }
    }
#endif
  }
};

#if V8_ENABLE_WEBASSEMBLY
struct JSWasmInliningPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(JSWasmInlining)
  void Run(TFPipelineData* data, Zone* temp_zone) {
    DCHECK(data->has_js_wasm_calls());
    DCHECK_NOT_NULL(data->wasm_native_module_for_inlining());

    OptimizedCompilationInfo* info = data->info();
    GraphReducer graph_reducer(temp_zone, data->graph(), &info->tick_counter(),
                               data->broker(), data->jsgraph()->Dead());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    CommonOperatorReducer common_reducer(
        &graph_reducer, data->graph(), data->broker(), data->common(),
        data->machine(), temp_zone, BranchSemantics::kMachine);
    // If we want to inline in Turboshaft instead (i.e., later in the
    // pipeline), only inline the wrapper here in TurboFan.
    // TODO(dlehmann,353475584): Long-term, also inline the JS-to-Wasm wrappers
    // in Turboshaft (or in Maglev, depending on the shared frontend).
    JSInliningHeuristic::Mode mode =
        (v8_flags.turboshaft_wasm_in_js_inlining)
            ? JSInliningHeuristic::kWasmWrappersOnly
            : JSInliningHeuristic::kWasmFullInlining;
    JSInliningHeuristic inlining(&graph_reducer, temp_zone, data->info(),
                                 data->jsgraph(), data->broker(),
                                 data->source_positions(), data->node_origins(),
                                 mode, data->wasm_native_module_for_inlining(),
                                 data->js_wasm_calls_sidetable());
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    AddReducer(data, &graph_reducer, &common_reducer);
    AddReducer(data, &graph_reducer, &inlining);
    graph_reducer.ReduceGraph();
  }
};

struct JSWasmLoweringPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(JSWasmLowering)
  void Run(TFPipelineData* data, Zone* temp_zone) {
    DCHECK(data->has_js_wasm_calls());
    DCHECK_NOT_NULL(data->wasm_native_module_for_inlining());

    OptimizedCompilationInfo* info = data->info();
    GraphReducer graph_reducer(temp_zone, data->graph(), &info->tick_counter(),
                               data->broker(), data->jsgraph()->Dead());
    // The Wasm trap handler is not supported in JavaScript.
    const bool disable_trap_handler = true;
    WasmGCLowering lowering(&graph_reducer, data->jsgraph(),
                            data->wasm_native_module_for_inlining()->module(),
                            disable_trap_handler, data->source_positions());
    AddReducer(data, &graph_reducer, &lowering);
    graph_reducer.ReduceGraph();
  }
};
#endif  // V8_ENABLE_WEBASSEMBLY

struct EarlyGraphTrimmingPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(EarlyGraphTrimming)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphTrimmer trimmer(temp_zone, data->graph());
    NodeVector roots(temp_zone);
    data->jsgraph()->GetCachedNodes(&roots);
    UnparkedScopeIfNeeded scope(data->broker(), v8_flags.trace_turbo_trimming);
    trimmer.TrimGraph(roots.begin(), roots.end());
  }
};

struct TyperPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(Typer)

  void Run(TFPipelineData* data, Zone* temp_zone, Typer* typer) {
    NodeVector roots(temp_zone);
    data->jsgraph()->GetCachedNodes(&roots);

    // Make sure we always type True and False. Needed for escape analysis.
    roots.push_back(data->jsgraph()->TrueConstant());
    roots.push_back(data->jsgraph()->FalseConstant());

    LoopVariableOptimizer induction_vars(data->jsgraph()->graph(),
                                         data->common(), temp_zone);
    if (v8_flags.turbo_loop_variable) induction_vars.Run();

    // The typer inspects heap objects, so we need to unpark the local heap.
    UnparkedScopeIfNeeded scope(data->broker());
    typer->Run(roots, &induction_vars);
  }
};

struct UntyperPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(Untyper)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    class RemoveTypeReducer final : public Reducer {
     public:
      const char* reducer_name() const override { return "RemoveTypeReducer"; }
      Reduction Reduce(Node* node) final {
        if (NodeProperties::IsTyped(node)) {
          NodeProperties::RemoveType(node);
          return Changed(node);
        }
        return NoChange();
      }
    };

    NodeVector roots(temp_zone);
    data->jsgraph()->GetCachedNodes(&roots);
    for (Node* node : roots) {
      NodeProperties::RemoveType(node);
    }

    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    RemoveTypeReducer remove_type_reducer;
    AddReducer(data, &graph_reducer, &remove_type_reducer);
    graph_reducer.ReduceGraph();
  }
};

struct HeapBrokerInitializationPhase {
  DECL_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS(HeapBrokerInitialization)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    data->broker()->AttachCompilationInfo(data->info());
    data->broker()->InitializeAndStartSerializing(data->native_context());
  }
};

struct TypedLoweringPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(TypedLowering)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    JSCreateLowering create_lowering(&graph_reducer, data->jsgraph(),
                                     data->broker(), temp_zone);
    JSTypedLowering typed_lowering(&graph_reducer, data->jsgraph(),
                                   data->broker(), temp_zone);
    ConstantFoldingReducer constant_folding_reducer(
        &graph_reducer, data->jsgraph(), data->broker());
    TypedOptimization typed_optimization(&graph_reducer, data->dependencies(),
                                         data->jsgraph(), data->broker());
    SimplifiedOperatorReducer simple_reducer(
        &graph_reducer, data->jsgraph(), data->broker(), BranchSemantics::kJS);
    CheckpointElimination checkpoint_elimination(&graph_reducer);
    CommonOperatorReducer common_reducer(
        &graph_reducer, data->graph(), data->broker(), data->common(),
        data->machine(), temp_zone, BranchSemantics::kJS);
    AddReducer(data, &graph_reducer, &dead_code_elimination);

    AddReducer(data, &graph_reducer, &create_lowering);
    AddReducer(data, &graph_reducer, &constant_folding_reducer);
    AddReducer(data, &graph_reducer, &typed_lowering);
    AddReducer(data, &graph_reducer, &typed_optimization);
    AddReducer(data, &graph_reducer, &simple_reducer);
    AddReducer(data, &graph_reducer, &checkpoint_elimination);
    AddReducer(data, &graph_reducer, &common_reducer);

    // ConstantFoldingReducer, JSCreateLowering, JSTypedLowering, and
    // TypedOptimization access the heap.
    UnparkedScopeIfNeeded scope(data->broker());

    graph_reducer.ReduceGraph();
  }
};

struct EscapeAnalysisPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(EscapeAnalysis)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    EscapeAnalysis escape_analysis(data->jsgraph(),
                                   &data->info()->tick_counter(), temp_zone);
    escape_analysis.ReduceGraph();

    GraphReducer reducer(temp_zone, data->graph(),
                         &data->info()->tick_counter(), data->broker(),
                         data->jsgraph()->Dead(), data->observe_node_manager());
    EscapeAnalysisReducer escape_reducer(
        &reducer, data->jsgraph(), data->broker(),
        escape_analysis.analysis_result(), temp_zone);

    AddReducer(data, &reducer, &escape_reducer);

    // EscapeAnalysisReducer accesses the heap.
    UnparkedScopeIfNeeded scope(data->broker());

    reducer.ReduceGraph();
    // TODO(turbofan): Turn this into a debug mode check once we have
    // confidence.
    escape_reducer.VerifyReplacement();
  }
};

struct TypeAssertionsPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(TypeAssertions)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    Schedule* schedule = Scheduler::ComputeSchedule(
        temp_zone, data->graph(), Scheduler::kTempSchedule,
        &data->info()->tick_counter(), data->profile_data());

    AddTypeAssertions(data->jsgraph(), schedule, temp_zone);
  }
};

struct SimplifiedLoweringPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(SimplifiedLowering)

  void Run(TFPipelineData* data, Zone* temp_zone, Linkage* linkage) {
    SimplifiedLowering lowering(data->jsgraph(), data->broker(), temp_zone,
                                data->source_positions(), data->node_origins(),
                                &data->info()->tick_counter(), linkage,
                                data->info(), data->observe_node_manager());

    // RepresentationChanger accesses the heap.
    UnparkedScopeIfNeeded scope(data->broker());

    lowering.LowerAllNodes();
  }
};

struct LoopPeelingPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(LoopPeeling)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphTrimmer trimmer(temp_zone, data->graph());
    NodeVector roots(temp_zone);
    data->jsgraph()->GetCachedNodes(&roots);
    {
      UnparkedScopeIfNeeded scope(data->broker(),
                                  v8_flags.trace_turbo_trimming);
      trimmer.TrimGraph(roots.begin(), roots.end());
    }

    LoopTree* loop_tree = LoopFinder::BuildLoopTree(
        data->jsgraph()->graph(), &data->info()->tick_counter(), temp_zone);
    // We call the typer inside of PeelInnerLoopsOfTree which inspects heap
    // objects, so we need to unpark the local heap.
    UnparkedScopeIfNeeded scope(data->broker());
    LoopPeeler(data->graph(), data->common(), loop_tree, temp_zone,
               data->source_positions(), data->node_origins())
        .PeelInnerLoopsOfTree();
  }
};

struct LoopExitEliminationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(LoopExitElimination)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    LoopPeeler::EliminateLoopExits(data->graph(), temp_zone);
  }
};

struct GenericLoweringPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(GenericLowering)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    JSGenericLowering generic_lowering(data->jsgraph(), &graph_reducer,
                                       data->broker());
    AddReducer(data, &graph_reducer, &generic_lowering);

    // JSGEnericLowering accesses the heap due to ObjectRef's type checks.
    UnparkedScopeIfNeeded scope(data->broker());

    graph_reducer.ReduceGraph();
  }
};

struct EarlyOptimizationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(EarlyOptimization)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    SimplifiedOperatorReducer simple_reducer(&graph_reducer, data->jsgraph(),
                                             data->broker(),
                                             BranchSemantics::kMachine);
    RedundancyElimination redundancy_elimination(&graph_reducer,
                                                 data->jsgraph(), temp_zone);
    ValueNumberingReducer value_numbering(temp_zone, data->graph()->zone());
    MachineOperatorReducer machine_reducer(
        &graph_reducer, data->jsgraph(),
        MachineOperatorReducer::kPropagateSignallingNan);
    CommonOperatorReducer common_reducer(
        &graph_reducer, data->graph(), data->broker(), data->common(),
        data->machine(), temp_zone, BranchSemantics::kMachine);
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    AddReducer(data, &graph_reducer, &simple_reducer);
    AddReducer(data, &graph_reducer, &redundancy_elimination);
    AddReducer(data, &graph_reducer, &machine_reducer);
    AddReducer(data, &graph_reducer, &common_reducer);
    AddReducer(data, &graph_reducer, &value_numbering);
    graph_reducer.ReduceGraph();
  }
};

struct LoadEliminationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(LoadElimination)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    BranchElimination branch_condition_elimination(
        &graph_reducer, data->jsgraph(), temp_zone, BranchElimination::kEARLY);
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    RedundancyElimination redundancy_elimination(&graph_reducer,
                                                 data->jsgraph(), temp_zone);
    LoadElimination load_elimination(&graph_reducer, data->broker(),
                                     data->jsgraph(), temp_zone);
    CheckpointElimination checkpoint_elimination(&graph_reducer);
    ValueNumberingReducer value_numbering(temp_zone, data->graph()->zone());
    CommonOperatorReducer common_reducer(
        &graph_reducer, data->graph(), data->broker(), data->common(),
        data->machine(), temp_zone, BranchSemantics::kJS);
    TypedOptimization typed_optimization(&graph_reducer, data->dependencies(),
                                         data->jsgraph(), data->broker());
    ConstantFoldingReducer constant_folding_reducer(
        &graph_reducer, data->jsgraph(), data->broker());
    TypeNarrowingReducer type_narrowing_reducer(&graph_reducer, data->jsgraph(),
                                                data->broker());

    AddReducer(data, &graph_reducer, &branch_condition_elimination);
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    AddReducer(data, &graph_reducer, &redundancy_elimination);
    AddReducer(data, &graph_reducer, &load_elimination);
    AddReducer(data, &graph_reducer, &type_narrowing_reducer);
    AddReducer(data, &graph_reducer, &constant_folding_reducer);
    AddReducer(data, &graph_reducer, &typed_optimization);
    AddReducer(data, &graph_reducer, &checkpoint_elimination);
    AddReducer(data, &graph_reducer, &common_reducer);
    AddReducer(data, &graph_reducer, &value_numbering);

    // ConstantFoldingReducer and TypedOptimization access the heap.
    UnparkedScopeIfNeeded scope(data->broker());

    graph_reducer.ReduceGraph();
  }
};

struct MemoryOptimizationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(MemoryOptimization)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    // The memory optimizer requires the graphs to be trimmed, so trim now.
    GraphTrimmer trimmer(temp_zone, data->graph());
    NodeVector roots(temp_zone);
    data->jsgraph()->GetCachedNodes(&roots);
    {
      UnparkedScopeIfNeeded scope(data->broker(),
                                  v8_flags.trace_turbo_trimming);
      trimmer.TrimGraph(roots.begin(), roots.end());
    }

    // Optimize allocations and load/store operations.
#if V8_ENABLE_WEBASSEMBLY
    bool is_wasm = data->info()->IsWasm() || data->info()->IsWasmBuiltin();
#else
    bool is_wasm = false;
#endif
    MemoryOptimizer optimizer(
        data->broker(), data->jsgraph(), temp_zone,
        data->info()->allocation_folding()
            ? MemoryLowering::AllocationFolding::kDoAllocationFolding
            : MemoryLowering::AllocationFolding::kDontAllocationFolding,
        data->debug_name(), &data->info()->tick_counter(), is_wasm);
    optimizer.Optimize();
  }
};

#if V8_ENABLE_WEBASSEMBLY
struct WasmTypingPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(WasmTyping)

  void Run(TFPipelineData* data, Zone* temp_zone, uint32_t function_index) {
    MachineGraph* mcgraph = data->mcgraph() ? data->mcgraph() : data->jsgraph();
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    WasmTyper typer(&graph_reducer, mcgraph, function_index);
    AddReducer(data, &graph_reducer, &typer);
    graph_reducer.ReduceGraph();
  }
};

struct WasmGCOptimizationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(WasmGCOptimization)

  void Run(TFPipelineData* data, Zone* temp_zone,
           const wasm::WasmModule* module, MachineGraph* mcgraph) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    WasmLoadElimination load_elimination(&graph_reducer, data->jsgraph(),
                                         temp_zone);
    WasmGCOperatorReducer wasm_gc(&graph_reducer, temp_zone, mcgraph, module,
                                  data->source_positions());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    AddReducer(data, &graph_reducer, &load_elimination);
    AddReducer(data, &graph_reducer, &wasm_gc);
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    graph_reducer.ReduceGraph();
  }
};


struct WasmGCLoweringPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(WasmGCLowering)

  void Run(TFPipelineData* data, Zone* temp_zone,
           const wasm::WasmModule* module) {
    GraphReducer graph_reducer(
        temp_zone, data->graph(), &data->info()->tick_counter(), data->broker(),
        data->jsgraph()->Dead(), data->observe_node_manager());
    WasmGCLowering lowering(&graph_reducer, data->mcgraph(), module, false,
                            data->source_positions());
    DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                              data->common(), temp_zone);
    AddReducer(data, &graph_reducer, &lowering);
    AddReducer(data, &graph_reducer, &dead_code_elimination);
    graph_reducer.ReduceGraph();
  }
};

struct WasmOptimizationPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(WasmOptimization)

  void Run(TFPipelineData* data, Zone* temp_zone,
           MachineOperatorReducer::SignallingNanPropagation
               signalling_nan_propagation,
           wasm::WasmDetectedFeatures detected_features) {
    // Run optimizations in two rounds: First one around load elimination and
    // then one around branch elimination. This is because those two
    // optimizations sometimes display quadratic complexity when run together.
    // We only need load elimination for managed objects.
    if (detected_features.has_gc()) {
      GraphReducer graph_reducer(temp_zone, data->graph(),
                                 &data->info()->tick_counter(), data->broker(),
                                 data->jsgraph()->Dead(),
                                 data->observe_node_manager());
      MachineOperatorReducer machine_reducer(&graph_reducer, data->jsgraph(),
                                             signalling_nan_propagation);
      DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                                data->common(), temp_zone);
      CommonOperatorReducer common_reducer(
          &graph_reducer, data->graph(), data->broker(), data->common(),
          data->machine(), temp_zone, BranchSemantics::kMachine);
      ValueNumberingReducer value_numbering(temp_zone, data->graph()->zone());
      CsaLoadElimination load_elimination(&graph_reducer, data->jsgraph(),
                                          temp_zone);
      WasmEscapeAnalysis escape(&graph_reducer, data->mcgraph());
      AddReducer(data, &graph_reducer, &machine_reducer);
      AddReducer(data, &graph_reducer, &dead_code_elimination);
      AddReducer(data, &graph_reducer, &common_reducer);
      AddReducer(data, &graph_reducer, &value_numbering);
      AddReducer(data, &graph_reducer, &load_elimination);
      AddReducer(data, &graph_reducer, &escape);
      graph_reducer.ReduceGraph();
    }
    {
      GraphReducer graph_reducer(temp_zone, data->graph(),
                                 &data->info()->tick_counter(), data->broker(),
                                 data->jsgraph()->Dead(),
                                 data->observe_node_manager());
      MachineOperatorReducer machine_reducer(&graph_reducer, data->jsgraph(),
                                             signalling_nan_propagation);
      DeadCodeElimination dead_code_elimination(&graph_reducer, data->graph(),
                                                data->common(), temp_zone);
      CommonOperatorReducer common_reducer(
          &graph_reducer, data->graph(), data->broker(), data->common(),
          data->machine(), temp_zone, BranchSemantics::kMachine);
      ValueNumberingReducer value_numbering(temp_zone, data->graph()->zone());
      BranchElimination branch_condition_elimination(
          &graph_reducer, data->jsgraph(), temp_zone);
      AddReducer(data, &graph_reducer, &machine_reducer);
      AddReducer(data, &graph_reducer, &dead_code_elimination);
      AddReducer(data, &graph_reducer, &common_reducer);
      AddReducer(data, &graph_reducer, &value_numbering);
      AddReducer(data, &graph_reducer, &branch_condition_elimination);
      graph_reducer.ReduceGraph();
    }
  }
};
#endif  // V8_ENABLE_WEBASSEMBLY

struct ComputeSchedulePhase {
  DECL_PIPELINE_PHASE_CONSTANTS(Scheduling)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    Schedule* schedule = Scheduler::ComputeSchedule(
        temp_zone, data->graph(),
        data->info()->splitting() ? Scheduler::kSplitNodes
                                  : Scheduler::kNoFlags,
        &data->info()->tick_counter(), data->profile_data());
    data->set_schedule(schedule);
  }
};

#if V8_ENABLE_WASM_SIMD256_REVEC
struct RevectorizePhase {
  DECL_PIPELINE_PHASE_CONSTANTS(Revectorizer)

  void Run(TFPipelineData* data, Zone* temp_zone) {
    Revectorizer revec(temp_zone, data->graph(), data->mcgraph(),
                       data->source_positions());
    revec.TryRevectorize(data->info()->GetDebugName().get());
  }
};
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

struct PrintGraphPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(PrintGraph)

  void Run(TFPipelineData* data, Zone* temp_zone, const char* phase) {
    OptimizedCompilationInfo* info = data->info();
    TFGraph* graph = data->graph();
    if (info->trace_turbo_json()) {  // Print JSON.
      UnparkedScopeIfNeeded scope(data->broker());
      AllowHandleDereference allow_deref;

      TurboJsonFile json_of(info, std::ios_base::app);
      json_of << "{\"name\":\"" << phase << "\",\"type\":\"graph\",\"data\":"
              << AsJSON(*graph, data->source_positions(), data->node_origins())
              << "},\n";
    }

    if (info->trace_turbo_scheduled()) {
      AccountingAllocator allocator;
      Schedule* schedule = data->schedule();
      if (schedule == nullptr) {
        schedule = Scheduler::ComputeSchedule(
            temp_zone, data->graph(), Scheduler::kNoFlags,
            &info->tick_counter(), data->profile_data());
      }

      UnparkedScopeIfNeeded scope(data->broker());
      AllowHandleDereference allow_deref;
      CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
      tracing_scope.stream()
          << "----- Graph after " << phase << " ----- " << std::endl
          << AsScheduledGraph(schedule);
    } else if (info->trace_turbo_graph()) {  // Simple textual RPO.
      UnparkedScopeIfNeeded scope(data->broker());
      AllowHandleDereference allow_deref;
      CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
      tracing_scope.stream()
          << "----- Graph after " << phase << " ----- " << std::endl
          << AsRPO(*graph);
    }
  }
};

struct VerifyGraphPhase {
  DECL_PIPELINE_PHASE_CONSTANTS(VerifyGraph)

  void Run(TFPipelineData* data, Zone* temp_zone, const bool untyped,
           bool values_only = false) {
    Verifier::CodeType code_type;
    switch (data->info()->code_kind()) {
      case CodeKind::WASM_FUNCTION:
      case CodeKind::WASM_TO_CAPI_FUNCTION:
      case CodeKind::WASM_TO_JS_FUNCTION:
      case CodeKind::JS_TO_WASM_FUNCTION:
      case CodeKind::C_WASM_ENTRY:
        code_type = Verifier::kWasm;
        break;
      default:
        code_type = Verifier::kDefault;
    }
    Verifier::Run(data->graph(), !untyped ? Verifier::TYPED : Verifier::UNTYPED,
                  values_only ? Verifier::kValuesOnly : Verifier::kAll,
                  code_type);
  }
};

#undef DECL_MAIN_THREAD_PIPELINE_PHASE_CONSTANTS
#undef DECL_PIPELINE_PHASE_CONSTANTS
#undef DECL_PIPELINE_PHASE_CONSTANTS_HELPER

#if V8_ENABLE_WEBASSEMBLY
class WasmHeapStubCompilationJob final : public TurbofanCompilationJob {
 public:
  WasmHeapStubCompilationJob(Isolate* isolate, CallDescriptor* call_descriptor,
                             std::unique_ptr<Zone> zone, TFGraph* graph,
                             CodeKind kind, std::unique_ptr<char[]> debug_name,
                             const AssemblerOptions& options)
      // Note that the OptimizedCompilationInfo is not initialized at the time
      // we pass it to the CompilationJob constructor, but it is not
      // dereferenced there.
      : TurbofanCompilationJob(isolate, &info_,
                               CompilationJob::State::kReadyToExecute),
        debug_name_(std::move(debug_name)),
        info_(base::CStrVector(debug_name_.get()), graph->zone(), kind),
        call_descriptor_(call_descriptor),
        zone_stats_(zone->allocator()),
        zone_(std::move(zone)),
        graph_(graph),
        turboshaft_data_(&zone_stats_,
                         turboshaft::TurboshaftPipelineKind::kWasm, isolate,
                         &info_, options),
        data_(&zone_stats_, &info_, isolate, wasm::GetWasmEngine()->allocator(),
              graph_, nullptr, nullptr, nullptr,
              zone_->New<NodeOriginTable>(graph_), nullptr, options, nullptr),
        pipeline_(&data_) {}

  WasmHeapStubCompilationJob(const WasmHeapStubCompilationJob&) = delete;
  WasmHeapStubCompilationJob& operator=(const WasmHeapStubCompilationJob&) =
      delete;

 protected:
  Status PrepareJobImpl(Isolate* isolate) final;
  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) final;
  Status FinalizeJobImpl(Isolate* isolate) final;

 private:
  const std::unique_ptr<char[]> debug_name_;
  OptimizedCompilationInfo info_;
  CallDescriptor* const call_descriptor_;
  ZoneStats zone_stats_;
  const std::unique_ptr<Zone> zone_;
  TFGraph* const graph_;
  turboshaft::PipelineData turboshaft_data_;
  TFPipelineData data_;
  PipelineImpl pipeline_;
};

#if V8_ENABLE_WEBASSEMBLY
class WasmTurboshaftWrapperCompilationJob final
    : public turboshaft::TurboshaftCompilationJob {
 public:
  WasmTurboshaftWrapperCompilationJob(Isolate* isolate,
                                      const wasm::CanonicalSig* sig,
                                      wasm::WrapperCompilationInfo wrapper_info,
                                      std::unique_ptr<char[]> debug_name,
                                      const AssemblerOptions& options)
      // Note that the OptimizedCompilationInfo is not initialized at the time
      // we pass it to the CompilationJob constructor, but it is not
      // dereferenced there.
      : TurboshaftCompilationJob(&info_,
                                 CompilationJob::State::kReadyToExecute),
        zone_(wasm::GetWasmEngine()->allocator(), ZONE_NAME),
        debug_name_(std::move(debug_name)),
        info_(base::CStrVector(debug_name_.get()), &zone_,
              wrapper_info.code_kind),
        sig_(sig),
        wrapper_info_(wrapper_info),
        call_descriptor_(
            GetCallDescriptor(&zone_, sig, wrapper_info_.code_kind)),
        zone_stats_(zone_.allocator()),
        turboshaft_data_(
            &zone_stats_,
            wrapper_info_.code_kind == CodeKind::JS_TO_WASM_FUNCTION
                ? turboshaft::TurboshaftPipelineKind::kJSToWasm
                : turboshaft::TurboshaftPipelineKind::kWasm,
            isolate, &info_, options),
        data_(&zone_stats_, &info_, isolate, wasm::GetWasmEngine()->allocator(),
              nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, options,
              nullptr),
        pipeline_(&data_) {}

  WasmTurboshaftWrapperCompilationJob(
      const WasmTurboshaftWrapperCompilationJob&) = delete;
  WasmTurboshaftWrapperCompilationJob& operator=(
      const WasmTurboshaftWrapperCompilationJob&) = delete;

 protected:
  Status PrepareJobImpl(Isolate* isolate) final;
  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) final;
  Status FinalizeJobImpl(Isolate* isolate) final;

 private:
  static CallDescriptor* GetCallDescriptor(Zone* zone,
                                           const wasm::CanonicalSig* sig,
                                           CodeKind code_kind) {
    if (code_kind == CodeKind::WASM_TO_JS_FUNCTION) {
      CallDescriptor* call_descriptor = compiler::GetWasmCallDescriptor(
          zone, sig, WasmCallKind::kWasmImportWrapper);
      return Is64() ? call_descriptor
                    : GetI32WasmCallDescriptor(zone, call_descriptor);
    }
    DCHECK_EQ(code_kind, CodeKind::JS_TO_WASM_FUNCTION);
    return Linkage::GetJSCallDescriptor(
        zone, false, static_cast<int>(sig->parameter_count()) + 1,
        CallDescriptor::kNoFlags);
  }

  Zone zone_;
  const std::unique_ptr<char[]> debug_name_;
  OptimizedCompilationInfo info_;
  const wasm::CanonicalSig* const sig_;
  const wasm::WrapperCompilationInfo wrapper_info_;
  CallDescriptor* const call_descriptor_;  // Incoming call descriptor.
  ZoneStats zone_stats_;
  turboshaft::PipelineData turboshaft_data_;
  TFPipelineData data_;
  PipelineImpl pipeline_;
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics_;
};

// static
std::unique_ptr<TurbofanCompilationJob> Pipeline::NewWasmHeapStubCompilationJob(
    Isolate* isolate, CallDescriptor* call_descriptor,
    std::unique_ptr<Zone> zone, TFGraph* graph, CodeKind kind,
    std::unique_ptr<char[]> debug_name, const AssemblerOptions& options) {
  return std::make_unique<WasmHeapStubCompilationJob>(
      isolate, call_descriptor, std::move(zone), graph, kind,
      std::move(debug_name), options);
}

// static
std::unique_ptr<turboshaft::TurboshaftCompilationJob>
Pipeline::NewWasmTurboshaftWrapperCompilationJob(
    Isolate* isolate, const wasm::CanonicalSig* sig,
    wasm::WrapperCompilationInfo wrapper_info,
    std::unique_ptr<char[]> debug_name, const AssemblerOptions& options) {
  return std::make_unique<WasmTurboshaftWrapperCompilationJob>(
      isolate, sig, wrapper_info, std::move(debug_name), options);
}
#endif

CompilationJob::Status WasmHeapStubCompilationJob::PrepareJobImpl(
    Isolate* isolate) {
  UNREACHABLE();
}

namespace {
// Temporary helpers for logic shared by the TurboFan and Turboshaft wrapper
// compilation jobs. Remove them once wrappers are fully ported to Turboshaft.
void TraceWrapperCompilation(const char* compiler,
                             OptimizedCompilationInfo* info,
                             TFPipelineData* data) {
  if (info->trace_turbo_json() || info->trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling method " << info->GetDebugName().get() << " using "
        << compiler << std::endl;
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    json_of << "{\"function\":\"" << info->GetDebugName().get()
            << "\", \"source\":\"\",\n\"phases\":[";
  }
}

void TraceWrapperCompilation(OptimizedCompilationInfo* info,
                             turboshaft::PipelineData* data) {
  if (info->trace_turbo_json() || info->trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling method " << info->GetDebugName().get()
        << " using Turboshaft" << std::endl;
  }

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    json_of << "{\"function\":\"" << info->GetDebugName().get()
            << "\", \"source\":\"\",\n\"phases\":[";
  }
}

CompilationJob::Status FinalizeWrapperCompilation(
    turboshaft::PipelineData* turboshaft_data, OptimizedCompilationInfo* info,
    CallDescriptor* call_descriptor, Isolate* isolate,
    const char* method_name) {
  Handle<Code> code;
  turboshaft::Pipeline pipeline(turboshaft_data);
  if (!pipeline.FinalizeCode(call_descriptor).ToHandle(&code)) {
    V8::FatalProcessOutOfMemory(isolate, method_name);
  }
  DCHECK_NULL(turboshaft_data->depedencies());
  info->SetCode(code);
#ifdef ENABLE_DISASSEMBLER
  if (v8_flags.print_wasm_code) {
    CodeTracer::StreamScope tracing_scope(isolate->GetCodeTracer());
    code->Disassemble(info->GetDebugName().get(), tracing_scope.stream(),
                      isolate);
  }
#endif

  if (isolate->IsLoggingCodeCreation()) {
    PROFILE(isolate, CodeCreateEvent(LogEventListener::CodeTag::kStub,
                                     Cast<AbstractCode>(code),
                                     info->GetDebugName().get()));
  }
  if (code->kind() == CodeKind::WASM_TO_JS_FUNCTION) {
    code->set_wasm_js_tagged_parameter_count(
        call_descriptor->GetTaggedParameterSlots() & 0xffff);
    code->set_wasm_js_first_tagged_parameter(
        call_descriptor->GetTaggedParameterSlots() >> 16);
  }
  return CompilationJob::SUCCEEDED;
}
}  // namespace

CompilationJob::Status WasmHeapStubCompilationJob::ExecuteJobImpl(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics.reset(new TurbofanPipelineStatistics(
        &info_, wasm::GetWasmEngine()->GetOrCreateTurboStatistics(),
        &zone_stats_));
    pipeline_statistics->BeginPhaseKind("V8.WasmStubCodegen");
  }
  TraceWrapperCompilation("Turbofan", &info_, &data_);
  pipeline_.RunPrintAndVerify("V8.WasmMachineCode", true);
  if (!pipeline_.Run<MemoryOptimizationPhase>()) return FAILED;
  if (!pipeline_.ComputeScheduledGraph()) return FAILED;

  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data_);

  // We convert the turbofan graph to turboshaft.
  Linkage linkage(call_descriptor_);
  if (!turboshaft_pipeline.CreateGraphFromTurbofan(&data_, &linkage)) {
    return FAILED;
  }

  // We need to run simplification to normalize some patterns for instruction
  // selection (e.g. loads and stores).
  if (!turboshaft_pipeline.RunSimplificationAndNormalizationPhase()) {
    return FAILED;
  }

  const bool success = GenerateCodeFromTurboshaftGraph(
      &linkage, turboshaft_pipeline, &pipeline_, data_.osr_helper_ptr());
  return success ? SUCCEEDED : FAILED;
}

CompilationJob::Status WasmHeapStubCompilationJob::FinalizeJobImpl(
    Isolate* isolate) {
  return FinalizeWrapperCompilation(
      &turboshaft_data_, &info_, call_descriptor_, isolate,
      "WasmHeapStubCompilationJob::FinalizeJobImpl");
}

CompilationJob::Status WasmTurboshaftWrapperCompilationJob::PrepareJobImpl(
    Isolate* isolate) {
  UNREACHABLE();
}

CompilationJob::Status WasmTurboshaftWrapperCompilationJob::ExecuteJobImpl(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    DCHECK_NULL(pipeline_statistics_);
    pipeline_statistics_ = std::make_unique<TurbofanPipelineStatistics>(
        &info_, wasm::GetWasmEngine()->GetOrCreateTurboStatistics(),
        &zone_stats_);
    pipeline_statistics_->BeginPhaseKind("V8.WasmStubCodegen");
  }
  TraceWrapperCompilation(&info_, &turboshaft_data_);
  Linkage linkage(call_descriptor_);

  turboshaft_data_.set_pipeline_statistics(pipeline_statistics_.get());
  turboshaft_data_.SetIsWasmWrapper(sig_);

  AccountingAllocator allocator;
  turboshaft_data_.InitializeGraphComponent(nullptr);
  BuildWasmWrapper(&turboshaft_data_, &allocator, turboshaft_data_.graph(),
                   sig_, wrapper_info_);
  CodeTracer* code_tracer = nullptr;
  if (info_.trace_turbo_graph()) {
    // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
    // because it may not yet be initialized then and doing so from the
    // background thread is not threadsafe.
    code_tracer = turboshaft_data_.GetCodeTracer();
  }
  Zone printing_zone(&allocator, ZONE_NAME);
  turboshaft::PrintTurboshaftGraph(&turboshaft_data_, &printing_zone,
                                   code_tracer, "Graph generation");

  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data_);
  // Skip the LoopUnrolling, WasmGCOptimize and WasmLowering phases for
  // wrappers.
  // TODO(14108): Do we need value numbering if wasm_opt is turned off?
  if (v8_flags.wasm_opt) {
    if (!turboshaft_pipeline.Run<turboshaft::WasmOptimizePhase>()) {
      return FAILED;
    }
  }
#if DEBUG
  if (!v8_flags.wasm_opt) {
    // We still need to lower allocation operations even with optimizations
    // being turned off.
    if (!turboshaft_pipeline.Run<turboshaft::WasmDebugMemoryLoweringPhase>()) {
      return FAILED;
    }
  }
#endif

  if (!Is64()) {
    if (!turboshaft_pipeline.Run<turboshaft::Int64LoweringPhase>()) {
      return FAILED;
    }
  }

  if (!turboshaft_pipeline.Run<turboshaft::WasmDeadCodeEliminationPhase>()) {
    return FAILED;
  }

  if (V8_UNLIKELY(v8_flags.turboshaft_enable_debug_features)) {
    // This phase has to run very late to allow all previous phases to use
    // debug features.
    if (!turboshaft_pipeline.Run<turboshaft::DebugFeatureLoweringPhase>()) {
      return FAILED;
    }
  }

  turboshaft_pipeline.BeginPhaseKind("V8.InstructionSelection");

  const bool success = GenerateCodeFromTurboshaftGraph(
      &linkage, turboshaft_pipeline, &pipeline_);
  return success ? SUCCEEDED : FAILED;
}

CompilationJob::Status WasmTurboshaftWrapperCompilationJob::FinalizeJobImpl(
    Isolate* isolate) {
  return FinalizeWrapperCompilation(
      &turboshaft_data_, &info_, call_descriptor_, isolate,
      "WasmTurboshaftWrapperCompilationJob::FinalizeJobImpl");
}

#endif  // V8_ENABLE_WEBASSEMBLY

void PipelineImpl::RunPrintAndVerify(const char* phase, bool untyped) {
  if (info()->trace_turbo_json() || info()->trace_turbo_graph()) {
    USE(Run<PrintGraphPhase>(phase));
  }
  if (v8_flags.turbo_verify) {
    USE(Run<VerifyGraphPhase>(untyped));
  }
}

bool PipelineImpl::InitializeHeapBroker() {
  TFPipelineData* data = data_;

  data->BeginPhaseKind("V8.TFBrokerInitAndSerialization");

  if (info()->trace_turbo_json() || info()->trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling method " << info()->GetDebugName().get()
        << " using TurboFan" << std::endl;
  }
  if (info()->trace_turbo_json()) {
    TurboCfgFile tcf(isolate());
    tcf << AsC1VCompilation(info());
  }
  if (data->info()->has_bytecode_array()) {
    if (data->info()->bytecode_array()->SourcePositionTable()->DataSize() ==
        0) {
      data->source_positions()->Disable();
    }
    data->source_positions()->AddDecorator();
  }
  if (data->info()->trace_turbo_json()) {
    data->node_origins()->AddDecorator();
  }

  RUN_MAYBE_ABORT(HeapBrokerInitializationPhase);
  data->broker()->StopSerializing();
  data->EndPhaseKind();
  return !info()->was_cancelled();
}

bool PipelineImpl::CreateGraph(Linkage* linkage) {
  DCHECK(!v8_flags.turbolev);
  TFPipelineData* data = this->data_;
  UnparkedScopeIfNeeded unparked_scope(data->broker());

  data->BeginPhaseKind("V8.TFGraphCreation");

  RUN_MAYBE_ABORT(GraphBuilderPhase, linkage);
  RunPrintAndVerify(GraphBuilderPhase::phase_name(), true);

  // Perform function context specialization and inlining (if enabled).
  RUN_MAYBE_ABORT(InliningPhase);
  RunPrintAndVerify(InliningPhase::phase_name(), true);

  // Determine the Typer operation flags.
  {
    SharedFunctionInfoRef shared_info =
        MakeRef(data->broker(), info()->shared_info());
    if (is_sloppy(shared_info.language_mode()) &&
        shared_info.IsUserJavaScript()) {
      // Sloppy mode functions always have an Object for this.
      data->AddTyperFlag(Typer::kThisIsReceiver);
    }
    if (IsClassConstructor(shared_info.kind())) {
      // Class constructors cannot be [[Call]]ed.
      data->AddTyperFlag(Typer::kNewTargetIsReceiver);
    }
  }

  data->EndPhaseKind();

  return !info()->was_cancelled();
}

bool PipelineImpl::OptimizeTurbofanGraph(Linkage* linkage) {
  DCHECK(!v8_flags.turbolev);
  TFPipelineData* data = this->data_;

  data->BeginPhaseKind("V8.TFLowering");

  // Trim the graph before typing to ensure all nodes are typed.
  RUN_MAYBE_ABORT(EarlyGraphTrimmingPhase);
  RunPrintAndVerify(EarlyGraphTrimmingPhase::phase_name(), true);

  // Type the graph and keep the Typer running such that new nodes get
  // automatically typed when they are created.
  RUN_MAYBE_ABORT(TyperPhase, data->CreateTyper());
  RunPrintAndVerify(TyperPhase::phase_name());

  RUN_MAYBE_ABORT(TypedLoweringPhase);
  RunPrintAndVerify(TypedLoweringPhase::phase_name());

  if (data->info()->loop_peeling()) {
    RUN_MAYBE_ABORT(LoopPeelingPhase);
    RunPrintAndVerify(LoopPeelingPhase::phase_name(), true);
  } else {
    RUN_MAYBE_ABORT(LoopExitEliminationPhase);
    RunPrintAndVerify(LoopExitEliminationPhase::phase_name(), true);
  }

  if (v8_flags.turbo_load_elimination) {
    RUN_MAYBE_ABORT(LoadEliminationPhase);
    RunPrintAndVerify(LoadEliminationPhase::phase_name());
  }
  data->DeleteTyper();

  if (v8_flags.turbo_escape) {
    RUN_MAYBE_ABORT(EscapeAnalysisPhase);
    RunPrintAndVerify(EscapeAnalysisPhase::phase_name());
  }

  if (v8_flags.assert_types) {
    RUN_MAYBE_ABORT(TypeAssertionsPhase);
    RunPrintAndVerify(TypeAssertionsPhase::phase_name());
  }

  // Perform simplified lowering. This has to run w/o the Typer decorator,
  // because we cannot compute meaningful types anyways, and the computed
  // types might even conflict with the representation/truncation logic.
  RUN_MAYBE_ABORT(SimplifiedLoweringPhase, linkage);
  RunPrintAndVerify(SimplifiedLoweringPhase::phase_name(), true);

#if V8_ENABLE_WEBASSEMBLY
  if (data->has_js_wasm_calls()) {
    DCHECK(data->info()->inline_js_wasm_calls());
    RUN_MAYBE_ABORT(JSWasmInliningPhase);
    RunPrintAndVerify(JSWasmInliningPhase::phase_name(), true);

    RUN_MAYBE_ABORT(WasmTypingPhase, -1);
    RunPrintAndVerify(WasmTypingPhase::phase_name(), true);

    if (v8_flags.wasm_opt) {
      RUN_MAYBE_ABORT(WasmGCOptimizationPhase,
                      data->wasm_native_module_for_inlining()->module(),
                      data->jsgraph());
      RunPrintAndVerify(WasmGCOptimizationPhase::phase_name(), true);
    }
    RUN_MAYBE_ABORT(JSWasmLoweringPhase);
    RunPrintAndVerify(JSWasmLoweringPhase::phase_name(), true);

    if (v8_flags.turbo_optimize_inlined_js_wasm_wrappers && v8_flags.wasm_opt) {
      wasm::WasmDetectedFeatures detected({wasm::WasmDetectedFeature::gc});
      RUN_MAYBE_ABORT(WasmOptimizationPhase,
                      MachineOperatorReducer::kSilenceSignallingNan, detected);
      RunPrintAndVerify(WasmOptimizationPhase::phase_name(), true);
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // From now on it is invalid to look at types on the nodes, because the
  // types on the nodes might not make sense after representation selection
  // due to the way we handle truncations; if we'd want to look at types
  // afterwards we'd essentially need to re-type (large portions of) the
  // graph.

  // In order to catch bugs related to type access after this point, we now
  // remove the types from the nodes (currently only in Debug builds).
#ifdef DEBUG
  RUN_MAYBE_ABORT(UntyperPhase);
  RunPrintAndVerify(UntyperPhase::phase_name(), true);
#endif

  // Run generic lowering pass.
  RUN_MAYBE_ABORT(GenericLoweringPhase);
  RunPrintAndVerify(GenericLoweringPhase::phase_name(), true);

  data->BeginPhaseKind("V8.TFBlockBuilding");

  data->InitializeFrameData(linkage->GetIncomingDescriptor());

  // Run early optimization pass.
  RUN_MAYBE_ABORT(EarlyOptimizationPhase);
  RunPrintAndVerify(EarlyOptimizationPhase::phase_name(), true);

  data->source_positions()->RemoveDecorator();
  if (data->info()->trace_turbo_json()) {
    data->node_origins()->RemoveDecorator();
  }

  if (!ComputeScheduledGraph()) return false;

  return !info()->was_cancelled();
}

namespace {

int HashGraphForPGO(const turboshaft::Graph* graph) {
  size_t hash = 0;
  for (const turboshaft::Operation& op : graph->AllOperations()) {
    VisitOperation(op, [&hash, &graph](const auto& derived) {
      const auto op_hash =
          derived.hash_value(turboshaft::HashingStrategy::kMakeSnapshotStable);
      hash = turboshaft::fast_hash_combine(hash, op_hash);
      // Use for tracing while developing:
      constexpr bool kTraceHashing = false;
      if constexpr (kTraceHashing) {
        std::cout << "[" << std::setw(3) << graph->Index(derived)
                  << "] Type: " << std::setw(30)
                  << turboshaft::OpcodeName(
                         turboshaft::operation_to_opcode_v<decltype(derived)>);
        std::cout << " + 0x" << std::setw(20) << std::left << std::hex
                  << op_hash << " => 0x" << hash << std::dec << std::endl;
      }
    });
  }
  return Tagged<Smi>(IntToSmi(static_cast<int>(hash))).value();
}

// Compute a hash of the given graph, in a way that should provide the same
// result in multiple runs of mksnapshot, meaning the hash cannot depend on any
// external pointer values or uncompressed heap constants. This hash can be used
// to reject profiling data if the builtin's current code doesn't match the
// version that was profiled. Hash collisions are not catastrophic; in the worst
// case, we just defer some blocks that ideally shouldn't be deferred. The
// result value is in the valid Smi range.
int HashGraphForPGO(const TFGraph* graph) {
  AccountingAllocator allocator;
  Zone local_zone(&allocator, ZONE_NAME);

  constexpr NodeId kUnassigned = static_cast<NodeId>(-1);

  constexpr uint8_t kUnvisited = 0;
  constexpr uint8_t kOnStack = 1;
  constexpr uint8_t kVisited = 2;

  // Do a depth-first post-order traversal of the graph. For every node, hash:
  //
  //   - the node's traversal number
  //   - the opcode
  //   - the number of inputs
  //   - each input node's traversal number
  //
  // What's a traversal number? We can't use node IDs because they're not stable
  // build-to-build, so we assign a new number for each node as it is visited.

  ZoneVector<uint8_t> state(graph->NodeCount(), kUnvisited, &local_zone);
  ZoneVector<NodeId> traversal_numbers(graph->NodeCount(), kUnassigned,
                                       &local_zone);
  ZoneStack<Node*> stack(&local_zone);

  NodeId visited_count = 0;
  size_t hash = 0;

  stack.push(graph->end());
  state[graph->end()->id()] = kOnStack;
  traversal_numbers[graph->end()->id()] = visited_count++;
  while (!stack.empty()) {
    Node* n = stack.top();
    bool pop = true;
    for (Node* const i : n->inputs()) {
      if (state[i->id()] == kUnvisited) {
        state[i->id()] = kOnStack;
        traversal_numbers[i->id()] = visited_count++;
        stack.push(i);
        pop = false;
        break;
      }
    }
    if (pop) {
      state[n->id()] = kVisited;
      stack.pop();
      hash = base::hash_combine(hash, traversal_numbers[n->id()], n->opcode(),
                                n->InputCount());
      for (Node* const i : n->inputs()) {
        DCHECK(traversal_numbers[i->id()] != kUnassigned);
        hash = base::hash_combine(hash, traversal_numbers[i->id()]);
      }
    }
  }
  return Tagged<Smi>(IntToSmi(static_cast<int>(hash))).value();
}

template <typename Graph>
int ComputeInitialGraphHash(Builtin builtin,
                            const ProfileDataFromFile* profile_data,
                            const Graph* graph) {
  int initial_graph_hash = 0;
  if (v8_flags.turbo_profiling || v8_flags.dump_builtins_hashes_to_file ||
      profile_data != nullptr) {
    initial_graph_hash = HashGraphForPGO(graph);
    if (v8_flags.dump_builtins_hashes_to_file) {
      std::ofstream out(v8_flags.dump_builtins_hashes_to_file,
                        std::ios_base::app);
      out << "Builtin: " << Builtins::name(builtin) << ", hash: 0x" << std::hex
          << initial_graph_hash << std::endl;
    }
  }
  return initial_graph_hash;
}

const ProfileDataFromFile* ValidateProfileData(
    const ProfileDataFromFile* profile_data, int initial_graph_hash,
    const char* debug_name) {
  if (profile_data != nullptr && profile_data->hash() != initial_graph_hash) {
    if (v8_flags.reorder_builtins) {
      BuiltinsCallGraph::Get()->set_all_hash_matched(false);
    }
    if (v8_flags.abort_on_bad_builtin_profile_data ||
        v8_flags.warn_about_builtin_profile_data) {
      base::EmbeddedVector<char, 256> msg;
      SNPrintF(
          msg,
          "Rejected profile data for %s due to function change. "
          "Please use tools/builtins-pgo/generate.py to refresh it, you can "
          "also try adding `'checkout_pgo_profiles': True` to your .gclient "
          "file and run `gclient sync` to download the latest PGO profiles.",
          debug_name);
      if (v8_flags.abort_on_bad_builtin_profile_data) {
        // mksnapshot might fail here because of the following reasons:
        // * builtins were changed since the builtins profile generation,
        // * current build options affect builtins code and they don't match
        //   the options used for building the profile (for example, it might
        //   be because of gn argument 'dcheck_always_on=true').
        // To fix the issue one must either update the builtins PGO profiles
        // (see tools/builtins-pgo/generate.py) or disable builtins PGO by
        // setting gn argument v8_builtins_profiling_log_file="".
        // One might also need to update the tools/builtins-pgo/generate.py if
        // the set of default release arguments has changed.
        FATAL("%s", msg.begin());
      } else {
        PrintF("%s\n", msg.begin());
      }
    }
#ifdef LOG_BUILTIN_BLOCK_COUNT
    if (v8_flags.turbo_log_builtins_count_input) {
      PrintF("The hash came from execution count file for %s was not match!\n",
             debug_name);
    }
#endif
    return nullptr;
  }
  return profile_data;
}

}  // namespace

struct CodeAssemblerCompilationJob::TFDataAndPipeline {
  explicit TFDataAndPipeline(CodeAssemblerCompilationJob* job)
      : data(&job->zone_stats_, &job->compilation_info_,
             job->raw_assembler()->isolate(),
             job->raw_assembler()->isolate()->allocator(),
             job->raw_assembler()->graph(), job->jsgraph(), nullptr,
             job->raw_assembler()->source_positions(),
             &job->node_origins_.value(), job->jump_opt_.get(),
             job->assembler_options_, job->profile_data_),
        pipeline(&data) {
    data.set_verify_graph(v8_flags.verify_csa);
  }
  TFPipelineData data;
  PipelineImpl pipeline;
};

CodeAssemblerCompilationJob::CodeAssemblerCompilationJob(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CodeAssemblerInstaller installer, const AssemblerOptions& assembler_options,
    std::function<compiler::CallDescriptor*(Zone*)> get_call_descriptor,
    CodeKind code_kind, const char* name,
    const ProfileDataFromFile* profile_data, int finalize_order)
    : TurbofanCompilationJob(isolate, &compilation_info_,
                             State::kReadyToPrepare),
      generator_(generator),
      installer_(installer),
      profile_data_(profile_data),
      zone_(isolate->allocator(), ZONE_NAME),
      zone_stats_(isolate->allocator()),
      code_assembler_state_(isolate, &zone_, get_call_descriptor(&zone_),
                            code_kind, name, builtin),
      assembler_options_(assembler_options),
      compilation_info_(base::CStrVector(name), &zone_, code_kind),
      jump_opt_(ShouldOptimizeJumps(isolate) ? new JumpOptimizationInfo()
                                             : nullptr),
      finalize_order_(finalize_order) {
  DCHECK(code_kind == CodeKind::BUILTIN ||
         code_kind == CodeKind::BYTECODE_HANDLER ||
         code_kind == CodeKind::FOR_TESTING);
  compilation_info_.set_builtin(builtin);
}

// static
bool CodeAssemblerCompilationJob::ShouldOptimizeJumps(Isolate* isolate) {
  return isolate->serializer_enabled() && v8_flags.turbo_rewrite_far_jumps &&
         !v8_flags.turbo_profiling && !v8_flags.dump_builtins_hashes_to_file;
}

PipelineCompilationJob::Status CodeAssemblerCompilationJob::PrepareJobImpl(
    Isolate* isolate) {
  {
    // Work around that the PersistentHandlesScope inside CompilationHandleScope
    // requires there to be at least one handle.
    HandleScope handle_scope(isolate);
    DirectHandle<Object> dummy(ReadOnlyRoots(isolate->heap()).empty_string(),
                               isolate);
    CompilationHandleScope compilation_scope(isolate, &compilation_info_);
    generator_(&code_assembler_state_);
  }

  node_origins_.emplace(raw_assembler()->ExportForOptimization());

  PipelineImpl* pipeline = EmplacePipeline(isolate);
  TFPipelineData* data = pipeline->data();
  PipelineJobScope pipeline_scope(data,
                                  isolate->counters()->runtime_call_stats());
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics_.reset(new TurbofanPipelineStatistics(
        &compilation_info_, isolate->GetTurboStatistics(), &zone_stats_));
  }

  const char* debug_name = code_assembler_state_.name_;
  if (compilation_info_.trace_turbo_json() ||
      compilation_info_.trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data->GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling " << debug_name << " using TurboFan" << std::endl;
    if (compilation_info_.trace_turbo_json()) {
      TurboJsonFile json_of(&compilation_info_, std::ios_base::trunc);
      json_of << "{\"function\" : ";
      JsonPrintFunctionSource(json_of, -1, compilation_info_.GetDebugName(),
                              DirectHandle<Script>(), isolate,
                              DirectHandle<SharedFunctionInfo>());
      json_of << ",\n\"phases\":[";
    }
    if (!pipeline->Run<PrintGraphPhase>("V8.TFMachineCode")) return FAILED;
  }

  // Validate pgo profile.
  initial_graph_hash_ = ComputeInitialGraphHash(compilation_info_.builtin(),
                                                profile_data_, data->graph());
  profile_data_ = ValidateProfileData(profile_data_, initial_graph_hash_,
                                      code_assembler_state_.name_);
  data->set_profile_data(profile_data_);

  return SUCCEEDED;
}

PipelineCompilationJob::Status CodeAssemblerCompilationJob::FinalizeJobImpl(
    Isolate* isolate) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeFinalizePipelineJob);

  HandleScope scope(isolate);
  Handle<Code> code = FinalizeCode(isolate);

#ifdef ENABLE_DISASSEMBLER
  if (v8_flags.trace_ignition_codegen &&
      code->kind() == CodeKind::BYTECODE_HANDLER) {
    StdoutStream os;
    code->Disassemble(code_assembler_state_.name_, os, isolate);
    os << std::flush;
  }
#endif  // ENABLE_DISASSEMBLER

  code_assembler_state_.code_generated_ = true;
  installer_(compilation_info_.builtin(), scope.CloseAndEscape(code));

  return SUCCEEDED;
}

inline constexpr char kBuiltinCompilationZoneName[] =
    "builtin-compilation-zone";

class TurboshaftAssemblerBuiltinCompilationJob final
    : public TurbofanCompilationJob {
 public:
  using CallDescriptorBuilder = std::function<compiler::CallDescriptor*(Zone*)>;
  using Generator = Pipeline::TurboshaftAssemblerGenerator;
  using Installer = Pipeline::TurboshaftAssemblerInstaller;

  TurboshaftAssemblerBuiltinCompilationJob(
      Isolate* isolate, Builtin builtin, Generator generator,
      Installer installer, const AssemblerOptions& assembler_options,
      CallDescriptorBuilder call_descriptor_builder, CodeKind code_kind,
      const char* name, interpreter::BytecodeHandlerData bytecode_handler_data,
      const ProfileDataFromFile* profile_data, int finalize_order);

  Status PrepareJobImpl(Isolate* isolate) override;
  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) override;
  Status FinalizeJobImpl(Isolate* isolate) override;

  Handle<Code> FinalizeCode(Isolate* isolate);
  bool ShouldOptimizeJumps(Isolate* isolate);

  static constexpr int kNoFinalizeOrder = -1;
  int FinalizeOrder() const final {
    DCHECK_NE(kNoFinalizeOrder, finalize_order_);
    return finalize_order_;
  }

 private:
  Generator generator_;
  Installer installer_;
  CallDescriptorBuilder call_descriptor_builder_;
  interpreter::BytecodeHandlerData bytecode_handler_data_;
  const char* name_;

  ZoneStats zone_stats_;
  turboshaft::ZoneWithName<kBuiltinCompilationZoneName> zone_;
  OptimizedCompilationInfo compilation_info_;
  turboshaft::PipelineData data_;
  AssemblerOptions assembler_options_;
  const ProfileDataFromFile* profile_data_ = nullptr;
  int initial_graph_hash_ = 0;

  // Conditionally allocated, depending on flags.
  std::unique_ptr<JumpOptimizationInfo> jump_opt_;
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics_;

  // Used to deterministically order finalization.
  int finalize_order_;
};

TurboshaftAssemblerBuiltinCompilationJob::
    TurboshaftAssemblerBuiltinCompilationJob(
        Isolate* isolate, Builtin builtin, Generator generator,
        Installer installer, const AssemblerOptions& assembler_options,
        CallDescriptorBuilder call_descriptor_builder, CodeKind code_kind,
        const char* name,
        interpreter::BytecodeHandlerData bytecode_handler_data,
        const ProfileDataFromFile* profile_data, int finalize_order)
    : TurbofanCompilationJob(isolate, &compilation_info_,
                             State::kReadyToPrepare),
      generator_(generator),
      installer_(installer),
      call_descriptor_builder_(call_descriptor_builder),
      bytecode_handler_data_(std::move(bytecode_handler_data)),
      name_(name),
      zone_stats_(isolate->allocator()),
      zone_(&zone_stats_, kBuiltinCompilationZoneName),
      compilation_info_(base::CStrVector(name), zone_, code_kind),
      data_(&zone_stats_, turboshaft::TurboshaftPipelineKind::kTSABuiltin,
            isolate, &compilation_info_, assembler_options),
      jump_opt_(ShouldOptimizeJumps(isolate) ? new JumpOptimizationInfo()
                                             : nullptr),
      finalize_order_(finalize_order) {
  USE(finalize_order_);
  DCHECK_EQ(code_kind, CodeKind::BYTECODE_HANDLER);
  compilation_info_.set_builtin(builtin);
}

PipelineCompilationJob::Status
TurboshaftAssemblerBuiltinCompilationJob::PrepareJobImpl(Isolate* isolate) {
  using namespace compiler::turboshaft;
  {
    // Work around that the PersistentHandlesScope inside CompilationHandleScope
    // requires there to be at least one handle.
    HandleScope handle_scope(isolate);
    DirectHandle<Object> dummy(ReadOnlyRoots(isolate->heap()).empty_string(),
                               isolate);
    CompilationHandleScope compilation_scope(isolate, &compilation_info_);

    const CallDescriptor* call_descriptor = call_descriptor_builder_(zone_);
    data_.InitializeBuiltinComponent(call_descriptor,
                                     std::move(bytecode_handler_data_));
    data_.InitializeGraphComponent(nullptr);

    ZoneWithName<turboshaft::kTempZoneName> temp_zone(&zone_stats_,
                                                      kTempZoneName);
    generator_(&data_, isolate, data_.graph(), temp_zone);
  }

  PipelineJobScope pipeline_scope(&data_,
                                  isolate->counters()->runtime_call_stats());
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics_.reset(new TurbofanPipelineStatistics(
        &compilation_info_, isolate->GetTurboStatistics(), &zone_stats_));
  }

  turboshaft::BuiltinPipeline turboshaft_pipeline(&data_);
  if (compilation_info_.trace_turbo_graph() ||
      compilation_info_.trace_turbo_json()) {
    CodeTracer::StreamScope tracing_scope(data_.GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling method " << name_ << " using TurboFan" << std::endl;
    if (compilation_info_.trace_turbo_json()) {
      TurboJsonFile json_of(&compilation_info_, std::ios_base::trunc);
      json_of << "{\"function\" : ";
      JsonPrintFunctionSource(json_of, -1, compilation_info_.GetDebugName(),
                              DirectHandle<Script>(), isolate,
                              DirectHandle<SharedFunctionInfo>());
      json_of << ",\n\"phases\":[";
    }
    turboshaft::ZoneWithName<turboshaft::kTempZoneName> print_zone(
        &zone_stats_, turboshaft::kTempZoneName);
    std::vector<char> name_buffer(strlen("TSA: ") + strlen(name_) + 1);
    memcpy(name_buffer.data(), "TSA: ", 5);
    memcpy(name_buffer.data() + 5, name_, strlen(name_));
    turboshaft_pipeline.PrintGraph(print_zone, name_buffer.data());
  }

  // Validate pgo profile.
  initial_graph_hash_ = ComputeInitialGraphHash(compilation_info_.builtin(),
                                                profile_data_, &data_.graph());
  profile_data_ =
      ValidateProfileData(profile_data_, initial_graph_hash_, name_);

  return Status::SUCCEEDED;
}

PipelineCompilationJob::Status
TurboshaftAssemblerBuiltinCompilationJob::ExecuteJobImpl(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  turboshaft::BuiltinPipeline pipeline(&data_);

  // TODO(nicohartmann): Get rid of const_cast<> once CallDescriptors apply
  // proper const-correctness.
  const CallDescriptor* call_descriptor = data_.builtin_call_descriptor();
  Linkage linkage(const_cast<CallDescriptor*>(call_descriptor));

  pipeline.OptimizeBuiltin();

  CHECK(pipeline.GenerateCode(&linkage, nullptr, jump_opt_.get(), profile_data_,
                              initial_graph_hash_));

  return SUCCEEDED;
}

PipelineCompilationJob::Status
TurboshaftAssemblerBuiltinCompilationJob::FinalizeJobImpl(Isolate* isolate) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeFinalizePipelineJob);

  HandleScope scope(isolate);
  Handle<Code> code = FinalizeCode(isolate);

#ifdef ENABLE_DISASSEMBLER
  if (v8_flags.trace_ignition_codegen &&
      code->kind() == CodeKind::BYTECODE_HANDLER) {
    StdoutStream os;
    code->Disassemble(name_, os, isolate);
    os << std::flush;
  }
#endif  // ENABLE_DISASSEMBLER

  installer_(compilation_info_.builtin(), scope.CloseAndEscape(code));

  return SUCCEEDED;
}

// static
bool TurboshaftAssemblerBuiltinCompilationJob::ShouldOptimizeJumps(
    Isolate* isolate) {
  return isolate->serializer_enabled() && v8_flags.turbo_rewrite_far_jumps &&
         !v8_flags.turbo_profiling && !v8_flags.dump_builtins_hashes_to_file;
}

Handle<Code> TurboshaftAssemblerBuiltinCompilationJob::FinalizeCode(
    Isolate* isolate) {
  PipelineJobScope scope(&data_, isolate->counters()->runtime_call_stats());
  turboshaft::Pipeline turboshaft_pipeline(&data_);
  turboshaft::Tracing::Scope tracing_scope(&compilation_info_);
  return turboshaft_pipeline.FinalizeCode().ToHandleChecked();
}

class CodeAssemblerTurboshaftCompilationJob final
    : public CodeAssemblerCompilationJob {
 public:
  using CodeAssemblerCompilationJob::CodeAssemblerCompilationJob;

  ~CodeAssemblerTurboshaftCompilationJob() final;
  CodeAssemblerTurboshaftCompilationJob(
      const CodeAssemblerTurboshaftCompilationJob&) = delete;
  CodeAssemblerTurboshaftCompilationJob& operator=(
      const CodeAssemblerTurboshaftCompilationJob&) = delete;

  Status ExecuteJobImpl(RuntimeCallStats* stats,
                        LocalIsolate* local_isolate) final;

 private:
  PipelineImpl* EmplacePipeline(Isolate* isolate) final;
  Handle<Code> FinalizeCode(Isolate* isolate) final;

  // Initialized after CSA generates the graph in PrepareJobImpl.
  std::optional<TFDataAndPipeline> pipeline_;
  std::optional<turboshaft::PipelineData> turboshaft_data_;
};

CodeAssemblerTurboshaftCompilationJob::
    ~CodeAssemblerTurboshaftCompilationJob() = default;

PipelineImpl* CodeAssemblerTurboshaftCompilationJob::EmplacePipeline(
    Isolate* isolate) {
  pipeline_.emplace(this);
  turboshaft_data_.emplace(pipeline_->data.zone_stats(),
                           turboshaft::TurboshaftPipelineKind::kCSA, isolate,
                           pipeline_->data.info(), assembler_options_,
                           pipeline_->data.start_source_position());
  return &pipeline_->pipeline;
}

PipelineCompilationJob::Status
CodeAssemblerTurboshaftCompilationJob::ExecuteJobImpl(
    RuntimeCallStats* stats, LocalIsolate* local_isolate) {
  if (!pipeline_->pipeline.ComputeScheduledGraph()) return FAILED;
  DCHECK_NULL(pipeline_->data.frame());
  DCHECK_NOT_NULL(pipeline_->data.schedule());

  turboshaft::BuiltinPipeline turboshaft_pipeline(&turboshaft_data_.value());
  CallDescriptor* call_descriptor = raw_assembler()->call_descriptor();
  Linkage linkage(call_descriptor);

  CHECK(
      turboshaft_pipeline.CreateGraphFromTurbofan(&pipeline_->data, &linkage));

  turboshaft_pipeline.OptimizeBuiltin();

  CHECK_NULL(pipeline_->data.osr_helper_ptr());
  CHECK(turboshaft_pipeline.GenerateCode(
      &linkage, pipeline_->data.osr_helper_ptr(), jump_opt_.get(),
      profile_data_, initial_graph_hash_));

  return SUCCEEDED;
}

Handle<Code> CodeAssemblerTurboshaftCompilationJob::FinalizeCode(
    Isolate* isolate) {
  PipelineJobScope scope(&pipeline_->data,
                         isolate->counters()->runtime_call_stats());
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data_.value());
  turboshaft::Tracing::Scope tracing_scope(pipeline_->data.info());
  return turboshaft_pipeline.FinalizeCode().ToHandleChecked();
}

// static
std::unique_ptr<TurbofanCompilationJob>
Pipeline::NewCSLinkageCodeStubBuiltinCompilationJob(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CodeAssemblerInstaller installer, const AssemblerOptions& assembler_options,
    CallDescriptors::Key interface_descriptor, const char* name,
    const ProfileDataFromFile* profile_data, int finalize_order) {
  auto get_call_descriptor =
      [interface_descriptor](Zone* zone) {
        CallInterfaceDescriptor descriptor(interface_descriptor);
        // Ensure descriptor is already initialized.
        DCHECK_LE(0, descriptor.GetRegisterParameterCount());
        return Linkage::GetStubCallDescriptor(
            zone, descriptor, descriptor.GetStackParameterCount(),
            CallDescriptor::kNoFlags, Operator::kNoProperties);
      };
    return std::make_unique<CodeAssemblerTurboshaftCompilationJob>(
        isolate, builtin, generator, installer, assembler_options,
        get_call_descriptor, CodeKind::BUILTIN, name, profile_data,
        finalize_order);
}

// static
std::unique_ptr<TurbofanCompilationJob>
Pipeline::NewJSLinkageCodeStubBuiltinCompilationJob(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CodeAssemblerInstaller installer, const AssemblerOptions& assembler_options,
    int argc, const char* name, const ProfileDataFromFile* profile_data,
    int finalize_order) {
  auto get_call_descriptor = [argc](Zone* zone) {
    return Linkage::GetJSCallDescriptor(zone, false, argc,
                                        CallDescriptor::kCanUseRoots);
  };
    return std::make_unique<CodeAssemblerTurboshaftCompilationJob>(
        isolate, builtin, generator, installer, assembler_options,
        get_call_descriptor, CodeKind::BUILTIN, name, profile_data,
        finalize_order);
}

// static
std::unique_ptr<TurbofanCompilationJob>
Pipeline::NewBytecodeHandlerCompilationJob(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CodeAssemblerInstaller installer, const AssemblerOptions& assembler_options,
    const char* name, const ProfileDataFromFile* profile_data,
    int finalize_order) {
  auto get_call_descriptor =
      [](Zone* zone) {
        InterpreterDispatchDescriptor descriptor;
        return Linkage::GetStubCallDescriptor(
            zone, descriptor, descriptor.GetStackParameterCount(),
            CallDescriptor::kNoFlags, Operator::kNoProperties);
      };
    return std::make_unique<CodeAssemblerTurboshaftCompilationJob>(
        isolate, builtin, generator, installer, assembler_options,
        get_call_descriptor, CodeKind::BYTECODE_HANDLER, name, profile_data,
        finalize_order);
}

// static
std::unique_ptr<TurbofanCompilationJob>
Pipeline::NewBytecodeHandlerCompilationJobTSA(
    Isolate* isolate, Builtin builtin,
    Pipeline::TurboshaftAssemblerGenerator generator,
    Pipeline::TurboshaftAssemblerInstaller installer,
    const AssemblerOptions& assembler_options, const char* name,
    interpreter::BytecodeHandlerData bytecode_handler_data,
    const ProfileDataFromFile* profile_data, int finalize_order) {
  auto get_call_descriptor = [](Zone* zone) {
    InterpreterDispatchDescriptor descriptor;
    return Linkage::GetStubCallDescriptor(
        zone, descriptor, descriptor.GetStackParameterCount(),
        CallDescriptor::kNoFlags, Operator::kNoProperties);
  };
  return std::make_unique<TurboshaftAssemblerBuiltinCompilationJob>(
      isolate, builtin, generator, installer, assembler_options,
      get_call_descriptor, CodeKind::BYTECODE_HANDLER, name,
      std::move(bytecode_handler_data), profile_data, finalize_order);
}

// static
std::unique_ptr<CodeAssemblerCompilationJob>
CodeAssemblerCompilationJob::NewJobForTesting(
    Isolate* isolate, Builtin builtin, CodeAssemblerGenerator generator,
    CodeAssemblerInstaller installer,
    std::function<compiler::CallDescriptor*(Zone*)> get_call_descriptor,
    CodeKind code_kind, const char* name) {
  AssemblerOptions assembler_options = AssemblerOptions::Default(isolate);
    return std::make_unique<CodeAssemblerTurboshaftCompilationJob>(
        isolate, builtin, generator, installer, assembler_options,
        get_call_descriptor, code_kind, name, nullptr, -1);
}

MaybeHandle<Code> Pipeline::GenerateCodeForTurboshaftBuiltin(
    turboshaft::PipelineData* turboshaft_data, CallDescriptor* call_descriptor,
    Builtin builtin, const char* debug_name,
    const ProfileDataFromFile* profile_data) {
  DCHECK_EQ(builtin, turboshaft_data->info()->builtin());
  Isolate* isolate = turboshaft_data->isolate();

#if V8_ENABLE_EXPERIMENTAL_TSA_BUILTINS
// TODO(nicohartmann): Use during development and remove afterwards.
#ifdef DEBUG
  std::cout << "=== Generating Builtin '" << debug_name
            << "' with Turboshaft ===" << std::endl;
#endif

#endif

  // Initialize JumpOptimizationInfo if required.
  JumpOptimizationInfo jump_opt;
  bool should_optimize_jumps =
      isolate->serializer_enabled() && v8_flags.turbo_rewrite_far_jumps &&
      !v8_flags.turbo_profiling && !v8_flags.dump_builtins_hashes_to_file;
  JumpOptimizationInfo* jump_optimization_info =
      should_optimize_jumps ? &jump_opt : nullptr;

  PipelineJobScope scope(turboshaft_data,
                         isolate->counters()->runtime_call_stats());
  RCS_SCOPE(isolate, RuntimeCallCounterId::kOptimizeCode);

  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics(
      CreatePipelineStatistics(Handle<Script>::null(), turboshaft_data->info(),
                               isolate, turboshaft_data->zone_stats()));

  turboshaft::BuiltinPipeline turboshaft_pipeline(turboshaft_data);
  OptimizedCompilationInfo* info = turboshaft_data->info();
  if (info->trace_turbo_graph() || info->trace_turbo_json()) {
    turboshaft::ZoneWithName<turboshaft::kTempZoneName> print_zone(
        turboshaft_data->zone_stats(), turboshaft::kTempZoneName);
    std::vector<char> name_buffer(strlen("TSA: ") + strlen(debug_name) + 1);
    memcpy(name_buffer.data(), "TSA: ", 5);
    memcpy(name_buffer.data() + 5, debug_name, strlen(debug_name));
    turboshaft_pipeline.PrintGraph(print_zone, name_buffer.data());
  }

  // Validate pgo profile.
  const int initial_graph_hash =
      ComputeInitialGraphHash(builtin, profile_data, &turboshaft_data->graph());
  profile_data =
      ValidateProfileData(profile_data, initial_graph_hash, debug_name);

  turboshaft_pipeline.OptimizeBuiltin();
  Linkage linkage(call_descriptor);
  CHECK(turboshaft_pipeline.GenerateCode(&linkage, {}, jump_optimization_info,
                                         profile_data, initial_graph_hash));
  return turboshaft_pipeline.FinalizeCode();
}

#if V8_ENABLE_WEBASSEMBLY

namespace {

wasm::WasmCompilationResult WrapperCompilationResult(
    CodeGenerator* code_generator, CallDescriptor* call_descriptor,
    CodeKind kind) {
  wasm::WasmCompilationResult result;
  code_generator->masm()->GetCode(
      nullptr, &result.code_desc, code_generator->safepoint_table_builder(),
      static_cast<int>(code_generator->handler_table_offset()));
  result.instr_buffer = code_generator->masm()->ReleaseBuffer();
  result.source_positions = code_generator->GetSourcePositionTable();
  result.protected_instructions_data =
      code_generator->GetProtectedInstructionsData();
  result.frame_slot_count = code_generator->frame()->GetTotalFrameSlotCount();
  result.tagged_parameter_slots = call_descriptor->GetTaggedParameterSlots();
  result.result_tier = wasm::ExecutionTier::kTurbofan;
  if (kind == CodeKind::WASM_TO_JS_FUNCTION) {
    result.kind = wasm::WasmCompilationResult::kWasmToJsWrapper;
  }
  return result;
}

void TraceFinishWrapperCompilation(OptimizedCompilationInfo& info,
                                   CodeTracer* code_tracer,
                                   const wasm::WasmCompilationResult& result,
                                   CodeGenerator* code_generator) {
  if (info.trace_turbo_json()) {
    TurboJsonFile json_of(&info, std::ios_base::app);
    json_of << "{\"name\":\"disassembly\",\"type\":\"disassembly\""
            << BlockStartsAsJSON{&code_generator->block_starts()}
            << "\"data\":\"";
#ifdef ENABLE_DISASSEMBLER
    std::stringstream disassembler_stream;
    Disassembler::Decode(
        nullptr, disassembler_stream, result.code_desc.buffer,
        result.code_desc.buffer + result.code_desc.safepoint_table_offset,
        CodeReference(&result.code_desc));
    for (auto const c : disassembler_stream.str()) {
      json_of << AsEscapedUC16ForJSON(c);
    }
#endif  // ENABLE_DISASSEMBLER
    json_of << "\"}\n]";
    json_of << "\n}";
  }

  if (info.trace_turbo_json() || info.trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(code_tracer);
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Finished compiling method " << info.GetDebugName().get()
        << " using TurboFan" << std::endl;
  }
}

}  // namespace

// static
wasm::WasmCompilationResult Pipeline::GenerateCodeForWasmNativeStub(
    CallDescriptor* call_descriptor, MachineGraph* mcgraph, CodeKind kind,
    const char* debug_name, const AssemblerOptions& options,
    SourcePositionTable* source_positions) {
  TFGraph* graph = mcgraph->graph();
  OptimizedCompilationInfo info(base::CStrVector(debug_name), graph->zone(),
                                kind);
  // Construct a pipeline for scheduling and code generation.
  wasm::WasmEngine* wasm_engine = wasm::GetWasmEngine();
  ZoneStats zone_stats(wasm_engine->allocator());
  NodeOriginTable* node_positions = graph->zone()->New<NodeOriginTable>(graph);
  TFPipelineData data(&zone_stats, wasm_engine, &info, mcgraph, nullptr,
                      source_positions, node_positions, options);
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics.reset(new TurbofanPipelineStatistics(
        &info, wasm_engine->GetOrCreateTurboStatistics(), &zone_stats));
    pipeline_statistics->BeginPhaseKind("V8.WasmStubCodegen");
  }
  TraceWrapperCompilation("TurboFan", &info, &data);

  PipelineImpl pipeline(&data);
  pipeline.RunPrintAndVerify("V8.WasmNativeStubMachineCode", true);

  CHECK(pipeline.Run<MemoryOptimizationPhase>());
  pipeline.RunPrintAndVerify(MemoryOptimizationPhase::phase_name(), true);

  CHECK(pipeline.ComputeScheduledGraph());

  Linkage linkage(call_descriptor);

  turboshaft::PipelineData turboshaft_data(
      &zone_stats, turboshaft::TurboshaftPipelineKind::kWasm, nullptr, &info,
      options);
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data);

  CHECK(turboshaft_pipeline.CreateGraphFromTurbofan(&data, &linkage));
  // We need to run simplification to normalize some patterns for instruction
  // selection (e.g. loads and stores).
  CHECK(turboshaft_pipeline.RunSimplificationAndNormalizationPhase());

  CHECK(GenerateCodeFromTurboshaftGraph(&linkage, turboshaft_pipeline,
                                        &pipeline, data.osr_helper_ptr()));

  auto result = WrapperCompilationResult(turboshaft_data.code_generator(),
                                         call_descriptor, kind);
  DCHECK(result.succeeded());
  CodeTracer* code_tracer = nullptr;
  if (info.trace_turbo_json() || info.trace_turbo_graph()) {
    code_tracer = data.GetCodeTracer();
  }
  TraceFinishWrapperCompilation(info, code_tracer, result,
                                pipeline.code_generator());
  return result;
}

// static
wasm::WasmCompilationResult
Pipeline::GenerateCodeForWasmNativeStubFromTurboshaft(
    const wasm::CanonicalSig* sig, wasm::WrapperCompilationInfo wrapper_info,
    const char* debug_name, const AssemblerOptions& options,
    SourcePositionTable* source_positions) {
  wasm::WasmEngine* wasm_engine = wasm::GetWasmEngine();
  Zone zone(wasm_engine->allocator(), ZONE_NAME);
  WasmCallKind call_kind =
      wrapper_info.code_kind == CodeKind::WASM_TO_JS_FUNCTION
          ? WasmCallKind::kWasmImportWrapper
          : WasmCallKind::kWasmCapiFunction;
  CallDescriptor* call_descriptor =
      GetWasmCallDescriptor(&zone, sig, call_kind);
  if (!Is64()) {
    call_descriptor = GetI32WasmCallDescriptor(&zone, call_descriptor);
  }
  Linkage linkage(call_descriptor);
  OptimizedCompilationInfo info(base::CStrVector(debug_name), &zone,
                                wrapper_info.code_kind);
  ZoneStats zone_stats(wasm_engine->allocator());
  TFPipelineData data(&zone_stats, &info, nullptr,
                      wasm::GetWasmEngine()->allocator(), nullptr, nullptr,
                      nullptr, nullptr, nullptr, nullptr, options, nullptr);
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics.reset(new TurbofanPipelineStatistics(
        &info, wasm_engine->GetOrCreateTurboStatistics(), &zone_stats));
    pipeline_statistics->BeginPhaseKind("V8.WasmStubCodegen");
  }
  TraceWrapperCompilation("Turboshaft", &info, &data);

  PipelineImpl pipeline(&data);

  {
    turboshaft::PipelineData turboshaft_data(
        &zone_stats, turboshaft::TurboshaftPipelineKind::kWasm, nullptr, &info,
        options);
    turboshaft_data.SetIsWasmWrapper(sig);
    AccountingAllocator allocator;
    turboshaft_data.InitializeGraphComponent(source_positions);
    BuildWasmWrapper(&turboshaft_data, &allocator, turboshaft_data.graph(), sig,
                     wrapper_info);
    CodeTracer* code_tracer = nullptr;
    if (info.trace_turbo_json() || info.trace_turbo_graph()) {
      // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
      // because it may not yet be initialized then and doing so from the
      // background thread is not threadsafe.
      code_tracer = data.GetCodeTracer();
    }
    Zone printing_zone(&allocator, ZONE_NAME);
    turboshaft::PrintTurboshaftGraph(&turboshaft_data, &printing_zone,
                                     code_tracer, "Graph generation");

    // Skip the LoopUnrolling, WasmGCOptimize and WasmLowering phases for
    // wrappers.
    // TODO(14108): Do we need value numbering if wasm_opt is turned off?
    turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data);
    if (v8_flags.wasm_opt) {
      CHECK(turboshaft_pipeline.Run<turboshaft::WasmOptimizePhase>());
    }
#if DEBUG
    if (!v8_flags.wasm_opt) {
      // We still need to lower allocation operations even with optimizations
      // being turned off.
      CHECK(
          turboshaft_pipeline.Run<turboshaft::WasmDebugMemoryLoweringPhase>());
    }
#endif

    if (!Is64()) {
      CHECK(turboshaft_pipeline.Run<turboshaft::Int64LoweringPhase>());
    }

    CHECK(turboshaft_pipeline.Run<turboshaft::WasmDeadCodeEliminationPhase>());

    if (V8_UNLIKELY(v8_flags.turboshaft_enable_debug_features)) {
      // This phase has to run very late to allow all previous phases to use
      // debug features.
      CHECK(turboshaft_pipeline.Run<turboshaft::DebugFeatureLoweringPhase>());
    }

    data.BeginPhaseKind("V8.InstructionSelection");

    const bool success = GenerateCodeFromTurboshaftGraph(
        &linkage, turboshaft_pipeline, &pipeline, data.osr_helper_ptr());
    CHECK(success);

    auto result =
        WrapperCompilationResult(turboshaft_data.code_generator(),
                                 call_descriptor, wrapper_info.code_kind);
    DCHECK(result.succeeded());

    TraceFinishWrapperCompilation(info, code_tracer, result,
                                  turboshaft_data.code_generator());
    return result;
  }
}

namespace {

base::OwnedVector<uint8_t> SerializeInliningPositions(
    const ZoneVector<WasmInliningPosition>& positions) {
  const size_t entry_size = sizeof positions[0].inlinee_func_index +
                            sizeof positions[0].was_tail_call +
                            sizeof positions[0].caller_pos;
  auto result = base::OwnedVector<uint8_t>::New(positions.size() * entry_size);
  uint8_t* iter = result.begin();
  for (const auto& [func_index, was_tail_call, caller_pos] : positions) {
    size_t index_size = sizeof func_index;
    std::memcpy(iter, &func_index, index_size);
    iter += index_size;
    size_t was_tail_call_size = sizeof was_tail_call;
    std::memcpy(iter, &was_tail_call, was_tail_call_size);
    iter += was_tail_call_size;
    size_t pos_size = sizeof caller_pos;
    std::memcpy(iter, &caller_pos, pos_size);
    iter += pos_size;
  }
  DCHECK_EQ(iter, result.end());
  return result;
}

}  // namespace

// static
wasm::WasmCompilationResult Pipeline::GenerateWasmCode(
    wasm::CompilationEnv* env, WasmCompilationData& compilation_data,
    wasm::WasmDetectedFeatures* detected, Counters* counters) {
  auto* wasm_engine = wasm::GetWasmEngine();
  const wasm::WasmModule* module = env->module;
  base::TimeTicks start_time;
  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    start_time = base::TimeTicks::Now();
  }
  ZoneStats zone_stats(wasm_engine->allocator());

  Zone graph_zone{wasm_engine->allocator(), ZONE_NAME};
  OptimizedCompilationInfo info(
      GetDebugName(&graph_zone, env->module,
                   compilation_data.wire_bytes_storage,
                   compilation_data.func_index),
      &graph_zone, CodeKind::WASM_FUNCTION);

  if (info.trace_turbo_json()) {
    TurboCfgFile tcf;
    tcf << AsC1VCompilation(&info);
  }

  // TODO(nicohartmann): We should not allocate TurboFan graph(s) here but
  // instead use only Turboshaft.
  compiler::MachineGraph* mcgraph = graph_zone.New<compiler::MachineGraph>(
      graph_zone.New<compiler::TFGraph>(&graph_zone),
      graph_zone.New<CommonOperatorBuilder>(&graph_zone),
      graph_zone.New<MachineOperatorBuilder>(
          &graph_zone, MachineType::PointerRepresentation(),
          InstructionSelector::SupportedMachineOperatorFlags(),
          InstructionSelector::AlignmentRequirements()));
  if (info.trace_turbo_json()) {
    compilation_data.node_origins =
        graph_zone.New<NodeOriginTable>(mcgraph->graph());
  }

  compilation_data.source_positions =
      mcgraph->zone()->New<SourcePositionTable>(mcgraph->graph());
  auto call_descriptor =
      GetWasmCallDescriptor(&graph_zone, compilation_data.func_body.sig);

  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics(
      CreatePipelineStatistics(compilation_data, module, &info, &zone_stats));
  AssemblerOptions options = WasmAssemblerOptions();
  TFPipelineData data(&zone_stats, wasm_engine, &info, mcgraph,
                      pipeline_statistics.get(),
                      compilation_data.source_positions,
                      compilation_data.node_origins, options);

  PipelineImpl pipeline(&data);

  if (data.info()->trace_turbo_json() || data.info()->trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data.GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Begin compiling method " << data.info()->GetDebugName().get()
        << " using Turboshaft" << std::endl;
  }

  if (mcgraph->machine()->Is32()) {
    call_descriptor =
        GetI32WasmCallDescriptor(mcgraph->zone(), call_descriptor);
  }
  Linkage linkage(call_descriptor);

  Zone inlining_positions_zone(wasm_engine->allocator(), ZONE_NAME);
  ZoneVector<WasmInliningPosition> inlining_positions(&inlining_positions_zone);

  wasm::WasmFunctionCoverageData* function_coverage_data = nullptr;
  if (V8_UNLIKELY(v8_flags.wasm_code_coverage)) {
    DCHECK_NOT_NULL(env->module_coverage_data);
    int declared_function_index =
        wasm::declared_function_index(env->module, compilation_data.func_index);
    function_coverage_data = env->module_coverage_data->GetFunctionCoverageData(
        declared_function_index);
    if (!function_coverage_data) {
      // There can be a race here, if another thread finds that
      // GetFunctionCoverageData() returns nullptr, but in that case we would
      // just waste some time calling ComputeBasicBlocks().
      size_t func_body_size =
          compilation_data.func_body.end - compilation_data.func_body.start;
      wasm::BasicBlockCalculator basic_block_calculator(
          &graph_zone, {compilation_data.func_body.start, func_body_size});
      basic_block_calculator.ComputeBasicBlocks();
      function_coverage_data = env->module_coverage_data->InstallCoverageData(
          declared_function_index, basic_block_calculator.GetCodeRanges());
    }
  }

  turboshaft::PipelineData turboshaft_data(
      &zone_stats, turboshaft::TurboshaftPipelineKind::kWasm, nullptr, &info,
      options);
  turboshaft_data.set_pipeline_statistics(pipeline_statistics.get());
  const wasm::FunctionSig* sig = compilation_data.func_body.sig;
  turboshaft_data.SetIsWasmFunction(env->module, sig,
                                    compilation_data.func_body.is_shared);
  DCHECK_NOT_NULL(turboshaft_data.wasm_module());

  // TODO(nicohartmann): This only works here because source positions are not
  // actually allocated inside the graph zone of TFPipelineData. We should
  // properly allocate source positions inside Turboshaft's graph zone right
  // from the beginning.
  turboshaft_data.InitializeGraphComponent(data.source_positions());

  AccountingAllocator allocator;
  wasm::BuildTSGraph(&turboshaft_data, &allocator, env, detected,
                     turboshaft_data.graph(), compilation_data.func_body,
                     compilation_data.wire_bytes_storage,
                     &compilation_data.assumptions, &inlining_positions,
                     compilation_data.func_index, function_coverage_data);
  CodeTracer* code_tracer = nullptr;
  if (turboshaft_data.info()->trace_turbo_graph()) {
    // NOTE: We must not call `GetCodeTracer` if tracing is not enabled,
    // because it may not yet be initialized then and doing so from the
    // background thread is not threadsafe.
    code_tracer = data.GetCodeTracer();
  }
  Zone printing_zone(&allocator, ZONE_NAME);
  turboshaft::PrintTurboshaftGraph(&turboshaft_data, &printing_zone,
                                   code_tracer, "Graph generation");

  data.BeginPhaseKind("V8.WasmOptimization");
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data);

#if defined(V8_ENABLE_WASM_SIMD256_REVEC) && defined(V8_TARGET_ARCH_X64)
  if (v8_flags.experimental_wasm_revectorize) {
    bool cpu_feature_support =
        CpuFeatures::IsSupported(AVX) && CpuFeatures::IsSupported(AVX2);
    if (cpu_feature_support && detected->has_simd()) {
      if (v8_flags.trace_wasm_revectorize) {
        std::cout << "Begin revec function "
                  << data.info()->GetDebugName().get() << std::endl;
      }
      CHECK(turboshaft_pipeline.Run<turboshaft::WasmRevecPhase>());
      if (v8_flags.trace_wasm_revectorize) {
        std::cout << "Finished revec function "
                  << data.info()->GetDebugName().get() << std::endl;
      }
    }
  }
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

  const bool uses_wasm_gc_features =
      detected->has_gc() || detected->has_typed_funcref() ||
      detected->has_stringref() || detected->has_imported_strings() ||
      detected->has_imported_strings_utf8();
  if (v8_flags.wasm_loop_peeling && uses_wasm_gc_features) {
    CHECK(turboshaft_pipeline.Run<turboshaft::LoopPeelingPhase>());
  }

  if (v8_flags.wasm_loop_unrolling) {
    // TODO(384870251): Note that if we don't run this, subsequent analyses and
    // optimizations (DCE, decompression optimization) can run much slower.
    CHECK(turboshaft_pipeline.Run<turboshaft::LoopUnrollingPhase>());
  }

  if (v8_flags.wasm_opt && uses_wasm_gc_features) {
    CHECK(turboshaft_pipeline.Run<turboshaft::WasmGCOptimizePhase>());
  }

  // TODO(mliedtke): This phase could be merged with the WasmGCOptimizePhase
  // if wasm_opt is enabled to improve compile time. Consider potential code
  // size increase.
  // TODO(384870251,dmercardier): Run a CopyingPhase with LoopFinder to
  // improve block ordering, independent of loop unrolling.
  CHECK(turboshaft_pipeline.Run<turboshaft::WasmLoweringPhase>());

  // TODO(14108): Do we need value numbering if wasm_opt is turned off?
  const bool is_asm_js = is_asmjs_module(module);
  if (v8_flags.wasm_opt || is_asm_js) {
    CHECK(turboshaft_pipeline.Run<turboshaft::WasmOptimizePhase>());
  }

#if V8_TARGET_ARCH_ARM64
  if (v8_flags.experimental_wasm_simd_opt && v8_flags.wasm_opt &&
      detected->has_simd()) {
    CHECK(turboshaft_pipeline.Run<turboshaft::WasmSimdPhase>());
  }
#endif  // V8_TARGET_ARCH_ARM64

#if DEBUG
  if (!v8_flags.wasm_opt) {
    // We still need to lower allocation operations even with optimizations
    // being turned off.
    CHECK(turboshaft_pipeline.Run<turboshaft::WasmDebugMemoryLoweringPhase>());
  }
#endif

  if (mcgraph->machine()->Is32()) {
    CHECK(turboshaft_pipeline.Run<turboshaft::Int64LoweringPhase>());
  }

  CHECK(turboshaft_pipeline.Run<turboshaft::WasmDeadCodeEliminationPhase>());

  if (V8_UNLIKELY(v8_flags.turboshaft_enable_debug_features)) {
    // This phase has to run very late to allow all previous phases to use
    // debug features.
    CHECK(turboshaft_pipeline.Run<turboshaft::DebugFeatureLoweringPhase>());
  }

  data.BeginPhaseKind("V8.InstructionSelection");

  // Instruction selection for JavaScript may fail, but for Wasm we should
  // always succeed (e.g., by enforcing limits in earlier phases).
  CHECK(GenerateCodeFromTurboshaftGraph(&linkage, turboshaft_pipeline,
                                        &pipeline, data.osr_helper_ptr()));

  CodeGenerator* code_generator = turboshaft_data.code_generator();

  wasm::WasmCompilationResult result;
  code_generator->masm()->GetCode(
      nullptr, &result.code_desc, code_generator->safepoint_table_builder(),
      static_cast<int>(code_generator->handler_table_offset()));

  result.instr_buffer = code_generator->masm()->ReleaseBuffer();
  result.frame_slot_count = code_generator->frame()->GetTotalFrameSlotCount();
  result.tagged_parameter_slots = call_descriptor->GetTaggedParameterSlots();
  result.source_positions = code_generator->GetSourcePositionTable();
  result.inlining_positions = SerializeInliningPositions(inlining_positions);
  result.protected_instructions_data =
      code_generator->GetProtectedInstructionsData();
  result.deopt_data = code_generator->GenerateWasmDeoptimizationData();
  result.result_tier = wasm::ExecutionTier::kTurbofan;

  if (data.info()->trace_turbo_json()) {
    TurboJsonFile json_of(data.info(), std::ios_base::app);
    json_of << "{\"name\":\"disassembly\",\"type\":\"disassembly\""
            << BlockStartsAsJSON{&code_generator->block_starts()}
            << "\"data\":\"";
#ifdef ENABLE_DISASSEMBLER
    std::stringstream disassembler_stream;
    Disassembler::Decode(
        nullptr, disassembler_stream, result.code_desc.buffer,
        result.code_desc.buffer + result.code_desc.safepoint_table_offset,
        CodeReference(&result.code_desc));
    for (auto const c : disassembler_stream.str()) {
      json_of << AsEscapedUC16ForJSON(c);
    }
#endif  // ENABLE_DISASSEMBLER
    json_of << "\"}\n],\n";
    JsonPrintAllSourceWithPositionsWasm(json_of, module,
                                        compilation_data.wire_bytes_storage,
                                        base::VectorOf(inlining_positions));
    json_of << "}";
    json_of << "\n}";
  }

  if (data.info()->trace_turbo_json() || data.info()->trace_turbo_graph()) {
    CodeTracer::StreamScope tracing_scope(data.GetCodeTracer());
    tracing_scope.stream()
        << "---------------------------------------------------\n"
        << "Finished compiling method " << data.info()->GetDebugName().get()
        << " using Turboshaft" << std::endl;
  }

  if (V8_UNLIKELY(v8_flags.trace_wasm_compilation_times)) {
    base::TimeDelta time = base::TimeTicks::Now() - start_time;
    int codesize = result.code_desc.body_size();
    StdoutStream{} << "Compiled function "
                   << reinterpret_cast<const void*>(module) << "#"
                   << compilation_data.func_index << " using TurboFan, took "
                   << time.InMilliseconds() << " ms and "
                   << zone_stats.GetMaxAllocatedBytes() << " / "
                   << zone_stats.GetTotalAllocatedBytes()
                   << " max/total bytes; bodysize "
                   << compilation_data.body_size() << " codesize " << codesize
                   << " name " << data.info()->GetDebugName().get()
                   << std::endl;
  }

  if (counters && compilation_data.body_size() >= 100 * KB) {
    size_t zone_bytes = zone_stats.GetMaxAllocatedBytes();
    counters->wasm_compile_huge_function_peak_memory_bytes()->AddSample(
        static_cast<int>(std::min(size_t{kMaxInt}, zone_bytes)));
  }

  // Add a "0 deopts" sample for the first tier-up of a function that contains
  // any deopt data. This indicates a baseline of how many functions can
  // potentially deopt, so that the statistics of having x functions that
  // deopted at least once becomes more meaningful.
  if (counters && !result.deopt_data.empty()) {
    DCHECK(v8_flags.wasm_deopt);
    bool is_first_tierup = false;
    {
      const wasm::TypeFeedbackStorage& feedback = module->type_feedback;
      base::MutexGuard mutex_guard(&feedback.mutex);
      is_first_tierup = !feedback.deopt_count_for_function.contains(
          compilation_data.func_index);
    }
    if (is_first_tierup) {
      counters->wasm_deopts_per_function()->AddSample(0);
    }
  }

  DCHECK(result.succeeded());
  return result;
}
#endif  // V8_ENABLE_WEBASSEMBLY

// static
MaybeHandle<Code> Pipeline::GenerateCodeForTesting(
    OptimizedCompilationInfo* info, Isolate* isolate) {
  ZoneStats zone_stats(isolate->allocator());
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics(
      CreatePipelineStatistics(Handle<Script>::null(), info, isolate,
                               &zone_stats));

  TFPipelineData data(&zone_stats, isolate, info, pipeline_statistics.get());
  turboshaft::PipelineData turboshaft_data(
      &zone_stats, turboshaft::TurboshaftPipelineKind::kJS, isolate, info,
      AssemblerOptions::Default(isolate));
  turboshaft_data.set_pipeline_statistics(pipeline_statistics.get());
  PipelineJobScope scope(&data, isolate->counters()->runtime_call_stats());
  PipelineImpl pipeline(&data);
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data);

  Linkage linkage(Linkage::ComputeIncoming(data.instruction_zone(), info));

  {
    CompilationHandleScope compilation_scope(isolate, info);
    info->ReopenAndCanonicalizeHandlesInNewScope(isolate);
    if (!pipeline.InitializeHeapBroker()) return {};
  }

  {
    LocalIsolateScope local_isolate_scope(data.broker(), info,
                                          isolate->main_thread_local_isolate());
    turboshaft_data.InitializeBrokerAndDependencies(data.broker_ptr(),
                                                    data.dependencies());

    if (V8_UNLIKELY(v8_flags.turbolev)) {
      if (!turboshaft_pipeline.CreateGraphWithMaglev(&linkage)) return {};
    } else {
      if (!pipeline.CreateGraph(&linkage)) return {};
      // We selectively Unpark inside OptimizeTurbofanGraph.
      if (!pipeline.OptimizeTurbofanGraph(&linkage)) return {};

      // We convert the turbofan graph to turboshaft.
      if (!turboshaft_pipeline.CreateGraphFromTurbofan(&data, &linkage)) {
        data.EndPhaseKind();
        return {};
      }
    }

    if (!turboshaft_pipeline.OptimizeTurboshaftGraph(&linkage)) {
      return {};
    }

    const bool success = GenerateCodeFromTurboshaftGraph(
        &linkage, turboshaft_pipeline, &pipeline, data.osr_helper_ptr());
    if (!success) return {};

    Handle<Code> code;
    if (turboshaft_pipeline.FinalizeCode().ToHandle(&code) &&
        turboshaft_pipeline.CommitDependencies(code)) {
      return code;
    }
    return {};
  }
}

// static
MaybeHandle<Code> Pipeline::GenerateCodeForTesting(
    OptimizedCompilationInfo* info, Isolate* isolate,
    CallDescriptor* call_descriptor, TFGraph* graph,
    const AssemblerOptions& opts, Schedule* schedule) {
  // TODO(nicohartmann): Callers should properly set this, but it's hard to do
  // this through testing logic shared between JS and Wasm.
  AssemblerOptions options = opts;
#if V8_ENABLE_WEBASSEMBLY
  if (info->IsWasm() || info->IsWasmBuiltin()) {
    options.is_wasm = true;
  }
#endif
  // Construct a pipeline for scheduling and code generation.
  ZoneStats zone_stats(isolate->allocator());
  NodeOriginTable* node_positions = info->zone()->New<NodeOriginTable>(graph);
  TFPipelineData data(&zone_stats, info, isolate, isolate->allocator(), graph,
                      nullptr, schedule, nullptr, node_positions, nullptr,
                      options, nullptr);
  turboshaft::PipelineData turboshaft_data(
      &zone_stats, turboshaft::TurboshaftPipelineKind::kCSA, isolate, info,
      options);
  PipelineJobScope scope(&data, isolate->counters()->runtime_call_stats());
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics.reset(new TurbofanPipelineStatistics(
        info, isolate->GetTurboStatistics(), &zone_stats));
    pipeline_statistics->BeginPhaseKind("V8.TFTestCodegen");
  }

  PipelineImpl pipeline(&data);
  turboshaft::Pipeline turboshaft_pipeline(&turboshaft_data);

  if (info->trace_turbo_json()) {
    TurboJsonFile json_of(info, std::ios_base::trunc);
    json_of << "{\"function\":\"" << info->GetDebugName().get()
            << "\", \"source\":\"\",\n\"phases\":[";
  }
  // TODO(rossberg): Should this really be untyped?
  pipeline.RunPrintAndVerify("V8.TFMachineCode", true);

  // Ensure we have a schedule.
  if (data.schedule() == nullptr) {
    CHECK(pipeline.ComputeScheduledGraph());
  }

  Linkage linkage(call_descriptor);

  // We convert the turbofan graph to turboshaft.
  if (!turboshaft_pipeline.CreateGraphFromTurbofan(&data, &linkage)) {
    return {};
  }

  // We need to run simplification to normalize some patterns for instruction
  // selection (e.g. loads and stores).
  if (!turboshaft_pipeline.RunSimplificationAndNormalizationPhase()) {
    return {};
  }

  if (!GenerateCodeFromTurboshaftGraph(&linkage, turboshaft_pipeline, nullptr,
                                       data.osr_helper_ptr())) {
    return {};
  }

  MaybeHandle<Code> maybe_code = turboshaft_pipeline.FinalizeCode();
  Handle<Code> code;
  if (maybe_code.ToHandle(&code)) {
    return code;
  }
  return {};
}

// static
MaybeHandle<Code> Pipeline::GenerateTurboshaftCodeForTesting(
    CallDescriptor* call_descriptor, turboshaft::PipelineData* data) {
  Isolate* isolate = data->isolate();
  OptimizedCompilationInfo* info = data->info();
  PipelineJobScope scope(data, isolate->counters()->runtime_call_stats());
  std::unique_ptr<TurbofanPipelineStatistics> pipeline_statistics;
  if (v8_flags.turbo_stats || v8_flags.turbo_stats_nvp) {
    pipeline_statistics.reset(new TurbofanPipelineStatistics(
        info, isolate->GetTurboStatistics(), data->zone_stats()));
    pipeline_statistics->BeginPhaseKind("V8.TFTestCodegen");
  }

  turboshaft::Pipeline pipeline(data);

  if (info->trace_turbo_json()) {
    {
      TurboJsonFile json_of(info, std::ios_base::trunc);
      json_of << "{\"function\":\"" << info->GetDebugName().get()
              << "\", \"source\":\"\",\n\"phases\":[";
    }
    {
      UnparkedScopeIfNeeded unparked_scope(data->broker());
      AllowHandleDereference allow_deref;

      TurboJsonFile json_of(data->info(), std::ios_base::app);
      turboshaft::PrintTurboshaftGraphForTurbolizer(
          json_of, data->graph(), "V8.TSMachineCode", data->node_origins(),
          data->graph_zone());
    }
  }

  info->tick_counter().TickAndMaybeEnterSafepoint();

  data->InitializeCodegenComponent(nullptr);

  Handle<Code> code;
  if (pipeline.GenerateCode(call_descriptor).ToHandle(&code) &&
      pipeline.CommitDependencies(code)) {
    return code;
  }
  return {};
}

// static
std::unique_ptr<TurbofanCompilationJob> Pipeline::NewCompilationJob(
    Isolate* isolate, Handle<JSFunction> function, CodeKind code_kind,
    bool has_script, BytecodeOffset osr_offset) {
  Handle<SharedFunctionInfo> shared(function->shared(), isolate);
  return std::make_unique<PipelineCompilationJob>(isolate, shared, function,
                                                  osr_offset, code_kind);
}

bool PipelineImpl::ComputeScheduledGraph() {
  TFPipelineData* data = this->data_;

  // We should only schedule the graph if it is not scheduled yet.
  DCHECK_NULL(data->schedule());

  RUN_MAYBE_ABORT(ComputeSchedulePhase);
  TraceScheduleAndVerify(data->info(), data, data->schedule(), "schedule");
  return true;
}

#if V8_ENABLE_WASM_SIMD256_REVEC
bool PipelineImpl::Revectorize() { return Run<RevectorizePhase>(); }
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

OptimizedCompilationInfo* PipelineImpl::info() const { return data_->info(); }

Isolate* PipelineImpl::isolate() const { return data_->isolate(); }

CodeGenerator* PipelineImpl::code_generator() const {
  return data_->code_generator();
}

ObserveNodeManager* PipelineImpl::observe_node_manager() const {
  return data_->observe_node_manager();
}

std::ostream& operator<<(std::ostream& out, const InstructionRangesAsJSON& s) {
  const int max = static_cast<int>(s.sequence->LastInstructionIndex());

  out << ", \"nodeIdToInstructionRange\": {";
  bool need_comma = false;
  for (size_t i = 0; i < s.instr_origins->size(); ++i) {
    std::pair<int, int> offset = (*s.instr_origins)[i];
    if (offset.first == -1) continue;
    const int first = max - offset.first + 1;
    const int second = max - offset.second + 1;
    if (need_comma) out << ", ";
    out << "\"" << i << "\": [" << first << ", " << second << "]";
    need_comma = true;
  }
  out << "}";
  out << ", \"blockIdToInstructionRange\": {";
  need_comma = false;
  for (auto block : s.sequence->instruction_blocks()) {
    if (need_comma) out << ", ";
    out << "\"" << block->rpo_number() << "\": [" << block->code_start() << ", "
        << block->code_end() << "]";
    need_comma = true;
  }
  out << "}";
  return out;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
