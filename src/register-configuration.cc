// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/register-configuration.h"
#include "src/globals.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

namespace {

#define REGISTER_COUNT(R) 1 +
static const int kMaxAllocatableGeneralRegisterCount =
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_COUNT)0;
static const int kMaxAllocatableDoubleRegisterCount =
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_COUNT)0;

static const int kAllocatableGeneralCodes[] = {
#define REGISTER_CODE(R) Register::kCode_##R,
    ALLOCATABLE_GENERAL_REGISTERS(REGISTER_CODE)};
#undef REGISTER_CODE

static const int kAllocatableDoubleCodes[] = {
#define REGISTER_CODE(R) DoubleRegister::kCode_##R,
    ALLOCATABLE_DOUBLE_REGISTERS(REGISTER_CODE)};
#undef REGISTER_CODE

static const char* const kGeneralRegisterNames[] = {
#define REGISTER_NAME(R) #R,
    GENERAL_REGISTERS(REGISTER_NAME)
#undef REGISTER_NAME
};

static const char* const kFloatRegisterNames[] = {
#define REGISTER_NAME(R) #R,
    FLOAT_REGISTERS(REGISTER_NAME)
#undef REGISTER_NAME
};

static const char* const kDoubleRegisterNames[] = {
#define REGISTER_NAME(R) #R,
    DOUBLE_REGISTERS(REGISTER_NAME)
#undef REGISTER_NAME
};

STATIC_ASSERT(RegisterConfiguration::kMaxGeneralRegisters >=
              Register::kNumRegisters);
STATIC_ASSERT(RegisterConfiguration::kMaxFPRegisters >=
              DoubleRegister::kMaxNumRegisters);

enum CompilerSelector { CRANKSHAFT, TURBOFAN };

class ArchDefaultRegisterConfiguration : public RegisterConfiguration {
 public:
  explicit ArchDefaultRegisterConfiguration(CompilerSelector compiler)
      : RegisterConfiguration(
            Register::kNumRegisters, DoubleRegister::kMaxNumRegisters,
#if V8_TARGET_ARCH_IA32
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_X87
            kMaxAllocatableGeneralRegisterCount,
            compiler == TURBOFAN ? 1 : kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_X64
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_ARM
            FLAG_enable_embedded_constant_pool
                ? (kMaxAllocatableGeneralRegisterCount - 1)
                : kMaxAllocatableGeneralRegisterCount,
            CpuFeatures::IsSupported(VFP32DREGS)
                ? kMaxAllocatableDoubleRegisterCount
                : (ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(REGISTER_COUNT) 0),
#elif V8_TARGET_ARCH_ARM64
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_MIPS
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_MIPS64
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_PPC
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#elif V8_TARGET_ARCH_S390
            kMaxAllocatableGeneralRegisterCount,
            kMaxAllocatableDoubleRegisterCount,
#else
#error Unsupported target architecture.
#endif
            kAllocatableGeneralCodes, kAllocatableDoubleCodes,
            kSimpleFPAliasing ? AliasingKind::OVERLAP : AliasingKind::COMBINE,
            kGeneralRegisterNames, kFloatRegisterNames, kDoubleRegisterNames) {
  }
};

template <CompilerSelector compiler>
struct RegisterConfigurationInitializer {
  static void Construct(ArchDefaultRegisterConfiguration* config) {
    new (config) ArchDefaultRegisterConfiguration(compiler);
  }
};

static base::LazyInstance<ArchDefaultRegisterConfiguration,
                          RegisterConfigurationInitializer<CRANKSHAFT>>::type
    kDefaultRegisterConfigurationForCrankshaft = LAZY_INSTANCE_INITIALIZER;

static base::LazyInstance<ArchDefaultRegisterConfiguration,
                          RegisterConfigurationInitializer<TURBOFAN>>::type
    kDefaultRegisterConfigurationForTurboFan = LAZY_INSTANCE_INITIALIZER;

}  // namespace

const RegisterConfiguration* RegisterConfiguration::Crankshaft() {
  return &kDefaultRegisterConfigurationForCrankshaft.Get();
}

const RegisterConfiguration* RegisterConfiguration::Turbofan() {
  return &kDefaultRegisterConfigurationForTurboFan.Get();
}

