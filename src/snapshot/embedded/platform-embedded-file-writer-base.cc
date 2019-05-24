// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/snapshot/embedded/platform-embedded-file-writer-base.h"

#include <string>

#include "src/common/globals.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-aix.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-generic.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-mac.h"
#include "src/snapshot/embedded/platform-embedded-file-writer-win.h"

namespace v8 {
namespace internal {

DataDirective PointerSizeDirective() {
  if (kSystemPointerSize == 8) {
    return kQuad;
  } else {
    CHECK_EQ(4, kSystemPointerSize);
    return kLong;
  }
}

namespace {

EmbeddedTargetArch DefaultEmbeddedTargetArch() {
#if defined(V8_TARGET_ARCH_ARM)
  return EmbeddedTargetArch::kArm;
#elif defined(V8_TARGET_ARCH_ARM64)
  return EmbeddedTargetArch::kArm64;
#elif defined(V8_TARGET_ARCH_IA32)
  return EmbeddedTargetArch::kIA32;
#elif defined(V8_TARGET_ARCH_X64)
  return EmbeddedTargetArch::kX64;
#else
  return EmbeddedTargetArch::kGeneric;
#endif
}

EmbeddedTargetArch ToEmbeddedTargetArch(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetArch();
  }

  std::string string(s);
  if (string == "arm") {
    return EmbeddedTargetArch::kArm;
  } else if (string == "arm64") {
    return EmbeddedTargetArch::kArm64;
  } else if (string == "ia32") {
    return EmbeddedTargetArch::kIA32;
  } else if (string == "x64") {
    return EmbeddedTargetArch::kX64;
  } else {
    return EmbeddedTargetArch::kGeneric;
  }
}

EmbeddedTargetOs DefaultEmbeddedTargetOs() {
#if defined(V8_OS_AIX)
  return EmbeddedTargetOs::kAIX;
#elif defined(V8_OS_MACOSX)
  return EmbeddedTargetOs::kMac;
#elif defined(V8_OS_WIN)
  return EmbeddedTargetOs::kWin;
#else
  return EmbeddedTargetOs::kGeneric;
#endif
}

EmbeddedTargetOs ToEmbeddedTargetOs(const char* s) {
  if (s == nullptr) {
    return DefaultEmbeddedTargetOs();
  }

  std::string string(s);
  if (string == "aix") {
    return EmbeddedTargetOs::kAIX;
  } else if (string == "chromeos") {
    return EmbeddedTargetOs::kChromeOS;
  } else if (string == "fuchsia") {
    return EmbeddedTargetOs::kFuchsia;
  } else if (string == "mac") {
    return EmbeddedTargetOs::kMac;
  } else if (string == "win") {
    return EmbeddedTargetOs::kWin;
  } else {
    return EmbeddedTargetOs::kGeneric;
  }
}

}  // namespace

std::unique_ptr<PlatformEmbeddedFileWriterBase> NewPlatformEmbeddedFileWriter(
    const char* target_arch, const char* target_os) {
  auto embedded_target_arch = ToEmbeddedTargetArch(target_arch);
  auto embedded_target_os = ToEmbeddedTargetOs(target_os);

  if (embedded_target_os == EmbeddedTargetOs::kAIX) {
    return base::make_unique<PlatformEmbeddedFileWriterAIX>(
        embedded_target_arch, embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kMac) {
    return base::make_unique<PlatformEmbeddedFileWriterMac>(
        embedded_target_arch, embedded_target_os);
  } else if (embedded_target_os == EmbeddedTargetOs::kWin) {
    return base::make_unique<PlatformEmbeddedFileWriterWin>(
        embedded_target_arch, embedded_target_os);
  } else {
    return base::make_unique<PlatformEmbeddedFileWriterGeneric>(
        embedded_target_arch, embedded_target_os);
  }

  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
