// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import * as C from "../../common/constants";
import { measureText } from "../../common/util";
import { TurboshaftGraphEdge } from "./turboshaft-graph-edge";
import { TurboshaftGraphBlock } from "./turboshaft-graph-block";
import { Node } from "../../node";
import { BytecodePosition, SourcePosition } from "../../position";
import { NodeOrigin } from "../../origin";
import { TurboshaftCustomDataPhase } from "../turboshaft-custom-data-phase";

const SUBSCRIPT_DY: string = "50px";

enum Opcode {
  Constant = "Constant",
  WordBinop = "WordBinop",
}

enum WordRepresentation {
  Word32 = "Word32",
  Word64 = "Word64",
}

function toEnum<T>(type: T, option: string): T[keyof T] {
  for(const key of Object.keys(type)) {
    if(option === type[key]) {
      const r: T[keyof T] = (type[key]) as T[keyof T];
      return r;
    }
  }
  throw new CompactOperationError(`Option "${option}" not recognized as ${type}`);
}
 
class CompactOperationError {
  message: string;

  constructor(message: string) {
    this.message = message;
  }
}

type InputPrinter = (n: number) => string;

abstract class CompactOperationPrinter {
  public IsFullyInlined(): boolean { return false; }
  public abstract Print(id: number, input: InputPrinter): string;
  public abstract PrintInLine(): string;

  protected parseOptions(properties: string, expectedCount: number = -1): Array<string> {
    if(!properties.startsWith("[") || !properties.endsWith("]")) {
      throw new CompactOperationError(`Unexpected options format: "${properties}"`);
    }

    const options = properties.substring(1, properties.length - 1)
                              .split(",")
                              .map(o => o.trim());
    if(expectedCount > -1) {
      if(options.length != expectedCount) {
        throw new CompactOperationError(
          `Unexpected option count: ${options} (expected ${expectedCount})`);
      }
    }
    return options;
  }

  protected sub(text: string): string {
    return `<tspan class="subscript" dy="${SUBSCRIPT_DY}">${text}</tspan><tspan dy="-${SUBSCRIPT_DY}"> </tspan>`
  }
}

enum Constant_Kind {
  Word32 = "word32",
  Word64 = "word64",
}

class CompactOperationPrinter_Constant extends CompactOperationPrinter {
  kind: Constant_Kind;
  value: string;

  constructor(properties: string) {
    super();

    const options = this.parseOptions(properties, 1);
    // Format of {options[0]} is "kind: value".
    let [key, value] = options[0].split(":").map(x => x.trim());
    this.kind = toEnum(Constant_Kind, key);
    this.value = value;
  }

  public IsFullyInlined(): boolean {
    return true;
  }
  public override Print(n: number, input: InputPrinter): string { return ""; }
  public PrintInLine(): string {
    switch(this.kind) {
      case Constant_Kind.Word32: return `${this.value}${this.sub("w32")}`;
      case Constant_Kind.Word64: return `${this.value}${this.sub("w64")}`;
    }
  }
}

enum WordBinop_Kind {
  Add = "Add",
  Mul = "Mul",
  SignedMulOverflownBits = "SignedMulOverflownBits",
  UnsignedMulOverflownBits = "UnsignedMulOverflownBits",
  BitwiseAnd = "BitwiseAnd",
  BitwiseOr = "BitwiseOr",
  BitwiseXor = "BitwiseXor",
  Sub = "Sub",
  SignedDiv = "SignedDiv",
  UnsignedDiv = "UnsignedDiv",
  SignedMod = "SignedMod",
  UnsignedMod = "UnsignedMod",
}

class CompactOperationPrinter_WordBinop extends CompactOperationPrinter {
  kind: WordBinop_Kind;
  rep: WordRepresentation;

  constructor(properties: string) {
    super();

    const options = this.parseOptions(properties, 2);
    this.kind = toEnum(WordBinop_Kind, options[0]);
    this.rep = toEnum(WordRepresentation, options[1]);
  }

   public override Print(id: number, input: InputPrinter): string {
    let symbol: string;
    let subscript = "";
    switch(this.kind) {
      case WordBinop_Kind.Add: symbol = "+"; break;
      case WordBinop_Kind.Mul: symbol = "*"; break;
      case WordBinop_Kind.SignedMulOverflownBits:
        symbol = "*";
        subscript = "s,of";
        break;
      case WordBinop_Kind.UnsignedMulOverflownBits:
        symbol = "*";
        subscript = "u,of";
        break;
      case WordBinop_Kind.BitwiseAnd: symbol = "&"; break;
      case WordBinop_Kind.BitwiseOr: symbol = "|"; break;
      case WordBinop_Kind.BitwiseXor: symbol = "^"; break;
      case WordBinop_Kind.Sub: symbol = "-"; break;
      case WordBinop_Kind.SignedDiv:
        symbol = "/";
        subscript = "s";
        break;
      case WordBinop_Kind.UnsignedDiv:
        symbol = "/";
        subscript = "u";
        break;
      case WordBinop_Kind.SignedMod:
        symbol = "%";
        subscript = "s";
        break;
      case WordBinop_Kind.UnsignedMod:
        symbol = "%";
        subscript = "u";
        break;
    }
    if(subscript.length > 0) subscript += ",";
    switch(this.rep) {
      case WordRepresentation.Word32:
        subscript += "w32";
        break;
      case WordRepresentation.Word64:
        subscript += "w64";
        break;
    }
 
    return `v${id} = ${input(0)} ${symbol}${this.sub(subscript)} ${input(1)}`;
  }

