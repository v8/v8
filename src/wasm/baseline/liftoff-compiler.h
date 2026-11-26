// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
#define V8_WASM_BASELINE_LIFTOFF_COMPILER_H_

#include <memory>
#include <optional>

#include "src/base/small-vector.h"
#include "src/base/unique-array.h"
#include "src/wasm/baseline/liftoff-bailout-reasons.h"
#include "src/wasm/function-compiler.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

class AccountingAllocator;
class Counters;

namespace wasm {

struct CompilationEnv;
class DebugSideTable;
struct FunctionBody;
class WasmDetectedFeatures;

// Further information about a location for a deopt: A call_ref can either be
// just an inline call (that didn't cause a deopt) with a deopt happening within
// the inlinee or it could be the deopt point itself. This changes whether the
// relevant stackstate is the one before the call or after the call.
enum class LocationKindForDeopt : uint8_t {
  kNone,
  kEagerDeopt,   // The location is the point of an eager deopt.
  kInlinedCall,  // The loation is an inlined call, not a deopt.
};

// State that is reused across the Liftoff compilation of several functions, to
// avoid paying per-function allocation cost for the compilation zone and the
// assembler buffer. A context must only be used by a single thread, and must
// outlive all {WasmCompilationResult}s produced with it, until their code has
// been copied out (see {ReleaseCode}).
class LiftoffCompilationContext {
 public:
  LiftoffCompilationContext();
  ~LiftoffCompilationContext();
  LiftoffCompilationContext(const LiftoffCompilationContext&) = delete;
  LiftoffCompilationContext& operator=(const LiftoffCompilationContext&) =
      delete;

  // Returns the (empty) zone to use for the next function.
  Zone* zone();

  // Frees everything allocated in the zone, keeping at most
  // {kMaxRetainedZoneSize} for reuse.
  void ResetZone();

  // Returns an assembler buffer located in the code arena of this context,
  // with at least {min_size} bytes.
  std::unique_ptr<AssemblerBuffer> NewAssemblerBuffer(int min_size);

  // Called after code generation finished: Moves the relocation info right
  // behind the instructions, updates {desc} accordingly, retains any
  // standalone grown {buffer} until {ReleaseCode}, and marks that memory as
  // used until the next call to {ReleaseCode}.
  void CommitCode(std::unique_ptr<AssemblerBuffer> buffer, CodeDesc* desc);

  // Invalidates all code committed so far. To be called once the code of all
  // compilation results produced with this context has been copied out.
  void ReleaseCode();

 private:
  class ArenaAssemblerBuffer;

  // Allocates a new chunk with at least {min_size} bytes and makes it the
  // current (last) chunk.
  void AddChunk(size_t min_size);

  static constexpr size_t kChunkSize = 64 * KB;
  // Chunks bigger than this are not kept alive in {ReleaseCode}.
  static constexpr size_t kMaxRetainedChunkSize = 256 * KB;
  // Zones with more segment memory than this are not reused. Keep at least
  // 512 KB to accommodate Managed Zone Memory's minimum page size on 64-bit.
  static constexpr size_t kMaxRetainedZoneSize = 512 * KB;

  std::optional<Zone> zone_;
  base::SmallVector<base::UniqueArray<uint8_t>, 2> chunks_;
  // Offset in the last chunk behind the last committed code.
  size_t committed_pos_ = 0;
};

struct LiftoffOptions {
  // func_index is non-optional. Make it const with no default value to force
  // assigning a value to this field on aggregate initialization.
  const int func_index;

  ForDebugging for_debugging = kNotForDebugging;
  DelayedCounterUpdates* counter_updates = nullptr;
  WasmDetectedFeatures* detected_features = nullptr;
  // Optional context for reusing memory across functions. If set, the code of
  // the compilation result lives in that context.
  LiftoffCompilationContext* context = nullptr;
  base::Vector<const int> breakpoints = {};
  base::Vector<const int> dead_breakpoints = {};
  std::unique_ptr<DebugSideTable>* debug_sidetable = nullptr;
  int32_t* max_steps = nullptr;
  uint32_t deopt_info_bytecode_offset = std::numeric_limits<uint32_t>::max();
  LocationKindForDeopt deopt_location_kind = LocationKindForDeopt::kNone;
};

V8_EXPORT_PRIVATE WasmCompilationResult ExecuteLiftoffCompilation(
    CompilationEnv*, const FunctionBody&, const LiftoffOptions&);

V8_EXPORT_PRIVATE std::unique_ptr<DebugSideTable> GenerateLiftoffDebugSideTable(
    const WasmCode*);

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_COMPILER_H_
