// Copyright 2009 the V8 project authors. All rights reserved.
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


#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>


#include "d8.h"
#include "d8-debug.h"
#include "debug.h"


namespace v8 {


// If the buffer ends in the middle of a UTF-8 sequence then we return
// the length of the string up to but not including the incomplete UTF-8
// sequence.  If the buffer ends with a valid UTF-8 sequence then we
// return the whole buffer.
static int LengthWithoutIncompleteUtf8(char* buffer, int len) {
  int answer = len;
  // 1-byte encoding.
  static const int kUtf8SingleByteMask = 0x80;
  static const int kUtf8SingleByteValue = 0x00;
  // 2-byte encoding.
  static const int kUtf8TwoByteMask = 0xe0;
  static const int kUtf8TwoByteValue = 0xc0;
  // 3-byte encoding.
  static const int kUtf8ThreeByteMask = 0xf0;
  static const int kUtf8ThreeByteValue = 0xe0;
  // 4-byte encoding.
  static const int kUtf8FourByteMask = 0xf8;
  static const int kUtf8FourByteValue = 0xf0;
  // Subsequent bytes of a multi-byte encoding.
  static const int kMultiByteMask = 0xc0;
  static const int kMultiByteValue = 0x80;
  int multi_byte_bytes_seen = 0;
  while (answer > 0) {
    int c = buffer[answer - 1];
    // Ends in valid single-byte sequence?
    if ((c & kUtf8SingleByteMask) == kUtf8SingleByteValue) return answer;
    // Ends in one or more subsequent bytes of a multi-byte value?
    if ((c & kMultiByteMask) == kMultiByteValue) {
      multi_byte_bytes_seen++;
      answer--;
    } else {
      if ((c & kUtf8TwoByteMask) == kUtf8TwoByteValue) {
        if (multi_byte_bytes_seen >= 1) {
          return answer + 2;
        }
        return answer - 1;
      } else if ((c & kUtf8ThreeByteMask) == kUtf8ThreeByteValue) {
        if (multi_byte_bytes_seen >= 2) {
          return answer + 3;
        }
        return answer - 1;
      } else if ((c & kUtf8FourByteMask) == kUtf8FourByteValue) {
        if (multi_byte_bytes_seen >= 3) {
          return answer + 4;
        }
        return answer - 1;
      } else {
        return answer;  // Malformed UTF-8.
      }
    }
  }
  return 0;
}


// Suspends the thread until there is data available from the child process.
// Returns false on timeout, true on data ready.
static bool WaitOnFD(int fd,
                     int read_timeout,
                     int* total_timeout,
                     struct timeval& start_time) {
  fd_set readfds, writefds, exceptfds;
  struct timeval timeout;
  if (*total_timeout != -1) {
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    int seconds = time_now.tv_sec - start_time.tv_sec;
    int gone = seconds * 1000 + (time_now.tv_usec - start_time.tv_usec) / 1000;
    if (gone >= *total_timeout) return false;
    *total_timeout -= gone;
  }
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);
  FD_SET(fd, &readfds);
  FD_SET(fd, &exceptfds);
  if (read_timeout == -1 ||
      (*total_timeout != -1 && *total_timeout < read_timeout)) {
    read_timeout = *total_timeout;
  }
  timeout.tv_usec = (read_timeout % 1000) * 1000;
  timeout.tv_sec = read_timeout / 1000;
  int number_of_fds_ready = select(fd + 1,
                                   &readfds,
                                   &writefds,
                                   &exceptfds,
                                   read_timeout != -1 ? &timeout : NULL);
  return number_of_fds_ready == 1;
}


