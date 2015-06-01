// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CCTEST_COMPILER_CALL_TESTER_H_
#define V8_CCTEST_COMPILER_CALL_TESTER_H_

#include "src/v8.h"

#include "src/simulator.h"

#include "test/cctest/compiler/c-signature.h"

#if V8_TARGET_ARCH_IA32
#if __GNUC__
#define V8_CDECL __attribute__((cdecl))
#else
#define V8_CDECL __cdecl
#endif
#else
#define V8_CDECL
#endif

namespace v8 {
namespace internal {
namespace compiler {

template <typename R>
inline R CastReturnValue(uintptr_t r) {
  return reinterpret_cast<R>(r);
}

template <>
inline void CastReturnValue(uintptr_t r) {}

template <>
inline bool CastReturnValue(uintptr_t r) {
  return static_cast<bool>(r);
}

template <>
inline int32_t CastReturnValue(uintptr_t r) {
  return static_cast<int32_t>(r);
}

template <>
inline uint32_t CastReturnValue(uintptr_t r) {
  return static_cast<uint32_t>(r);
}

template <>
inline int64_t CastReturnValue(uintptr_t r) {
  return static_cast<int64_t>(r);
}

template <>
inline uint64_t CastReturnValue(uintptr_t r) {
  return static_cast<uint64_t>(r);
}

template <>
inline int16_t CastReturnValue(uintptr_t r) {
  return static_cast<int16_t>(r);
}

template <>
inline uint16_t CastReturnValue(uintptr_t r) {
  return static_cast<uint16_t>(r);
}

template <>
inline int8_t CastReturnValue(uintptr_t r) {
  return static_cast<int8_t>(r);
}

template <>
inline uint8_t CastReturnValue(uintptr_t r) {
  return static_cast<uint8_t>(r);
}

template <>
inline double CastReturnValue(uintptr_t r) {
  UNREACHABLE();
  return 0.0;
}

template <typename R>
struct ParameterTraits {
  static uintptr_t Cast(R r) { return static_cast<uintptr_t>(r); }
};

template <>
struct ParameterTraits<int*> {
  static uintptr_t Cast(int* r) { return reinterpret_cast<uintptr_t>(r); }
};

template <typename T>
struct ParameterTraits<T*> {
  static uintptr_t Cast(void* r) { return reinterpret_cast<uintptr_t>(r); }
};


#if !V8_TARGET_ARCH_32_BIT

// Additional template specialization required for mips64 to sign-extend
// parameters defined by calling convention.
template <>
struct ParameterTraits<int32_t> {
  static int64_t Cast(int32_t r) { return static_cast<int64_t>(r); }
};

template <>
struct ParameterTraits<uint32_t> {
  static int64_t Cast(uint32_t r) {
    return static_cast<int64_t>(static_cast<int32_t>(r));
  }
};

#endif  // !V8_TARGET_ARCH_64_BIT


template <typename R>
class CallHelper {
 public:
  explicit CallHelper(Isolate* isolate, MachineSignature* machine_sig)
      : machine_sig_(machine_sig), isolate_(isolate) {
    USE(isolate_);
  }
  virtual ~CallHelper() {}

  R Call() {
    typedef R V8_CDECL FType();
    VerifyParameters0();
    return DoCall(FUNCTION_CAST<FType*>(Generate()));
  }

  template <typename P1>
  R Call(P1 p1) {
    typedef R V8_CDECL FType(P1);
    VerifyParameters1<P1>();
    return DoCall(FUNCTION_CAST<FType*>(Generate()), p1);
  }

  template <typename P1, typename P2>
  R Call(P1 p1, P2 p2) {
    typedef R V8_CDECL FType(P1, P2);
    VerifyParameters2<P1, P2>();
    return DoCall(FUNCTION_CAST<FType*>(Generate()), p1, p2);
  }

