// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/pipeline.h"

#include <fstream>  // NOLINT(readability/streams)
#include <sstream>

#include "src/base/platform/elapsed-timer.h"
#include "src/compiler/ast-graph-builder.h"
#include "src/compiler/basic-block-instrumentor.h"
#include "src/compiler/change-lowering.h"
#include "src/compiler/code-generator.h"
#include "src/compiler/control-reducer.h"
#include "src/compiler/graph-replay.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/instruction.h"
#include "src/compiler/instruction-selector.h"
#include "src/compiler/js-context-specialization.h"
#include "src/compiler/js-generic-lowering.h"
#include "src/compiler/js-inlining.h"
#include "src/compiler/js-typed-lowering.h"
#include "src/compiler/machine-operator-reducer.h"
#include "src/compiler/pipeline-statistics.h"
#include "src/compiler/register-allocator.h"
#include "src/compiler/register-allocator-verifier.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/select-lowering.h"
#include "src/compiler/simplified-lowering.h"
#include "src/compiler/simplified-operator-reducer.h"
#include "src/compiler/typer.h"
#include "src/compiler/value-numbering-reducer.h"
#include "src/compiler/verifier.h"
#include "src/compiler/zone-pool.h"
#include "src/ostreams.h"
#include "src/utils.h"

namespace v8 {
namespace internal {
namespace compiler {

class PipelineData {
 public:
  PipelineData(CompilationInfo* info, ZonePool* zone_pool,
               PipelineStatistics* pipeline_statistics)
      : isolate_(info->zone()->isolate()),
        info_(info),
        outer_zone_(info->zone()),
        zone_pool_(zone_pool),
        pipeline_statistics_(pipeline_statistics),
        compilation_failed_(false),
        code_(Handle<Code>::null()),
        graph_zone_scope_(zone_pool_),
        graph_zone_(graph_zone_scope_.zone()),
        graph_(new (graph_zone()) Graph(graph_zone())),
        source_positions_(new SourcePositionTable(graph())),
        machine_(new (graph_zone()) MachineOperatorBuilder(
            graph_zone(), kMachPtr,
            InstructionSelector::SupportedMachineOperatorFlags())),
        common_(new (graph_zone()) CommonOperatorBuilder(graph_zone())),
        javascript_(new (graph_zone()) JSOperatorBuilder(graph_zone())),
        jsgraph_(new (graph_zone())
                 JSGraph(graph(), common(), javascript(), machine())),
        typer_(new Typer(graph(), info->context())),
        context_node_(nullptr),
        schedule_(nullptr),
        instruction_zone_scope_(zone_pool_),
        instruction_zone_(instruction_zone_scope_.zone()),
        sequence_(nullptr),
        frame_(nullptr) {}


  // For machine graph testing only.
  PipelineData(Graph* graph, Schedule* schedule, ZonePool* zone_pool)
      : isolate_(graph->zone()->isolate()),
        info_(nullptr),
        outer_zone_(nullptr),
        zone_pool_(zone_pool),
        pipeline_statistics_(nullptr),
        compilation_failed_(false),
        code_(Handle<Code>::null()),
        graph_zone_scope_(zone_pool_),
        graph_zone_(nullptr),
        graph_(graph),
        source_positions_(new SourcePositionTable(graph)),
        machine_(nullptr),
        common_(nullptr),
        javascript_(nullptr),
        jsgraph_(nullptr),
        typer_(nullptr),
        context_node_(nullptr),
        schedule_(schedule),
        instruction_zone_scope_(zone_pool_),
        instruction_zone_(instruction_zone_scope_.zone()),
        sequence_(nullptr),
        frame_(nullptr) {}

  ~PipelineData() {
    DeleteInstructionZone();
    DeleteGraphZone();
  }

