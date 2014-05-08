#!/usr/bin/env python
# Copyright 2014 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import re
import shutil
import sys

# TODO(jkummerow): Support DATA_VIEW_{G,S}ETTER in runtime.cc

FILENAME = "src/runtime.cc"
HEADERFILENAME = "src/runtime.h"
FUNCTION = re.compile("^RUNTIME_FUNCTION\(Runtime_(\w+)")
ARGSLENGTH = re.compile(".*ASSERT\(.*args\.length\(\) == (\d+)\);")
FUNCTIONEND = "}\n"

WORKSPACE = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), ".."))
BASEPATH = os.path.join(WORKSPACE, "test", "mjsunit", "runtime-gen")
THIS_SCRIPT = os.path.relpath(sys.argv[0])

# Counts of functions in each detection state. These are used to assert
# that the parser doesn't bit-rot. Change the values as needed when you add,
# remove or change runtime functions, but make sure we don't lose our ability
# to parse them!
EXPECTED_FUNCTION_COUNT = 339
EXPECTED_FUZZABLE_COUNT = 316
EXPECTED_CCTEST_COUNT = 6
EXPECTED_UNKNOWN_COUNT = 5


# Don't call these at all.
BLACKLISTED = [
  "Abort",  # Kills the process.
  "AbortJS",  # Kills the process.
  "CompileForOnStackReplacement",  # Riddled with ASSERTs.
  "IS_VAR",  # Not implemented in the runtime.
  "ListNatives",  # Not available in Release mode.
  "SetAllocationTimeout",  # Too slow for fuzzing.
  "SystemBreak",  # Kills (int3) the process.

  # These are weird. They violate some invariants when called after
  # bootstrapping.
  "DisableAccessChecks",
  "EnableAccessChecks",

  # Seems to be incompatible with --stress-runs.
  "LiveEditReplaceScript",

  # TODO(jkummerow): Fix these and un-blacklist them!
  "CreateDateTimeFormat",
  "CreateNumberFormat",
]


# These will always throw.
THROWS = [
  "CheckExecutionState",  # Needs to hit a break point.
  "CheckIsBootstrapping",  # Needs to be bootstrapping.
  "DebugEvaluate",  # Needs to hit a break point.
  "DebugEvaluateGlobal",  # Needs to hit a break point.
  "DebugIndexedInterceptorElementValue",  # Needs an indexed interceptor.
  "DebugNamedInterceptorPropertyValue",  # Needs a named interceptor.
  "DebugSetScriptSource",  # Checks compilation state of script.
  "GetAllScopesDetails",  # Needs to hit a break point.
  "GetFrameCount",  # Needs to hit a break point.
  "GetFrameDetails",  # Needs to hit a break point.
  "GetRootNaN",  # Needs to be bootstrapping.
  "GetScopeCount",  # Needs to hit a break point.
  "GetScopeDetails",  # Needs to hit a break point.
  "GetStepInPositions",  # Needs to hit a break point.
  "GetTemplateField",  # Needs a {Function,Object}TemplateInfo.
  "GetThreadCount",  # Needs to hit a break point.
  "GetThreadDetails",  # Needs to hit a break point.
  "IsAccessAllowedForObserver",  # Needs access-check-required object.
  "LiveEditFunctionSourceUpdated",  # Needs a SharedFunctionInfo.
  "LiveEditPatchFunctionPositions",  # Needs a SharedFunctionInfo.
  "LiveEditReplaceFunctionCode",  # Needs a SharedFunctionInfo.
  "LiveEditReplaceRefToNestedFunction",  # Needs a SharedFunctionInfo.
  "LiveEditRestartFrame",  # Needs to hit a break point.
  "UnblockConcurrentRecompilation"  # Needs --block-concurrent-recompilation.
]


# Definitions used in CUSTOM_KNOWN_GOOD_INPUT below.
_BREAK_ITERATOR = (
    "%GetImplFromInitializedIntlObject(new Intl.v8BreakIterator())")
