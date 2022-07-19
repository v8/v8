// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export interface ClearableHandler {
  brokeredClear(): void;
}

export interface SelectionHandler {
  select(nodeIds: any, selected: any): void;
  clear(): void;
  brokeredSourcePositionSelect(sourcePositions: any, selected: any): void;
}

export interface NodeSelectionHandler {
  select(nodeIds: any, selected: any): void;
  clear(): void;
  brokeredNodeSelect(nodeIds: any, selected: any): void;
}

export interface BlockSelectionHandler {
  select(nodeIds: any, selected: any): void;
  clear(): void;
  brokeredBlockSelect(blockIds: any, selected: any): void;
}

export interface InstructionSelectionHandler {
  select(instructionIds: any, selected: any): void;
  clear(): void;
  brokeredInstructionSelect(instructionIds: any, selected: any): void;
}

export interface RegisterAllocationSelectionHandler {
  // These are called instructionIds since the class of the divs is "instruction-id"
  select(instructionIds: any, selected: any): void;
  clear(): void;
  brokeredRegisterAllocationSelect(instructionIds: any, selected: any): void;
}
