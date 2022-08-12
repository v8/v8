// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import { createElement } from "../common/util";
import { CodeMode, View } from "./view";
import { SelectionBroker } from "../selection/selection-broker";
import { BytecodeSource } from "../source";
import { SourceResolver } from "../source-resolver";

export class BytecodeSourceView extends View {
  broker: SelectionBroker;
  source: BytecodeSource;
  sourceResolver: SourceResolver;
  codeMode: CodeMode;
  bytecodeOffsetToHtmlElement: Map<number, HTMLElement>;

  constructor(parent: HTMLElement, broker: SelectionBroker,  sourceFunction: BytecodeSource,
              sourceResolver: SourceResolver, codeMode: CodeMode) {
    super(parent);
    this.broker = broker;
    this.source = sourceFunction;
    this.sourceResolver = sourceResolver;
    this.codeMode = codeMode;
    this.bytecodeOffsetToHtmlElement = new Map<number, HTMLElement>();

    this.initializeCode();
  }

  protected createViewElement(): HTMLElement {
    return createElement("div", "bytecode-source-container");
  }

  private initializeCode(): void {
    const view = this;
    const source = this.source;
    const bytecodeContainer = this.divNode;
    bytecodeContainer.classList.add(view.getSourceClass());

    const bytecodeHeader = createElement("div", "code-header");
    bytecodeHeader.setAttribute("id", view.getBytecodeHeaderHtmlElementName());

    const codeFileFunction = createElement("div", "code-file-function", source.functionName);
    bytecodeHeader.appendChild(codeFileFunction);

    const codeMode = createElement("div", "code-mode", view.codeMode);
    bytecodeHeader.appendChild(codeMode);

    const clearElement = document.createElement("div");
    clearElement.style.clear = "both";
    bytecodeHeader.appendChild(clearElement);
    bytecodeContainer.appendChild(bytecodeHeader);

    const codePre = createElement("pre", "prettyprint linenums");
    codePre.setAttribute("id", view.getBytecodeHtmlElementName());
    bytecodeContainer.appendChild(codePre);

    bytecodeHeader.onclick = () => {
      codePre.style.display = codePre.style.display === "none" ? "block" : "none";
    };

    const sourceList = createElement("ol", "linenums");
    for (const bytecodeSource of view.source.data) {
      const currentLine = createElement("li", `L${bytecodeSource.offset}`);
      currentLine.setAttribute("id", `li${bytecodeSource.offset}`);
      view.insertLineContent(currentLine, bytecodeSource.disassembly);
      view.insertLineNumber(currentLine, bytecodeSource.offset);
      view.bytecodeOffsetToHtmlElement.set(bytecodeSource.offset, currentLine);
      sourceList.appendChild(currentLine);
    }
    codePre.appendChild(sourceList);

    if (view.source.constantPool.length === 0) return;

    const constantList = createElement("ol", "linenums constants");
    const constantListHeader = createElement("li", "");
    view.insertLineContent(constantListHeader,
      `Constant pool (size = ${view.source.constantPool.length})`);
    constantList.appendChild(constantListHeader);

    for (const [idx, constant] of view.source.constantPool.entries()) {
      const currentLine = createElement("li", `C${idx}`);
      view.insertLineContent(currentLine, `${idx}: ${constant}`);
      constantList.appendChild(currentLine);
    }
    codePre.appendChild(constantList);
  }

  private getBytecodeHeaderHtmlElementName(): string {
    return `source-pre-${this.source.sourceId}-header`;
  }

  private getBytecodeHtmlElementName(): string {
    return `source-pre-${this.source.sourceId}`;
  }

  private getSourceClass(): string {
    return this.codeMode == CodeMode.MainSource ? "main-source" : "inlined-source";
  }

  private insertLineContent(lineElement: HTMLElement, content: string): void {
    const lineContentElement = createElement("span", "", content);
    lineElement.appendChild(lineContentElement);
  }

  private insertLineNumber(lineElement: HTMLElement, lineNumber: number): void {
    const lineNumberElement = createElement("div", "line-number", String(lineNumber));
    lineElement.insertBefore(lineNumberElement, lineElement.firstChild);
  }
}