RegisterConfiguration::RegisterConfiguration(
    int num_general_registers, int num_double_registers,
    int num_allocatable_general_registers, int num_allocatable_double_registers,
    const int* allocatable_general_codes, const int* allocatable_double_codes,
    AliasingKind fp_aliasing_kind, const char* const* general_register_names,
    const char* const* float_register_names,
    const char* const* double_register_names)
    : num_general_registers_(num_general_registers),
      num_float_registers_(0),
      num_double_registers_(num_double_registers),
      num_allocatable_general_registers_(num_allocatable_general_registers),
      num_allocatable_double_registers_(num_allocatable_double_registers),
      num_allocatable_float_registers_(0),
      allocatable_general_codes_mask_(0),
      allocatable_double_codes_mask_(0),
      allocatable_float_codes_mask_(0),
      allocatable_general_codes_(allocatable_general_codes),
      allocatable_double_codes_(allocatable_double_codes),
      fp_aliasing_kind_(fp_aliasing_kind),
      general_register_names_(general_register_names),
      float_register_names_(float_register_names),
      double_register_names_(double_register_names) {
  DCHECK(num_general_registers_ <= RegisterConfiguration::kMaxGeneralRegisters);
  DCHECK(num_double_registers_ <= RegisterConfiguration::kMaxFPRegisters);
  for (int i = 0; i < num_allocatable_general_registers_; ++i) {
    allocatable_general_codes_mask_ |= (1 << allocatable_general_codes_[i]);
  }
  for (int i = 0; i < num_allocatable_double_registers_; ++i) {
    allocatable_double_codes_mask_ |= (1 << allocatable_double_codes_[i]);
  }

  if (fp_aliasing_kind_ == COMBINE) {
    num_float_registers_ = num_double_registers_ * 2 <= kMaxFPRegisters
                               ? num_double_registers_ * 2
                               : kMaxFPRegisters;
    num_allocatable_float_registers_ = 0;
    for (int i = 0; i < num_allocatable_double_registers_; i++) {
      int base_code = allocatable_double_codes_[i] * 2;
      if (base_code >= kMaxFPRegisters) continue;
      allocatable_float_codes_[num_allocatable_float_registers_++] = base_code;
      allocatable_float_codes_[num_allocatable_float_registers_++] =
          base_code + 1;
      allocatable_float_codes_mask_ |= (0x3 << base_code);
    }
  } else {
    DCHECK(fp_aliasing_kind_ == OVERLAP);
    num_float_registers_ = num_double_registers_;
    num_allocatable_float_registers_ = num_allocatable_double_registers_;
    for (int i = 0; i < num_allocatable_float_registers_; ++i) {
      allocatable_float_codes_[i] = allocatable_double_codes_[i];
    }
    allocatable_float_codes_mask_ = allocatable_double_codes_mask_;
  }
}

int RegisterConfiguration::GetAliases(MachineRepresentation rep, int index,
                                      MachineRepresentation other_rep,
                                      int* alias_base_index) const {
  DCHECK(fp_aliasing_kind_ == COMBINE);
  DCHECK(rep == MachineRepresentation::kFloat32 ||
         rep == MachineRepresentation::kFloat64);
  DCHECK(other_rep == MachineRepresentation::kFloat32 ||
         other_rep == MachineRepresentation::kFloat64);
  if (rep == other_rep) {
    *alias_base_index = index;
    return 1;
  }
  if (rep == MachineRepresentation::kFloat32) {
    DCHECK(other_rep == MachineRepresentation::kFloat64);
    DCHECK(index < num_allocatable_float_registers_);
    *alias_base_index = index / 2;
    return 1;
  }
  DCHECK(rep == MachineRepresentation::kFloat64);
  DCHECK(other_rep == MachineRepresentation::kFloat32);
  if (index * 2 >= kMaxFPRegisters) {
    // Alias indices are out of float register range.
    return 0;
  }
  *alias_base_index = index * 2;
  return 2;
}

bool RegisterConfiguration::AreAliases(MachineRepresentation rep, int index,
                                       MachineRepresentation other_rep,
                                       int other_index) const {
  DCHECK(fp_aliasing_kind_ == COMBINE);
  DCHECK(rep == MachineRepresentation::kFloat32 ||
         rep == MachineRepresentation::kFloat64);
  DCHECK(other_rep == MachineRepresentation::kFloat32 ||
         other_rep == MachineRepresentation::kFloat64);
  if (rep == other_rep) {
    return index == other_index;
  }
  if (rep == MachineRepresentation::kFloat32) {
    DCHECK(other_rep == MachineRepresentation::kFloat64);
    return index / 2 == other_index;
  }
  DCHECK(rep == MachineRepresentation::kFloat64);
  DCHECK(other_rep == MachineRepresentation::kFloat32);
  if (index * 2 >= kMaxFPRegisters) {
    // Alias indices are out of float register range.
    return false;
  }
  return index == other_index / 2;
}

#undef REGISTER_COUNT

}  // namespace internal
}  // namespace v8
