// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_DEBUG_OBJECTS_H_
#define V8_OBJECTS_DEBUG_OBJECTS_H_

#include "src/objects.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

// The DebugInfo class holds additional information for a function being
// debugged.
class DebugInfo : public Struct {
 public:
  // The shared function info for the source being debugged.
  DECL_ACCESSORS(shared, SharedFunctionInfo)

  // Bit field containing various information collected for debugging.
  DECL_INT_ACCESSORS(debugger_hints)

  DECL_ACCESSORS(debug_bytecode_array, Object)
  // Fixed array holding status information for each active break point.
  DECL_ACCESSORS(break_points, FixedArray)

  // Check if there is a break point at a source position.
  bool HasBreakPoint(int source_position);
  // Attempt to clear a break point. Return true if successful.
  static bool ClearBreakPoint(Handle<DebugInfo> debug_info,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<DebugInfo> debug_info, int source_position,
                            Handle<Object> break_point_object);
  // Get the break point objects for a source position.
  Handle<Object> GetBreakPointObjects(int source_position);
  // Find the break point info holding this break point object.
  static Handle<Object> FindBreakPointInfo(Handle<DebugInfo> debug_info,
                                           Handle<Object> break_point_object);
  // Get the number of break points for this function.
  int GetBreakPointCount();

  inline bool HasDebugBytecodeArray();
  inline bool HasDebugCode();

  inline BytecodeArray* OriginalBytecodeArray();
  inline BytecodeArray* DebugBytecodeArray();
  inline Code* DebugCode();

  DECLARE_CAST(DebugInfo)

  // Dispatched behavior.
  DECLARE_PRINTER(DebugInfo)
  DECLARE_VERIFIER(DebugInfo)

  static const int kSharedFunctionInfoIndex = Struct::kHeaderSize;
  static const int kDebuggerHintsIndex =
      kSharedFunctionInfoIndex + kPointerSize;
  static const int kDebugBytecodeArrayIndex =
      kDebuggerHintsIndex + kPointerSize;
  static const int kBreakPointsStateIndex =
      kDebugBytecodeArrayIndex + kPointerSize;
  static const int kSize = kBreakPointsStateIndex + kPointerSize;

  static const int kEstimatedNofBreakPointsInFunction = 4;

 private:
  // Get the break point info object for a source position.
  Object* GetBreakPointInfo(int source_position);

  DISALLOW_IMPLICIT_CONSTRUCTORS(DebugInfo);
};

// The BreakPointInfo class holds information for break points set in a
// function. The DebugInfo object holds a BreakPointInfo object for each code
// position with one or more break points.
class BreakPointInfo : public Tuple2 {
 public:
  // The position in the source for the break position.
  DECL_INT_ACCESSORS(source_position)
  // List of related JavaScript break points.
  DECL_ACCESSORS(break_point_objects, Object)

  // Removes a break point.
  static void ClearBreakPoint(Handle<BreakPointInfo> info,
                              Handle<Object> break_point_object);
  // Set a break point.
  static void SetBreakPoint(Handle<BreakPointInfo> info,
                            Handle<Object> break_point_object);
  // Check if break point info has this break point object.
  static bool HasBreakPointObject(Handle<BreakPointInfo> info,
                                  Handle<Object> break_point_object);
  // Get the number of break points for this code offset.
  int GetBreakPointCount();

  int GetStatementPosition(Handle<DebugInfo> debug_info);

  DECLARE_CAST(BreakPointInfo)

  static const int kSourcePositionIndex = kValue1Offset;
  static const int kBreakPointObjectsIndex = kValue2Offset;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(BreakPointInfo);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_DEBUG_OBJECTS_H_
