// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { measureText } from "./common/util";
import { GraphEdge } from "./phases/graph-phase/graph-edge";
import { TurboshaftGraphEdge } from "./phases/turboshaft-graph-phase/turboshaft-graph-edge";

export abstract class Node<EdgeType extends GraphEdge | TurboshaftGraphEdge> {
  id: number;
  displayLabel: string;
  inputs: Array<EdgeType>;
  outputs: Array<EdgeType>;
  visible: boolean;
  x: number;
  y: number;
  labelBox: { width: number, height: number };
  visitOrderWithinRank: number;

  constructor(id: number, displayLabel?: string) {
    this.id = id;
    this.displayLabel = displayLabel;
    this.inputs = new Array<EdgeType>();
    this.outputs = new Array<EdgeType>();
    this.visible = false;
    this.x = 0;
    this.y = 0;
    this.labelBox = measureText(this.displayLabel);
    this.visitOrderWithinRank = 0;
  }

  public areAnyOutputsVisible(): number {
    // TODO (danylo boiko) Move 0, 1, 2 logic to enum
    let visibleCount = 0;
    for (const edge of this.outputs) {
      if (edge.isVisible()) {
        ++visibleCount;
      }
    }
    if (this.outputs.length === visibleCount) return 2;
    if (visibleCount !== 0) return 1;
    return 0;
  }

  public setOutputVisibility(visibility: boolean): boolean {
    let result = false;
    for (const edge of this.outputs) {
      edge.visible = visibility;
      if (visibility && !edge.target.visible) {
        edge.target.visible = true;
        result = true;
      }
    }
    return result;
  }

  public setInputVisibility(edgeIdx: number, visibility: boolean): boolean {
    const edge = this.inputs[edgeIdx];
    edge.visible = visibility;
    if (visibility && !edge.source.visible) {
      edge.source.visible = true;
      return true;
    }
    return false;
  }

  public identifier(): string {
    return `${this.id}`;
  }

  public toString(): string {
    return `N${this.id}`;
  }
}