  Isolate* isolate() const { return isolate_; }
  CompilationInfo* info() const { return info_; }
  ZonePool* zone_pool() const { return zone_pool_; }
  PipelineStatistics* pipeline_statistics() { return pipeline_statistics_; }
  bool compilation_failed() const { return compilation_failed_; }
  void set_compilation_failed() { compilation_failed_ = true; }
  Handle<Code> code() { return code_; }
  void set_code(Handle<Code> code) {
    DCHECK(code_.is_null());
    code_ = code;
  }

  // RawMachineAssembler generally produces graphs which cannot be verified.
  bool MayHaveUnverifiableGraph() const { return outer_zone_ == nullptr; }

  Zone* graph_zone() const { return graph_zone_; }
  Graph* graph() const { return graph_; }
  SourcePositionTable* source_positions() const {
    return source_positions_.get();
  }
  MachineOperatorBuilder* machine() const { return machine_; }
  CommonOperatorBuilder* common() const { return common_; }
  JSOperatorBuilder* javascript() const { return javascript_; }
  JSGraph* jsgraph() const { return jsgraph_; }
  Typer* typer() const { return typer_.get(); }

  Node* context_node() const { return context_node_; }
  void set_context_node(Node* context_node) {
    DCHECK_EQ(nullptr, context_node_);
    context_node_ = context_node;
  }

  Schedule* schedule() const { return schedule_; }
  void set_schedule(Schedule* schedule) {
    DCHECK_EQ(nullptr, schedule_);
    schedule_ = schedule;
  }

  Zone* instruction_zone() const { return instruction_zone_; }
  InstructionSequence* sequence() const { return sequence_; }
  void set_sequence(InstructionSequence* sequence) {
    DCHECK_EQ(nullptr, sequence_);
    sequence_ = sequence;
  }
  Frame* frame() const { return frame_; }
  void set_frame(Frame* frame) {
    DCHECK_EQ(nullptr, frame_);
    frame_ = frame;
  }

  void DeleteGraphZone() {
    // Destroy objects with destructors first.
    source_positions_.Reset(nullptr);
    typer_.Reset(nullptr);
    if (graph_zone_ == nullptr) return;
    // Destroy zone and clear pointers.
    graph_zone_scope_.Destroy();
    graph_zone_ = nullptr;
    graph_ = nullptr;
    machine_ = nullptr;
    common_ = nullptr;
    javascript_ = nullptr;
    jsgraph_ = nullptr;
    context_node_ = nullptr;
    schedule_ = nullptr;
  }

  void DeleteInstructionZone() {
    if (instruction_zone_ == nullptr) return;
    instruction_zone_scope_.Destroy();
    instruction_zone_ = nullptr;
    sequence_ = nullptr;
    frame_ = nullptr;
  }

 private:
  Isolate* isolate_;
  CompilationInfo* info_;
  Zone* outer_zone_;
  ZonePool* zone_pool_;
  PipelineStatistics* pipeline_statistics_;
  bool compilation_failed_;
  Handle<Code> code_;

  ZonePool::Scope graph_zone_scope_;
  Zone* graph_zone_;
  // All objects in the following group of fields are allocated in graph_zone_.
  // They are all set to NULL when the graph_zone_ is destroyed.
  Graph* graph_;
  // TODO(dcarney): make this into a ZoneObject.
  SmartPointer<SourcePositionTable> source_positions_;
  MachineOperatorBuilder* machine_;
  CommonOperatorBuilder* common_;
  JSOperatorBuilder* javascript_;
  JSGraph* jsgraph_;
  // TODO(dcarney): make this into a ZoneObject.
  SmartPointer<Typer> typer_;
  Node* context_node_;
  Schedule* schedule_;

  // All objects in the following group of fields are allocated in
  // instruction_zone_.  They are all set to NULL when the instruction_zone_ is
  // destroyed.
  ZonePool::Scope instruction_zone_scope_;
  Zone* instruction_zone_;
  InstructionSequence* sequence_;
  Frame* frame_;

