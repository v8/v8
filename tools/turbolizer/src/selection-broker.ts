// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { SourceResolver, sourcePositionValid } from "../src/source-resolver";
import { ClearableHandler, SelectionHandler, NodeSelectionHandler, BlockSelectionHandler, InstructionSelectionHandler } from "../src/selection-handler";

export class SelectionBroker {
  sourceResolver: SourceResolver;
  allHandlers: Array<ClearableHandler>;
  sourcePositionHandlers: Array<SelectionHandler>;
  nodeHandlers: Array<NodeSelectionHandler>;
  blockHandlers: Array<BlockSelectionHandler>;
  instructionHandlers: Array<InstructionSelectionHandler>;

  constructor(sourceResolver) {
    this.allHandlers = [];
    this.sourcePositionHandlers = [];
    this.nodeHandlers = [];
    this.blockHandlers = [];
    this.instructionHandlers = [];
    this.sourceResolver = sourceResolver;
  }

  addSourcePositionHandler(handler: SelectionHandler & ClearableHandler) {
    this.allHandlers.push(handler);
    this.sourcePositionHandlers.push(handler);
  }

  addNodeHandler(handler: NodeSelectionHandler & ClearableHandler) {
    this.allHandlers.push(handler);
    this.nodeHandlers.push(handler);
  }

  addBlockHandler(handler: BlockSelectionHandler & ClearableHandler) {
    this.allHandlers.push(handler);
    this.blockHandlers.push(handler);
  }

  addInstructionHandler(handler: InstructionSelectionHandler & ClearableHandler) {
    this.allHandlers.push(handler);
    this.instructionHandlers.push(handler);
  }

  broadcastInstructionSelect(from, instructionOffsets, selected) {
    for (const b of this.instructionHandlers) {
      if (b != from) b.brokeredInstructionSelect(instructionOffsets, selected);
    }
  }

  broadcastSourcePositionSelect(from, sourcePositions, selected) {
    sourcePositions = sourcePositions.filter(l => {
      if (!sourcePositionValid(l)) {
        console.log("Warning: invalid source position");
        return false;
      }
      return true;
    });

    // Select the lines from the source panel (left panel)
    for (const b of this.sourcePositionHandlers) {
      if (b != from) b.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    // Select the nodes (middle panel)
    const nodes = this.sourceResolver.sourcePositionsToNodeIds(sourcePositions);
    for (const b of this.nodeHandlers) {
      if (b != from) b.brokeredNodeSelect(nodes, selected);
    }

    // Select the lines from the disassembly (right panel)
    for (const node of nodes) {
      const instructionOffsets = this.sourceResolver.nodeIdToInstructionRange[node];
      // Skip nodes which do not have an associated instruction range.
      if (instructionOffsets == undefined) continue;
      for (const b of this.instructionHandlers) {
        if (b != from) b.brokeredInstructionSelect(instructionOffsets, selected);
      }
    }
  }

  broadcastNodeSelect(from, nodes, selected) {
    // Select the nodes (middle panel)
    for (const b of this.nodeHandlers) {
      if (b != from) b.brokeredNodeSelect(nodes, selected);
    }

    // Select the lines from the source panel (left panel)
    const sourcePositions = this.sourceResolver.nodeIdsToSourcePositions(nodes);
    for (const b of this.sourcePositionHandlers) {
      if (b != from) b.brokeredSourcePositionSelect(sourcePositions, selected);
    }

    // Select the lines from the disassembly (right panel)
    for (const node of nodes) {
      const instructionOffsets = this.sourceResolver.nodeIdToInstructionRange[node];
      // Skip nodes which do not have an associated instruction range.
      if (instructionOffsets == undefined) continue;
      for (const b of this.instructionHandlers) {
        if (b != from) b.brokeredInstructionSelect(instructionOffsets, selected);
      }
    }
  }

  broadcastBlockSelect(from, blocks, selected) {
    for (const b of this.blockHandlers) {
      if (b != from) b.brokeredBlockSelect(blocks, selected);
    }
  }

  broadcastClear(from) {
    this.allHandlers.forEach(function (b) {
      if (b != from) b.brokeredClear();
    });
  }
}
