// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BYTECODE_ANALYSIS_H_
#define V8_COMPILER_BYTECODE_ANALYSIS_H_

#include "src/base/hashmap.h"
#include "src/bit-vector.h"
#include "src/compiler/bytecode-liveness-map.h"
#include "src/handles.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace compiler {

class V8_EXPORT_PRIVATE BytecodeAnalysis BASE_EMBEDDED {
 public:
  BytecodeAnalysis(Handle<BytecodeArray> bytecode_array, Zone* zone,
                   bool do_liveness_analysis);

  // Analyze the bytecodes to find the loop ranges and nesting. No other
  // methods in this class return valid information until this has been called.
  void Analyze();

  // Return true if the given offset is a loop header
  bool IsLoopHeader(int offset) const;
  // Get the loop header offset of the containing loop for arbitrary
  // {offset}, or -1 if the {offset} is not inside any loop.
  int GetLoopOffsetFor(int offset) const;
  // Gets the loop header offset of the parent loop of the loop header
  // at {header_offset}, or -1 for outer-most loops.
  int GetParentLoopFor(int header_offset) const;

  // Gets the in-liveness for the bytecode at {offset}. The liveness bit vector
  // represents the liveness of the registers and the accumulator, with the last
  // bit being the accumulator liveness bit, and so is (register count + 1) bits
  // long.
  const BitVector* GetInLivenessFor(int offset) const;

  // Gets the out-liveness for the bytecode at {offset}. The liveness bit vector
  // represents the liveness of the registers and the accumulator, with the last
  // bit being the accumulator liveness bit, and so is (register count + 1) bits
  // long.
  const BitVector* GetOutLivenessFor(int offset) const;

  std::ostream& PrintLivenessTo(std::ostream& os) const;

 private:
  void PushLoop(int loop_header, int loop_end);

#if DEBUG
  bool LivenessIsValid();
#endif

  Zone* zone() const { return zone_; }
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }

 private:
  Handle<BytecodeArray> bytecode_array_;
  bool do_liveness_analysis_;
  Zone* zone_;

  ZoneStack<int> loop_stack_;

  ZoneMap<int, int> end_to_header_;
  ZoneMap<int, int> header_to_parent_;

  BytecodeLivenessMap liveness_map_;

  DISALLOW_COPY_AND_ASSIGN(BytecodeAnalysis);
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BYTECODE_ANALYSIS_H_
