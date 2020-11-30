// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import './stats-panel.mjs';
import './map-panel/map-details.mjs';
import './map-panel/map-transitions.mjs';

import {MapLogEntry} from '../log/map.mjs';

import {FocusEvent} from './events.mjs';
import {DOM, V8CustomElement} from './helper.mjs';

DOM.defineCustomElement('view/map-panel',
                        (templateText) =>
                            class MapPanel extends V8CustomElement {
  _map;
  constructor() {
    super(templateText);
    this.searchBarBtn.addEventListener('click', e => this.handleSearchBar(e));
    this.addEventListener(FocusEvent.name, e => this.handleUpdateMapDetails(e));
  }

  handleUpdateMapDetails(e) {
    if (e.entry instanceof MapLogEntry) {
      this.mapDetailsPanel.map = e.entry;
    }
  }

  get mapTransitionsPanel() {
    return this.$('#map-transitions');
  }

  get mapDetailsPanel() {
    return this.$('#map-details');
  }

  get searchBarBtn() {
    return this.$('#searchBarBtn');
  }

  get searchBar() {
    return this.$('#searchBar');
  }

  set timeline(timeline) {
    this._timeline = timeline;
    this.mapTransitionsPanel.timeline = timeline;
  }

  set map(map) {
    this._map = map;
    this.mapTransitionsPanel.map = map;
    this.mapDetailsPanel.map = map;
  }

  handleSearchBar(e) {
    let searchBar = this.$('#searchBarInput');
    let searchBarInput = searchBar.value;
    // access the map from model cache
    let selectedMap = MapLogEntry.get(searchBarInput);
    if (selectedMap) {
      searchBar.className = 'success';
      this.dispatchEvent(new FocusEvent(selectedMap));
    } else {
      searchBar.className = 'failure';
    }
  }

  set selectedMapLogEntries(list) {
    this.mapTransitionsPanel.selectedMapLogEntries = list;
    if (list.length === 1) this.mapDetailsPanel.map = list[0];
  }
  get selectedMapLogEntries() {
    return this.mapTransitionsPanel.selectedMapLogEntries;
  }
});
