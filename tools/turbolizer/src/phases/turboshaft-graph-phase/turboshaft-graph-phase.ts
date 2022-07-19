// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { GraphStateType, Phase, PhaseType } from "../phase";
import { TurboshaftGraphNode } from "./turboshaft-graph-node";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";

export class TurboshaftGraphPhase extends Phase {
  data: TurboshaftGraphData;
  stateType: GraphStateType;
  nodeIdToNodeMap: Array<TurboshaftGraphNode>;
  blockIdToBlockMap: Array<TurboshaftGraphBlock>;
  rendered: boolean;
  transform: { x: number, y: number, scale: number };

  constructor(name: string, dataJson) {
    super(name, PhaseType.TurboshaftGraph);
    this.stateType = GraphStateType.NeedToFullRebuild;
    this.nodeIdToNodeMap = new Array<TurboshaftGraphNode>();
    this.blockIdToBlockMap = new Array<TurboshaftGraphBlock>();
    this.rendered = false;
    this.parseDataFromJSON(dataJson);
  }

  private parseDataFromJSON(dataJson): void {
    this.data = new TurboshaftGraphData();
    this.parseBlocksFromJSON(dataJson.blocks);
    this.parseNodesFromJSON(dataJson.nodes);
    this.parseEdgesFromJSON(dataJson.edges);
  }

  private parseBlocksFromJSON(blocksJson): void {
    for (const blockJson of blocksJson) {
      const block = new TurboshaftGraphBlock(blockJson.id, blockJson.type,
        blockJson.deferred, blockJson.predecessors);
      this.data.blocks.push(block);
      this.blockIdToBlockMap[block.identifier()] = block;
    }
    for (const block of this.blockIdToBlockMap) {
      for (const [idx, predecessor] of block.predecessors.entries()) {
        const source = this.blockIdToBlockMap[predecessor];
        const edge = new TurboshaftGraphEdge(block, idx, source);
        block.inputs.push(edge);
        source.outputs.push(edge);
      }
    }
  }

  private parseNodesFromJSON(nodesJson): void {
    for (const nodeJson of nodesJson) {
      const block = this.blockIdToBlockMap[nodeJson.block_id];
      const node = new TurboshaftGraphNode(nodeJson.id, nodeJson.title,
        block, nodeJson.op_properties_type, nodeJson.properties);
      block.nodes.push(node);
      this.data.nodes.push(node);
      this.nodeIdToNodeMap[node.identifier()] = node;
    }
    for (const block of this.blockIdToBlockMap) {
      block.initCollapsedLabel();
    }
  }

  private parseEdgesFromJSON(edgesJson): void {
    for (const edgeJson of edgesJson) {
      const target = this.nodeIdToNodeMap[edgeJson.target];
      const source = this.nodeIdToNodeMap[edgeJson.source];
      const edge = new TurboshaftGraphEdge(target, -1, source);
      this.data.edges.push(edge);
      target.inputs.push(edge);
      source.outputs.push(edge);
    }
    for (const node of this.data.nodes) {
      node.initDisplayLabel();
    }
  }
}

export class TurboshaftGraphData {
  nodes: Array<TurboshaftGraphNode>;
  edges: Array<TurboshaftGraphEdge<TurboshaftGraphNode>>;
  blocks: Array<TurboshaftGraphBlock>;

  constructor() {
    this.nodes = new Array<TurboshaftGraphNode>();
    this.edges = new Array<TurboshaftGraphEdge<TurboshaftGraphNode>>();
    this.blocks = new Array<TurboshaftGraphBlock>();
  }
}