  template <typename P1, typename P2, typename P3>
  R Call(P1 p1, P2 p2, P3 p3) {
    typedef R V8_CDECL FType(P1, P2, P3);
    VerifyParameters3<P1, P2, P3>();
    return DoCall(FUNCTION_CAST<FType*>(Generate()), p1, p2, p3);
  }

  template <typename P1, typename P2, typename P3, typename P4>
  R Call(P1 p1, P2 p2, P3 p3, P4 p4) {
    typedef R V8_CDECL FType(P1, P2, P3, P4);
    VerifyParameters4<P1, P2, P3, P4>();
    return DoCall(FUNCTION_CAST<FType*>(Generate()), p1, p2, p3, p4);
  }

 protected:
  MachineSignature* machine_sig_;

  void VerifyParameters(size_t parameter_count, MachineType* parameter_types) {
    CHECK(machine_sig_->parameter_count() == parameter_count);
    for (size_t i = 0; i < parameter_count; i++) {
      CHECK_EQ(machine_sig_->GetParam(i), parameter_types[i]);
    }
  }

  virtual byte* Generate() = 0;

 private:
#if USE_SIMULATOR && V8_TARGET_ARCH_ARM64
  uintptr_t CallSimulator(byte* f, Simulator::CallArgument* args) {
    Simulator* simulator = Simulator::current(isolate_);
    return static_cast<uintptr_t>(simulator->CallInt64(f, args));
  }

  template <typename F>
  R DoCall(F* f) {
    Simulator::CallArgument args[] = {Simulator::CallArgument::End()};
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f), args));
  }
  template <typename F, typename P1>
  R DoCall(F* f, P1 p1) {
    Simulator::CallArgument args[] = {Simulator::CallArgument(p1),
                                      Simulator::CallArgument::End()};
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f), args));
  }
  template <typename F, typename P1, typename P2>
  R DoCall(F* f, P1 p1, P2 p2) {
    Simulator::CallArgument args[] = {Simulator::CallArgument(p1),
                                      Simulator::CallArgument(p2),
                                      Simulator::CallArgument::End()};
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f), args));
  }
  template <typename F, typename P1, typename P2, typename P3>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3) {
    Simulator::CallArgument args[] = {
        Simulator::CallArgument(p1), Simulator::CallArgument(p2),
        Simulator::CallArgument(p3), Simulator::CallArgument::End()};
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f), args));
  }
  template <typename F, typename P1, typename P2, typename P3, typename P4>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3, P4 p4) {
    Simulator::CallArgument args[] = {
        Simulator::CallArgument(p1), Simulator::CallArgument(p2),
        Simulator::CallArgument(p3), Simulator::CallArgument(p4),
        Simulator::CallArgument::End()};
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f), args));
  }
#elif USE_SIMULATOR && (V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_PPC64)
  uintptr_t CallSimulator(byte* f, int64_t p1 = 0, int64_t p2 = 0,
                          int64_t p3 = 0, int64_t p4 = 0) {
    Simulator* simulator = Simulator::current(isolate_);
    return static_cast<uintptr_t>(simulator->Call(f, 4, p1, p2, p3, p4));
  }


  template <typename F>
  R DoCall(F* f) {
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f)));
  }
  template <typename F, typename P1>
  R DoCall(F* f, P1 p1) {
    return CastReturnValue<R>(
        CallSimulator(FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1)));
  }
  template <typename F, typename P1, typename P2>
  R DoCall(F* f, P1 p1, P2 p2) {
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f),
                                            ParameterTraits<P1>::Cast(p1),
                                            ParameterTraits<P2>::Cast(p2)));
  }
  template <typename F, typename P1, typename P2, typename P3>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3) {
    return CastReturnValue<R>(CallSimulator(
        FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1),
        ParameterTraits<P2>::Cast(p2), ParameterTraits<P3>::Cast(p3)));
  }
  template <typename F, typename P1, typename P2, typename P3, typename P4>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3, P4 p4) {
    return CastReturnValue<R>(CallSimulator(
        FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1),
        ParameterTraits<P2>::Cast(p2), ParameterTraits<P3>::Cast(p3),
        ParameterTraits<P4>::Cast(p4)));
  }
