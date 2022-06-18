// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphNode } from "./turboshaft-graph-node";

export class TurboshaftGraphBlock {
  id: string;
  type: TurboshaftGraphBlockType;
  deferred: boolean;
  predecessors: Array<string>;
  nodes: Array<TurboshaftGraphNode>;

  constructor(id: string, type: TurboshaftGraphBlockType, deferred: boolean,
              predecessors: Array<string>) {
    this.id = id;
    this.type = type;
    this.deferred = deferred;
    this.predecessors = predecessors ?? new Array<string>();
    this.nodes = new Array<TurboshaftGraphNode>();
  }
}

export enum TurboshaftGraphBlockType {
  Loop = "LOOP",
  Merge = "MERGE",
  Block = "BLOCK"
}
