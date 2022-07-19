// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { Phase, PhaseType } from "../phase";
import { NodeLabel } from "../../node-label";
import { BytecodeOrigin, NodeOrigin } from "../../origin";
import { SourcePosition } from "../../position";
import { GraphNode } from "./graph-node";
import { GraphEdge } from "./graph-edge";

export class GraphPhase extends Phase {
  highestNodeId: number;
  data: GraphData;
  stateType: GraphStateType;
  nodeLabelMap: Array<NodeLabel>;
  nodeIdToNodeMap: Array<GraphNode>;
  originIdToNodesMap: Map<string, Array<GraphNode>>;
  rendered: boolean;
  transform: { x: number, y: number, scale: number };

  constructor(name: string, highestNodeId: number, dataJson, nodeLabelMap?: Array<NodeLabel>) {
    super(name, PhaseType.Graph);
    this.highestNodeId = highestNodeId;
    this.data = new GraphData();
    this.stateType = GraphStateType.NeedToFullRebuild;
    this.nodeIdToNodeMap = new Array<GraphNode>();
    this.originIdToNodesMap = new Map<string, Array<GraphNode>>();
    this.rendered = false;
    this.parseDataFromJSON(dataJson, nodeLabelMap);
    this.nodeLabelMap = nodeLabelMap?.slice();
  }

  private parseDataFromJSON(dataJson, nodeLabelMap: Array<NodeLabel>): void {
    this.data = new GraphData();
    this.nodeIdToNodeMap = this.parseNodesFromJSON(dataJson.nodes, nodeLabelMap);
    this.parseEdgesFromJSON(dataJson.edges);
  }

  private parseNodesFromJSON(nodesJSON, nodeLabelMap: Array<NodeLabel>): Array<GraphNode> {
    const nodeIdToNodeMap = new Array<GraphNode>();
    for (const node of nodesJSON) {
      let origin: NodeOrigin | BytecodeOrigin = null;
      const jsonOrigin = node.origin;
      if (jsonOrigin) {
        if (jsonOrigin.nodeId) {
          origin = new NodeOrigin(jsonOrigin.nodeId, jsonOrigin.phase, jsonOrigin.reducer);
        } else {
          origin = new BytecodeOrigin(jsonOrigin.bytecodePosition, jsonOrigin.phase,
            jsonOrigin.reducer);
        }
      }

      let sourcePosition: SourcePosition = null;
      if (node.sourcePosition) {
        const scriptOffset = node.sourcePosition.scriptOffset;
        const inliningId = node.sourcePosition.inliningId;
        sourcePosition = new SourcePosition(scriptOffset, inliningId);
      }

      const label = new NodeLabel(node.id, node.label, node.title, node.live, node.properties,
        sourcePosition, origin, node.opcode, node.control, node.opinfo, node.type);

      const previous = nodeLabelMap[label.id];
      if (!label.equals(previous)) {
        if (previous !== undefined) {
          label.setInplaceUpdatePhase(this.name);
        }
        nodeLabelMap[label.id] = label;
      }
      const newNode = new GraphNode(label);
      this.data.nodes.push(newNode);
      nodeIdToNodeMap[newNode.identifier()] = newNode;
      if (origin) {
        const identifier = origin.identifier();
        if (!this.originIdToNodesMap.has(identifier)) {
          this.originIdToNodesMap.set(identifier, new Array<GraphNode>());
        }
        this.originIdToNodesMap.get(identifier).push(newNode);
      }
    }
    return nodeIdToNodeMap;
  }

  private parseEdgesFromJSON(edgesJSON): void {
    for (const edge of edgesJSON) {
      const target = this.nodeIdToNodeMap[edge.target];
      const source = this.nodeIdToNodeMap[edge.source];
      const newEdge = new GraphEdge(target, edge.index, source, edge.type);
      this.data.edges.push(newEdge);
      target.inputs.push(newEdge);
      source.outputs.push(newEdge);
      if (edge.type === "control") {
        source.cfg = true;
      }
    }
  }
}

export class GraphData {
  nodes: Array<GraphNode>;
  edges: Array<GraphEdge>;

  constructor() {
    this.nodes = new Array<GraphNode>();
    this.edges = new Array<GraphEdge>();
  }
}

export enum GraphStateType {
  NeedToFullRebuild,
  Cached
}
