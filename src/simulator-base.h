// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SIMULATOR_BASE_H_
#define V8_SIMULATOR_BASE_H_

#include "src/assembler.h"
#include "src/globals.h"

#if defined(USE_SIMULATOR)

namespace v8 {
namespace internal {

class Instruction;
class Redirection;

class SimulatorBase {
 public:
  // Call on process start and exit.
  static void InitializeOncePerProcess();
  static void GlobalTearDown();

  // Call on isolate initialization and teardown.
  static void Initialize(Isolate* isolate);
  // TODO(mstarzinger): Move implementation to "simulator-base.cc" file.
  static void TearDown(base::CustomMatcherHashMap* i_cache);

  static base::Mutex* redirection_mutex() { return redirection_mutex_; }
  static Redirection* redirection() { return redirection_; }
  static void set_redirection(Redirection* r) { redirection_ = r; }

 private:
  // Runtime call support. Uses the isolate in a thread-safe way.
  static void* RedirectExternalReference(Isolate* isolate,
                                         void* external_function,
                                         ExternalReference::Type type);

  static base::Mutex* redirection_mutex_;
  static Redirection* redirection_;
};

// When the generated code calls an external reference we need to catch that in
// the simulator.  The external reference will be a function compiled for the
// host architecture.  We need to call that function instead of trying to
// execute it with the simulator.  We do that by redirecting the external
// reference to a trapping instruction that is handled by the simulator.  We
// write the original destination of the jump just at a known offset from the
// trapping instruction so the simulator knows what to call.
//
// The following are trapping instructions used for various architectures:
//  - V8_TARGET_ARCH_ARM: svc (Supervisor Call)
//  - V8_TARGET_ARCH_ARM64: svc (Supervisor Call)
//  - V8_TARGET_ARCH_MIPS: swi (software-interrupt)
//  - V8_TARGET_ARCH_MIPS64: swi (software-interrupt)
//  - V8_TARGET_ARCH_PPC: svc (Supervisor Call)
//  - V8_TARGET_ARCH_S390: svc (Supervisor Call)
class Redirection {
 public:
  Redirection(Isolate* isolate, void* external_function,
              ExternalReference::Type type);

  Address address_of_instruction() {
#if ABI_USES_FUNCTION_DESCRIPTORS
    return reinterpret_cast<Address>(function_descriptor_);
#else
    return reinterpret_cast<Address>(&instruction_);
#endif
  }

  void* external_function() { return external_function_; }
  ExternalReference::Type type() { return type_; }

  static Redirection* Get(Isolate* isolate, void* external_function,
                          ExternalReference::Type type);

  static Redirection* FromInstruction(Instruction* instruction) {
    Address addr_of_instruction = reinterpret_cast<Address>(instruction);
    Address addr_of_redirection =
        addr_of_instruction - offsetof(Redirection, instruction_);
    return reinterpret_cast<Redirection*>(addr_of_redirection);
  }

  static void* ReverseRedirection(intptr_t reg) {
    Redirection* redirection = FromInstruction(
        reinterpret_cast<Instruction*>(reinterpret_cast<void*>(reg)));
    return redirection->external_function();
  }

  static void DeleteChain(Redirection* redirection) {
    while (redirection != nullptr) {
      Redirection* next = redirection->next_;
      delete redirection;
      redirection = next;
    }
  }

 private:
  void* external_function_;
  uint32_t instruction_;
  ExternalReference::Type type_;
  Redirection* next_;
#if ABI_USES_FUNCTION_DESCRIPTORS
  intptr_t function_descriptor_[3];
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // defined(USE_SIMULATOR)
#endif  // V8_SIMULATOR_BASE_H_