// Checks whether we ran out of time on the timeout.  Returns true if we ran out
// of time, false if we still have time.
static bool TimeIsOut(const struct timeval& start_time, const int& total_time) {
  if (total_time == -1) return false;
  struct timeval time_now;
  gettimeofday(&time_now, NULL);
  // Careful about overflow.
  int seconds = time_now.tv_sec - start_time.tv_sec;
  if (seconds > 100) {
    if (seconds * 1000 > total_time) return true;
    return false;
  }
  int useconds = time_now.tv_usec - start_time.tv_usec;
  if (seconds * 1000000 + useconds > total_time * 1000) {
    return true;
  }
  return false;
}


// A utility class that does a non-hanging waitpid on the child process if we
// bail out of the System() function early.  If you don't ever do a waitpid on
// a subprocess then it turns into one of those annoying 'zombie processes'.
class ZombieProtector {
 public:
  explicit ZombieProtector(int pid): pid_(pid) { }
  ~ZombieProtector() { if (pid_ != 0) waitpid(pid_, NULL, WNOHANG); }
  void ChildIsDeadNow() { pid_ = 0; }
 private:
  int pid_;
};


// A utility class that closes a file descriptor when it goes out of scope.
class OpenFDCloser {
 public:
  explicit OpenFDCloser(int fd): fd_(fd) { }
  ~OpenFDCloser() { close(fd_); }
 private:
  int fd_;
};


// A utility class that takes the array of command arguments and puts then in an
// array of new[]ed UTF-8 C strings.  Deallocates them again when it goes out of
// scope.
class ExecArgs {
 public:
  ExecArgs(Handle<Value> arg0, Handle<Array> command_args) {
    String::Utf8Value prog(arg0);
    int len = prog.length() + 1;
    char* c_arg = new char[len];
    snprintf(c_arg, len, "%s", *prog);
    exec_args_[0] = c_arg;
    int i = 1;
    for (unsigned j = 0; j < command_args->Length(); i++, j++) {
      Handle<Value> arg(command_args->Get(Integer::New(j)));
      String::Utf8Value utf8_arg(arg);
      int len = utf8_arg.length() + 1;
      char* c_arg = new char[len];
      snprintf(c_arg, len, "%s", *utf8_arg);
      exec_args_[i] = c_arg;
    }
    exec_args_[i] = NULL;
  }
  ~ExecArgs() {
    for (unsigned i = 0; i < kMaxArgs; i++) {
      if (exec_args_[i] == NULL) {
        return;
      }
      delete [] exec_args_[i];
      exec_args_[i] = 0;
    }
  }
  static const unsigned kMaxArgs = 1000;
  char** arg_array() { return exec_args_; }
  char* arg0() { return exec_args_[0]; }
 private:
  char* exec_args_[kMaxArgs + 1];
};


// Gets the optional timeouts from the arguments to the system() call.
static bool GetTimeouts(const Arguments& args,
                        int* read_timeout,
                        int* total_timeout) {
  if (args.Length() > 3) {
    if (args[3]->IsNumber()) {
      *total_timeout = args[3]->Int32Value();
    } else {
      ThrowException(String::New("system: Argument 4 must be a number"));
      return false;
    }
  }
  if (args.Length() > 2) {
    if (args[2]->IsNumber()) {
      *read_timeout = args[2]->Int32Value();
    } else {
      ThrowException(String::New("system: Argument 3 must be a number"));
      return false;
    }
  }
  return true;
}


static const int kReadFD = 0;
static const int kWriteFD = 1;


// This is run in the child process after fork() but before exec().  It normally
// ends with the child process being replaced with the desired child program.
// It only returns if an error occurred.
static void ExecSubprocess(int* exec_error_fds,
                           int* stdout_fds,
                           ExecArgs& exec_args) {
  close(exec_error_fds[kReadFD]);  // Don't need this in the child.
  close(stdout_fds[kReadFD]);      // Don't need this in the child.
  close(1);                        // Close stdout.
  dup2(stdout_fds[kWriteFD], 1);   // Dup pipe fd to stdout.
  close(stdout_fds[kWriteFD]);     // Don't need the original fd now.
  fcntl(exec_error_fds[kWriteFD], F_SETFD, FD_CLOEXEC);
  execvp(exec_args.arg0(), exec_args.arg_array());
  // Only get here if the exec failed.  Write errno to the parent to tell
  // them it went wrong.  If it went well the pipe is closed.
  int err = errno;
  write(exec_error_fds[kWriteFD], &err, sizeof(err));
  // Return (and exit child process).
}


