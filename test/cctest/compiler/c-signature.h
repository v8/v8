// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_C_SIGNATURE_H_
#define V8_COMPILER_C_SIGNATURE_H_

#include "src/compiler/machine-type.h"

namespace v8 {
namespace internal {
namespace compiler {

#define FOREACH_CTYPE_MACHINE_TYPE_MAPPING(V) \
  V(void, kMachNone)                          \
  V(bool, kMachBool)                          \
  V(int8_t, kMachInt8)                        \
  V(uint8_t, kMachUint8)                      \
  V(int16_t, kMachInt16)                      \
  V(uint16_t, kMachUint16)                    \
  V(int32_t, kMachInt32)                      \
  V(uint32_t, kMachUint32)                    \
  V(int64_t, kMachInt64)                      \
  V(uint64_t, kMachUint64)                    \
  V(float, kMachFloat32)                      \
  V(double, kMachFloat64)                     \
  V(void*, kMachPtr)                          \
  V(int*, kMachPtr)

template <typename T>
inline MachineType MachineTypeForC() {
  while (false) {
    // All other types T must be assignable to Object*
    *(static_cast<Object* volatile*>(0)) = static_cast<T>(0);
  }
  return kMachAnyTagged;
}

#define DECLARE_TEMPLATE_SPECIALIZATION(ctype, mtype) \
  template <>                                         \
  inline MachineType MachineTypeForC<ctype>() {       \
    return mtype;                                     \
  }
FOREACH_CTYPE_MACHINE_TYPE_MAPPING(DECLARE_TEMPLATE_SPECIALIZATION)
#undef DECLARE_TEMPLATE_SPECIALIZATION

// Helper for building machine signatures from C types.
class CSignature : public MachineSignature {
 protected:
  CSignature(size_t return_count, size_t parameter_count, MachineType* reps)
      : MachineSignature(return_count, parameter_count, reps) {}

 public:
  template <typename P1 = void, typename P2 = void, typename P3 = void,
            typename P4 = void, typename P5 = void>
  void Verify() {
    // Verifies the C signature against the machine types. Maximum {5} params.
    CHECK_LT(parameter_count(), 6);
    const int kMax = 5;
    MachineType params[] = {MachineTypeForC<P1>(), MachineTypeForC<P2>(),
                            MachineTypeForC<P3>(), MachineTypeForC<P4>(),
                            MachineTypeForC<P5>()};
    for (int p = kMax - 1; p >= 0; p--) {
      if (p < parameter_count()) {
        CHECK_EQ(GetParam(p), params[p]);
      } else {
        CHECK_EQ(kMachNone, params[p]);
      }
    }
  }
};


template <typename Ret, uint16_t kParamCount>
class CSignatureOf : public CSignature {
 protected:
  MachineType storage_[1 + kParamCount];

  CSignatureOf()
      : CSignature(MachineTypeForC<Ret>() != kMachNone ? 1 : 0, kParamCount,
                   reinterpret_cast<MachineType*>(&storage_)) {
    if (return_count_ == 1) storage_[0] = MachineTypeForC<Ret>();
  }
  void Set(int index, MachineType type) {
    DCHECK(index >= 0 && index < kParamCount);
    reps_[return_count_ + index] = type;
  }
};

// Helper classes for instantiating Signature objects to be callable from C.
template <typename Ret>
class CSignature0 : public CSignatureOf<Ret, 0> {
 public:
  CSignature0() : CSignatureOf<Ret, 0>() {}
};

template <typename Ret, typename P1>
class CSignature1 : public CSignatureOf<Ret, 1> {
 public:
  CSignature1() : CSignatureOf<Ret, 1>() {
    this->Set(0, MachineTypeForC<P1>());
  }
};

template <typename Ret, typename P1, typename P2>
class CSignature2 : public CSignatureOf<Ret, 2> {
 public:
  CSignature2() : CSignatureOf<Ret, 2>() {
    this->Set(0, MachineTypeForC<P1>());
    this->Set(1, MachineTypeForC<P2>());
  }
};

template <typename Ret, typename P1, typename P2, typename P3>
class CSignature3 : public CSignatureOf<Ret, 3> {
 public:
  CSignature3() : CSignatureOf<Ret, 3>() {
    this->Set(0, MachineTypeForC<P1>());
    this->Set(1, MachineTypeForC<P2>());
    this->Set(2, MachineTypeForC<P3>());
  }
};

typedef CSignature2<int32_t, int32_t, int32_t> CSignature_i_ii;
typedef CSignature2<uint32_t, uint32_t, uint32_t> CSignature_u_uu;
typedef CSignature2<float, float, float> CSignature_f_ff;
typedef CSignature2<double, double, double> CSignature_d_dd;
typedef CSignature2<Object*, Object*, Object*> CSignature_o_oo;
}
}
}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_C_SIGNATURE_H_
