// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { GenericPosition, SourceResolver } from "../source-resolver";
import {
  ClearableHandler,
  SourcePositionSelectionHandler,
  NodeSelectionHandler,
  BlockSelectionHandler,
  InstructionSelectionHandler,
  RegisterAllocationSelectionHandler
} from "./selection-handler";

export class SelectionBroker {
  sourceResolver: SourceResolver;
  allHandlers: Array<ClearableHandler>;
  nodeHandlers: Array<NodeSelectionHandler>;
  blockHandlers: Array<BlockSelectionHandler>;
  instructionHandlers: Array<InstructionSelectionHandler>;
  sourcePositionHandlers: Array<SourcePositionSelectionHandler>;
  registerAllocationHandlers: Array<RegisterAllocationSelectionHandler>;

  constructor(sourceResolver: SourceResolver) {
    this.sourceResolver = sourceResolver;
    this.allHandlers = new Array<ClearableHandler>();
    this.nodeHandlers = new Array<NodeSelectionHandler>();
    this.blockHandlers = new Array<BlockSelectionHandler>();
    this.instructionHandlers = new Array<InstructionSelectionHandler>();
    this.sourcePositionHandlers = new Array<SourcePositionSelectionHandler>();
    this.registerAllocationHandlers = new Array<RegisterAllocationSelectionHandler>();
  }

  public addNodeHandler(handler: NodeSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.nodeHandlers.push(handler);
  }

  public deleteNodeHandler(handler: NodeSelectionHandler & ClearableHandler): void {
    this.allHandlers = this.allHandlers.filter(h => h != handler);
    this.nodeHandlers = this.nodeHandlers.filter(h => h != handler);
  }

  public addBlockHandler(handler: BlockSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.blockHandlers.push(handler);
  }

  public deleteBlockHandler(handler: BlockSelectionHandler & ClearableHandler): void {
    this.allHandlers = this.allHandlers.filter(h => h != handler);
    this.blockHandlers = this.blockHandlers.filter(h => h != handler);
  }

  public addInstructionHandler(handler: InstructionSelectionHandler & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.instructionHandlers.push(handler);
  }

  public addSourcePositionHandler(handler: SourcePositionSelectionHandler & ClearableHandler):
    void {
    this.allHandlers.push(handler);
    this.sourcePositionHandlers.push(handler);
  }

  public addRegisterAllocatorHandler(handler: RegisterAllocationSelectionHandler
    & ClearableHandler): void {
    this.allHandlers.push(handler);
    this.registerAllocationHandlers.push(handler);
  }

  // TODO (danylo boiko) Add instructionOffsets type
  public broadcastInstructionSelect(from, instructionOffsets, selected: boolean): void {
    // Select the lines from the disassembly (right panel)
    for (const handler of this.instructionHandlers) {
      if (handler != from) handler.brokeredInstructionSelect(instructionOffsets, selected);
    }

    // Select the lines from the source panel (left panel)
    const pcOffsets = this.sourceResolver.instructionsPhase
      .instructionsToKeyPcOffsets(instructionOffsets);

    for (const offset of pcOffsets) {
      const nodes = this.sourceResolver.instructionsPhase.nodesForPCOffset(offset);
      const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
      for (const handler of this.sourcePositionHandlers) {
        if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
      }
    }
    // The middle panel lines have already been selected so there's no need to reselect them.
  }

  public broadcastSourcePositionSelect(from, sourcePositions: Array<GenericPosition>,
                                       selected: boolean): void {
    sourcePositions = sourcePositions.filter(sourcePosition => {
      if (!sourcePosition.isValid()) {
        console.warn("Invalid source position");
        return false;
      }
      return true;
    });

    // Select the lines from the source panel (left panel)
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    // Select the nodes (middle panel)
    const nodes = this.sourceResolver.sourcePositionsToNodeIds(sourcePositions);
    for (const handler of this.nodeHandlers) {
      if (handler != from) handler.brokeredNodeSelect(nodes, selected);
    }

    this.selectInstructionsAndRegisterAllocations(from, nodes, selected);
  }

  public broadcastNodeSelect(from, nodes: Set<string>, selected: boolean): void {
    // Select the nodes (middle panel)
    for (const handler of this.nodeHandlers) {
      if (handler != from) handler.brokeredNodeSelect(nodes, selected);
    }

    // Select the lines from the source panel (left panel)
    const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
    for (const handler of this.sourcePositionHandlers) {
      if (handler != from) handler.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    this.selectInstructionsAndRegisterAllocations(from, nodes, selected);
  }

  public broadcastBlockSelect(from, blocksIds: Array<string>, selected: boolean): void {
    for (const handler of this.blockHandlers) {
      if (handler != from) handler.brokeredBlockSelect(blocksIds, selected);
    }
  }

  public broadcastClear(from): void {
    for (const handler of this.allHandlers) {
      if (handler != from) handler.brokeredClear();
    }
  }

  private selectInstructionsAndRegisterAllocations(from, nodes: Set<string>, selected: boolean):
    void {
    const instructionsOffsets = new Array<[number, number]>();
    for (const node of nodes) {
      const instructionRange = this.sourceResolver.instructionsPhase.nodeIdToInstructionRange[node];
      if (instructionRange) instructionsOffsets.push(instructionRange);
    }

    if (instructionsOffsets.length > 0) {
      // Select the lines from the disassembly (right panel)
      for (const handler of this.instructionHandlers) {
        if (handler != from) handler.brokeredInstructionSelect(instructionsOffsets, selected);
      }

      // Select the lines from the middle panel for the register allocation phase.
      for (const handler of this.registerAllocationHandlers) {
        if (handler != from) {
          handler.brokeredRegisterAllocationSelect(instructionsOffsets, selected);
        }
      }
    }
  }
}
