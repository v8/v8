// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "./common/constants";
import { GraphPhase } from "./phases/graph-phase";
import { GraphEdge } from "./phases/graph-phase/graph-edge";
import { GraphNode } from "./phases/graph-phase/graph-node";

export class Graph {
  nodeMap: Array<GraphNode>;
  minGraphX: number;
  maxGraphX: number;
  minGraphY: number;
  maxGraphY: number;
  maxGraphNodeX: number;
  maxBackEdgeNumber: number;
  width: number;
  height: number;

  constructor(graphPhase: GraphPhase) {
    this.nodeMap = [];

    this.minGraphX = 0;
    this.maxGraphX = 1;
    this.minGraphY = 0;
    this.maxGraphY = 1;
    this.width = 1;
    this.height = 1;

    graphPhase.data.nodes.forEach((jsonNode: GraphNode) => {
      this.nodeMap[jsonNode.id] = new GraphNode(jsonNode.nodeLabel);
    });

    graphPhase.data.edges.forEach((e: any) => {
      const t = this.nodeMap[e.target.id];
      const s = this.nodeMap[e.source.id];
      const newEdge = new GraphEdge(t, e.index, s, e.type);
      t.inputs.push(newEdge);
      s.outputs.push(newEdge);
      if (e.type == 'control') {
        // Every source of a control edge is a CFG node.
        s.cfg = true;
      }
    });
  }

  *nodes(p = (n: GraphNode) => true) {
    for (const node of this.nodeMap) {
      if (!node || !p(node)) continue;
      yield node;
    }
  }

  *filteredEdges(p: (e: GraphEdge) => boolean) {
    for (const node of this.nodes()) {
      for (const edge of node.inputs) {
        if (p(edge)) yield edge;
      }
    }
  }

  forEachEdge(p: (e: GraphEdge) => void) {
    for (const node of this.nodeMap) {
      if (!node) continue;
      for (const edge of node.inputs) {
        p(edge);
      }
    }
  }

  redetermineGraphBoundingBox(showTypes: boolean): [[number, number], [number, number]] {
    this.minGraphX = 0;
    this.maxGraphNodeX = 1;
    this.maxGraphX = undefined;  // see below
    this.minGraphY = 0;
    this.maxGraphY = 1;

    for (const node of this.nodes()) {
      if (!node.visible) {
        continue;
      }

      if (node.x < this.minGraphX) {
        this.minGraphX = node.x;
      }
      if ((node.x + node.getTotalNodeWidth()) > this.maxGraphNodeX) {
        this.maxGraphNodeX = node.x + node.getTotalNodeWidth();
      }
      if ((node.y - 50) < this.minGraphY) {
        this.minGraphY = node.y - 50;
      }
      if ((node.y + node.getNodeHeight(showTypes) + 50) > this.maxGraphY) {
        this.maxGraphY = node.y + node.getNodeHeight(showTypes) + 50;
      }
    }

    this.maxGraphX = this.maxGraphNodeX +
      this.maxBackEdgeNumber * C.MINIMUM_EDGE_SEPARATION;

    this.width = this.maxGraphX - this.minGraphX;
    this.height = this.maxGraphY - this.minGraphY;

    const extent: [[number, number], [number, number]] = [
      [this.minGraphX - this.width / 2, this.minGraphY - this.height / 2],
      [this.maxGraphX + this.width / 2, this.maxGraphY + this.height / 2]
    ];

    return extent;
  }

}
