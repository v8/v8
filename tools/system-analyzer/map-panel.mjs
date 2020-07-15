// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import "./stats-panel.mjs";
import {V8Map} from "./map-processor.mjs";

defineCustomElement('map-panel', (templateText) =>
 class MapPanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.transitionView.addEventListener(
        'mousemove', e => this.handleTransitionViewChange(e));
    this.$('#searchBarBtn').addEventListener(
        'click', e => this.handleSearchBar(e));
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get transitionView() {
    return this.$('#transitionView');
  }

  get searchBar() {
    return this.$('#searchBar');
  }

  get mapDetails() {
    return this.$('#mapDetails');
  }

  get tooltip() {
    return this.$('#tooltip');
  }

  get tooltipContents() {
    return this.$('#tooltipContents');
  }

  get statsPanel() {
    return this.$('#stats-panel');
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


  handleTransitionViewChange(e){
    this.tooltip.style.left = e.pageX + "px";
    this.tooltip.style.top = e.pageY + "px";
    let map = e.target.map;
    if (map) {
        this.tooltipContents.innerText = map.description;
    }
  }

  handleSearchBar(e){
    let dataModel = Object.create(null);
    let searchBar = this.$('#searchBarInput');
    let searchBarInput = searchBar.value;
    //access the map from model cache
    let selectedMap = V8Map.get(searchBarInput);
    if(selectedMap){
      dataModel.isValidMap = true;
      dataModel.map = selectedMap;
      searchBar.className = "success";
    } else {
      dataModel.isValidMap = false;
      searchBar.className = "failure";
    }
    this.dispatchEvent(new CustomEvent(
      'click', {bubbles: true, composed: true, detail: dataModel}));
  }

});

