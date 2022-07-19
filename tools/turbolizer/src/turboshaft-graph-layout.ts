// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "./common/constants";
import { TurboshaftGraph } from "./turboshaft-graph";
import { GraphStateType } from "./phases/phase";
import { LayoutOccupation } from "./layout-occupation";
import {
  TurboshaftGraphBlock,
  TurboshaftGraphBlockType
} from "./phases/turboshaft-graph-phase/turboshaft-graph-block";

export class TurboshaftGraphLayout {
  graph: TurboshaftGraph;
  layoutOccupation: LayoutOccupation;
  startTime: number;
  maxRank: number;
  visitOrderWithinRank: number;

  constructor(graph: TurboshaftGraph) {
    this.graph = graph;
    this.layoutOccupation = new LayoutOccupation(graph);
    this.maxRank = 0;
  }

  public rebuild(showProperties: boolean): void {
    switch (this.graph.graphPhase.stateType) {
      case GraphStateType.NeedToFullRebuild:
        this.fullRebuild(showProperties);
        break;
      case GraphStateType.Cached:
        this.cachedRebuild();
        break;
      default:
        throw "Unsupported graph state type";
    }
    this.graph.graphPhase.rendered = true;
  }

  private fullRebuild(showProperties: boolean): void {
    this.startTime = performance.now();
    this.maxRank = 0;
    this.visitOrderWithinRank = 0;

    const blocks = this.initBlocks();
    this.initWorkList(blocks);

    let visited = new Array<boolean>();
    blocks.forEach((block: TurboshaftGraphBlock) => this.dfsFindRankLate(visited, block));
    visited = new Array<boolean>();
    blocks.forEach((block: TurboshaftGraphBlock) => this.dfsRankOrder(visited, block));

    const rankSets = this.getRankSets(showProperties);
    this.placeBlocks(rankSets, showProperties);
    this.calculateBackEdgeNumbers();
    this.graph.graphPhase.stateType = GraphStateType.Cached;
  }

  private cachedRebuild(): void {
    this.calculateBackEdgeNumbers();
  }

  private initBlocks(): Array<TurboshaftGraphBlock> {
    // First determine the set of blocks that have no inputs. Those are the
    // basis for top-down DFS to determine rank and block placement.
    const blocksHasNoInputs = new Array<boolean>();
    for (const block of this.graph.blocks()) {
      block.collapsed = false;
      blocksHasNoInputs[block.id] = true;
    }
    for (const edge of this.graph.blocksEdges()) {
      blocksHasNoInputs[edge.target.id] = false;
    }

    // Finialize the list of blocks.
    const blocks = new Array<TurboshaftGraphBlock>();
    const visited = new Array<boolean>();
    for (const block of this.graph.blocks()) {
      if (blocksHasNoInputs[block.id]) {
        blocks.push(block);
      }
      visited[block.id] = false;
      block.rank = 0;
      block.visitOrderWithinRank = 0;
      block.outputApproach = C.MINIMUM_NODE_OUTPUT_APPROACH;
    }
    this.trace("layoutGraph init");
    return blocks;
  }

  private initWorkList(blocks: Array<TurboshaftGraphBlock>): void {
    const workList: Array<TurboshaftGraphBlock> = blocks.slice();
    while (workList.length != 0) {
      const block: TurboshaftGraphBlock = workList.pop();
      let changed = false;
      if (block.rank == C.MAX_RANK_SENTINEL) {
        block.rank = 1;
        changed = true;
      }
      let begin = 0;
      let end = block.inputs.length;
      if (block.type == TurboshaftGraphBlockType.Merge && block.inputs.length > 0) {
        begin = block.inputs.length - 1;
      } else if (block.hasBackEdges()) {
        end = 1;
      }
      for (let l = begin; l < end; ++l) {
        const input = block.inputs[l].source;
        if (input.visible && input.rank >= block.rank) {
          block.rank = input.rank + 1;
          changed = true;
        }
      }
      if (changed) {
        const hasBackEdges = block.hasBackEdges();
        for (let l = block.outputs.length - 1; l >= 0; --l) {
          if (hasBackEdges && (l != 0)) {
            workList.unshift(block.outputs[l].target);
          } else {
            workList.push(block.outputs[l].target);
          }
        }
      }
      this.maxRank = Math.max(block.rank, this.maxRank);
      this.trace("layoutGraph work list");
    }
  }

