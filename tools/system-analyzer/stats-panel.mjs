// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {div, table, tr, td} from './map-model.mjs';

defineCustomElement('stats-panel', (templateText) =>
 class StatsPanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.timeline_ = undefined;
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get stats() {
    return this.$('#stats');
  }


  // decouple stats panel
  removeAllChildren(node) {
    while (node.lastChild) {
      node.removeChild(node.lastChild);
    }
  }

  set timeline(value){
    this.timeline_ = value;
  }

  get timeline(){
    return this.timeline_;
  }

  set timeline(value){
    this.timeline_ = value;
  }

  get timeline(){
    return this.timeline_;
  }

  update() {
    this.removeAllChildren(this.stats);
    this.updateGeneralStats();
    this.updateNamedTransitionsStats();
  }

  updateGeneralStats() {
    console.assert(this.timeline_ !== undefined, "Timeline not set yet!");
    let pairs = [
      ['Total', null, e => true],
      ['Transitions', 'black', e => e.edge && e.edge.isTransition()],
      ['Fast to Slow', 'violet', e => e.edge && e.edge.isFastToSlow()],
      ['Slow to Fast', 'orange', e => e.edge && e.edge.isSlowToFast()],
      ['Initial Map', 'yellow', e => e.edge && e.edge.isInitial()],
      [
        'Replace Descriptors', 'red',
        e => e.edge && e.edge.isReplaceDescriptors()
      ],
      ['Copy as Prototype', 'red', e => e.edge && e.edge.isCopyAsPrototype()],
      [
        'Optimize as Prototype', null,
        e => e.edge && e.edge.isOptimizeAsPrototype()
      ],
      ['Deprecated', null, e => e.isDeprecated()],
      ['Bootstrapped', 'green', e => e.isBootstrapped()],
    ];

    let text = '';
    let tableNode = table('transitionType');
    tableNode.innerHTML =
        '<thead><tr><td>Color</td><td>Type</td><td>Count</td><td>Percent</td></tr></thead>';
    let name, filter;
    //TODO(zc) timeline
    let total = this.timeline.size();
    pairs.forEach(([name, color, filter]) => {
      let row = tr();
      if (color !== null) {
        row.appendChild(td(div(['colorbox', color])));
      } else {
        row.appendChild(td(''));
      }
      row.onclick = (e) => {
        // lazily compute the stats
        let node = e.target.parentNode;
        if (node.maps == undefined) {
          node.maps = this.timeline.filterUniqueTransitions(filter);
        }
        this.dispatchEvent(new CustomEvent(
          'change', {bubbles: true, composed: true, detail: node.maps}));
      };
      row.appendChild(td(name));
      let count = this.timeline.count(filter);
      row.appendChild(td(count));
      let percent = Math.round(count / total * 1000) / 10;
      row.appendChild(td(percent.toFixed(1) + '%'));
      tableNode.appendChild(row);
    });
    this.stats.appendChild(tableNode);
  }

  updateNamedTransitionsStats() {
    let tableNode = table('transitionTable');
    let nameMapPairs = Array.from(this.timeline.transitions.entries());
    tableNode.innerHTML =
        '<thead><tr><td>Propery Name</td><td>#</td></tr></thead>';
    nameMapPairs.sort((a, b) => b[1].length - a[1].length).forEach(([
                                                                     name, maps
                                                                   ]) => {
      let row = tr();
      row.maps = maps;
     row.addEventListener(
      'click',
      e => this.dispatchEvent(new CustomEvent(
        'change', {bubbles: true, composed: true, detail: e.target.parentNode.maps.map(map => map.to)})));
      row.appendChild(td(name));
      row.appendChild(td(maps.length));
      tableNode.appendChild(row);
    });
    this.stats.appendChild(tableNode);
  }
});