  DISALLOW_COPY_AND_ASSIGN(PipelineData);
};


static inline bool VerifyGraphs() {
#ifdef DEBUG
  return true;
#else
  return FLAG_turbo_verify;
#endif
}


struct TurboCfgFile : public std::ofstream {
  explicit TurboCfgFile(Isolate* isolate)
      : std::ofstream(isolate->GetTurboCfgFileName().c_str(),
                      std::ios_base::app) {}
};


static void TraceSchedule(Schedule* schedule) {
  if (!FLAG_trace_turbo) return;
  OFStream os(stdout);
  os << "-- Schedule --------------------------------------\n" << *schedule;
}


static SmartArrayPointer<char> GetDebugName(CompilationInfo* info) {
  SmartArrayPointer<char> name;
  if (info->IsStub()) {
    if (info->code_stub() != NULL) {
      CodeStub::Major major_key = info->code_stub()->MajorKey();
      const char* major_name = CodeStub::MajorName(major_key, false);
      size_t len = strlen(major_name);
      name.Reset(new char[len]);
      memcpy(name.get(), major_name, len);
    }
  } else {
    AllowHandleDereference allow_deref;
    name = info->function()->debug_name()->ToCString();
  }
  return name;
}


class AstGraphBuilderWithPositions : public AstGraphBuilder {
 public:
  explicit AstGraphBuilderWithPositions(Zone* local_zone, CompilationInfo* info,
                                        JSGraph* jsgraph,
                                        SourcePositionTable* source_positions)
      : AstGraphBuilder(local_zone, info, jsgraph),
        source_positions_(source_positions) {}

  bool CreateGraph() {
    SourcePositionTable::Scope pos(source_positions_,
                                   SourcePosition::Unknown());
    return AstGraphBuilder::CreateGraph();
  }

#define DEF_VISIT(type)                                               \
  virtual void Visit##type(type* node) OVERRIDE {                     \
    SourcePositionTable::Scope pos(source_positions_,                 \
                                   SourcePosition(node->position())); \
    AstGraphBuilder::Visit##type(node);                               \
  }
  AST_NODE_LIST(DEF_VISIT)
#undef DEF_VISIT

  Node* GetFunctionContext() { return AstGraphBuilder::GetFunctionContext(); }

 private:
  SourcePositionTable* source_positions_;
};


class PipelineRunScope {
 public:
  PipelineRunScope(PipelineData* data, const char* phase_name)
      : phase_scope_(
            phase_name == nullptr ? nullptr : data->pipeline_statistics(),
            phase_name),
        zone_scope_(data->zone_pool()) {}

  Zone* zone() { return zone_scope_.zone(); }

 private:
  PhaseScope phase_scope_;
  ZonePool::Scope zone_scope_;
};


template <typename Phase>
void Pipeline::Run() {
  PipelineRunScope scope(this->data_, Phase::phase_name());
  Phase phase;
  phase.Run(this->data_, scope.zone());
}


template <typename Phase, typename Arg0>
void Pipeline::Run(Arg0 arg_0) {
  PipelineRunScope scope(this->data_, Phase::phase_name());
  Phase phase;
  phase.Run(this->data_, scope.zone(), arg_0);
}


// TODO(dcarney): this one should be unecessary.
template <typename Phase, typename Arg0, typename Arg1>
void Pipeline::Run(Arg0 arg_0, Arg1 arg_1) {
  PipelineRunScope scope(this->data_, Phase::phase_name());
  Phase phase;
  phase.Run(this->data_, scope.zone(), arg_0, arg_1);
}


struct GraphBuilderPhase {
  static const char* phase_name() { return "graph builder"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    AstGraphBuilderWithPositions graph_builder(
        temp_zone, data->info(), data->jsgraph(), data->source_positions());
    if (graph_builder.CreateGraph()) {
      data->set_context_node(graph_builder.GetFunctionContext());
    } else {
      data->set_compilation_failed();
    }
  }
};


struct ContextSpecializerPhase {
  static const char* phase_name() { return nullptr; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    JSContextSpecializer spec(data->info(), data->jsgraph(),
                              data->context_node());
    spec.SpecializeToContext();
  }
};


struct InliningPhase {
  static const char* phase_name() { return "inlining"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    JSInliner inliner(temp_zone, data->info(), data->jsgraph());
    inliner.Inline();
  }
};


struct TyperPhase {
  static const char* phase_name() { return "typer"; }

