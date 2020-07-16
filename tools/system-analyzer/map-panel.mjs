// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import "./stats-panel.mjs";
import {V8Map} from "./map-processor.mjs";
import {defineCustomElement, V8CustomElement} from './helper.mjs';

defineCustomElement('map-panel', (templateText) =>
 class MapPanel extends V8CustomElement {
  constructor() {
    super(templateText);
    this.transitionView.addEventListener(
        'mousemove', e => this.handleTransitionViewChange(e));
    this.$('#searchBarBtn').addEventListener(
        'click', e => this.handleSearchBar(e));
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

  //TODO(zcankara) Take view, transitionView logic inside map panel
  //TODO(zcankara) VIEW RELATED
  updateStats(timeline) {
    this.timeline = timeline;
  }

  updateMapDetails(map) {
    let details = '';
    if (map) {
      details += 'ID: ' + map.id;
      details += '\nSource location: ' + map.filePosition;
      details += '\n' + map.description;
    }
    this.mapDetails.innerText = details;
  }

  //TODO(zcankara) DOM RELATED
  div(classes) {
    let node = document.createElement('div');
    if (classes !== void 0) {
      if (typeof classes === 'string') {
        node.classList.add(classes);
      } else {
        classes.forEach(cls => node.classList.add(cls));
      }
    }
    return node;
  }

  removeAllChildren(node) {
    while (node.lastChild) {
      node.removeChild(node.lastChild);
    }
  }

  //TODO(zcankara) TRANSITIONVIEW RELATED
  set transitionView(state){
    this.state = state;
    this.container = this.transitionView;
    this.currentNode = this.transitionView;
    this.currentMap = undefined;
  }

  selectMap(map) {
    this.currentMap = map;
    this.state.map = map;
  }

  showMap(map) {
    this.updateMapDetails(map);
    if (this.currentMap === map) return;
    this.currentMap = map;
    this._showMaps([map]);
  }

  showMaps(list) {
    this.state.view.isLocked = true;
    this._showMaps(list);
  }

  _showMaps(list) {
    // Hide the container to avoid any layouts.
    this.container.style.display = 'none';
    this.removeAllChildren(this.container);
    list.forEach(map => this.addMapAndParentTransitions(map));
    this.container.style.display = ''
  }

  addMapAndParentTransitions(map) {
    if (map === void 0) return;
    this.currentNode = this.container;
    let parents = map.getParents();
    if (parents.length > 0) {
      this.addTransitionTo(parents.pop());
      parents.reverse().forEach(each => this.addTransitionTo(each));
    }
    let mapNode = this.addSubtransitions(map);
    // Mark and show the selected map.
    mapNode.classList.add('selected');
    if (this.selectedMap == map) {
      setTimeout(
          () => mapNode.scrollIntoView(
              {behavior: 'smooth', block: 'nearest', inline: 'nearest'}),
          1);
    }
  }

  addMapNode(map) {
    let node = this.div('map');
    if (map.edge) node.style.backgroundColor = map.edge.getColor();
    node.map = map;
    node.addEventListener('click', () => this.selectMap(map));
    if (map.children.length > 1) {
      node.innerText = map.children.length;
      let showSubtree = this.div('showSubtransitions');
      showSubtree.addEventListener('click', (e) => this.toggleSubtree(e, node));
      node.appendChild(showSubtree);
    } else if (map.children.length == 0) {
      node.innerHTML = '&#x25CF;'
    }
    this.currentNode.appendChild(node);
    return node;
  }

  addSubtransitions(map) {
    let mapNode = this.addTransitionTo(map);
    // Draw outgoing linear transition line.
    let current = map;
    while (current.children.length == 1) {
      current = current.children[0].to;
      this.addTransitionTo(current);
    }
    return mapNode;
  }

  addTransitionEdge(map) {
    let classes = ['transitionEdge'];
    let edge = this.div(classes);
    edge.style.backgroundColor = map.edge.getColor();
    let labelNode = this.div('transitionLabel');
    labelNode.innerText = map.edge.toString();
    edge.appendChild(labelNode);
    return edge;
  }

  addTransitionTo(map) {
    // transition[ transitions[ transition[...], transition[...], ...]];

    let transition = this.div('transition');
    if (map.isDeprecated()) transition.classList.add('deprecated');
    if (map.edge) {
      transition.appendChild(this.addTransitionEdge(map));
    }
    let mapNode = this.addMapNode(map);
    transition.appendChild(mapNode);

    let subtree = this.div('transitions');
    transition.appendChild(subtree);

    this.currentNode.appendChild(transition);
    this.currentNode = subtree;

    return mapNode;
  }

  toggleSubtree(event, node) {
    let map = node.map;
    event.target.classList.toggle('opened');
    let transitionsNode = node.parentElement.querySelector('.transitions');
    let subtransitionNodes = transitionsNode.children;
    if (subtransitionNodes.length <= 1) {
      // Add subtransitions excepth the one that's already shown.
      let visibleTransitionMap = subtransitionNodes.length == 1 ?
          transitionsNode.querySelector('.map').map :
          void 0;
      map.children.forEach(edge => {
        if (edge.to != visibleTransitionMap) {
          this.currentNode = transitionsNode;
          this.addSubtransitions(edge.to);
        }
      });
    } else {
      // remove all but the first (currently selected) subtransition
      for (let i = subtransitionNodes.length - 1; i > 0; i--) {
        transitionsNode.removeChild(subtransitionNodes[i]);
      }
    }
  }

});

