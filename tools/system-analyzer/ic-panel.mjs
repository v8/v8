// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Group, properties} from './ic-model.js';

defineCustomElement('ic-panel', (templateText) =>
 class ICPanel extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this.groupKeySelect.addEventListener(
        'change', e => this.updateTable(e));
    this.filterICBtnSelect.addEventListener(
        'click', e => this.handleICFilter(e));
    this._noOfItems = 100;
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  get groupKeySelect() {
    return this.$('#group-key');
  }

  get filterICBtnSelect() {
    return this.$('#filterICBtn');
  }

  get tableSelect() {
    return this.$('#table');
  }

  get tableBodySelect() {
    return this.$('#table-body');
  }

  get countSelect() {
    return this.$('#count');
  }

  get spanSelectAll(){
    return this.querySelectorAll("span");
  }

  updateTable(event) {
    let select = this.groupKeySelect;
    let key = select.options[select.selectedIndex].text;
    let tableBody = this.tableBodySelect;
    this.removeAllChildren(tableBody);
    let groups = Group.groupBy(entries, key, true);
    this.display(groups, tableBody);
  }

  escapeHtml(unsafe) {
    if (!unsafe) return "";
    return unsafe.toString()
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;")
      .replace(/'/g, "&#039;");
  }
  processValue(unsafe) {
    if (!unsafe) return "";
    if (!unsafe.startsWith("http")) return this.escapeHtml(unsafe);
    let a = document.createElement("a");
    a.href = unsafe;
    a.textContent = unsafe;
    return a;
  }

  removeAllChildren(node) {
    while (node.firstChild) {
      node.removeChild(node.firstChild);
    }
  }

  td(tr, content, className) {
    let node = document.createElement("td");
    if (typeof content == "object") {
      node.appendChild(content);
    } else {
      node.innerHTML = content;
    }
    node.className = className;
    tr.appendChild(node);
    return node
  }

  set noOfItems(value){
    this._noOfItems = value;
  }

  get noOfItems(){
    return this._noOfItems;
  }


  display(entries, parent) {
    let fragment = document.createDocumentFragment();
    let max = Math.min(this.noOfItems, entries.length)
    for (let i = 0; i < max; i++) {
      let entry = entries[i];
      let tr = document.createElement("tr");
      tr.entry = entry;
      let details = this.td(tr,'<span>&#8505;</a>', 'details');
      details.onclick = _ => this.toggleDetails(details);
      this.td(tr, entry.percentage + "%", 'percentage');
      this.td(tr, entry.count, 'count');
      this.td(tr, this.processValue(entry.key), 'key');
      fragment.appendChild(tr);
    }
    let omitted = entries.length - max;
    if (omitted > 0) {
      let tr = document.createElement("tr");
      let tdNode = td(tr, 'Omitted ' + omitted + " entries.");
      tdNode.colSpan = 4;
      fragment.appendChild(tr);
    }
    parent.appendChild(fragment);
  }

  displayDrilldown(entry, previousSibling) {
    let tr = document.createElement('tr');
    tr.className = "entry-details";
    tr.style.display = "none";
    // indent by one td.
    tr.appendChild(document.createElement("td"));
    let td = document.createElement("td");
    td.colSpan = 3;
    for (let key in entry.groups) {
      td.appendChild(this.displayDrilldownGroup(entry, key));
    }
    tr.appendChild(td);
    // Append the new TR after previousSibling.
    previousSibling.parentNode.insertBefore(tr, previousSibling.nextSibling)
  }

  displayDrilldownGroup(entry, key) {
    let max = 20;
    let group = entry.groups[key];
    let div = document.createElement("div")
    div.className = 'drilldown-group-title'
    div.textContent = key + ' [top ' + max + ' out of ' + group.length + ']';
    let table = document.createElement("table");
    this.display(group.slice(0, max), table, false)
    div.appendChild(table);
    return div;
  }

 toggleDetails(node) {
  let tr = node.parentNode;
  let entry = tr.entry;
  // Create subgroup in-place if the don't exist yet.
  if (entry.groups === undefined) {
    entry.createSubGroups();
    this.displayDrilldown(entry, tr);
  }
  let details = tr.nextSibling;
  let display = details.style.display;
  if (display != "none") {
    display = "none";
  } else {
    display = "table-row"
  };
  details.style.display = display;
  }

  initGroupKeySelect() {
    let select = this.groupKeySelect;
    select.options.length = 0;
    for (let i in properties) {
      let option = document.createElement("option");
      option.text = properties[i];
      select.add(option);
    }
  }

  //TODO(zc): Function processing the timestamps of ICEvents
  // Processes the IC Events which have V8Map's in the map-processor
  processICEventTime(){
    let ICTimeToEvent = new Map();
    // save the occurance time of V8Maps
    let eventTimes = []
    console.log("Num of stats: " + entries.length);
    // fetch V8 Maps from the IC Events
    entries.forEach(element => {
      let v8Map = V8Map.get("0x" + element.map);
      if(!v8Map){
        ICTimeToEvent.set(-1, element);
      } else {
        ICTimeToEvent.set(v8Map.time, element);
        eventTimes.push(v8Map.time);
      }
    });
    eventTimes.sort();
    // save the IC events which have Map states
    let eventsList = [];
    for(let i = 0;  i < eventTimes.length; i++){
      eventsList.push(ICTimeToEvent.get(eventTimes[i]));
    }
    return eventList;
  }

  handleICFilter(e){
    let noOfItemsInput = parseInt(this.$('#filter-input').value);
    this.noOfItems = noOfItemsInput;
    this.updateTable(e);
  }
});
