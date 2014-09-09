#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import js2c
import os
import re
import sys

FILENAME = "src/runtime.cc"
FUNCTION = re.compile("^RUNTIME_FUNCTION\(Runtime_(\w+)")
FUNCTIONEND = "}\n"
MACRO = re.compile(r"^#define ([^ ]+)\(([^)]*)\) *([^\\]*)\\?\n$")
FIRST_WORD = re.compile("^\s*(.*?)[\s({\[]")

# Expand these macros, they define further runtime functions.
EXPAND_MACROS = [
  "BUFFER_VIEW_GETTER",
  "DATA_VIEW_GETTER",
  "DATA_VIEW_SETTER",
  "ELEMENTS_KIND_CHECK_RUNTIME_FUNCTION",
  "FIXED_TYPED_ARRAYS_CHECK_RUNTIME_FUNCTION",
  "RUNTIME_UNARY_MATH",
  "TYPED_ARRAYS_CHECK_RUNTIME_FUNCTION",
]


class Function(object):
  def __init__(self, match):
    self.name = match.group(1)


class Macro(object):
  def __init__(self, match):
    self.name = match.group(1)
    self.args = [s.strip() for s in match.group(2).split(",")]
    self.lines = []
    self.indentation = 0
    self.AddLine(match.group(3))

  def AddLine(self, line):
    if not line: return
    if not self.lines:
      # This is the first line, detect indentation.
      self.indentation = len(line) - len(line.lstrip())
    line = line.rstrip("\\\n ")
    if not line: return
    assert len(line[:self.indentation].strip()) == 0, \
        ("expected whitespace: '%s', full line: '%s'" %
         (line[:self.indentation], line))
    line = line[self.indentation:]
    if not line: return
    self.lines.append(line + "\n")

  def Finalize(self):
    for arg in self.args:
      pattern = re.compile(r"(##|\b)%s(##|\b)" % arg)
      for i in range(len(self.lines)):
        self.lines[i] = re.sub(pattern, "%%(%s)s" % arg, self.lines[i])

  def FillIn(self, arg_values):
    filler = {}
    assert len(arg_values) == len(self.args)
    for i in range(len(self.args)):
      filler[self.args[i]] = arg_values[i]
    result = []
    for line in self.lines:
      result.append(line % filler)
    return result


def ReadFileAndExpandMacros(filename):
  found_macros = {}
  expanded_lines = []
  with open(filename, "r") as f:
    found_macro = None
    for line in f:
      if found_macro is not None:
        found_macro.AddLine(line)
        if not line.endswith("\\\n"):
          found_macro.Finalize()
          found_macro = None
        continue

      match = MACRO.match(line)
      if match:
        found_macro = Macro(match)
        if found_macro.name in EXPAND_MACROS:
          found_macros[found_macro.name] = found_macro
        else:
          found_macro = None
        continue

      match = FIRST_WORD.match(line)
      if match:
        first_word = match.group(1)
        if first_word in found_macros:
          MACRO_CALL = re.compile("%s\(([^)]*)\)" % first_word)
          match = MACRO_CALL.match(line)
          assert match
          args = [s.strip() for s in match.group(1).split(",")]
          expanded_lines += found_macros[first_word].FillIn(args)
          continue

      expanded_lines.append(line)
  return expanded_lines


# Detects runtime functions by parsing FILENAME.
def FindRuntimeFunctions():
  functions = []
  expanded_lines = ReadFileAndExpandMacros(FILENAME)
  function = None
  partial_line = ""
  for line in expanded_lines:
    # Multi-line definition support, ignoring macros.
    if line.startswith("RUNTIME_FUNCTION") and not line.endswith("{\n"):
      if line.endswith("\\\n"): continue
      partial_line = line.rstrip()
      continue
    if partial_line:
      partial_line += " " + line.strip()
      if partial_line.endswith("{"):
        line = partial_line
        partial_line = ""
      else:
        continue

    match = FUNCTION.match(line)
    if match:
      function = Function(match)
      continue
    if function is None: continue

    if line == FUNCTIONEND:
      if function is not None:
        functions.append(function)
        function = None
  return functions


class Builtin(object):
  def __init__(self, match):
    self.name = match.group(1)


def FindJSNatives():
  PATH = "src"
  fileslist = []
  for (root, dirs, files) in os.walk(PATH):
    for f in files:
      if f.endswith(".js"):
        fileslist.append(os.path.join(root, f))
  natives = []
  regexp = re.compile("^function (\w+)\s*\((.*?)\) {")
  matches = 0
  for filename in fileslist:
    with open(filename, "r") as f:
      file_contents = f.read()
    file_contents = js2c.ExpandInlineMacros(file_contents)
    lines = file_contents.split("\n")
    partial_line = ""
    for line in lines:
      if line.startswith("function") and not '{' in line:
        partial_line += line.rstrip()
        continue
      if partial_line:
        partial_line += " " + line.strip()
        if '{' in line:
          line = partial_line
          partial_line = ""
        else:
          continue
      match = regexp.match(line)
      if match:
        natives.append(Builtin(match))
  return natives


def Main():
  functions = FindRuntimeFunctions()
  natives = FindJSNatives()
  errors = 0
  runtime_map = {}
  for f in functions:
    runtime_map[f.name] = 1
  for b in natives:
    if b.name in runtime_map:
      print("JS_Native/Runtime_Function name clash: %s" % b.name)
      errors += 1

  if errors > 0:
    return 1
  print("Runtime/Natives name clashes: checked %d/%d functions, all good." %
        (len(functions), len(natives)))
  return 0


if __name__ == "__main__":
  sys.exit(Main())
