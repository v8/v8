// Copyright 2006-2008 Google Inc. All Rights Reserved.
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

#include <ctype.h>
#include <stdlib.h>

#include "v8.h"

#include "platform.h"

namespace v8 { namespace internal {

// -----------------------------------------------------------------------------
// Helpers

static inline char NormalizeChar(char ch) {
  return ch == '_' ? '-' : ch;
}


static const char* NormalizeName(const char* name) {
  int len = strlen(name);
  char* result = NewArray<char>(len + 1);
  for (int i = 0; i <= len; i++) {
    result[i] = NormalizeChar(name[i]);
  }
  return const_cast<const char*>(result);
}


static bool EqualNames(const char* a, const char* b) {
  for (int i = 0; NormalizeChar(a[i]) == NormalizeChar(b[i]); i++) {
    if (a[i] == '\0') {
      return true;
    }
  }
  return false;
}


// -----------------------------------------------------------------------------
// Implementation of Flag

Flag::Flag(const char* file, const char* name, const char* comment,
           Type type, void* variable, FlagValue default_) {
  file_ = file;
  name_ = NormalizeName(name);
  comment_ = comment;
  type_ = type;
  variable_ = reinterpret_cast<FlagValue*>(variable);
  this->default_ = default_;
  FlagList::Register(this);
}


void Flag::SetToDefault() {
  // Note that we cannot simply do '*variable_ = default_;' since
  // flag variables are not really of type FlagValue and thus may
  // be smaller! The FlagValue union is simply 'overlayed' on top
  // of a flag variable for convenient access. Since union members
  // are guarantee to be aligned at the beginning, this works.
  switch (type_) {
    case Flag::BOOL:
      variable_->b = default_.b;
      return;
    case Flag::INT:
      variable_->i = default_.i;
      return;
    case Flag::FLOAT:
      variable_->f = default_.f;
      return;
    case Flag::STRING:
      variable_->s = default_.s;
      return;
  }
  UNREACHABLE();
}


bool Flag::IsDefault() const {
  switch (type_) {
    case Flag::BOOL:
      return variable_->b == default_.b;
    case Flag::INT:
      return variable_->i == default_.i;
    case Flag::FLOAT:
      return variable_->f == default_.f;
    case Flag::STRING:
      if (variable_->s && default_.s) {
        return strcmp(variable_->s, default_.s) == 0;
      } else {
        return variable_->s == default_.s;
      }
  }
  UNREACHABLE();
  return false;
}


static const char* Type2String(Flag::Type type) {
  switch (type) {
    case Flag::BOOL: return "bool";
    case Flag::INT: return "int";
    case Flag::FLOAT: return "float";
    case Flag::STRING: return "string";
  }
  UNREACHABLE();
  return NULL;
}


static char* ToString(Flag::Type type, FlagValue* variable) {
  char* value = NULL;
  switch (type) {
    case Flag::BOOL:
      value = NewArray<char>(6);
      OS::SNPrintF(value, 6, "%s", (variable->b ? "true" : "false"));
      break;
    case Flag::INT:
      value = NewArray<char>(12);
      OS::SNPrintF(value, 12, "%d", variable->i);
      break;
    case Flag::FLOAT:
      value = NewArray<char>(20);
      OS::SNPrintF(value, 20, "%f", variable->f);
      break;
    case Flag::STRING:
      if (variable->s) {
        int length = strlen(variable->s) + 1;
        value = NewArray<char>(length);
        OS::SNPrintF(value, length, "%s", variable->s);
      } else {
        value = NewArray<char>(5);
        OS::SNPrintF(value, 5, "NULL");
      }
      break;
  }
  ASSERT(value != NULL);
  return value;
}


static void PrintFlagValue(Flag::Type type, FlagValue* variable) {
  char* value = ToString(type, variable);
  printf("%s", value);
  DeleteArray(value);
}


char* Flag::StringValue() const {
  return ToString(type_, variable_);
}


void Flag::Print(bool print_current_value) {
  printf("  --%s (%s)  type: %s  default: ", name_, comment_,
         Type2String(type_));
  PrintFlagValue(type_, &default_);
  if (print_current_value) {
    printf("  current value: ");
    PrintFlagValue(type_, variable_);
  }
  printf("\n");
}


// -----------------------------------------------------------------------------
// Implementation of FlagList

Flag* FlagList::list_ = NULL;


List<char *>* FlagList::argv() {
  List<char *>* args = new List<char*>(8);
  for (Flag* f = list_; f != NULL; f = f->next()) {
    if (!f->IsDefault()) {
      char* cmdline_flag;
      if (f->type() != Flag::BOOL || *(f->bool_variable())) {
        int length = strlen(f->name()) + 2 + 1;
        cmdline_flag = NewArray<char>(length);
        OS::SNPrintF(cmdline_flag, length, "--%s", f->name());
      } else {
        int length = strlen(f->name()) + 4 + 1;
        cmdline_flag = NewArray<char>(length);
        OS::SNPrintF(cmdline_flag, length, "--no%s", f->name());
      }
      args->Add(cmdline_flag);
      if (f->type() != Flag::BOOL) {
        args->Add(f->StringValue());
      }
    }
  }
  return args;
}


void FlagList::Print(const char* file, bool print_current_value) {
  // Since flag registration is likely by file (= C++ file),
  // we don't need to sort by file and still get grouped output.
  const char* current = NULL;
  for (Flag* f = list_; f != NULL; f = f->next()) {
    if (file == NULL || file == f->file()) {
      if (current != f->file()) {
        printf("Flags from %s:\n", f->file());
        current = f->file();
      }
      f->Print(print_current_value);
    }
  }
}


Flag* FlagList::Lookup(const char* name) {
  Flag* f = list_;
  while (f != NULL && !EqualNames(name, f->name()))
    f = f->next();
  return f;
}


void FlagList::SplitArgument(const char* arg,
                             char* buffer,
                             int buffer_size,
                             const char** name,
                             const char** value,
                             bool* is_bool) {
  *name = NULL;
  *value = NULL;
  *is_bool = false;

  if (*arg == '-') {
    // find the begin of the flag name
    arg++;  // remove 1st '-'
    if (*arg == '-')
      arg++;  // remove 2nd '-'
    if (arg[0] == 'n' && arg[1] == 'o') {
      arg += 2;  // remove "no"
      *is_bool = true;
    }
    *name = arg;

    // find the end of the flag name
    while (*arg != '\0' && *arg != '=')
      arg++;

    // get the value if any
    if (*arg == '=') {
      // make a copy so we can NUL-terminate flag name
      int n = arg - *name;
      CHECK(n < buffer_size);  // buffer is too small
      memcpy(buffer, *name, n);
      buffer[n] = '\0';
      *name = buffer;
      // get the value
      *value = arg + 1;
    }
  }
}


int FlagList::SetFlagsFromCommandLine(int* argc,
                                      char** argv,
                                      bool remove_flags) {
  // parse arguments
  for (int i = 1; i < *argc;) {
    int j = i;  // j > 0
    const char* arg = argv[i++];

    // split arg into flag components
    char buffer[1*KB];
    const char* name;
    const char* value;
    bool is_bool;
    SplitArgument(arg, buffer, sizeof buffer, &name, &value, &is_bool);

    if (name != NULL) {
      // lookup the flag
      Flag* flag = Lookup(name);
      if (flag == NULL) {
        if (remove_flags) {
          // We don't recognize this flag but since we're removing
          // the flags we recognize we assume that the remaining flags
          // will be processed somewhere else so this flag might make
          // sense there.
          continue;
        } else {
          fprintf(stderr, "Error: unrecognized flag %s\n", arg);
          return j;
        }
      }

      // if we still need a flag value, use the next argument if available
      if (flag->type() != Flag::BOOL && value == NULL) {
        if (i < *argc) {
          value = argv[i++];
        } else {
          fprintf(stderr, "Error: missing value for flag %s of type %s\n",
                  arg, Type2String(flag->type()));
          return j;
        }
      }

      // set the flag
      char* endp = const_cast<char*>("");  // *endp is only read
      switch (flag->type()) {
        case Flag::BOOL:
          *flag->bool_variable() = !is_bool;
          break;
        case Flag::INT:
          *flag->int_variable() = strtol(value, &endp, 10);  // NOLINT
          break;
        case Flag::FLOAT:
          *flag->float_variable() = strtod(value, &endp);
          break;
        case Flag::STRING:
          *flag->string_variable() = value;
          break;
      }

      // handle errors
      if ((flag->type() == Flag::BOOL && value != NULL) ||
          (flag->type() != Flag::BOOL && is_bool) ||
          *endp != '\0') {
        fprintf(stderr, "Error: illegal value for flag %s of type %s\n",
                arg, Type2String(flag->type()));
        return j;
      }

      // remove the flag & value from the command
      if (remove_flags)
        while (j < i)
          argv[j++] = NULL;
    }
  }

  // shrink the argument list
  if (remove_flags) {
    int j = 1;
    for (int i = 1; i < *argc; i++) {
      if (argv[i] != NULL)
        argv[j++] = argv[i];
    }
    *argc = j;
  }

  // parsed all flags successfully
  return 0;
}


static char* SkipWhiteSpace(char* p) {
  while (*p != '\0' && isspace(*p) != 0) p++;
  return p;
}


static char* SkipBlackSpace(char* p) {
  while (*p != '\0' && isspace(*p) == 0) p++;
  return p;
}


int FlagList::SetFlagsFromString(const char* str, int len) {
  // make a 0-terminated copy of str
  char* copy0 = NewArray<char>(len + 1);
  memcpy(copy0, str, len);
  copy0[len] = '\0';

  // strip leading white space
  char* copy = SkipWhiteSpace(copy0);

  // count the number of 'arguments'
  int argc = 1;  // be compatible with SetFlagsFromCommandLine()
  for (char* p = copy; *p != '\0'; argc++) {
    p = SkipBlackSpace(p);
    p = SkipWhiteSpace(p);
  }

  // allocate argument array
  char** argv = NewArray<char*>(argc);

  // split the flags string into arguments
  argc = 1;  // be compatible with SetFlagsFromCommandLine()
  for (char* p = copy; *p != '\0'; argc++) {
    argv[argc] = p;
    p = SkipBlackSpace(p);
    if (*p != '\0') *p++ = '\0';  // 0-terminate argument
    p = SkipWhiteSpace(p);
  }

  // set the flags
  int result = SetFlagsFromCommandLine(&argc, argv, false);

  // cleanup
  DeleteArray(argv);
  // don't delete copy0 since the substrings
  // may be pointed to by FLAG variables!
  // (this is a memory leak, but it's minor since this
  // code is only used for debugging, or perhaps once
  // during initialization).

  return result;
}


void FlagList::Register(Flag* flag) {
  ASSERT(flag != NULL && strlen(flag->name()) > 0);
  if (Lookup(flag->name()) != NULL)
    V8_Fatal(flag->file(), 0, "flag %s declared twice", flag->name());
  flag->next_ = list_;
  list_ = flag;
}

} }  // namespace v8::internal
