// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import {V8CustomElement, defineCustomElement} from './helper.mjs';

defineCustomElement('stats-panel', (templateText) =>
 class StatsPanel extends V8CustomElement {
  constructor() {
    super(templateText);
    this.timeline_ = undefined;
  }

  get stats() {
    return this.$('#stats');
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
    let tableNode = this.table('transitionType');
    tableNode.innerHTML =
        '<thead><tr><td>Color</td><td>Type</td><td>Count</td><td>Percent</td></tr></thead>';
    let name, filter;
    //TODO(zc) timeline
    let total = this.timeline.size();
    pairs.forEach(([name, color, filter]) => {
      let row = this.tr();
      if (color !== null) {
        row.appendChild(this.td(this.div(['colorbox', color])));
      } else {
        row.appendChild(this.td(''));
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
      row.appendChild(this.td(name));
      let count = this.timeline.count(filter);
      row.appendChild(this.td(count));
      let percent = Math.round(count / total * 1000) / 10;
      row.appendChild(this.td(percent.toFixed(1) + '%'));
      tableNode.appendChild(row);
    });
    this.stats.appendChild(tableNode);
  }

  updateNamedTransitionsStats() {
    let tableNode = this.table('transitionTable');
    let nameMapPairs = Array.from(this.timeline.transitions.entries());
    tableNode.innerHTML =
        '<thead><tr><td>Propery Name</td><td>#</td></tr></thead>';
    nameMapPairs.sort((a, b) => b[1].length - a[1].length).forEach(([
                                                                     name, maps
                                                                   ]) => {
      let row = this.tr();
      row.maps = maps;
     row.addEventListener(
      'click',
      e => this.dispatchEvent(new CustomEvent(
        'change', {bubbles: true, composed: true, detail: e.target.parentNode.maps.map(map => map.to)})));
      row.appendChild(this.td(name));
      row.appendChild(this.td(maps.length));
      tableNode.appendChild(row);
    });
    this.stats.appendChild(tableNode);
  }
});
