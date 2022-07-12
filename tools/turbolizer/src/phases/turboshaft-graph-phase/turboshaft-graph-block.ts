// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../../common/constants";
import { TurboshaftGraphNode } from "./turboshaft-graph-node";
import { Node } from "../../node";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";

export class TurboshaftGraphBlock extends Node<TurboshaftGraphEdge<TurboshaftGraphBlock>> {
  type: TurboshaftGraphBlockType;
  deferred: boolean;
  predecessors: Array<string>;
  nodes: Array<TurboshaftGraphNode>;
  showProperties: boolean;
  width: number;
  height: number;

  constructor(id: number, type: TurboshaftGraphBlockType, deferred: boolean,
              predecessors: Array<string>) {
    super(id, `${type} ${id}${deferred ? " (deferred)" : ""}`);
    this.type = type;
    this.deferred = deferred;
    this.predecessors = predecessors ?? new Array<string>();
    this.nodes = new Array<TurboshaftGraphNode>();
    this.visible = true;
  }

  public getHeight(showProperties: boolean): number {
    if (this.showProperties != showProperties) {
      this.height = this.nodes.reduce<number>((accumulator: number, node: TurboshaftGraphNode) => {
        return accumulator + node.getHeight(showProperties);
      }, this.labelBox.height);
      this.showProperties = showProperties;
    }
    return this.height;
  }

  public getWidth(): number {
    if (!this.width) {
      const maxNodesWidth = Math.max(...this.nodes.map((node: TurboshaftGraphNode) =>
        node.getWidth()));
      this.width = Math.max(maxNodesWidth, this.labelBox.width) + C.TURBOSHAFT_NODE_X_INDENT * 2;
    }
    return this.width;
  }

  public hasBackEdges(): boolean {
    return (this.type == TurboshaftGraphBlockType.Loop) ||
      (this.type == TurboshaftGraphBlockType.Merge &&
        this.inputs.length > 0 &&
        this.inputs[this.inputs.length - 1].source.type == TurboshaftGraphBlockType.Loop);
  }

  public toString(): string {
    return `B${this.id}`;
  }
}

export enum TurboshaftGraphBlockType {
  Loop = "LOOP",
  Merge = "MERGE",
  Block = "BLOCK"
}
