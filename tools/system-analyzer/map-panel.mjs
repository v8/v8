// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import "./stats-panel.mjs";
import "./map-panel/map-details.mjs";
import "./map-panel/map-transitions.mjs";
import {SelectEvent} from './events.mjs';
import {V8Map} from "./map-processor.mjs";
import {defineCustomElement, V8CustomElement} from './helper.mjs';

defineCustomElement('map-panel', (templateText) =>
 class MapPanel extends V8CustomElement {
  #map;
  constructor() {
    super(templateText);
    this.searchBarBtn.addEventListener(
        'click', e => this.handleSearchBar(e));
    this.addEventListener(
      'mapdetailsupdate', e => this.handleUpdateMapDetails(e));
  }

  handleUpdateMapDetails(e){
    this.mapDetailsPanel.mapDetails = e.detail;
  }

  get statsPanel() {
    return this.$('#stats-panel');
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

  get mapDetails() {
    return this.mapDetailsPanel.mapDetails;
  }

  // send a timeline to the stats-panel
  get timeline() {
    return this.statsPanel.timeline;
  }
  set timeline(value) {
    console.assert(value !== undefined, "timeline undefined!");
    this.statsPanel.timeline = value;
    this.statsPanel.update();
  }
  get transitions() {
    return this.statsPanel.transitions;
  }
  set transitions(value) {
    this.statsPanel.transitions = value;
  }

  set map(value) {
    this.#map = value;
    this.mapTransitionsPanel.map = this.#map;
  }

  handleSearchBar(e){
    let searchBar = this.$('#searchBarInput');
    let searchBarInput = searchBar.value;
    //access the map from model cache
    let selectedMap = V8Map.get(searchBarInput);
    if(selectedMap){
      searchBar.className = "success";
    } else {
      searchBar.className = "failure";
    }
    this.dispatchEvent(new SelectEvent(selectedMap));
  }

  set mapEntries(list){
    this.mapTransitionsPanel.mapEntries = list;
  }
  get mapEntries(){
    return this.mapTransitionsPanel.mapEntries;
  }

});