  void Run(PipelineData* data, Zone* temp_zone) { data->typer()->Run(); }
};


struct TypedLoweringPhase {
  static const char* phase_name() { return "typed lowering"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    ValueNumberingReducer vn_reducer(data->graph_zone());
    JSTypedLowering lowering(data->jsgraph());
    SimplifiedOperatorReducer simple_reducer(data->jsgraph());
    GraphReducer graph_reducer(data->graph(), temp_zone);
    graph_reducer.AddReducer(&vn_reducer);
    graph_reducer.AddReducer(&lowering);
    graph_reducer.AddReducer(&simple_reducer);
    graph_reducer.ReduceGraph();
  }
};


struct SimplifiedLoweringPhase {
  static const char* phase_name() { return "simplified lowering"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    SimplifiedLowering lowering(data->jsgraph());
    lowering.LowerAllNodes();
    ValueNumberingReducer vn_reducer(data->graph_zone());
    SimplifiedOperatorReducer simple_reducer(data->jsgraph());
    GraphReducer graph_reducer(data->graph(), temp_zone);
    graph_reducer.AddReducer(&vn_reducer);
    graph_reducer.AddReducer(&simple_reducer);
    graph_reducer.ReduceGraph();
  }
};


struct ChangeLoweringPhase {
  static const char* phase_name() { return "change lowering"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    Linkage linkage(data->graph_zone(), data->info());
    ValueNumberingReducer vn_reducer(data->graph_zone());
    SimplifiedOperatorReducer simple_reducer(data->jsgraph());
    ChangeLowering lowering(data->jsgraph(), &linkage);
    MachineOperatorReducer mach_reducer(data->jsgraph());
    GraphReducer graph_reducer(data->graph(), temp_zone);
    // TODO(titzer): Figure out if we should run all reducers at once here.
    graph_reducer.AddReducer(&vn_reducer);
    graph_reducer.AddReducer(&simple_reducer);
    graph_reducer.AddReducer(&lowering);
    graph_reducer.AddReducer(&mach_reducer);
    graph_reducer.ReduceGraph();
  }
};


struct ControlReductionPhase {
  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    ControlReducer::ReduceGraph(temp_zone, data->jsgraph(), data->common());
  }
};


struct EarlyControlReductionPhase : ControlReductionPhase {
  static const char* phase_name() { return "early control reduction"; }
};


struct LateControlReductionPhase : ControlReductionPhase {
  static const char* phase_name() { return "late control reduction"; }
};


struct GenericLoweringPhase {
  static const char* phase_name() { return "generic lowering"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    SourcePositionTable::Scope pos(data->source_positions(),
                                   SourcePosition::Unknown());
    JSGenericLowering generic(data->info(), data->jsgraph());
    SelectLowering select(data->jsgraph()->graph(), data->jsgraph()->common());
    GraphReducer graph_reducer(data->graph(), temp_zone);
    graph_reducer.AddReducer(&generic);
    graph_reducer.AddReducer(&select);
    graph_reducer.ReduceGraph();
  }
};


struct ComputeSchedulePhase {
  static const char* phase_name() { return "scheduling"; }

  void Run(PipelineData* data, Zone* temp_zone) {
    Schedule* schedule = Scheduler::ComputeSchedule(temp_zone, data->graph());
    TraceSchedule(schedule);
    if (VerifyGraphs()) ScheduleVerifier::Run(schedule);
    data->set_schedule(schedule);
  }
};


struct InstructionSelectionPhase {
  static const char* phase_name() { return "select instructions"; }

  void Run(PipelineData* data, Zone* temp_zone, Linkage* linkage) {
    InstructionSelector selector(temp_zone, data->graph(), linkage,
                                 data->sequence(), data->schedule(),
                                 data->source_positions());
    selector.SelectInstructions();
  }
};


// TODO(dcarney): break this up.
struct RegisterAllocationPhase {
  static const char* phase_name() { return nullptr; }