_COLLATOR = "%GetImplFromInitializedIntlObject(new Intl.Collator('en-US'))"
_DATETIME_FORMAT = (
    "%GetImplFromInitializedIntlObject(new Intl.DateTimeFormat('en-US'))")
_NUMBER_FORMAT = (
    "%GetImplFromInitializedIntlObject(new Intl.NumberFormat('en-US'))")
_SCRIPT = "%DebugGetLoadedScripts()[1]"


# Custom definitions for function input that does not throw.
# Format: "FunctionName": ["arg0", "arg1", ..., argslength].
# None means "fall back to autodetected value".
CUSTOM_KNOWN_GOOD_INPUT = {
  "Apply": ["function() {}", None, None, None, None, None],
  "ArrayBufferSliceImpl": [None, None, 0, None],
  "ArrayConcat": ["[1, 'a']", None],
  "BreakIteratorAdoptText": [_BREAK_ITERATOR, None, None],
  "BreakIteratorBreakType": [_BREAK_ITERATOR, None],
  "BreakIteratorCurrent": [_BREAK_ITERATOR, None],
  "BreakIteratorFirst": [_BREAK_ITERATOR, None],
  "BreakIteratorNext": [_BREAK_ITERATOR, None],
  "CompileString": [None, "false", None],
  "CreateBreakIterator": ["'en-US'", "{type: 'string'}", None, None],
  "CreateJSFunctionProxy": [None, "function() {}", None, None, None],
  "CreatePrivateSymbol": ["\"foo\"", None],
  "CreateSymbol": ["\"foo\"", None],
  "DateParseString": [None, "new Array(8)", None],
  "DebugSetScriptSource": [_SCRIPT, None, None],
  "DefineOrRedefineAccessorProperty": [None, None, "function() {}",
                                       "function() {}", 2, None],
  "GetBreakLocations": [None, 0, None],
  "GetDefaultReceiver": ["function() {}", None],
  "GetImplFromInitializedIntlObject": ["new Intl.NumberFormat('en-US')", None],
  "InternalCompare": [_COLLATOR, None, None, None],
  "InternalDateFormat": [_DATETIME_FORMAT, None, None],
  "InternalDateParse": [_DATETIME_FORMAT, None, None],
  "InternalNumberFormat": [_NUMBER_FORMAT, None, None],
  "InternalNumberParse": [_NUMBER_FORMAT, None, None],
  "IsSloppyModeFunction": ["function() {}", None],
  "LiveEditFindSharedFunctionInfosForScript": [_SCRIPT, None],
  "LiveEditGatherCompileInfo": [_SCRIPT, None, None],
  "LoadMutableDouble": ["{foo: 1.2}", None, None],
  "NewObjectFromBound": ["(function() {}).bind({})", None],
  "NumberToRadixString": [None, "2", None],
  "ParseJson": ["\"{}\"", 1],
  "RegExpExecMultiple": [None, None, "['a']", "['a']", None],
  "SetAccessorProperty": [None, None, "undefined", "undefined", None, None,
                          None],
  "SetCreateIterator": [None, "2", None],
  "SetDebugEventListener": ["undefined", None, None],
  "SetFunctionBreakPoint": [None, 200, None, None],
  "SetScriptBreakPoint": [_SCRIPT, None, 0, None, None],
  "StringBuilderConcat": ["[1, 2, 3]", 3, None, None],
  "StringBuilderJoin": ["['a', 'b']", 4, None, None],
  "StringMatch": [None, None, "['a', 'b']", None],
  "StringNormalize": [None, 2, None],
  "StringReplaceGlobalRegExpWithString": [None, None, None, "['a']", None],
  "TypedArrayInitialize": [None, 6, "new ArrayBuffer(8)", None, 4, None],
  "TypedArrayInitializeFromArrayLike": [None, 6, None, None, None],
  "TypedArraySetFastCases": [None, None, "0", None],
}


