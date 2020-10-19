// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import { V8CustomElement, defineCustomElement } from "./helper.mjs";
import { SelectionEvent, FocusEvent } from "./events.mjs";
import { MapLogEntry } from "./log/map.mjs";
import { IcLogEntry } from "./log/ic.mjs";

defineCustomElement(
  "source-panel",
  (templateText) =>
    class SourcePanel extends V8CustomElement {
      _selectedSourcePositions;
      _scripts = [];
      _script;
      constructor() {
        super(templateText);
        this.scriptDropdown.addEventListener(
          'change', e => this.handleSelectScript(e));
      }
      get script() {
        return this.$('#script');
      }
      get scriptNode() {
        return this.$('.scriptNode');
      }
      set script(script) {
        this._script = script;
        // TODO: fix undefined scripts
        if (script !== undefined) this.renderSourcePanel();
      }
      set selectedSourcePositions(sourcePositions) {
        this._selectedSourcePositions = sourcePositions;
      }
      get selectedSourcePositions() {
        return this._selectedSourcePositions;
      }
      set data(value) {
        this._scripts = value;
        this.initializeScriptDropdown();
        this.script = this._scripts[0];
      }
      get scriptDropdown() {
        return this.$("#script-dropdown");
      }
      initializeScriptDropdown() {
        this._scripts.sort((a, b) => a.name.localeCompare(b.name));
        let select = this.scriptDropdown;
        select.options.length = 0;
        for (const script of this._scripts) {
          const option = document.createElement("option");
          option.text = `${script.name} (id=${script.id})`;
          option.script = script;
          select.add(option);
        }
      }

      renderSourcePanel() {
        const builder = new LineBuilder(this, this._script);
        const scriptNode = builder.createScriptNode();
        const oldScriptNode = this.script.childNodes[1];
        this.script.replaceChild(scriptNode, oldScriptNode);
      }

      handleSelectScript(e) {
        const option = this.scriptDropdown.options[this.scriptDropdown.selectedIndex];
        this.script = option.script;
      }

      handleSourcePositionClick(e) {
        let icLogEntries = [];
        let mapLogEntries = [];
        for (const entry of e.target.sourcePosition.entries) {
          if (entry instanceof MapLogEntry) {
            mapLogEntries.push(entry);
          } else if (entry instanceof IcLogEntry) {
            icLogEntries.push(entry);
          }
        }
        if (icLogEntries.length > 0 ) {
          this.dispatchEvent(new SelectionEvent(icLogEntries));
          this.dispatchEvent(new FocusEvent(icLogEntries[0]));
        }
        if (mapLogEntries.length > 0) {
          this.dispatchEvent(new SelectionEvent(mapLogEntries));
          this.dispatchEvent(new FocusEvent(mapLogEntries[0]));
        }
      }

    }
);


class SourcePositionIterator {
  _entries;
  _index = 0;
  constructor(sourcePositions) {
    this._entries = sourcePositions;
  }

  *forLine(lineIndex) {
    while(!this._done() && this._current().line === lineIndex) {
      yield this._current();
      this._next();
    }
  }

  _current() {
    return this._entries[this._index];
  }

  _done() {
    return this._index + 1 >= this._entries.length;
  }

  _next() {
    this._index++;
  }
}

function * lineIterator(source) {
  let current = 0;
  let line = 1;
  while(current < source.length) {
    const next = source.indexOf("\n", current);
    if (next === -1) break;
    yield [line, source.substring(current, next)];
    line++;
    current = next + 1;
  }
  if (current < source.length) yield [line, source.substring(current)];
}

class LineBuilder {
  _script
  _clickHandler
  _sourcePositions

  constructor(panel, script) {
    this._script = script;
    this._clickHandler = panel.handleSourcePositionClick.bind(panel);
    // TODO: sort on script finalization.
    script.sourcePositions.sort((a, b) => {
      if (a.line === b.line) return a.column - b.column;
      return a.line - b.line;
    })
    this._sourcePositions
        = new SourcePositionIterator(script.sourcePositions);

  }

  createScriptNode() {
    const scriptNode = document.createElement("pre");
    scriptNode.classList.add('scriptNode');
    for (let [lineIndex, line] of lineIterator(this._script.source)) {
      scriptNode.appendChild(this._createLineNode(lineIndex, line));
    }
    return scriptNode;
  }

  _createLineNode(lineIndex, line) {
    const lineNode = document.createElement("span");
    let columnIndex  = 0;
    for (const sourcePosition of this._sourcePositions.forLine(lineIndex)) {
      const nextColumnIndex = sourcePosition.column - 1;
      lineNode.appendChild(
          document.createTextNode(
            line.substring(columnIndex, nextColumnIndex)));
      columnIndex = nextColumnIndex;

      lineNode.appendChild(
          this._createMarkerNode(line[columnIndex], sourcePosition));
      columnIndex++;
    }
    lineNode.appendChild(
        document.createTextNode(line.substring(columnIndex) + "\n"));
    return lineNode;
  }

  _createMarkerNode(text, sourcePosition) {
    const marker = document.createElement("mark");
    marker.classList.add('marked');
    marker.textContent = text;
    marker.sourcePosition = sourcePosition;
    marker.onclick = this._clickHandler;
    return marker;
  }
}