  void Run(PipelineData* data, Zone* temp_zone) {
    int node_count = data->sequence()->VirtualRegisterCount();
    if (node_count > UnallocatedOperand::kMaxVirtualRegisters) {
      data->set_compilation_failed();
      return;
    }

    SmartArrayPointer<char> debug_name;
#ifdef DEBUG
    if (data->info() != nullptr) {
      debug_name = GetDebugName(data->info());
    }
#endif

    RegisterAllocator allocator(RegisterConfiguration::ArchDefault(), temp_zone,
                                data->frame(), data->sequence(),
                                debug_name.get());

    if (!allocator.Allocate(data->pipeline_statistics())) {
      data->set_compilation_failed();
      return;
    }

    if (FLAG_trace_turbo) {
      OFStream os(stdout);
      PrintableInstructionSequence printable = {
          RegisterConfiguration::ArchDefault(), data->sequence()};
      os << "----- Instruction sequence after register allocation -----\n"
         << printable;
    }

    if (FLAG_trace_turbo && !data->MayHaveUnverifiableGraph()) {
      TurboCfgFile tcf(data->isolate());
      tcf << AsC1VAllocator("CodeGen", &allocator);
    }
  }
};


struct GenerateCodePhase {
  static const char* phase_name() { return "generate code"; }

  void Run(PipelineData* data, Zone* temp_zone, Linkage* linkage,
           CompilationInfo* info) {
    CodeGenerator generator(data->frame(), linkage, data->sequence(), info);
    data->set_code(generator.GenerateCode());
  }
};


struct PrintGraphPhase {
  static const char* phase_name() { return nullptr; }

  void Run(PipelineData* data, Zone* temp_zone, const char* phase) {
    CompilationInfo* info = data->info();
    Graph* graph = data->graph();
    char buffer[256];
    Vector<char> filename(buffer, sizeof(buffer));
    SmartArrayPointer<char> functionname;
    if (!info->shared_info().is_null()) {
      functionname = info->shared_info()->DebugName()->ToCString();
      if (strlen(functionname.get()) > 0) {
        SNPrintF(filename, "turbo-%s-%s", functionname.get(), phase);
      } else {
        SNPrintF(filename, "turbo-%p-%s", static_cast<void*>(info), phase);
      }
    } else {
      SNPrintF(filename, "turbo-none-%s", phase);
    }
    std::replace(filename.start(), filename.start() + filename.length(), ' ',
                 '_');

    char dot_buffer[256];
    Vector<char> dot_filename(dot_buffer, sizeof(dot_buffer));
    SNPrintF(dot_filename, "%s.dot", filename.start());
    FILE* dot_file = base::OS::FOpen(dot_filename.start(), "w+");
    OFStream dot_of(dot_file);
    dot_of << AsDOT(*graph);
    fclose(dot_file);

    char json_buffer[256];
    Vector<char> json_filename(json_buffer, sizeof(json_buffer));
    SNPrintF(json_filename, "%s.json", filename.start());
    FILE* json_file = base::OS::FOpen(json_filename.start(), "w+");
    OFStream json_of(json_file);
    json_of << AsJSON(*graph);
    fclose(json_file);

    OFStream os(stdout);
    os << "-- " << phase << " graph printed to file " << filename.start()
       << "\n";
  }
};


struct VerifyGraphPhase {
  static const char* phase_name() { return nullptr; }

