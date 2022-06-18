// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphNode } from "./turboshaft-graph-node";
import { Edge } from "../../edge";

export class TurboshaftGraphEdge extends Edge<TurboshaftGraphNode> {
  constructor(target: TurboshaftGraphNode, source: TurboshaftGraphNode) {
    super(target, source);
  }
}
