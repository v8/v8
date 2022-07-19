// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../../common/constants";
import { measureText } from "../../common/util";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";
import { Node } from "../../node";

export class TurboshaftGraphNode extends Node<TurboshaftGraphEdge<TurboshaftGraphNode>> {
  title: string;
  block: TurboshaftGraphBlock;
  opPropertiesType: OpPropertiesType;
  properties: string;
  propertiesBox: { width: number, height: number };

  constructor(id: number, title: string, block: TurboshaftGraphBlock,
              opPropertiesType: OpPropertiesType, properties: string) {
    super(id);
    this.title = title;
    this.block = block;
    this.opPropertiesType = opPropertiesType;
    this.properties = properties;
    this.propertiesBox = measureText(this.properties);
    this.visible = true;
  }

  public getHeight(showProperties: boolean): number {
    if (this.properties && showProperties) {
      return this.labelBox.height + this.propertiesBox.height;
    }
    return this.labelBox.height;
  }

  public getWidth(): number {
    return Math.max(this.inputs.length * C.NODE_INPUT_WIDTH, this.labelBox.width);
  }

  public initDisplayLabel() {
    this.displayLabel = this.getInlineLabel();
    this.labelBox = measureText(this.displayLabel);
  }

  public getTitle(): string {
    let title = `${this.id} ${this.title} ${this.opPropertiesType}`;
    if (this.inputs.length > 0) {
      title += `\nInputs: ${this.inputs.map(i => i.source.id).join(", ")}`;
    }
    if (this.outputs.length > 0) {
      title += `\nOutputs: ${this.outputs.map(i => i.target.id).join(", ")}`;
    }
    const opPropertiesStr = this.properties.length > 0 ? this.properties : "No op properties";
    return `${title}\n${opPropertiesStr}`;
  }

  public getInlineLabel(): string {
    if (this.inputs.length == 0) return `${this.id} ${this.title}`;
    return `${this.id} ${this.title}(${this.inputs.map(i => i.source.id).join(",")})`;
  }

  public getReadableProperties(blockWidth: number): string {
    if (blockWidth > this.propertiesBox.width) return this.properties;
    const widthOfOneSymbol = Math.floor(this.propertiesBox.width / this.properties.length);
    const lengthOfReadableProperties = Math.floor(blockWidth / widthOfOneSymbol);
    return `${this.properties.slice(0, lengthOfReadableProperties - 3)}..`;
  }
}

export enum OpPropertiesType {
  Pure = "Pure",
  Reading = "Reading",
  Writing = "Writing",
  CanDeopt = "CanDeopt",
  AnySideEffects = "AnySideEffects",
  BlockTerminator = "BlockTerminator"
}
