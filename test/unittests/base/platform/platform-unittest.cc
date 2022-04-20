// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/platform/platform.h"

#include <cstring>

#include "src/base/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#ifdef V8_TARGET_OS_LINUX
#include <sys/sysmacros.h>

#include "src/base/platform/platform-linux.h"
#endif

#ifdef V8_OS_WIN
#include <windows.h>
#endif

namespace v8 {
namespace base {

#ifdef V8_TARGET_OS_WIN
// Alignemnt is constrained on Windows.
constexpr size_t kMaxPageSize = 4096;
#else
constexpr size_t kMaxPageSize = 16384;
#endif

alignas(kMaxPageSize) const char kArray[kMaxPageSize] =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua.";

TEST(OS, GetCurrentProcessId) {
#ifdef V8_OS_POSIX
  EXPECT_EQ(static_cast<int>(getpid()), OS::GetCurrentProcessId());
#endif

#ifdef V8_OS_WIN
  EXPECT_EQ(static_cast<int>(::GetCurrentProcessId()),
            OS::GetCurrentProcessId());
#endif
}

TEST(OS, RemapPages) {
  if constexpr (OS::IsRemapPageSupported()) {
    const size_t size = base::OS::AllocatePageSize();
    ASSERT_TRUE(size <= kMaxPageSize);
    const void* data = static_cast<const void*>(kArray);

    // Target mapping.
    void* remapped_data =
        OS::Allocate(nullptr, size, base::OS::AllocatePageSize(),
                     OS::MemoryPermission::kReadWrite);
    ASSERT_TRUE(remapped_data);

    EXPECT_TRUE(OS::RemapPages(data, size, remapped_data,
                               OS::MemoryPermission::kReadExecute));
    EXPECT_EQ(0, memcmp(remapped_data, data, size));

    OS::Free(remapped_data, size);
  }
}

#ifdef V8_TARGET_OS_LINUX
TEST(OS, ParseProcMaps) {
  // Truncated
  std::string line = "00000000-12345678 r--p";
  EXPECT_FALSE(MemoryRegion::FromMapsLine(line.c_str()));

  // Constants below are for 64 bit architectures.
#ifdef V8_TARGET_ARCH_64_BIT
  // File-backed.
  line =
      "7f861d1e3000-7f861d33b000 r-xp 00026000 fe:01 12583839                  "
      " /lib/x86_64-linux-gnu/libc-2.33.so";
  auto region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x7f861d1e3000u);
  EXPECT_EQ(region->end, 0x7f861d33b000u);
  EXPECT_EQ(std::string(region->permissions), std::string("r-xp"));
  EXPECT_EQ(region->offset, 0x00026000u);
  EXPECT_EQ(region->dev, makedev(0xfe, 0x01));
  EXPECT_EQ(region->inode, 12583839u);
  EXPECT_EQ(region->pathname,
            std::string("/lib/x86_64-linux-gnu/libc-2.33.so"));

  // Anonymous, but named.
  line =
      "5611cc7eb000-5611cc80c000 rw-p 00000000 00:00 0                         "
      " [heap]";
  region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x5611cc7eb000u);
  EXPECT_EQ(region->end, 0x5611cc80c000u);
  EXPECT_EQ(std::string(region->permissions), std::string("rw-p"));
  EXPECT_EQ(region->offset, 0u);
  EXPECT_EQ(region->dev, makedev(0x0, 0x0));
  EXPECT_EQ(region->inode, 0u);
  EXPECT_EQ(region->pathname, std::string("[heap]"));

  // Anonymous, not named.
  line = "5611cc7eb000-5611cc80c000 rw-p 00000000 00:00 0";
  region = MemoryRegion::FromMapsLine(line.c_str());
  EXPECT_TRUE(region);

  EXPECT_EQ(region->start, 0x5611cc7eb000u);
  EXPECT_EQ(region->end, 0x5611cc80c000u);
  EXPECT_EQ(std::string(region->permissions), std::string("rw-p"));
  EXPECT_EQ(region->offset, 0u);
  EXPECT_EQ(region->dev, makedev(0x0, 0x0));
  EXPECT_EQ(region->inode, 0u);
  EXPECT_EQ(region->pathname, std::string(""));
#endif  // V8_TARGET_ARCH_64_BIT
}
#endif  // V8_TARGET_OS_LINUX

namespace {

class ThreadLocalStorageTest : public Thread, public ::testing::Test {
 public:
  ThreadLocalStorageTest() : Thread(Options("ThreadLocalStorageTest")) {
    for (size_t i = 0; i < arraysize(keys_); ++i) {
      keys_[i] = Thread::CreateThreadLocalKey();
    }
  }
  ~ThreadLocalStorageTest() override {
    for (size_t i = 0; i < arraysize(keys_); ++i) {
      Thread::DeleteThreadLocalKey(keys_[i]);
    }
  }

  void Run() final {
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(!Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      Thread::SetThreadLocal(keys_[i], GetValue(i));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK_EQ(GetValue(i), Thread::GetThreadLocal(keys_[i]));
      CHECK_EQ(GetValue(i), Thread::GetExistingThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      Thread::SetThreadLocal(keys_[i], GetValue(arraysize(keys_) - i - 1));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK(Thread::HasThreadLocal(keys_[i]));
    }
    for (size_t i = 0; i < arraysize(keys_); i++) {
      CHECK_EQ(GetValue(arraysize(keys_) - i - 1),
               Thread::GetThreadLocal(keys_[i]));
      CHECK_EQ(GetValue(arraysize(keys_) - i - 1),
               Thread::GetExistingThreadLocal(keys_[i]));
    }
  }

 private:
  static void* GetValue(size_t x) {
    return bit_cast<void*>(static_cast<uintptr_t>(x + 1));
  }

  // Older versions of Android have fewer TLS slots (nominally 64, but the
  // system uses "about 5 of them" itself).
  Thread::LocalStorageKey keys_[32];
};

}  // namespace

TEST_F(ThreadLocalStorageTest, DoTest) {
  Run();
  CHECK(Start());
  Join();
}

TEST(StackTest, GetStackStart) { EXPECT_NE(nullptr, Stack::GetStackStart()); }

TEST(StackTest, GetCurrentStackPosition) {
  EXPECT_NE(nullptr, Stack::GetCurrentStackPosition());
}

#if !defined(V8_OS_FUCHSIA)
TEST(StackTest, StackVariableInBounds) {
  void* dummy;
  ASSERT_GT(static_cast<void*>(Stack::GetStackStart()),
            Stack::GetCurrentStackPosition());
  EXPECT_GT(static_cast<void*>(Stack::GetStackStart()),
            Stack::GetRealStackAddressForSlot(&dummy));
  EXPECT_LT(static_cast<void*>(Stack::GetCurrentStackPosition()),
            Stack::GetRealStackAddressForSlot(&dummy));
}
#endif  // !V8_OS_FUCHSIA

}  // namespace base
}  // namespace v8