  private dfsFindRankLate(visited: Array<boolean>, block: TurboshaftGraphBlock): void {
    if (visited[block.id]) return;
    visited[block.id] = true;
    const originalRank = block.rank;
    let newRank = block.rank;
    let isFirstInput = true;
    for (const outputEdge of block.outputs) {
      const output = outputEdge.target;
      this.dfsFindRankLate(visited, output);
      const outputRank = output.rank;
      if (output.visible && (isFirstInput || outputRank <= newRank) &&
        (outputRank > originalRank)) {
        newRank = outputRank - 1;
      }
      isFirstInput = false;
    }
    if (block.type != TurboshaftGraphBlockType.Merge) {
      block.rank = newRank;
    }
  }

  private dfsRankOrder(visited: Array<boolean>, block: TurboshaftGraphBlock): void {
    if (visited[block.id]) return;
    visited[block.id] = true;
    for (const outputEdge of block.outputs) {
      if (outputEdge.isVisible()) {
        const output = outputEdge.target;
        this.dfsRankOrder(visited, output);
      }
    }
    if (block.visitOrderWithinRank == 0) {
      block.visitOrderWithinRank = ++this.visitOrderWithinRank;
    }
  }

  private getRankSets(showProperties: boolean): Array<Array<TurboshaftGraphBlock>> {
    const rankMaxBlockHeight = new Array<number>();
    for (const block of this.graph.blocks()) {
      rankMaxBlockHeight[block.rank] = Math.max(rankMaxBlockHeight[block.rank] ?? 0,
        block.getHeight(showProperties));
    }

    const rankSets = new Array<Array<TurboshaftGraphBlock>>();
    for (const block of this.graph.blocks()) {
      block.y = rankMaxBlockHeight.slice(1, block.rank).reduce<number>((accumulator, current) => {
        return accumulator + current;
      }, block.rank * (C.TURBOSHAFT_BLOCK_ROW_SEPARATION + 2 * C.DEFAULT_NODE_BUBBLE_RADIUS));
      if (block.visible) {
        if (!rankSets[block.rank]) {
          rankSets[block.rank] = new Array<TurboshaftGraphBlock>(block);
        } else {
          rankSets[block.rank].push(block);
        }
      }
    }
    return rankSets;
  }

  private placeBlocks(rankSets: Array<Array<TurboshaftGraphBlock>>, showProperties: boolean): void {
    // Iterate backwards from highest to lowest rank, placing blocks so that they
    // spread out from the "center" as much as possible while still being
    // compact and not overlapping live input lines.
    rankSets.reverse().forEach((rankSet: Array<TurboshaftGraphBlock>) => {
      for (const block of rankSet) {
        this.layoutOccupation.clearOutputs(block, showProperties);
      }

      this.traceOccupation("After clearing outputs");

      let placedCount = 0;
      rankSet = rankSet.sort((a: TurboshaftGraphBlock, b: TurboshaftGraphBlock) => a.compare(b));
      for (const block of rankSet) {
        if (block.visible) {
          block.x = this.layoutOccupation.occupy(block);
          const blockWidth = block.getWidth();
          this.trace(`Block ${block.id} is placed between [${block.x}, ${block.x + blockWidth})`);
          const staggeredFlooredI = Math.floor(placedCount++ % 3);
          const delta = C.MINIMUM_EDGE_SEPARATION * staggeredFlooredI;
          block.outputApproach += delta;
        } else {
          block.x = 0;
        }
      }

      this.traceOccupation("Before clearing blocks");

      this.layoutOccupation.clearOccupied();

      this.traceOccupation("After clearing blocks");

      for (const block of rankSet) {
        this.layoutOccupation.occupyInputs(block, showProperties);
      }

      this.traceOccupation("After occupying inputs and determining bounding box");
    });
  }

  private calculateBackEdgeNumbers(): void {
    this.graph.maxBackEdgeNumber = 0;
    for (const edge of this.graph.blocksEdges()) {
      if (edge.isBackEdge()) {
        edge.backEdgeNumber = ++this.graph.maxBackEdgeNumber;
      } else {
        edge.backEdgeNumber = 0;
      }
    }
  }

  private trace(message: string): void {
    if (C.TRACE_LAYOUT) {
      console.log(`${message} ${performance.now() - this.startTime}`);
    }
  }

  private traceOccupation(message: string): void {
    if (C.TRACE_LAYOUT) {
      console.log(message);
      this.layoutOccupation.print();
    }
  }
}