// Runs in the parent process.  Checks that the child was able to exec (closing
// the file desriptor), or reports an error if it failed.
static bool ChildLaunchedOK(int* exec_error_fds) {
  int bytes_read;
  int err;
  do {
    bytes_read = read(exec_error_fds[kReadFD], &err, sizeof(err));
  } while (bytes_read == -1 && errno == EINTR);
  if (bytes_read != 0) {
    ThrowException(String::New(strerror(err)));
    return false;
  }
  return true;
}


// Accumulates the output from the child in a string handle.  Returns true if it
// succeeded or false if an exception was thrown.
static Handle<Value> GetStdout(int child_fd,
                               struct timeval& start_time,
                               int read_timeout,
                               int* total_timeout) {
  Handle<String> accumulator = String::Empty();
  const char* source = "function(a, b) { return a + b; }";
  Handle<Value> cons_as_obj(Script::Compile(String::New(source))->Run());
  Handle<Function> cons_function(Function::Cast(*cons_as_obj));
  Handle<Value> cons_args[2];

  int fullness = 0;
  static const int kStdoutReadBufferSize = 4096;
  char buffer[kStdoutReadBufferSize];

  if (fcntl(child_fd, F_SETFL, O_NONBLOCK) != 0) {
    return ThrowException(String::New(strerror(errno)));
  }

  int bytes_read;
  do {
    bytes_read = read(child_fd,
                      buffer + fullness,
                      kStdoutReadBufferSize - fullness);
    if (bytes_read == -1) {
      if (errno == EAGAIN) {
        if (!WaitOnFD(child_fd,
                      read_timeout,
                      total_timeout,
                      start_time) ||
            (TimeIsOut(start_time, *total_timeout))) {
          return ThrowException(String::New("Timed out waiting for output"));
        }
        continue;
      } else if (errno == EINTR) {
        continue;
      } else {
        break;
      }
    }
    if (bytes_read + fullness > 0) {
      int length = bytes_read == 0 ?
                   bytes_read + fullness :
                   LengthWithoutIncompleteUtf8(buffer, bytes_read + fullness);
      Handle<String> addition = String::New(buffer, length);
      cons_args[0] = accumulator;
      cons_args[1] = addition;
      accumulator = Handle<String>::Cast(cons_function->Call(
          Shell::utility_context()->Global(),
          2,
          cons_args));
      fullness = bytes_read + fullness - length;
      memcpy(buffer, buffer + length, fullness);
    }
  } while (bytes_read != 0);
  return accumulator;
}


// Modern Linux has the waitid call, which is like waitpid, but more useful
// if you want a timeout.  If we don't have waitid we can't limit the time
// waiting for the process to exit without losing the information about
// whether it exited normally.  In the common case this doesn't matter because
// we don't get here before the child has closed stdout and most programs don't
// do that before they exit.
#if defined(WNOWAIT) && !defined(ANDROID)
#define HAS_WAITID 1
#endif


