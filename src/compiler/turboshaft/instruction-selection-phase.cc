// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/instruction-selection-phase.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/graph-visualizer.h"
#include "src/compiler/pipeline.h"

namespace v8::internal::compiler::turboshaft {

base::Optional<BailoutReason> InstructionSelectionPhase::Run(Zone* temp_zone,
                                                             Linkage* linkage) {
  turboshaft::PipelineData* data = &turboshaft::PipelineData::Get();
  turboshaft::Graph& graph = turboshaft::PipelineData::Get().graph();

  InstructionSelector selector = InstructionSelector::ForTurboshaft(
      temp_zone, graph.op_id_count(), linkage, data->sequence(), &graph,
      data->source_positions(), data->frame(),
      data->info()->switch_jump_table()
          ? InstructionSelector::kEnableSwitchJumpTable
          : InstructionSelector::kDisableSwitchJumpTable,
      &data->info()->tick_counter(), data->broker(),
      data->address_of_max_unoptimized_frame_height(),
      data->address_of_max_pushed_argument_count(),
      data->info()->source_positions()
          ? InstructionSelector::kAllSourcePositions
          : InstructionSelector::kCallSourcePositions,
      InstructionSelector::SupportedFeatures(),
      v8_flags.turbo_instruction_scheduling
          ? InstructionSelector::kEnableScheduling
          : InstructionSelector::kDisableScheduling,
      data->assembler_options().enable_root_relative_access
          ? InstructionSelector::kEnableRootsRelativeAddressing
          : InstructionSelector::kDisableRootsRelativeAddressing,
      data->info()->trace_turbo_json()
          ? InstructionSelector::kEnableTraceTurboJson
          : InstructionSelector::kDisableTraceTurboJson);
  if (base::Optional<BailoutReason> bailout = selector.SelectInstructions()) {
    return bailout;
  }
  if (data->info()->trace_turbo_json()) {
    TurboJsonFile json_of(data->info(), std::ios_base::app);
    json_of << "{\"name\":\"" << phase_name() << "\",\"type\":\"instructions\""
            << InstructionRangesAsJSON{data->sequence(),
                                       &selector.instr_origins()}
            << "},\n";
  }
  return base::nullopt;
}

}  // namespace v8::internal::compiler::turboshaft
