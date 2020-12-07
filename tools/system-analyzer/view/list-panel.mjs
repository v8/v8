// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {SourcePosition} from '../../profile.mjs';
import {IcLogEntry} from '../log/ic.mjs';
import {MapLogEntry} from '../log/map.mjs';

import {FocusEvent, SelectionEvent, SelectTimeEvent} from './events.mjs';
import {groupBy, LazyTable} from './helper.mjs';
import {DOM, V8CustomElement} from './helper.mjs';

DOM.defineCustomElement('view/list-panel',
                        (templateText) =>
                            class ListPanel extends V8CustomElement {
  _selectedLogEntries;
  _selectedLogEntry;
  _timeline;

  _detailsClickHandler = this._handleDetailsClick.bind(this);
  _mapClickHandler = this._handleMapClick.bind(this);
  _fileClickHandler = this._handleFilePositionClick.bind(this);

  constructor() {
    super(templateText);
    this.groupKey.addEventListener('change', e => this.update());
  }

  static get observedAttributes() {
    return ['title'];
  }

  attributeChangedCallback(name, oldValue, newValue) {
    if (name == 'title') {
      this.$('#title').innerHTML = newValue;
    }
  }

  set timeline(value) {
    console.assert(value !== undefined, 'timeline undefined!');
    this._timeline = value;
    this.selectedLogEntries = this._timeline.all;
    this.initGroupKeySelect();
  }

  set selectedLogEntries(entries) {
    this._selectedLogEntries = entries;
    this.update();
  }

  set selectedLogEntry(entry) {
    // TODO: show details
  }

  get entryClass() {
    return this._timeline.at(0)?.constructor;
  }

  get groupKey() {
    return this.$('#group-key');
  }

  get table() {
    return this.$('#table');
  }

  get spanSelectAll() {
    return this.querySelectorAll('span');
  }

  get _propertyNames() {
    return this.entryClass?.propertyNames ?? [];
  }

  _update() {
    DOM.removeAllChildren(this.table);
    const propertyName = this.groupKey.selectedOptions[0].text;
    const groups =
        groupBy(this._selectedLogEntries, each => each[propertyName], true);
    this._render(groups, this.table);
  }

  createSubgroups(group) {
    const map = new Map();
    for (let propertyName of this._propertyNames) {
      map.set(
          propertyName,
          groupBy(group.entries, each => each[propertyName], true));
    }
    return map;
  }

  _handleMapClick(e) {
    const group = e.currentTarget.group;
    this.dispatchEvent(new FocusEvent(group.entries[0].map));
  }

  _handleFilePositionClick(e) {
    const group = e.currentTarget.group;
    const sourcePosition = group.entries[0].sourcePosition;
    this.dispatchEvent(new FocusEvent(sourcePosition));
  }

  _render(groups, table) {
    let last;
    new LazyTable(table, groups, group => {
      if (last && last.count < group.count) {
        console.log(last, group);
      }
      last = group;
      const tr = DOM.tr();
      tr.group = group;
      const details = tr.appendChild(DOM.td('', 'toggle'));
      details.onclick = this._detailsClickHandler;
      tr.appendChild(DOM.td(`${group.percent.toFixed(2)}%`, 'percentage'));
      tr.appendChild(DOM.td(group.count, 'count'));
      const valueTd = tr.appendChild(DOM.td(`${group.key}`, 'key'));
      if (group.key instanceof MapLogEntry) {
        tr.onclick = this._mapClickHandler;
        valueTd.classList.add('clickable');
      } else if (group.key instanceof SourcePosition) {
        valueTd.classList.add('clickable');
        tr.onclick = this._fileClickHandler;
      }
      return tr;
    }, 10);
  }

  _handleDetailsClick(event) {
    event.stopPropagation();
    const tr = event.target.parentNode;
    const group = tr.group;
    // Create subgroup in-place if the don't exist yet.
    if (tr.groups === undefined) {
      const groups = tr.groups = this.createSubgroups(group);
      this.renderDrilldown(groups, tr);
    }
    const detailsTr = tr.nextSibling;
    if (tr.classList.contains('open')) {
      tr.classList.remove('open');
      detailsTr.style.display = 'none';
    } else {
      tr.classList.add('open');
      detailsTr.style.display = 'table-row';
    }
  }

  renderDrilldown(groups, previousSibling) {
    const tr = DOM.tr('entry-details');
    tr.style.display = 'none';
    // indent by one td.
    tr.appendChild(DOM.td());
    const td = DOM.td();
    td.colSpan = 3;
    groups.forEach((group, key) => {
      this.renderDrilldownGroup(td, group, key);
    });
    tr.appendChild(td);
    // Append the new TR after previousSibling.
    previousSibling.parentNode.insertBefore(tr, previousSibling.nextSibling);
  }

  renderDrilldownGroup(td, group, key) {
    const div = DOM.div('drilldown-group-title');
    div.textContent = `Grouped by ${key}`;
    td.appendChild(div);
    const table = DOM.table();
    this._render(group, table, false)
    td.appendChild(table);
  }

  initGroupKeySelect() {
    const select = this.groupKey;
    select.options.length = 0;
    for (const propertyName of this._propertyNames) {
      const option = DOM.element('option');
      option.text = propertyName;
      select.add(option);
    }
  }
});