  public override PrintInLine(): string { return ""; }
}

export class TurboshaftGraphOperation extends Node<TurboshaftGraphEdge<TurboshaftGraphOperation>> {
  title: string;
  block: TurboshaftGraphBlock;
  sourcePosition: SourcePosition;
  bytecodePosition: BytecodePosition;
  origin: NodeOrigin;
  opEffects: String;

  compactPrinter: CompactOperationPrinter;

  constructor(id: number, title: string, block: TurboshaftGraphBlock,
              sourcePosition: SourcePosition, bytecodePosition: BytecodePosition,
              origin: NodeOrigin, opEffects: String) {
    super(id);
    this.title = title;
    this.block = block;
    this.sourcePosition = sourcePosition;
    this.bytecodePosition = bytecodePosition;
    this.origin = origin;
    this.opEffects = opEffects;
    this.visible = true;
    this.compactPrinter = null;
  }

  public propertiesChanged(customData: TurboshaftCustomDataPhase): void {
    // The properties have been parsed from the JSON, we need to update
    // operation printing.
    const properties = customData.data[this.id];
    this.compactPrinter = this.parseOperationForCompactRepresentation(properties);
  }

  public getHeight(showCustomData: boolean, compactView: boolean): number {
    if(compactView && this.compactPrinter?.IsFullyInlined()) return 0;
    return showCustomData ? this.labelBox.height * 2 : this.labelBox.height;
  }

  public getWidth(): number {
    return Math.max(this.inputs.length * C.NODE_INPUT_WIDTH, this.labelBox.width);
  }

  public initDisplayLabel(): void {
    this.displayLabel = this.getNonCompactedOperationText();
    this.labelBox = measureText(this.displayLabel);
  }

  public getTitle(): string {
    let title = `${this.id} ${this.title}`;
    title += `\nEffects: ${this.opEffects}`;
    if (this.origin) {
      title += `\nOrigin: ${this.origin.toString()}`;
    }
    if (this.inputs.length > 0) {
      title += `\nInputs: ${this.inputs.map(i => formatInput(i.source)).join(", ")}`;
    }
    if (this.outputs.length > 0) {
      title += `\nOutputs: ${this.outputs.map(i => i.target.id).join(", ")}`;
    }
    return title;

    function formatInput(input: TurboshaftGraphOperation) {
      return `[${input.block}] ${input.displayLabel}`;
    }
  }

  public getHistoryLabel(): string {
    return `${this.id} ${this.title}`;
  }

  public getNodeOrigin(): NodeOrigin {
    return this.origin;
  }

  public printInput(input: TurboshaftGraphOperation): string {
    if(input.compactPrinter && input.compactPrinter.IsFullyInlined()) {
      const s: string = input.compactPrinter.PrintInLine();
      return s;
    }
    return "v" + input.id.toString();
  }

  private getNonCompactedOperationText(): string {
    if(this.inputs.length == 0) {
      return `${this.id} ${this.title}`;
    } else {
      return `${this.id} ${this.title}(${this.inputs.map(i => i.source.id).join(", ")})`;
    }
  }

  public buildOperationText(compact: boolean): string {
    if(!compact) {
      return this.getNonCompactedOperationText();
    }
    const that = this;
    if(this.compactPrinter) {
      return this.compactPrinter.Print(this.id, n => that.printInput(this.inputs[n].source));
    } else if(this.inputs.length == 0) {
      return `v${this.id} ${this.title}`;
    } else {
      return `v${this.id} ${this.title}(${this.inputs.map(input => that.printInput(input.source)).join(", ")})`;
    }
  }

  public equals(that?: TurboshaftGraphOperation): boolean {
    if (!that) return false;
    if (this.id !== that.id) return false;
    return this.title === that.title;
  }

  private parseOperationForCompactRepresentation(properties: string): CompactOperationPrinter {
    try {
      switch(this.title) {
        case Opcode.Constant:
          return new CompactOperationPrinter_Constant(properties);
        case Opcode.WordBinop:
          return new CompactOperationPrinter_WordBinop(properties);
        default:
          return null;
      }
    }
    catch(e) {
      if(e instanceof CompactOperationError) {
        console.error(e.message);
        return null;
      }
      throw e;
    }
  }
}