# Types of arguments that cannot be generated in a JavaScript testcase.
NON_JS_TYPES = [
  "Code", "Context", "FixedArray", "FunctionTemplateInfo",
  "JSFunctionResultCache", "JSMessageObject", "Map", "ScopeInfo",
  "SharedFunctionInfo"]


# Maps argument types to concrete example inputs of that type.
JS_TYPE_GENERATORS = {
  "Boolean": "true",
  "HeapObject": "new Object()",
  "Int32": "32",
  "JSArray": "new Array()",
  "JSArrayBuffer": "new ArrayBuffer(8)",
  "JSDataView": "new DataView(new ArrayBuffer(8))",
  "JSDate": "new Date()",
  "JSFunction": "function() {}",
  "JSFunctionProxy": "Proxy.createFunction({}, function() {})",
  "JSGeneratorObject": "(function*(){ yield 1; })()",
  "JSMap": "new Map()",
  "JSMapIterator": "%MapCreateIterator(new Map(), 3)",
  "JSObject": "new Object()",
  "JSProxy": "Proxy.create({})",
  "JSReceiver": "new Object()",
  "JSRegExp": "/ab/g",
  "JSSet": "new Set()",
  "JSSetIterator": "%SetCreateIterator(new Set(), 2)",
  "JSTypedArray": "new Int32Array(2)",
  "JSValue": "new String('foo')",
  "JSWeakCollection": "new WeakMap()",
  "Name": "\"name\"",
  "Number": "1.5",
  "Object": "new Object()",
  "PropertyDetails": "513",
  "SeqString": "\"seqstring\"",
  "Smi": 1,
  "StrictMode": "1",
  "String": "\"foo\"",
  "Symbol": "Symbol(\"symbol\")",
  "Uint32": "32",
}


class ArgParser(object):
  def __init__(self, regex, ctor):
    self.regex = regex
    self.ArgCtor = ctor


class Arg(object):
  def __init__(self, typename, varname, index):
    self.type = typename
    self.name = "_%s" % varname
    self.index = index


