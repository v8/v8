// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {IcLogEntry} from '../log/ic.mjs';
import {MapLogEntry} from '../log/map.mjs';

import {FocusEvent, SelectionEvent, ToolTipEvent} from './events.mjs';
import {delay, DOM, formatBytes, V8CustomElement} from './helper.mjs';

DOM.defineCustomElement(
    'view/code-panel',
    (templateText) => class CodePanel extends V8CustomElement {
      _timeline;
      _selectedEntries;
      _entry;

      constructor() {
        super(templateText);
      }

      set timeline(timeline) {
        this._timeline = timeline;
        this.update();
      }

      set selectedEntries(entries) {
        this._selectedEntries = entries;
        // TODO: add code selection dropdown
        this._entry = entries.first();
        this.update();
      }

      set entry(entry) {
        this._entry = entry;
        this.update();
      }

      get codeNode() {
        return this.$('#code');
      }

      _update() {
        this.codeNode.innerText = this._entry?.disassemble ?? '';
      }
    });