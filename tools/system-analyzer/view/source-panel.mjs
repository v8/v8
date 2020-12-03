// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {IcLogEntry} from '../log/ic.mjs';
import {MapLogEntry} from '../log/map.mjs';

import {FocusEvent, SelectionEvent, ToolTipEvent} from './events.mjs';
import {delay, DOM, formatBytes, V8CustomElement} from './helper.mjs';

DOM.defineCustomElement('view/source-panel',
                        (templateText) =>
                            class SourcePanel extends V8CustomElement {
  _selectedSourcePositions = [];
  _sourcePositionsToMarkNodes;
  _scripts = [];
  _script;

  constructor() {
    super(templateText);
    this.scriptDropdown.addEventListener(
        'change', e => this._handleSelectScript(e));
  }

  get script() {
    return this.$('#script');
  }

  get scriptNode() {
    return this.$('.scriptNode');
  }

  set script(script) {
    if (this._script === script) return;
    this._script = script;
    this._renderSourcePanel();
    this._updateScriptDropdownSelection();
  }

  set selectedSourcePositions(sourcePositions) {
    this._selectedSourcePositions = sourcePositions;
    // TODO: highlight multiple scripts
    this.script = sourcePositions[0]?.script;
    this._focusSelectedMarkers();
  }

  set data(scripts) {
    this._scripts = scripts;
    this._initializeScriptDropdown();
  }

  get scriptDropdown() {
    return this.$('#script-dropdown');
  }

  _initializeScriptDropdown() {
    this._scripts.sort((a, b) => a.name.localeCompare(b.name));
    let select = this.scriptDropdown;
    select.options.length = 0;
    for (const script of this._scripts) {
      const option = document.createElement('option');
      const size = formatBytes(script.source.length);
      option.text = `${script.name} (id=${script.id} size=${size})`;
      option.script = script;
      select.add(option);
    }
  }
  _updateScriptDropdownSelection() {
    this.scriptDropdown.selectedIndex =
        this._script ? this._scripts.indexOf(this._script) : -1;
  }

  async _renderSourcePanel() {
    let scriptNode;
    if (this._script) {
      await delay(1);
      const builder =
          new LineBuilder(this, this._script, this._selectedSourcePositions);
      scriptNode = builder.createScriptNode();
      this._sourcePositionsToMarkNodes = builder.sourcePositionToMarkers;
    } else {
      scriptNode = DOM.div();
      this._selectedMarkNodes = undefined;
    }
    const oldScriptNode = this.script.childNodes[1];
    this.script.replaceChild(scriptNode, oldScriptNode);
  }

  async _focusSelectedMarkers() {
    await delay(100);
    // Remove all marked nodes.
    for (let markNode of this._sourcePositionsToMarkNodes.values()) {
      markNode.className = '';
    }
    for (let sourcePosition of this._selectedSourcePositions) {
      this._sourcePositionsToMarkNodes.get(sourcePosition).className = 'marked';
    }
    const sourcePosition = this._selectedSourcePositions[0];
    if (!sourcePosition) return;
    const markNode = this._sourcePositionsToMarkNodes.get(sourcePosition);
    markNode.scrollIntoView(
        {behavior: 'smooth', block: 'nearest', inline: 'center'});
  }

  _handleSelectScript(e) {
    const option =
        this.scriptDropdown.options[this.scriptDropdown.selectedIndex];
    this.script = option.script;
    this.selectLogEntries(this._script.entries);
  }

  handleSourcePositionClick(e) {
    const sourcePosition = e.target.sourcePosition;
    this.selectLogEntries(sourcePosition.entries);
    this.dispatchEvent(new SelectionEvent([sourcePosition]));
  }

  handleSourcePositionMouseOver(e) {
    const entries = e.target.sourcePosition.entries;
    let list =
        entries
            .map(entry => `${entry.__proto__.constructor.name}: ${entry.type}`)
            .join('<br/>');
    this.dispatchEvent(new ToolTipEvent(list, e.target));
  }

  selectLogEntries(logEntries) {
    this.dispatchEvent(new SelectionEvent(logEntries));
  }
});

class SourcePositionIterator {
  _entries;
  _index = 0;
  constructor(sourcePositions) {
    this._entries = sourcePositions;
  }

  * forLine(lineIndex) {
    this._findStart(lineIndex);
    while (!this._done() && this._current().line === lineIndex) {
      yield this._current();
      this._next();
    }
  }

  _findStart(lineIndex) {
    while (!this._done() && this._current().line < lineIndex) {
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

function* lineIterator(source) {
  let current = 0;
  let line = 1;
  while (current < source.length) {
    const next = source.indexOf('\n', current);
    if (next === -1) break;
    yield [line, source.substring(current, next)];
    line++;
    current = next + 1;
  }
  if (current < source.length) yield [line, source.substring(current)];
}

class LineBuilder {
  _script;
  _clickHandler;
  _mouseoverHandler;
  _sourcePositions;
  _selection;
  _sourcePositionToMarkers = new Map();

  constructor(panel, script, highlightPositions) {
    this._script = script;
    this._selection = new Set(highlightPositions);
    this._clickHandler = panel.handleSourcePositionClick.bind(panel);
    this._mouseoverHandler = panel.handleSourcePositionMouseOver.bind(panel);
    // TODO: sort on script finalization.
    script.sourcePositions.sort((a, b) => {
      if (a.line === b.line) return a.column - b.column;
      return a.line - b.line;
    });
    this._sourcePositions = new SourcePositionIterator(script.sourcePositions);
  }

  get sourcePositionToMarkers() {
    return this._sourcePositionToMarkers;
  }

  createScriptNode() {
    const scriptNode = DOM.div('scriptNode');
    for (let [lineIndex, line] of lineIterator(this._script.source)) {
      scriptNode.appendChild(this._createLineNode(lineIndex, line));
    }
    return scriptNode;
  }

  _createLineNode(lineIndex, line) {
    const lineNode = DOM.span();
    let columnIndex = 0;
    for (const sourcePosition of this._sourcePositions.forLine(lineIndex)) {
      const nextColumnIndex = sourcePosition.column - 1;
      lineNode.appendChild(document.createTextNode(
          line.substring(columnIndex, nextColumnIndex)));
      columnIndex = nextColumnIndex;

      lineNode.appendChild(
          this._createMarkerNode(line[columnIndex], sourcePosition));
      columnIndex++;
    }
    lineNode.appendChild(
        document.createTextNode(line.substring(columnIndex) + '\n'));
    return lineNode;
  }

  _createMarkerNode(text, sourcePosition) {
    const marker = document.createElement('mark');
    this._sourcePositionToMarkers.set(sourcePosition, marker);
    marker.textContent = text;
    marker.sourcePosition = sourcePosition;
    marker.onclick = this._clickHandler;
    marker.onmouseover = this._mouseoverHandler;
    return marker;
  }
}
