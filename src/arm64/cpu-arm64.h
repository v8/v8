// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_ARM64_CPU_ARM64_H_
#define V8_ARM64_CPU_ARM64_H_

#include <stdio.h>
#include "serialize.h"
#include "cpu.h"

namespace v8 {
namespace internal {


// CpuFeatures keeps track of which features are supported by the target CPU.
// Supported features must be enabled by a CpuFeatureScope before use.
class CpuFeatures : public AllStatic {
 public:
  // Detect features of the target CPU. Set safe defaults if the serializer
  // is enabled (snapshots must be portable).
  static void Probe(bool serializer_enabled);

  // Check whether a feature is supported by the target CPU.
  static bool IsSupported(CpuFeature f) {
    ASSERT(initialized_);
    return Check(f, supported_);
  }

  static bool IsSafeForSnapshot(Isolate* isolate, CpuFeature f) {
    return IsSupported(f);
  }

  // I and D cache line size in bytes.
  static unsigned dcache_line_size();
  static unsigned icache_line_size();

  static unsigned supported_;

  static bool VerifyCrossCompiling() {
    return cross_compile_ == 0;
  }

  static bool VerifyCrossCompiling(CpuFeature f) {
    unsigned mask = flag2set(f);
    return cross_compile_ == 0 ||
           (cross_compile_ & mask) == mask;
  }

  static bool SupportsCrankshaft() { return true; }

 private:
#ifdef DEBUG
  static bool initialized_;
#endif

  static unsigned found_by_runtime_probing_only_;
  static unsigned cross_compile_;

  static bool Check(CpuFeature f, unsigned set) {
    return (set & flag2set(f)) != 0;
  }

  static unsigned flag2set(CpuFeature f) {
    return 1u << f;
  }

  friend class PlatformFeatureScope;
  DISALLOW_COPY_AND_ASSIGN(CpuFeatures);
};

} }  // namespace v8::internal

#endif  // V8_ARM64_CPU_ARM64_H_
