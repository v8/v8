// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if !V8_ENABLE_WEBASSEMBLY
#error This header should only be included if WebAssembly is enabled.
#endif  // !V8_ENABLE_WEBASSEMBLY

#ifndef V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
#define V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_

#include <cstdint>
#include <ostream>

#include "src/base/functional.h"
#include "src/base/vector.h"
#include "src/compiler/linkage.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-linkage.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {

namespace wasm {
struct WasmModule;
class WireBytesStorage;
struct ModuleWireBytes;
}  // namespace wasm

namespace compiler {
class CallDescriptor;
class LinkageLocation;

// If {to} is nullable, it means that null passes the check.
// {from} may change in compiler optimization passes as the object's type gets
// narrowed.
// TODO(12166): Add modules if we have cross-module inlining.
struct WasmTypeCheckConfig {
  wasm::ValueType from;
  const wasm::ValueType to;
};

V8_INLINE std::ostream& operator<<(std::ostream& os,
                                   WasmTypeCheckConfig const& p) {
  return os << p.from.name() << " -> " << p.to.name();
}

V8_INLINE size_t hash_value(WasmTypeCheckConfig const& p) {
  return base::hash_combine(p.from.raw_bit_field(), p.to.raw_bit_field());
}

V8_INLINE bool operator==(const WasmTypeCheckConfig& p1,
                          const WasmTypeCheckConfig& p2) {
  return p1.from == p2.from && p1.to == p2.to;
}

static constexpr int kCharWidthBailoutSentinel = 3;

enum class NullCheckStrategy { kExplicit, kTrapHandler };

enum class EnforceBoundsCheck : bool {  // --
  kNeedsBoundsCheck = true,
  kCanOmitBoundsCheck = false
};

enum class BoundsCheckResult {
  // Dynamically checked (using 1-2 conditional branches).
  kDynamicallyChecked,
  // OOB handled via the trap handler.
  kTrapHandler,
  // Statically known to be in bounds.
  kInBounds
};

// Static knowledge about whether a wasm-gc operation, such as struct.get, needs
// a null check.
enum CheckForNull { kWithoutNullCheck, kWithNullCheck };

base::Vector<const char> GetDebugName(Zone* zone,
                                      const wasm::WasmModule* module,
                                      const wasm::WireBytesStorage* wire_bytes,
                                      int index);
enum WasmCallKind { kWasmFunction, kWasmImportWrapper, kWasmCapiFunction };

V8_EXPORT_PRIVATE CallDescriptor* GetWasmCallDescriptor(
    Zone* zone, const wasm::FunctionSig* signature,
    WasmCallKind kind = kWasmFunction, bool need_frame_state = false);

MachineRepresentation GetMachineRepresentation(wasm::ValueType type);

MachineRepresentation GetMachineRepresentation(MachineType type);

namespace {
// Helper for allocating either an GP or FP reg, or the next stack slot.
class LinkageLocationAllocator {
 public:
  template <size_t kNumGpRegs, size_t kNumFpRegs>
  constexpr LinkageLocationAllocator(const Register (&gp)[kNumGpRegs],
                                     const DoubleRegister (&fp)[kNumFpRegs],
                                     int slot_offset)
      : allocator_(wasm::LinkageAllocator(gp, fp)), slot_offset_(slot_offset) {}

  LinkageLocation Next(MachineRepresentation rep) {
    MachineType type = MachineType::TypeForRepresentation(rep);
    if (IsFloatingPoint(rep)) {
      if (allocator_.CanAllocateFP(rep)) {
        int reg_code = allocator_.NextFpReg(rep);
        return LinkageLocation::ForRegister(reg_code, type);
      }
    } else if (allocator_.CanAllocateGP()) {
      int reg_code = allocator_.NextGpReg();
      return LinkageLocation::ForRegister(reg_code, type);
    }
    // Cannot use register; use stack slot.
    int index = -1 - (slot_offset_ + allocator_.NextStackSlot(rep));
    return LinkageLocation::ForCallerFrameSlot(index, type);
  }

  int NumStackSlots() const { return allocator_.NumStackSlots(); }
  void EndSlotArea() { allocator_.EndSlotArea(); }

 private:
  wasm::LinkageAllocator allocator_;
  // Since params and returns are in different stack frames, we must allocate
  // them separately. Parameter slots don't need an offset, but return slots
  // must be offset to just before the param slots, using this |slot_offset_|.
  int slot_offset_;
};
}  // namespace

template <typename T>
LocationSignature* BuildLocations(Zone* zone, const Signature<T>* sig,
                                  bool extra_callable_param,
                                  int* parameter_slots, int* return_slots) {
  int extra_params = extra_callable_param ? 2 : 1;
  LocationSignature::Builder locations(zone, sig->return_count(),
                                       sig->parameter_count() + extra_params);

  // Add register and/or stack parameter(s).
  LinkageLocationAllocator params(
      wasm::kGpParamRegisters, wasm::kFpParamRegisters, 0 /* no slot offset */);

  // The instance object.
  locations.AddParam(params.Next(MachineRepresentation::kTaggedPointer));
  const size_t param_offset = 1;  // Actual params start here.

  // Parameters are separated into two groups (first all untagged, then all
  // tagged parameters). This allows for easy iteration of tagged parameters
  // during frame iteration.
  const size_t parameter_count = sig->parameter_count();
  bool has_tagged_param = false;
  for (size_t i = 0; i < parameter_count; i++) {
    MachineRepresentation param = GetMachineRepresentation(sig->GetParam(i));
    // Skip tagged parameters (e.g. any-ref).
    if (IsAnyTagged(param)) {
      has_tagged_param = true;
      continue;
    }
    auto l = params.Next(param);
    locations.AddParamAt(i + param_offset, l);
  }

  // End the untagged area, so tagged slots come after.
  params.EndSlotArea();

  if (has_tagged_param) {
    for (size_t i = 0; i < parameter_count; i++) {
      MachineRepresentation param = GetMachineRepresentation(sig->GetParam(i));
      // Skip untagged parameters.
      if (!IsAnyTagged(param)) continue;
      auto l = params.Next(param);
      locations.AddParamAt(i + param_offset, l);
    }
  }

  // Import call wrappers have an additional (implicit) parameter, the callable.
  // For consistency with JS, we use the JSFunction register.
  if (extra_callable_param) {
    locations.AddParam(LinkageLocation::ForRegister(
        kJSFunctionRegister.code(), MachineType::TaggedPointer()));
  }

  *parameter_slots = AddArgumentPaddingSlots(params.NumStackSlots());

  // Add return location(s).
  LinkageLocationAllocator rets(wasm::kGpReturnRegisters,
                                wasm::kFpReturnRegisters, *parameter_slots);

  const size_t return_count = locations.return_count_;
  for (size_t i = 0; i < return_count; i++) {
    MachineRepresentation ret = GetMachineRepresentation(sig->GetReturn(i));
    locations.AddReturn(rets.Next(ret));
  }

  *return_slots = rets.NumStackSlots();

  return locations.Get();
}
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_WASM_COMPILER_DEFINITIONS_H_