#elif USE_SIMULATOR && \
    (V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_MIPS || V8_TARGET_ARCH_PPC)
  uintptr_t CallSimulator(byte* f, int32_t p1 = 0, int32_t p2 = 0,
                          int32_t p3 = 0, int32_t p4 = 0) {
    Simulator* simulator = Simulator::current(isolate_);
    return static_cast<uintptr_t>(simulator->Call(f, 4, p1, p2, p3, p4));
  }
  template <typename F>
  R DoCall(F* f) {
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f)));
  }
  template <typename F, typename P1>
  R DoCall(F* f, P1 p1) {
    return CastReturnValue<R>(
        CallSimulator(FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1)));
  }
  template <typename F, typename P1, typename P2>
  R DoCall(F* f, P1 p1, P2 p2) {
    return CastReturnValue<R>(CallSimulator(FUNCTION_ADDR(f),
                                            ParameterTraits<P1>::Cast(p1),
                                            ParameterTraits<P2>::Cast(p2)));
  }
  template <typename F, typename P1, typename P2, typename P3>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3) {
    return CastReturnValue<R>(CallSimulator(
        FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1),
        ParameterTraits<P2>::Cast(p2), ParameterTraits<P3>::Cast(p3)));
  }
  template <typename F, typename P1, typename P2, typename P3, typename P4>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3, P4 p4) {
    return CastReturnValue<R>(CallSimulator(
        FUNCTION_ADDR(f), ParameterTraits<P1>::Cast(p1),
        ParameterTraits<P2>::Cast(p2), ParameterTraits<P3>::Cast(p3),
        ParameterTraits<P4>::Cast(p4)));
  }
#else
  template <typename F>
  R DoCall(F* f) {
    return f();
  }
  template <typename F, typename P1>
  R DoCall(F* f, P1 p1) {
    return f(p1);
  }
  template <typename F, typename P1, typename P2>
  R DoCall(F* f, P1 p1, P2 p2) {
    return f(p1, p2);
  }
  template <typename F, typename P1, typename P2, typename P3>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3) {
    return f(p1, p2, p3);
  }
  template <typename F, typename P1, typename P2, typename P3, typename P4>
  R DoCall(F* f, P1 p1, P2 p2, P3 p3, P4 p4) {
    return f(p1, p2, p3, p4);
  }
#endif

#ifndef DEBUG
  void VerifyParameters0() {}

  template <typename P1>
  void VerifyParameters1() {}

  template <typename P1, typename P2>
  void VerifyParameters2() {}

  template <typename P1, typename P2, typename P3>
  void VerifyParameters3() {}

  template <typename P1, typename P2, typename P3, typename P4>
  void VerifyParameters4() {}
#else
  void VerifyParameters0() { VerifyParameters(0, NULL); }

  template <typename P1>
  void VerifyParameters1() {
    MachineType parameters[] = {MachineTypeForC<P1>()};
    VerifyParameters(arraysize(parameters), parameters);
  }

  template <typename P1, typename P2>
  void VerifyParameters2() {
    MachineType parameters[] = {MachineTypeForC<P1>(), MachineTypeForC<P2>()};
    VerifyParameters(arraysize(parameters), parameters);
  }

  template <typename P1, typename P2, typename P3>
  void VerifyParameters3() {
    MachineType parameters[] = {MachineTypeForC<P1>(), MachineTypeForC<P2>(),
                                MachineTypeForC<P3>()};
    VerifyParameters(arraysize(parameters), parameters);
  }

  template <typename P1, typename P2, typename P3, typename P4>
  void VerifyParameters4() {
    MachineType parameters[] = {MachineTypeForC<P1>(), MachineTypeForC<P2>(),
                                MachineTypeForC<P3>(), MachineTypeForC<P4>()};
    VerifyParameters(arraysize(parameters), parameters);
  }
#endif

  Isolate* isolate_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_CCTEST_COMPILER_CALL_TESTER_H_
