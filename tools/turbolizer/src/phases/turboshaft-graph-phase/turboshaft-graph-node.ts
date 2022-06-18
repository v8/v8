// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";
import { Node } from "../../node";

export class TurboshaftGraphNode extends Node<TurboshaftGraphEdge> {
  title: string;
  block: TurboshaftGraphBlock;
  properties: string;

  constructor(id: number, title: string, block: TurboshaftGraphBlock, properties: string) {
    super(id);
    this.title = title;
    this.block = block;
    this.properties = properties;
  }
}
