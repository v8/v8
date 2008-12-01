// Copyright 2008 the V8 project authors. All rights reserved.
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

#ifndef V8_D8_H_
#define V8_D8_H_


// Disable exceptions on windows to not generate warnings from <map>.
#define _HAS_EXCEPTIONS 0
#include <map>

#include "v8.h"


namespace v8 {


namespace i = v8::internal;


class Counter {
 public:
  explicit Counter(const char* name)
    : name_(name), value_(0) { }
  int* GetValuePtr() { return &value_; }
  const char* name() { return name_; }
  int value() { return value_; }
 private:
  const char* name_;
  int value_;
};


class Shell: public i::AllStatic {
 public:
  static bool ExecuteString(Handle<String> source,
                            Handle<Value> name,
                            bool print_result,
                            bool report_exceptions);
  static void ReportException(TryCatch* try_catch);
  static void Initialize();
  static void OnExit();
  static int* LookupCounter(const char* name);
  static Handle<String> ReadFile(const char* name);
  static void RunShell();
  static int Main(int argc, char* argv[]);
  static Handle<Array> GetCompletions(Handle<String> text,
                                      Handle<String> full);

  static Handle<Value> Print(const Arguments& args);
  static Handle<Value> Quit(const Arguments& args);
  static Handle<Value> Version(const Arguments& args);
  static Handle<Value> Load(const Arguments& args);

  static const char* kHistoryFileName;
  static const char* kPrompt;
 private:
  static Persistent<Context> utility_context_;
  static Persistent<Context> evaluation_context_;
  typedef std::map<const char*, Counter*> CounterMap;
  static CounterMap counter_map_;
};


class LineEditor {
 public:
  enum Type { DUMB = 0, READLINE = 1 };
  LineEditor(Type type, const char* name);
  virtual ~LineEditor() { }

  virtual i::SmartPointer<char> Prompt(const char* prompt) = 0;
  virtual bool Open() { return true; }
  virtual bool Close() { return true; }
  virtual void AddHistory(const char* str) { }

  const char* name() { return name_; }
  static LineEditor* Get();
 private:
  Type type_;
  const char* name_;
  LineEditor* next_;
  static LineEditor* first_;
};


}  // namespace v8


#endif  // V8_D8_H_
