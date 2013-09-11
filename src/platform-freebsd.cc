// Copyright 2012 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// Platform specific code for FreeBSD goes here. For the POSIX comaptible parts
// the implementation is in platform-posix.cc.

#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/ucontext.h>
#include <stdlib.h>

#include <sys/types.h>  // mmap & munmap
#include <sys/mman.h>   // mmap & munmap
#include <sys/stat.h>   // open
#include <sys/fcntl.h>  // open
#include <unistd.h>     // getpagesize
// If you don't have execinfo.h then you need devel/libexecinfo from ports.
#include <execinfo.h>   // backtrace, backtrace_symbols
#include <strings.h>    // index
#include <errno.h>
#include <stdarg.h>
#include <limits.h>

#undef MAP_TYPE

#include "v8.h"
#include "v8threads.h"

#include "platform-posix.h"
#include "platform.h"
#include "vm-state-inl.h"


namespace v8 {
namespace internal {


const char* OS::LocalTimezone(double time) {
  if (std::isnan(time)) return "";
  time_t tv = static_cast<time_t>(floor(time/msPerSecond));
  struct tm* t = localtime(&tv);
  if (NULL == t) return "";
  return t->tm_zone;
}


double OS::LocalTimeOffset() {
  time_t tv = time(NULL);
  struct tm* t = localtime(&tv);
  // tm_gmtoff includes any daylight savings offset, so subtract it.
  return static_cast<double>(t->tm_gmtoff * msPerSecond -
                             (t->tm_isdst > 0 ? 3600 * msPerSecond : 0));
}


void OS::DumpBacktrace() {
  POSIXBacktraceHelper<backtrace, backtrace_symbols>::DumpBacktrace();
}


class PosixMemoryMappedFile : public OS::MemoryMappedFile {
 public:
  PosixMemoryMappedFile(FILE* file, void* memory, int size)
    : file_(file), memory_(memory), size_(size) { }
  virtual ~PosixMemoryMappedFile();
  virtual void* memory() { return memory_; }
  virtual int size() { return size_; }
 private:
  FILE* file_;
  void* memory_;
  int size_;
};


OS::MemoryMappedFile* OS::MemoryMappedFile::open(const char* name) {
  FILE* file = fopen(name, "r+");
  if (file == NULL) return NULL;

  fseek(file, 0, SEEK_END);
  int size = ftell(file);

  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


OS::MemoryMappedFile* OS::MemoryMappedFile::create(const char* name, int size,
    void* initial) {
  FILE* file = fopen(name, "w+");
  if (file == NULL) return NULL;
  int result = fwrite(initial, size, 1, file);
  if (result < 1) {
    fclose(file);
    return NULL;
  }
  void* memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(file), 0);
  return new PosixMemoryMappedFile(file, memory, size);
}


PosixMemoryMappedFile::~PosixMemoryMappedFile() {
  if (memory_) munmap(memory_, size_);
  fclose(file_);
}


static unsigned StringToLong(char* buffer) {
  return static_cast<unsigned>(strtol(buffer, NULL, 16));  // NOLINT
}


void OS::LogSharedLibraryAddresses(Isolate* isolate) {
  static const int MAP_LENGTH = 1024;
  int fd = open("/proc/self/maps", O_RDONLY);
  if (fd < 0) return;
  while (true) {
    char addr_buffer[11];
    addr_buffer[0] = '0';
    addr_buffer[1] = 'x';
    addr_buffer[10] = 0;
    int result = read(fd, addr_buffer + 2, 8);
    if (result < 8) break;
    unsigned start = StringToLong(addr_buffer);
    result = read(fd, addr_buffer + 2, 1);
    if (result < 1) break;
    if (addr_buffer[2] != '-') break;
    result = read(fd, addr_buffer + 2, 8);
    if (result < 8) break;
    unsigned end = StringToLong(addr_buffer);
    char buffer[MAP_LENGTH];
    int bytes_read = -1;
    do {
      bytes_read++;
      if (bytes_read >= MAP_LENGTH - 1)
        break;
      result = read(fd, buffer + bytes_read, 1);
      if (result < 1) break;
    } while (buffer[bytes_read] != '\n');
    buffer[bytes_read] = 0;
    // Ignore mappings that are not executable.
    if (buffer[3] != 'x') continue;
    char* start_of_path = index(buffer, '/');
    // There may be no filename in this line.  Skip to next.
    if (start_of_path == NULL) continue;
    buffer[bytes_read] = 0;
    LOG(isolate SharedLibraryEvent(start_of_path, start, end));
  }
  close(fd);
}


void OS::SignalCodeMovingGC() {
}


int OS::StackWalk(Vector<OS::StackFrame> frames) {
  return POSIXBacktraceHelper<backtrace, backtrace_symbols>::StackWalk(frames);
}

} }  // namespace v8::internal