class Function(object):
  def __init__(self, match):
    self.name = match.group(1)
    self.argslength = -1
    self.args = {}
    self.inline = ""

  handle_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_ARG_HANDLE_CHECKED\((\w+), (\w+), (\d+)\)"),
      lambda match: Arg(match.group(1), match.group(2), int(match.group(3))))

  plain_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_ARG_CHECKED\((\w+), (\w+), (\d+)\)"),
      lambda match: Arg(match.group(1), match.group(2), int(match.group(3))))

  number_handle_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_NUMBER_ARG_HANDLE_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("Number", match.group(1), int(match.group(2))))

  smi_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_SMI_ARG_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("Smi", match.group(1), int(match.group(2))))

  double_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_DOUBLE_ARG_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("Number", match.group(1), int(match.group(2))))

  number_arg_parser = ArgParser(
      re.compile(
          "^\s*CONVERT_NUMBER_CHECKED\(\w+, (\w+), (\w+), args\[(\d+)\]\)"),
      lambda match: Arg(match.group(2), match.group(1), int(match.group(3))))

  strict_mode_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_STRICT_MODE_ARG_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("StrictMode", match.group(1), int(match.group(2))))

  boolean_arg_parser = ArgParser(
      re.compile("^\s*CONVERT_BOOLEAN_ARG_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("Boolean", match.group(1), int(match.group(2))))

  property_details_parser = ArgParser(
      re.compile("^\s*CONVERT_PROPERTY_DETAILS_CHECKED\((\w+), (\d+)\)"),
      lambda match: Arg("PropertyDetails", match.group(1), int(match.group(2))))

  arg_parsers = [handle_arg_parser, plain_arg_parser, number_handle_arg_parser,
                 smi_arg_parser,
                 double_arg_parser, number_arg_parser, strict_mode_arg_parser,
                 boolean_arg_parser, property_details_parser]


  def SetArgsLength(self, match):
    self.argslength = int(match.group(1))

  def TryParseArg(self, line):
    for parser in Function.arg_parsers:
      match = parser.regex.match(line)
      if match:
        arg = parser.ArgCtor(match)
        self.args[arg.index] = arg
        return True
    return False

  def Filename(self):
    return "%s.js" % self.name.lower()

  def __str__(self):
    s = [self.name, "("]
    argcount = self.argslength
    if argcount < 0:
      print("WARNING: unknown argslength for function %s" % self.name)
      if self.args:
        argcount = max([self.args[i].index + 1 for i in self.args])
      else:
        argcount = 0
    for i in range(argcount):
      if i > 0: s.append(", ")
      s.append(self.args[i].type if i in self.args else "<unknown>")
    s.append(")")
    return "".join(s)

# Parses HEADERFILENAME to find out which runtime functions are "inline".
def FindInlineRuntimeFunctions():
  inline_functions = []
  with open(HEADERFILENAME, "r") as f:
    inline_list = "#define INLINE_FUNCTION_LIST(F) \\\n"
    inline_opt_list = "#define INLINE_OPTIMIZED_FUNCTION_LIST(F) \\\n"
    inline_function = re.compile(r"^\s*F\((\w+), \d+, \d+\)\s*\\?")
    mode = "SEARCHING"
    for line in f:
      if mode == "ACTIVE":
        match = inline_function.match(line)
        if match:
          inline_functions.append(match.group(1))
        if not line.endswith("\\\n"):
          mode = "SEARCHING"
      elif mode == "SEARCHING":
        if line == inline_list or line == inline_opt_list:
          mode = "ACTIVE"
  return inline_functions


# Detects runtime functions by parsing FILENAME.
def FindRuntimeFunctions():
  inline_functions = FindInlineRuntimeFunctions()
  functions = []
  with open(FILENAME, "r") as f:
    function = None
    partial_line = ""
    for line in f:
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
        if function.name in inline_functions:
          function.inline = "_"
        continue
      if function is None: continue

      match = ARGSLENGTH.match(line)
      if match:
        function.SetArgsLength(match)
        continue

      if function.TryParseArg(line):
        continue

      if line == FUNCTIONEND:
        if function is not None:
          functions.append(function)
          function = None
  return functions

# Classifies runtime functions.
def ClassifyFunctions(functions):
  # Can be fuzzed with a JavaScript testcase.
  js_fuzzable_functions = []
  # We have enough information to fuzz these, but they need inputs that
  # cannot be created or passed around in JavaScript.
  cctest_fuzzable_functions = []
  # This script does not have enough information about these.
  unknown_functions = []

  types = {}
  for f in functions:
    if f.name in BLACKLISTED:
      continue
    decision = js_fuzzable_functions
    custom = CUSTOM_KNOWN_GOOD_INPUT.get(f.name, None)
    if f.argslength < 0:
      # Unknown length -> give up unless there's a custom definition.
      if custom and custom[-1] is not None:
        f.argslength = custom[-1]
        assert len(custom) == f.argslength + 1, \
            ("%s: last custom definition must be argslength" % f.name)
      else:
        decision = unknown_functions
    else:
      if custom:
        # Any custom definitions must match the known argslength.
        assert len(custom) == f.argslength + 1, \
            ("%s should have %d custom definitions but has %d" %
            (f.name, f.argslength + 1, len(custom)))
      for i in range(f.argslength):
        if custom and custom[i] is not None:
          # All good, there's a custom definition.
          pass
        elif not i in f.args:
          # No custom definition and no parse result -> give up.
          decision = unknown_functions
        else:
          t = f.args[i].type
          if t in NON_JS_TYPES:
            decision = cctest_fuzzable_functions
          else:
            assert t in JS_TYPE_GENERATORS, \
                ("type generator not found for %s, function: %s" % (t, f))
    decision.append(f)
  return (js_fuzzable_functions, cctest_fuzzable_functions, unknown_functions)


def GenerateJSTestcaseForFunction(f):
  s = ["// Copyright 2014 the V8 project authors. All rights reserved.",
       "// AUTO-GENERATED BY tools/generate-runtime-tests.py, DO NOT MODIFY",
       "// Flags: --allow-natives-syntax --harmony"]
  call = "%%%s%s(" % (f.inline, f.name)
  custom = CUSTOM_KNOWN_GOOD_INPUT.get(f.name, None)
  for i in range(f.argslength):
    if custom and custom[i] is not None:
      (name, value) = ("arg%d" % i, custom[i])
    else:
      arg = f.args[i]
      (name, value) = (arg.name, JS_TYPE_GENERATORS[arg.type])
    s.append("var %s = %s;" % (name, value))
    if i > 0: call += ", "
    call += name
  call += ");"
  if f.name in THROWS:
    s.append("try {")
    s.append(call);
    s.append("} catch(e) {}")
  else:
    s.append(call)
  testcase = "\n".join(s)
  path = os.path.join(BASEPATH, f.Filename())
  with open(path, "w") as f:
    f.write("%s\n" % testcase)

def GenerateTestcases(functions):
  shutil.rmtree(BASEPATH)  # Re-generate everything.
  os.makedirs(BASEPATH)
  for f in functions:
    GenerateJSTestcaseForFunction(f)

def PrintUsage():
  print """Usage: %(this_script)s ACTION

where ACTION can be:

info      Print diagnostic info.
check     Check that runtime functions can be parsed as expected, and that
          test cases exist.
generate  Parse source code for runtime functions, and auto-generate
          test cases for them. Warning: this will nuke and re-create
          %(path)s.
""" % {"path": os.path.relpath(BASEPATH), "this_script": THIS_SCRIPT}

if __name__ == "__main__":
  if len(sys.argv) != 2:
    PrintUsage()
    sys.exit(1)
  action = sys.argv[1]
  if action in ["-h", "--help", "help"]:
    PrintUsage()
    sys.exit(0)

  functions = FindRuntimeFunctions()
  (js_fuzzable_functions, cctest_fuzzable_functions, unknown_functions) = \
      ClassifyFunctions(functions)

  if action == "info":
    print("%d functions total; js_fuzzable_functions: %d, "
          "cctest_fuzzable_functions: %d, unknown_functions: %d"
          % (len(functions), len(js_fuzzable_functions),
             len(cctest_fuzzable_functions), len(unknown_functions)))
    print("unknown functions:")
    for f in unknown_functions:
      print(f)
    sys.exit(0)

  if action == "check":
    error = False
    def CheckCount(actual, expected, description):
      global error
      if len(actual) != expected:
        print("Expected to detect %d %s, but found %d." % (
              expected, description, len(actual)))
        print("If this change is intentional, please update the expectations"
              " at the top of %s." % THIS_SCRIPT)
        error = True
    CheckCount(functions, EXPECTED_FUNCTION_COUNT, "functions in total")
    CheckCount(js_fuzzable_functions, EXPECTED_FUZZABLE_COUNT,
               "JavaScript-fuzzable functions")
    CheckCount(cctest_fuzzable_functions, EXPECTED_CCTEST_COUNT,
               "cctest-fuzzable functions")
    CheckCount(unknown_functions, EXPECTED_UNKNOWN_COUNT,
               "functions with incomplete type information")

    def CheckTestcasesExisting(functions):
      global error
      for f in functions:
        if not os.path.isfile(os.path.join(BASEPATH, f.Filename())):
          print("Missing testcase for %s, please run '%s generate'" %
                (f.name, THIS_SCRIPT))
          error = True
      files = filter(lambda filename: not filename.startswith("."),
                     os.listdir(BASEPATH))
      if (len(files) != len(functions)):
        unexpected_files = set(files) - set([f.Filename() for f in functions])
        for f in unexpected_files:
          print("Unexpected testcase: %s" % os.path.join(BASEPATH, f))
          error = True
    CheckTestcasesExisting(js_fuzzable_functions)

    if error:
      sys.exit(1)
    print("Generated runtime tests: all good.")
    sys.exit(0)

  if action == "generate":
    GenerateTestcases(js_fuzzable_functions)
    sys.exit(0)
