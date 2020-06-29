// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

defineCustomElement('map-panel', (templateText) =>
 class MapPanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.transitionViewSelect.addEventListener(
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

  get transitionViewSelect() {
    return this.$('#transitionView');
  }

  get searchBarSelect() {
    return this.$('#searchBar');
  }

  get mapDetailsSelect() {
    return this.$('#mapDetails');
  }

  get tooltipSelect() {
    return this.$('#tooltip');
  }
  get tooltipContentsSelect() {
    return this.$('#tooltipContents');
  }

  handleTransitionViewChange(e){
    this.tooltipSelect.style.left = e.pageX + "px";
    this.tooltipSelect.style.top = e.pageY + "px";
    let map = e.target.map;
    if (map) {
        this.tooltipContentsSelect.innerText = map.description;
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
      searchBar.className = "green";
    } else {
      dataModel.isValidMap = false;
      searchBar.className = "red";
    }
    this.dispatchEvent(new CustomEvent(
      'click', {bubbles: true, composed: true, detail: dataModel}));
  }
});
