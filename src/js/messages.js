// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// -------------------------------------------------------------------

(function(global, utils) {

%CheckIsBootstrapping();

// -------------------------------------------------------------------
// Imports

var ArrayJoin;
var Bool16x8ToString;
var Bool32x4ToString;
var Bool8x16ToString;
var CallSite = utils.ImportNow("CallSite");
var callSiteConstructorSymbol =
    utils.ImportNow("call_site_constructor_symbol");
var callSiteReceiverSymbol =
    utils.ImportNow("call_site_receiver_symbol");
var callSiteFunctionSymbol =
    utils.ImportNow("call_site_function_symbol");
var callSitePositionSymbol =
    utils.ImportNow("call_site_position_symbol");
var callSiteStrictSymbol =
    utils.ImportNow("call_site_strict_symbol");
var callSiteWasmObjectSymbol =
    utils.ImportNow("call_site_wasm_obj_symbol");
var callSiteWasmFunctionIndexSymbol =
    utils.ImportNow("call_site_wasm_func_index_symbol");
var Float32x4ToString;
var GlobalObject = global.Object;
var GlobalError = global.Error;
var GlobalEvalError = global.EvalError;
var GlobalRangeError = global.RangeError;
var GlobalReferenceError = global.ReferenceError;
var GlobalSyntaxError = global.SyntaxError;
var GlobalTypeError = global.TypeError;
var GlobalURIError = global.URIError;
var Int16x8ToString;
var Int32x4ToString;
var Int8x16ToString;
var InternalArray = utils.InternalArray;
var internalErrorSymbol = utils.ImportNow("internal_error_symbol");
var ObjectHasOwnProperty;
var ObjectToString = utils.ImportNow("object_to_string");
var Script = utils.ImportNow("Script");
var stackTraceSymbol = utils.ImportNow("stack_trace_symbol");
var StringIndexOf;
var toStringTagSymbol = utils.ImportNow("to_string_tag_symbol");
var Uint16x8ToString;
var Uint32x4ToString;
var Uint8x16ToString;

utils.Import(function(from) {
  ArrayJoin = from.ArrayJoin;
  Bool16x8ToString = from.Bool16x8ToString;
  Bool32x4ToString = from.Bool32x4ToString;
  Bool8x16ToString = from.Bool8x16ToString;
  Float32x4ToString = from.Float32x4ToString;
  Int16x8ToString = from.Int16x8ToString;
  Int32x4ToString = from.Int32x4ToString;
  Int8x16ToString = from.Int8x16ToString;
  ObjectHasOwnProperty = from.ObjectHasOwnProperty;
  StringIndexOf = from.StringIndexOf;
  Uint16x8ToString = from.Uint16x8ToString;
  Uint32x4ToString = from.Uint32x4ToString;
  Uint8x16ToString = from.Uint8x16ToString;
});

// -------------------------------------------------------------------



function NoSideEffectsObjectToString() {
  if (IS_UNDEFINED(this)) return "[object Undefined]";
  if (IS_NULL(this)) return "[object Null]";
  var O = TO_OBJECT(this);
  var builtinTag = %_ClassOf(O);
  var tag = %GetDataProperty(O, toStringTagSymbol);
  if (!IS_STRING(tag)) {
    tag = builtinTag;
  }
  return `[object ${tag}]`;
}

function IsErrorObject(obj) {
  return HAS_PRIVATE(obj, stackTraceSymbol);
}

function NoSideEffectsErrorToString() {
  var name = %GetDataProperty(this, "name");
  var message = %GetDataProperty(this, "message");
  name = IS_UNDEFINED(name) ? "Error" : NoSideEffectsToString(name);
  message = IS_UNDEFINED(message) ? "" : NoSideEffectsToString(message);
  if (name == "") return message;
  if (message == "") return name;
  return `${name}: ${message}`;
}

function NoSideEffectsToString(obj) {
  if (IS_STRING(obj)) return obj;
  if (IS_NUMBER(obj)) return %_NumberToString(obj);
  if (IS_BOOLEAN(obj)) return obj ? 'true' : 'false';
  if (IS_UNDEFINED(obj)) return 'undefined';
  if (IS_NULL(obj)) return 'null';
  if (IS_FUNCTION(obj)) {
    var str = %FunctionToString(obj);
    if (str.length > 128) {
      str = %_SubString(str, 0, 111) + "...<omitted>..." +
            %_SubString(str, str.length - 2, str.length);
    }
    return str;
  }
  if (IS_SYMBOL(obj)) return %SymbolDescriptiveString(obj);
  if (IS_SIMD_VALUE(obj)) {
    switch (typeof(obj)) {
      case 'float32x4': return %_Call(Float32x4ToString, obj);
      case 'int32x4':   return %_Call(Int32x4ToString, obj);
      case 'int16x8':   return %_Call(Int16x8ToString, obj);
      case 'int8x16':   return %_Call(Int8x16ToString, obj);
      case 'uint32x4':   return %_Call(Uint32x4ToString, obj);
      case 'uint16x8':   return %_Call(Uint16x8ToString, obj);
      case 'uint8x16':   return %_Call(Uint8x16ToString, obj);
      case 'bool32x4':  return %_Call(Bool32x4ToString, obj);
      case 'bool16x8':  return %_Call(Bool16x8ToString, obj);
      case 'bool8x16':  return %_Call(Bool8x16ToString, obj);
    }
  }

  if (IS_RECEIVER(obj)) {
    // When internally formatting error objects, use a side-effects-free version
    // of Error.prototype.toString independent of the actually installed
    // toString method.
    if (IsErrorObject(obj) ||
        %GetDataProperty(obj, "toString") === ErrorToString) {
      return %_Call(NoSideEffectsErrorToString, obj);
    }

    if (%GetDataProperty(obj, "toString") === ObjectToString) {
      var constructor = %GetDataProperty(obj, "constructor");
      if (IS_FUNCTION(constructor)) {
        var constructor_name = %FunctionGetName(constructor);
        if (constructor_name != "") return `#<${constructor_name}>`;
      }
    }
  }

  return %_Call(NoSideEffectsObjectToString, obj);
}


function MakeGenericError(constructor, type, arg0, arg1, arg2) {
  var error = new constructor(FormatMessage(type, arg0, arg1, arg2));
  error[internalErrorSymbol] = true;
  return error;
}


/**
 * Set up the Script function and constructor.
 */
%FunctionSetInstanceClassName(Script, 'Script');
%AddNamedProperty(Script.prototype, 'constructor', Script,
                  DONT_ENUM | DONT_DELETE | READ_ONLY);
%SetCode(Script, function(x) {
  // Script objects can only be created by the VM.
  throw MakeError(kUnsupported);
});


// Helper functions; called from the runtime system.
function FormatMessage(type, arg0, arg1, arg2) {
  var arg0 = NoSideEffectsToString(arg0);
  var arg1 = NoSideEffectsToString(arg1);
  var arg2 = NoSideEffectsToString(arg2);
  try {
    return %FormatMessageString(type, arg0, arg1, arg2);
  } catch (e) {
    return "<error>";
  }
}


function GetLineNumber(message) {
  var start_position = %MessageGetStartPosition(message);
  if (start_position == -1) return kNoLineNumberInfo;
  var script = %MessageGetScript(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return kNoLineNumberInfo;
  return location.line + 1;
}


//Returns the offset of the given position within the containing line.
function GetColumnNumber(message) {
  var script = %MessageGetScript(message);
  var start_position = %MessageGetStartPosition(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return -1;
  return location.column;
}


// Returns the source code line containing the given source
// position, or the empty string if the position is invalid.
function GetSourceLine(message) {
  var script = %MessageGetScript(message);
  var start_position = %MessageGetStartPosition(message);
  var location = script.locationFromPosition(start_position, true);
  if (location == null) return "";
  return location.sourceText;
}


/**
 * Get information on a specific source position.
 * Returns an object with the following following properties:
 *   script     : script object for the source
 *   line       : source line number
 *   column     : source column within the line
 *   position   : position within the source
 *   sourceText : a string containing the current line
 * @param {number} position The source position
 * @param {boolean} include_resource_offset Set to true to have the resource
 *     offset added to the location
 * @return If line is negative or not in the source null is returned.
 */
function ScriptLocationFromPosition(position,
                                    include_resource_offset) {
  return %ScriptPositionInfo(this, position, !!include_resource_offset);
}


/**
 * If sourceURL comment is available returns sourceURL comment contents.
 * Otherwise, script name is returned. See
 * http://fbug.googlecode.com/svn/branches/firebug1.1/docs/ReleaseNotes_1.1.txt
 * and Source Map Revision 3 proposal for details on using //# sourceURL and
 * deprecated //@ sourceURL comment to identify scripts that don't have name.
 *
 * @return {?string} script name if present, value for //# sourceURL comment or
 * deprecated //@ sourceURL comment otherwise.
 */
function ScriptNameOrSourceURL() {
  // Keep in sync with Script::GetNameOrSourceURL.
  if (this.source_url) return this.source_url;
  return this.name;
}


utils.SetUpLockedPrototype(Script, [
    "source",
    "name",
    "source_url",
    "source_mapping_url",
    "line_offset",
    "column_offset"
  ], [
    "locationFromPosition", ScriptLocationFromPosition,
    "nameOrSourceURL", ScriptNameOrSourceURL,
  ]
);


function GetStackTraceLine(recv, fun, pos, isGlobal) {
  return new CallSite(recv, fun, pos, false).toString();
}

// ----------------------------------------------------------------------------
// Error implementation

function CallSiteToString() {
  if (HAS_PRIVATE(this, callSiteWasmObjectSymbol)) {
    var funName = this.getFunctionName();
    var funcIndex = GET_PRIVATE(this, callSiteWasmFunctionIndexSymbol);
    var pos = this.getPosition();
    if (IS_NULL(funName)) funName = "<WASM UNNAMED>";
    return funName + " (<WASM>[" + funcIndex + "]+" + pos + ")";
  }

  var fileName;
  var fileLocation = "";
  if (this.isNative()) {
    fileLocation = "native";
  } else {
    fileName = this.getScriptNameOrSourceURL();
    if (!fileName && this.isEval()) {
      fileLocation = this.getEvalOrigin();
      fileLocation += ", ";  // Expecting source position to follow.
    }

    if (fileName) {
      fileLocation += fileName;
    } else {
      // Source code does not originate from a file and is not native, but we
      // can still get the source position inside the source string, e.g. in
      // an eval string.
      fileLocation += "<anonymous>";
    }
    var lineNumber = this.getLineNumber();
    if (lineNumber != null) {
      fileLocation += ":" + lineNumber;
      var columnNumber = this.getColumnNumber();
      if (columnNumber) {
        fileLocation += ":" + columnNumber;
      }
    }
  }

  var line = "";
  var functionName = this.getFunctionName();
  var addSuffix = true;
  var isConstructor = this.isConstructor();
  var isMethodCall = !(this.isToplevel() || isConstructor);
  if (isMethodCall) {
    var typeName = GetTypeName(GET_PRIVATE(this, callSiteReceiverSymbol), true);
    var methodName = this.getMethodName();
    if (functionName) {
      if (typeName && %_Call(StringIndexOf, functionName, typeName) != 0) {
        line += typeName + ".";
      }
      line += functionName;
      if (methodName &&
          (%_Call(StringIndexOf, functionName, "." + methodName) !=
           functionName.length - methodName.length - 1)) {
        line += " [as " + methodName + "]";
      }
    } else {
      line += typeName + "." + (methodName || "<anonymous>");
    }
  } else if (isConstructor) {
    line += "new " + (functionName || "<anonymous>");
  } else if (functionName) {
    line += functionName;
  } else {
    line += fileLocation;
    addSuffix = false;
  }
  if (addSuffix) {
    line += " (" + fileLocation + ")";
  }
  return line;
}

%AddNamedProperty(CallSite.prototype, "toString", CallSiteToString,
    DONT_ENUM | DONT_DELETE | READ_ONLY);
%SetNativeFlag(CallSiteToString);


function FormatErrorString(error) {
  try {
    return %_Call(ErrorToString, error);
  } catch (e) {
    try {
      return "<error: " + e + ">";
    } catch (ee) {
      return "<error>";
    }
  }
}


function GetStackFrames(raw_stack) {
  var internal_raw_stack = new InternalArray();
  %MoveArrayContents(raw_stack, internal_raw_stack);
  var frames = new InternalArray();
  var sloppy_frames = internal_raw_stack[0];
  for (var i = 1; i < internal_raw_stack.length; i += 4) {
    var recv = internal_raw_stack[i];
    var fun = internal_raw_stack[i + 1];
    var code = internal_raw_stack[i + 2];
    var pc = internal_raw_stack[i + 3];
    // For traps in wasm, the bytecode offset is passed as (-1 - offset).
    // Otherwise, lookup the position from the pc.
    var pos = IS_NUMBER(fun) && pc < 0 ? (-1 - pc) :
      %FunctionGetPositionForOffset(code, pc);
    sloppy_frames--;
    frames.push(new CallSite(recv, fun, pos, (sloppy_frames < 0)));
  }
  return frames;
}


// Flag to prevent recursive call of Error.prepareStackTrace.
var formatting_custom_stack_trace = false;


function FormatStackTrace(obj, raw_stack) {
  var frames = GetStackFrames(raw_stack);
  if (IS_FUNCTION(GlobalError.prepareStackTrace) &&
      !formatting_custom_stack_trace) {
    var array = [];
    %MoveArrayContents(frames, array);
    formatting_custom_stack_trace = true;
    var stack_trace = UNDEFINED;
    try {
      stack_trace = GlobalError.prepareStackTrace(obj, array);
    } catch (e) {
      throw e;  // The custom formatting function threw.  Rethrow.
    } finally {
      formatting_custom_stack_trace = false;
    }
    return stack_trace;
  }

  var lines = new InternalArray();
  lines.push(FormatErrorString(obj));
  for (var i = 0; i < frames.length; i++) {
    var frame = frames[i];
    var line;
    try {
      line = frame.toString();
    } catch (e) {
      try {
        line = "<error: " + e + ">";
      } catch (ee) {
        // Any code that reaches this point is seriously nasty!
        line = "<error>";
      }
    }
    lines.push("    at " + line);
  }
  return %_Call(ArrayJoin, lines, "\n");
}


function GetTypeName(receiver, requireConstructor) {
  if (IS_NULL_OR_UNDEFINED(receiver)) return null;
  if (IS_PROXY(receiver)) return "Proxy";
  return %GetConstructorName(receiver);
}

function ErrorToString() {
  if (!IS_RECEIVER(this)) {
    throw MakeTypeError(kCalledOnNonObject, "Error.prototype.toString");
  }

  var name = this.name;
  name = IS_UNDEFINED(name) ? "Error" : TO_STRING(name);

  var message = this.message;
  message = IS_UNDEFINED(message) ? "" : TO_STRING(message);

  if (name == "") return message;
  if (message == "") return name;
  return `${name}: ${message}`
}

function MakeError(type, arg0, arg1, arg2) {
  return MakeGenericError(GlobalError, type, arg0, arg1, arg2);
}

function MakeRangeError(type, arg0, arg1, arg2) {
  return MakeGenericError(GlobalRangeError, type, arg0, arg1, arg2);
}

function MakeSyntaxError(type, arg0, arg1, arg2) {
  return MakeGenericError(GlobalSyntaxError, type, arg0, arg1, arg2);
}

function MakeTypeError(type, arg0, arg1, arg2) {
  return MakeGenericError(GlobalTypeError, type, arg0, arg1, arg2);
}

function MakeURIError() {
  return MakeGenericError(GlobalURIError, kURIMalformed);
}

%InstallToContext([
  "error_format_stack_trace", FormatStackTrace,
  "get_stack_trace_line_fun", GetStackTraceLine,
  "make_error_function", MakeGenericError,
  "make_range_error", MakeRangeError,
  "make_type_error", MakeTypeError,
  "message_get_column_number", GetColumnNumber,
  "message_get_line_number", GetLineNumber,
  "message_get_source_line", GetSourceLine,
  "no_side_effects_to_string_fun", NoSideEffectsToString,
]);

utils.Export(function(to) {
  to.ErrorToString = ErrorToString;
  to.MakeError = MakeError;
  to.MakeRangeError = MakeRangeError;
  to.MakeSyntaxError = MakeSyntaxError;
  to.MakeTypeError = MakeTypeError;
  to.MakeURIError = MakeURIError;
});

});
