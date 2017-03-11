// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SSE_INSTR_H_
#define V8_SSE_INSTR_H_

#define SSE2_INSTRUCTION_LIST(V) \
  V(paddd, 66, 0F, FE)           \
  V(psubd, 66, 0F, FA)

#endif  // V8_SSE_INSTR_H_
