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


#include <stdlib.h>

#include "d8.h"
#include "debug.h"
#include "api.h"
#include "natives.h"


namespace v8 {


const char* Shell::kHistoryFileName = ".d8_history";
const char* Shell::kPrompt = "d8> ";


LineEditor *LineEditor::first_ = NULL;


LineEditor::LineEditor(Type type, const char* name)
    : type_(type),
      name_(name),
      next_(first_) {
  first_ = this;
}


LineEditor* LineEditor::Get() {
  LineEditor* current = first_;
  LineEditor* best = current;
  while (current != NULL) {
    if (current->type_ > best->type_)
      best = current;
    current = current->next_;
  }
  return best;
}


class DumbLineEditor: public LineEditor {
 public:
  DumbLineEditor() : LineEditor(LineEditor::DUMB, "dumb") { }
  virtual i::SmartPointer<char> Prompt(const char* prompt);
};


static DumbLineEditor dumb_line_editor;


i::SmartPointer<char> DumbLineEditor::Prompt(const char* prompt) {
  static const int kBufferSize = 256;
  char buffer[kBufferSize];
  printf("%s", prompt);
  char* str = fgets(buffer, kBufferSize, stdin);
  return i::SmartPointer<char>(str ? i::StrDup(str) : str);
}


Shell::CounterMap Shell::counter_map_;
Persistent<Context> Shell::utility_context_;
Persistent<Context> Shell::evaluation_context_;


// Executes a string within the current v8 context.
bool Shell::ExecuteString(Handle<String> source,
                          Handle<Value> name,
                          bool print_result,
                          bool report_exceptions) {
  HandleScope handle_scope;
  TryCatch try_catch;
  Handle<Script> script = Script::Compile(source, name);
  if (script.IsEmpty()) {
    // Print errors that happened during compilation.
    if (report_exceptions)
      ReportException(&try_catch);
    return false;
  } else {
    Handle<Value> result = script->Run();
    if (result.IsEmpty()) {
      // Print errors that happened during execution.
      if (report_exceptions)
        ReportException(&try_catch);
      return false;
    } else {
      if (print_result && !result->IsUndefined()) {
        // If all went well and the result wasn't undefined then print
        // the returned value.
        String::Utf8Value str(result);
        printf("%s\n", *str);
      }
      return true;
    }
  }
}


Handle<Value> Shell::Print(const Arguments& args) {
  bool first = true;
  for (int i = 0; i < args.Length(); i++) {
    HandleScope handle_scope;
    if (first) {
      first = false;
    } else {
      printf(" ");
    }
    String::Utf8Value str(args[i]);
    printf("%s", *str);
  }
  printf("\n");
  return Undefined();
}


Handle<Value> Shell::Load(const Arguments& args) {
  for (int i = 0; i < args.Length(); i++) {
    HandleScope handle_scope;
    String::Utf8Value file(args[i]);
    Handle<String> source = ReadFile(*file);
    if (source.IsEmpty()) {
      return ThrowException(String::New("Error loading file"));
    }
    if (!ExecuteString(source, String::New(*file), false, false)) {
      return ThrowException(String::New("Error executing  file"));
    }
  }
  return Undefined();
}


Handle<Value> Shell::Quit(const Arguments& args) {
  int exit_code = args[0]->Int32Value();
  OnExit();
  exit(exit_code);
  return Undefined();
}


Handle<Value> Shell::Version(const Arguments& args) {
  return String::New(V8::GetVersion());
}


void Shell::ReportException(v8::TryCatch* try_catch) {
  HandleScope handle_scope;
  String::Utf8Value exception(try_catch->Exception());
  Handle<Message> message = try_catch->Message();
  if (message.IsEmpty()) {
    // V8 didn't provide any extra information about this error; just
    // print the exception.
    printf("%s\n", *exception);
  } else {
    // Print (filename):(line number): (message).
    String::Utf8Value filename(message->GetScriptResourceName());
    int linenum = message->GetLineNumber();
    printf("%s:%i: %s\n", *filename, linenum, *exception);
    // Print line of source code.
    String::Utf8Value sourceline(message->GetSourceLine());
    printf("%s\n", *sourceline);
    // Print wavy underline (GetUnderline is deprecated).
    int start = message->GetStartColumn();
    for (int i = 0; i < start; i++) {
      printf(" ");
    }
    int end = message->GetEndColumn();
    for (int i = start; i < end; i++) {
      printf("^");
    }
    printf("\n");
  }
}


Handle<Array> Shell::GetCompletions(Handle<String> text, Handle<String> full) {
  HandleScope handle_scope;
  Context::Scope context_scope(utility_context_);
  Handle<Object> global = utility_context_->Global();
  Handle<Value> fun = global->Get(String::New("GetCompletions"));
  static const int kArgc = 3;
  Handle<Value> argv[kArgc] = { evaluation_context_->Global(), text, full };
  Handle<Value> val = Handle<Function>::Cast(fun)->Call(global, kArgc, argv);
  return handle_scope.Close(Handle<Array>::Cast(val));
}


int* Shell::LookupCounter(const char* name) {
  CounterMap::iterator item = counter_map_.find(name);
  if (item != counter_map_.end()) {
    Counter* result = (*item).second;
    return result->GetValuePtr();
  }
  Counter* result = new Counter(name);
  counter_map_[name] = result;
  return result->GetValuePtr();
}


void Shell::Initialize() {
  // Set up counters
  if (i::FLAG_dump_counters)
    V8::SetCounterFunction(LookupCounter);
  // Initialize the global objects
  HandleScope scope;
  Handle<ObjectTemplate> global_template = ObjectTemplate::New();
  global_template->Set(String::New("print"), FunctionTemplate::New(Print));
  global_template->Set(String::New("load"), FunctionTemplate::New(Load));
  global_template->Set(String::New("quit"), FunctionTemplate::New(Quit));
  global_template->Set(String::New("version"), FunctionTemplate::New(Version));

  utility_context_ = Context::New(NULL, global_template);
  utility_context_->SetSecurityToken(Undefined());
  Context::Scope utility_scope(utility_context_);

  i::JSArguments js_args = i::FLAG_js_arguments;
  i::Handle<i::FixedArray> arguments_array =
      i::Factory::NewFixedArray(js_args.argc());
  for (int j = 0; j < js_args.argc(); j++) {
    i::Handle<i::String> arg =
        i::Factory::NewStringFromUtf8(i::CStrVector(js_args[j]));
    arguments_array->set(j, *arg);
  }
  i::Handle<i::JSArray> arguments_jsarray =
      i::Factory::NewJSArrayWithElements(arguments_array);
  global_template->Set(String::New("arguments"),
                       Utils::ToLocal(arguments_jsarray));

  // Install the debugger object in the utility scope
  i::Debug::Load();
  i::JSObject* debug = i::Debug::debug_context()->global();
  utility_context_->Global()->Set(String::New("$debug"),
                                  Utils::ToLocal(&debug));

  // Run the d8 shell utility script in the utility context
  int source_index = i::NativesCollection<i::D8>::GetIndex("d8");
  i::Vector<const char> shell_source
      = i::NativesCollection<i::D8>::GetScriptSource(source_index);
  i::Vector<const char> shell_source_name
      = i::NativesCollection<i::D8>::GetScriptName(source_index);
  Handle<String> source = String::New(shell_source.start(),
                                      shell_source.length());
  Handle<String> name = String::New(shell_source_name.start(),
                                    shell_source_name.length());
  Script::Compile(source, name)->Run();

  // Create the evaluation context
  evaluation_context_ = Context::New(NULL, global_template);
  evaluation_context_->SetSecurityToken(Undefined());

  // Set the security token of the debug context to allow access.
  i::Debug::debug_context()->set_security_token(i::Heap::undefined_value());
}


void Shell::OnExit() {
  if (i::FLAG_dump_counters) {
    ::printf("+----------------------------------------+----------+\n");
    ::printf("| Name                                   | Value    |\n");
    ::printf("+----------------------------------------+----------+\n");
    for (CounterMap::iterator i = counter_map_.begin();
         i != counter_map_.end();
         i++) {
      Counter* counter = (*i).second;
      ::printf("| %-38ls | %8i |\n", counter->name(), counter->value());
    }
    ::printf("+----------------------------------------+----------+\n");
  }
}


// Reads a file into a v8 string.
Handle<String> Shell::ReadFile(const char* name) {
  FILE* file = i::OS::FOpen(name, "rb");
  if (file == NULL) return Handle<String>();

  fseek(file, 0, SEEK_END);
  int size = ftell(file);
  rewind(file);

  char* chars = new char[size + 1];
  chars[size] = '\0';
  for (int i = 0; i < size;) {
    int read = fread(&chars[i], 1, size - i, file);
    i += read;
  }
  fclose(file);
  Handle<String> result = String::New(chars, size);
  delete[] chars;
  return result;
}


void Shell::RunShell() {
  LineEditor* editor = LineEditor::Get();
  printf("V8 version %s [console: %s]\n", V8::GetVersion(), editor->name());
  editor->Open();
  while (true) {
    HandleScope handle_scope;
    i::SmartPointer<char> input = editor->Prompt(Shell::kPrompt);
    if (input.is_empty())
      break;
    editor->AddHistory(*input);
    Handle<String> name = String::New("(d8)");
    ExecuteString(String::New(*input), name, true, true);
  }
  editor->Close();
  printf("\n");
}


int Shell::Main(int argc, char* argv[]) {
  i::FlagList::SetFlagsFromCommandLine(&argc, argv, true);
  if (i::FLAG_help) {
    return 1;
  }
  Initialize();
  bool run_shell = (argc == 1);
  Context::Scope context_scope(evaluation_context_);
  for (int i = 1; i < argc; i++) {
    char* str = argv[i];
    if (strcmp(str, "-f") == 0) {
      // Ignore any -f flags for compatibility with other stand-alone
      // JavaScript engines.
      continue;
    } else if (strncmp(str, "--", 2) == 0) {
      printf("Warning: unknown flag %s.\nTry --help for options\n", str);
    } else if (strcmp(str, "-e") == 0 && i + 1 < argc) {
      // Execute argument given to -e option directly.
      v8::HandleScope handle_scope;
      v8::Handle<v8::String> file_name = v8::String::New("unnamed");
      v8::Handle<v8::String> source = v8::String::New(argv[i + 1]);
      if (!ExecuteString(source, file_name, false, true))
        return 1;
      i++;
    } else {
      // Use all other arguments as names of files to load and run.
      HandleScope handle_scope;
      Handle<String> file_name = v8::String::New(str);
      Handle<String> source = ReadFile(str);
      if (source.IsEmpty()) {
        printf("Error reading '%s'\n", str);
        return 1;
      }
      if (!ExecuteString(source, file_name, false, true))
        return 1;
    }
  }
  if (run_shell)
    RunShell();
  OnExit();
  return 0;
}


}  // namespace v8


int main(int argc, char* argv[]) {
  return v8::Shell::Main(argc, argv);
}