  void Run(PipelineData* data, Zone* temp_zone, const bool untyped) {
    Verifier::Run(data->graph(), FLAG_turbo_types && !untyped
                                     ? Verifier::TYPED
                                     : Verifier::UNTYPED);
  }
};


void Pipeline::RunPrintAndVerify(const char* phase, bool untyped) {
  if (FLAG_trace_turbo) {
    Run<PrintGraphPhase>(phase);
  }
  if (VerifyGraphs()) {
    Run<VerifyGraphPhase>(untyped);
  }
}


Handle<Code> Pipeline::GenerateCode() {
  // This list must be kept in sync with DONT_TURBOFAN_NODE in ast.cc.
  if (info()->function()->dont_optimize_reason() == kTryCatchStatement ||
      info()->function()->dont_optimize_reason() == kTryFinallyStatement ||
      // TODO(turbofan): Make ES6 for-of work and remove this bailout.
      info()->function()->dont_optimize_reason() == kForOfStatement ||
      // TODO(turbofan): Make super work and remove this bailout.
      info()->function()->dont_optimize_reason() == kSuperReference ||
      // TODO(turbofan): Make class literals work and remove this bailout.
      info()->function()->dont_optimize_reason() == kClassLiteral ||
      // TODO(turbofan): Make OSR work and remove this bailout.
      info()->is_osr()) {
    return Handle<Code>::null();
  }

  ZonePool zone_pool(isolate());
  SmartPointer<PipelineStatistics> pipeline_statistics;

  if (FLAG_turbo_stats) {
    pipeline_statistics.Reset(new PipelineStatistics(info(), &zone_pool));
    pipeline_statistics->BeginPhaseKind("graph creation");
  }

  PipelineData data(info(), &zone_pool, pipeline_statistics.get());
  this->data_ = &data;

  if (FLAG_trace_turbo) {
    OFStream os(stdout);
    os << "---------------------------------------------------\n"
       << "Begin compiling method " << GetDebugName(info()).get()
       << " using Turbofan" << std::endl;
    TurboCfgFile tcf(isolate());
    tcf << AsC1VCompilation(info());
  }

  data.source_positions()->AddDecorator();

  Run<GraphBuilderPhase>();
  if (data.compilation_failed()) return Handle<Code>::null();
  RunPrintAndVerify("Initial untyped", true);

  Run<EarlyControlReductionPhase>();
  RunPrintAndVerify("Early Control reduced", true);

  if (info()->is_context_specializing()) {
    // Specialize the code to the context as aggressively as possible.
    Run<ContextSpecializerPhase>();
    RunPrintAndVerify("Context specialized", true);
  }

  if (info()->is_inlining_enabled()) {
    Run<InliningPhase>();
    RunPrintAndVerify("Inlined", true);
  }

  if (FLAG_print_turbo_replay) {
    // Print a replay of the initial graph.
    GraphReplayPrinter::PrintReplay(data.graph());
  }

  // Bailout here in case target architecture is not supported.
  if (!SupportedTarget()) return Handle<Code>::null();

  if (info()->is_typing_enabled()) {
    // Type the graph.
    Run<TyperPhase>();
    RunPrintAndVerify("Typed");
  }

  if (!pipeline_statistics.is_empty()) {
    data.pipeline_statistics()->BeginPhaseKind("lowering");
  }

  if (info()->is_typing_enabled()) {
    // Lower JSOperators where we can determine types.
    Run<TypedLoweringPhase>();
    RunPrintAndVerify("Lowered typed");

    // Lower simplified operators and insert changes.
    Run<SimplifiedLoweringPhase>();
    RunPrintAndVerify("Lowered simplified");

    // Lower changes that have been inserted before.
    Run<ChangeLoweringPhase>();
    // // TODO(jarin, rossberg): Remove UNTYPED once machine typing works.
    RunPrintAndVerify("Lowered changes", true);

    Run<LateControlReductionPhase>();
    RunPrintAndVerify("Late Control reduced");
  }

  // Lower any remaining generic JSOperators.
  Run<GenericLoweringPhase>();
  // TODO(jarin, rossberg): Remove UNTYPED once machine typing works.
  RunPrintAndVerify("Lowered generic", true);

  if (!pipeline_statistics.is_empty()) {
    data.pipeline_statistics()->BeginPhaseKind("block building");
  }

  data.source_positions()->RemoveDecorator();

  // Compute a schedule.
  Run<ComputeSchedulePhase>();

  {
    // Generate optimized code.
    Linkage linkage(data.instruction_zone(), info());
    GenerateCode(&linkage);
  }
  Handle<Code> code = data.code();
  info()->SetCode(code);

  // Print optimized code.
  v8::internal::CodeGenerator::PrintCode(code, info());

  if (FLAG_trace_turbo) {
    OFStream os(stdout);
    os << "--------------------------------------------------\n"
       << "Finished compiling method " << GetDebugName(info()).get()
       << " using Turbofan" << std::endl;
  }

  return code;
}


Handle<Code> Pipeline::GenerateCodeForMachineGraph(Linkage* linkage,
                                                   Graph* graph,
                                                   Schedule* schedule) {
  ZonePool zone_pool(isolate());
  CHECK(SupportedBackend());
  PipelineData data(graph, schedule, &zone_pool);
  this->data_ = &data;
  if (schedule == NULL) {
    // TODO(rossberg): Should this really be untyped?
    RunPrintAndVerify("Machine", true);
    Run<ComputeSchedulePhase>();
  } else {
    TraceSchedule(schedule);
  }

  GenerateCode(linkage);
  Handle<Code> code = data.code();

#if ENABLE_DISASSEMBLER
  if (!code.is_null() && FLAG_print_opt_code) {
    CodeTracer::Scope tracing_scope(isolate()->GetCodeTracer());
    OFStream os(tracing_scope.file());
    code->Disassemble("test code", os);
  }
#endif
  return code;
}


void Pipeline::GenerateCode(Linkage* linkage) {
  PipelineData* data = this->data_;

  DCHECK_NOT_NULL(linkage);
  DCHECK_NOT_NULL(data->graph());
  DCHECK_NOT_NULL(data->schedule());
  CHECK(SupportedBackend());

  BasicBlockProfiler::Data* profiler_data = NULL;
  if (FLAG_turbo_profiling) {
    profiler_data = BasicBlockInstrumentor::Instrument(info(), data->graph(),
                                                       data->schedule());
  }

  InstructionBlocks* instruction_blocks =
      InstructionSequence::InstructionBlocksFor(data->instruction_zone(),
                                                data->schedule());
  data->set_sequence(new (data->instruction_zone()) InstructionSequence(
      data->instruction_zone(), instruction_blocks));

  // Select and schedule instructions covering the scheduled graph.
  Run<InstructionSelectionPhase>(linkage);

  if (FLAG_trace_turbo && !data->MayHaveUnverifiableGraph()) {
    TurboCfgFile tcf(isolate());
    tcf << AsC1V("CodeGen", data->schedule(), data->source_positions(),
                 data->sequence());
  }

  data->DeleteGraphZone();

  if (data->pipeline_statistics() != NULL) {
    data->pipeline_statistics()->BeginPhaseKind("register allocation");
  }

#ifdef DEBUG
  // Don't track usage for this zone in compiler stats.
  Zone verifier_zone(info()->isolate());
  RegisterAllocatorVerifier verifier(
      &verifier_zone, RegisterConfiguration::ArchDefault(), data->sequence());
#endif

  // Allocate registers.
  data->set_frame(new (data->instruction_zone()) Frame);
  Run<RegisterAllocationPhase>();
  if (data->compilation_failed()) {
    info()->AbortOptimization(kNotEnoughVirtualRegistersRegalloc);
    return;
  }

#ifdef DEBUG
  verifier.VerifyAssignment();
  verifier.VerifyGapMoves();
#endif

  if (data->pipeline_statistics() != NULL) {
    data->pipeline_statistics()->BeginPhaseKind("code generation");
  }

  // Generate native sequence.
  Run<GenerateCodePhase>(linkage, info());

  if (profiler_data != NULL) {
#if ENABLE_DISASSEMBLER
    std::ostringstream os;
    data->code()->Disassemble(NULL, os);
    profiler_data->SetCode(&os);
#endif
  }
}


void Pipeline::SetUp() {
  InstructionOperand::SetUpCaches();
}


void Pipeline::TearDown() {
  InstructionOperand::TearDownCaches();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