// Get exit status of child.
static bool WaitForChild(int pid,
                         ZombieProtector& child_waiter,
                         struct timeval& start_time,
                         int read_timeout,
                         int total_timeout) {
#ifdef HAS_WAITID

  siginfo_t child_info;
  child_info.si_pid = 0;
  int useconds = 1;
  // Wait for child to exit.
  while (child_info.si_pid == 0) {
    waitid(P_PID, pid, &child_info, WEXITED | WNOHANG | WNOWAIT);
    usleep(useconds);
    if (useconds < 1000000) useconds <<= 1;
    if ((read_timeout != -1 && useconds / 1000 > read_timeout) ||
        (TimeIsOut(start_time, total_timeout))) {
      ThrowException(String::New("Timed out waiting for process to terminate"));
      kill(pid, SIGINT);
      return false;
    }
  }
  child_waiter.ChildIsDeadNow();
  if (child_info.si_code == CLD_KILLED) {
    char message[999];
    snprintf(message,
             sizeof(message),
             "Child killed by signal %d",
             child_info.si_status);
    ThrowException(String::New(message));
    return false;
  }
  if (child_info.si_code == CLD_EXITED && child_info.si_status != 0) {
    char message[999];
    snprintf(message,
             sizeof(message),
             "Child exited with status %d",
             child_info.si_status);
    ThrowException(String::New(message));
    return false;
  }

#else  // No waitid call.

  int child_status;
  printf("waitpid");
  waitpid(pid, &child_status, 0);  // We hang here if the child doesn't exit.
  child_waiter.ChildIsDeadNow();
  if (WIFSIGNALED(child_status)) {
    char message[999];
    snprintf(message,
             sizeof(message),
             "Child killed by signal %d",
             WTERMSIG(child_status));
    ThrowException(String::New(message));
    return false;
  }
  if (WEXITSTATUS(child_status) != 0) {
    char message[999];
    int exit_status = WEXITSTATUS(child_status);
    snprintf(message,
             sizeof(message),
             "Child exited with status %d",
             exit_status);
    ThrowException(String::New(message));
    return false;
  }

#endif  // No waitid call.

  return true;
}


// Implementation of the system() function (see d8.h for details).
Handle<Value> Shell::System(const Arguments& args) {
  HandleScope scope;
  int read_timeout = -1;
  int total_timeout = -1;
  if (!GetTimeouts(args, &read_timeout, &total_timeout)) return v8::Undefined();
  Handle<Array> command_args;
  if (args.Length() > 1) {
    if (!args[1]->IsArray()) {
      return ThrowException(String::New("system: Argument 2 must be an array"));
    }
    command_args = Handle<Array>::Cast(args[1]);
  } else {
    command_args = Array::New(0);
  }
  if (command_args->Length() > ExecArgs::kMaxArgs) {
    return ThrowException(String::New("Too many arguments to system()"));
  }
  if (args.Length() < 1) {
    return ThrowException(String::New("Too few arguments to system()"));
  }

  struct timeval start_time;
  gettimeofday(&start_time, NULL);

  ExecArgs exec_args(args[0], command_args);
  int exec_error_fds[2];
  int stdout_fds[2];

  if (pipe(exec_error_fds) != 0) {
    return ThrowException(String::New("pipe syscall failed."));
  }
  if (pipe(stdout_fds) != 0) {
    return ThrowException(String::New("pipe syscall failed."));
  }

  pid_t pid = fork();
  if (pid == 0) {  // Child process.
    ExecSubprocess(exec_error_fds, stdout_fds, exec_args);
    exit(1);
  }

  // Parent process.  Ensure that we clean up if we exit this function early.
  ZombieProtector child_waiter(pid);
  close(exec_error_fds[kWriteFD]);
  close(stdout_fds[kWriteFD]);
  OpenFDCloser error_read_closer(exec_error_fds[kReadFD]);
  OpenFDCloser stdout_read_closer(stdout_fds[kReadFD]);

  if (!ChildLaunchedOK(exec_error_fds)) return v8::Undefined();

  Handle<Value> accumulator = GetStdout(stdout_fds[kReadFD],
                                        start_time,
                                        read_timeout,
                                        &total_timeout);
  if (accumulator->IsUndefined()) {
    kill(pid, SIGINT);  // On timeout, kill the subprocess.
    return accumulator;
  }

  if (!WaitForChild(pid,
                    child_waiter,
                    start_time,
                    read_timeout,
                    total_timeout)) {
    return v8::Undefined();
  }

  return scope.Close(accumulator);
}


}  // namespace v8
