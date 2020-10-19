// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
import { V8CustomElement, defineCustomElement } from "./helper.mjs";
import { SelectionEvent } from "./events.mjs";

defineCustomElement(
  "stats-panel",
  (templateText) =>
    class StatsPanel extends V8CustomElement {
      _timeline;
      _transitions;
      _selectedLogEntries;
      constructor() {
        super(templateText);
      }

      get stats() {
        return this.$("#stats");
      }

      set timeline(timeline) {
        this._timeline = timeline;
        this.selectedLogEntries = timeline.all
      }

      get timeline() {
        return this._timeline;
      }

      set selectedLogEntries(entries) {
        this._selectedLogEntries = entries;
        this.update();
      }

      set transitions(value) {
        this._transitions = value;
      }

      get transitions() {
        return this._transitions;
      }

      filterUniqueTransitions(filter) {
        // Returns a list of Maps whose parent is not in the list.
        return this._selectedLogEntries.filter((map) => {
          if (filter(map) === false) return false;
          let parent = map.parent();
          if (parent === undefined) return true;
          return filter(parent) === false;
        });
      }

      update() {
        this.removeAllChildren(this.stats);
        this.updateGeneralStats();
        this.updateNamedTransitionsStats();
      }

      updateGeneralStats() {
        console.assert(this._timeline !== undefined, "Timeline not set yet!");
        let pairs = [
          ["Total", null, (e) => true],
          ["Transitions", "primary", (e) => e.edge && e.edge.isTransition()],
          ["Fast to Slow", "violet", (e) => e.edge && e.edge.isFastToSlow()],
          ["Slow to Fast", "orange", (e) => e.edge && e.edge.isSlowToFast()],
          ["Initial Map", "yellow", (e) => e.edge && e.edge.isInitial()],
          [
            "Replace Descriptors",
            "red",
            (e) => e.edge && e.edge.isReplaceDescriptors(),
          ],
          [
            "Copy as Prototype",
            "red",
            (e) => e.edge && e.edge.isCopyAsPrototype(),
          ],
          [
            "Optimize as Prototype",
            null,
            (e) => e.edge && e.edge.isOptimizeAsPrototype(),
          ],
          ["Deprecated", null, (e) => e.isDeprecated()],
          ["Bootstrapped", "green", (e) => e.isBootstrapped()],
        ];

        let text = "";
        let tableNode = this.table("transitionType");
        tableNode.innerHTML =
          "<thead><tr><td>Color</td><td>Type</td><td>Count</td>" +
          "<td>Percent</td></tr></thead>";
        let name, filter;
        let total = this._selectedLogEntries.length;
        pairs.forEach(([name, color, filter]) => {
          let row = this.tr();
          if (color !== null) {
            row.appendChild(this.td(this.div(["colorbox", color])));
          } else {
            row.appendChild(this.td(""));
          }
          row.classList.add('clickable');
          row.onclick = (e) => {
            // lazily compute the stats
            let node = e.target.parentNode;
            if (node.maps == undefined) {
              node.maps = this.filterUniqueTransitions(filter);
            }
            this.dispatchEvent(new SelectionEvent(node.maps));
          };
          row.appendChild(this.td(name));
          let count = this.count(filter);
          row.appendChild(this.td(count));
          let percent = Math.round((count / total) * 1000) / 10;
          row.appendChild(this.td(percent.toFixed(1) + "%"));
          tableNode.appendChild(row);
        });
        this.stats.appendChild(tableNode);
      }

      count(filter) {
        let count = 0;
        for (const map of this._selectedLogEntries) {
          if (filter(map)) count++;
        }
        return count;
      }

      updateNamedTransitionsStats() {
        let tableNode = this.table("transitionTable");
        let nameMapPairs = Array.from(this.transitions.entries());
        tableNode.innerHTML =
          "<thead><tr><td>Count</td><td>Propery Name</td></tr></thead>";
        nameMapPairs
          .sort((a, b) => b[1].length - a[1].length)
          .forEach(([name, maps]) => {
            let row = this.tr();
            row.maps = maps;
            row.classList.add('clickable');
            row.addEventListener("click", (e) =>
              this.dispatchEvent(
                new SelectionEvent(
                  e.target.parentNode.maps.map((map) => map.to)
                )
              )
            );
            row.appendChild(this.td(maps.length));
            row.appendChild(this.td(name));
            tableNode.appendChild(row);
          });
        this.stats.appendChild(tableNode);
      }
    }
);
