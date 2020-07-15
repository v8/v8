// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {kChunkWidth, kChunkHeight} from './map-processor.mjs';

class State {
  constructor(mapPanelId, timelinePanelId) {
    this._nofChunks = 400;
    this._map = undefined;
    this._timeline = undefined;
    this._chunks = undefined;
    this._view = new View(this, mapPanelId, timelinePanelId);
    this._navigation = new Navigation(this, this.view);
  }
  set filteredEntries(value) {
    this._filteredEntries = value;
    if (this._filteredEntries) {
      //TODO(zcankara) update timeline view
    }
  }
  get filteredEntries() {
    return this._filteredEntries;
  }
  get timeline() {
    return this._timeline
  }
  set timeline(value) {
    this._timeline = value;
    this.updateChunks();
    this.view.updateTimeline();
    this.view.updateStats();
  }
  get chunks() {
    return this._chunks
  }
  get nofChunks() {
    return this._nofChunks
  }
  set nofChunks(count) {
    this._nofChunks = count;
    this.updateChunks();
    this.view.updateTimeline();
  }
  get view() {
    return this._view
  }
  get navigation() {
    return this._navigation
  }
  get map() {
    return this._map
  }
  set map(value) {
    this._map = value;
    this._navigation.updateUrl();
    this.view.updateMapDetails();
    this.view.redraw();
  }
  updateChunks() {
    this._chunks = this._timeline.chunks(this._nofChunks);
  }
  get entries() {
    if (!this.map) return {};
    return {
      map: this.map.id, time: this.map.time
    }
  }
}

// =========================================================================
// DOM Helper
function $(id) {
  return document.querySelector(id)
}

function removeAllChildren(node) {
  while (node.lastChild) {
    node.removeChild(node.lastChild);
  }
}

function selectOption(select, match) {
  let options = select.options;
  for (let i = 0; i < options.length; i++) {
    if (match(i, options[i])) {
      select.selectedIndex = i;
      return;
    }
  }
}

function div(classes) {
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

function table(className) {
  let node = document.createElement('table')
  if (className) node.classList.add(className)
  return node;
}

function td(textOrNode) {
  let node = document.createElement('td');
  if (typeof textOrNode === 'object') {
    node.appendChild(textOrNode);
  } else {
    node.innerText = textOrNode;
  }
  return node;
}

function tr() {
  return document.createElement('tr');
}

class Navigation {
  constructor(state, view) {
    this.state = state;
    this.view = view;
  }
  get map() {
    return this.state.map
  }
  set map(value) {
    this.state.map = value
  }
  get chunks() {
    return this.state.chunks
  }

  increaseTimelineResolution() {
    this.state.nofChunks *= 1.5;
  }

  decreaseTimelineResolution() {
    this.state.nofChunks /= 1.5;
  }

  selectNextEdge() {
    if (!this.map) return;
    if (this.map.children.length != 1) return;
    this.map = this.map.children[0].to;
  }

  selectPrevEdge() {
    if (!this.map) return;
    if (!this.map.parent()) return;
    this.map = this.map.parent();
  }

  selectDefaultMap() {
    this.map = this.chunks[0].at(0);
  }
  moveInChunks(next) {
    if (!this.map) return this.selectDefaultMap();
    let chunkIndex = this.map.chunkIndex(this.chunks);
    let chunk = this.chunks[chunkIndex];
    let index = chunk.indexOf(this.map);
    if (next) {
      chunk = chunk.next(this.chunks);
    } else {
      chunk = chunk.prev(this.chunks);
    }
    if (!chunk) return;
    index = Math.min(index, chunk.size() - 1);
    this.map = chunk.at(index);
  }

  moveInChunk(delta) {
    if (!this.map) return this.selectDefaultMap();
    let chunkIndex = this.map.chunkIndex(this.chunks)
    let chunk = this.chunks[chunkIndex];
    let index = chunk.indexOf(this.map) + delta;
    let map;
    if (index < 0) {
      map = chunk.prev(this.chunks).last();
    } else if (index >= chunk.size()) {
      map = chunk.next(this.chunks).first()
    } else {
      map = chunk.at(index);
    }
    this.map = map;
  }

  updateUrl() {
    let entries = this.state.entries;
    let params = new URLSearchParams(entries);
    window.history.pushState(entries, '', '?' + params.toString());
  }
}

class View {
  constructor(state, mapPanelId, timelinePanelId) {
    this.mapPanel_ = $(mapPanelId);
    this.timelinePanel_ = $(timelinePanelId);
    this.state = state;
    setInterval(this.updateOverviewWindow(timelinePanelId), 50);
    this.backgroundCanvas = document.createElement('canvas');
    this.transitionView =
        new TransitionView(state, this.mapPanel_.transitionView);
    this.isLocked = false;
    this._filteredEntries = [];
  }
  get chunks() {
    return this.state.chunks
  }
  get timeline() {
    return this.state.timeline
  }
  get map() {
    return this.state.map
  }

  updateStats() {
    this.mapPanel_.timeline = this.state.timeline;
  }

  updateMapDetails() {
    let details = '';
    if (this.map) {
      details += 'ID: ' + this.map.id;
      details += '\nSource location: ' + this.map.filePosition;
      details += '\n' + this.map.description;
    }
    this.mapPanel_.mapDetails.innerText = details;
    this.transitionView.showMap(this.map);
  }

  updateTimeline() {
    let chunksNode = this.timelinePanel_.timelineChunks;
    removeAllChildren(chunksNode);
    let chunks = this.chunks;
    let max = chunks.max(each => each.size());
    let start = this.timeline.startTime;
    let end = this.timeline.endTime;
    let duration = end - start;
    const timeToPixel = chunks.length * kChunkWidth / duration;
    let addTimestamp = (time, name) => {
      let timeNode = div('timestamp');
      timeNode.innerText = name;
      timeNode.style.left = ((time - start) * timeToPixel) + 'px';
      chunksNode.appendChild(timeNode);
    };
    let backgroundTodo = [];
    for (let i = 0; i < chunks.length; i++) {
      let chunk = chunks[i];
      let height = (chunk.size() / max * kChunkHeight);
      chunk.height = height;
      if (chunk.isEmpty()) continue;
      let node = div();
      node.className = 'chunk';
      node.style.left = (i * kChunkWidth) + 'px';
      node.style.height = height + 'px';
      node.chunk = chunk;
      node.addEventListener('mousemove', e => this.handleChunkMouseMove(e));
      node.addEventListener('click', e => this.handleChunkClick(e));
      node.addEventListener('dblclick', e => this.handleChunkDoubleClick(e));
      backgroundTodo.push([chunk, node])
      chunksNode.appendChild(node);
      chunk.markers.forEach(marker => addTimestamp(marker.time, marker.name));
    }

    this.asyncSetTimelineChunkBackground(backgroundTodo)

    // Put a time marker roughly every 20 chunks.
    let expected = duration / chunks.length * 20;
    let interval = (10 ** Math.floor(Math.log10(expected)));
    let correction = Math.log10(expected / interval);
    correction = (correction < 0.33) ? 1 : (correction < 0.75) ? 2.5 : 5;
    interval *= correction;

    let time = start;
    while (time < end) {
      addTimestamp(time, ((time - start) / 1000) + ' ms');
      time += interval;
    }
    this.drawOverview();
    this.redraw();
  }

  handleChunkMouseMove(event) {
    if (this.isLocked) return false;
    let chunk = event.target.chunk;
    if (!chunk) return;
    // topmost map (at chunk.height) == map #0.
    let relativeIndex =
        Math.round(event.layerY / event.target.offsetHeight * chunk.size());
    let map = chunk.at(relativeIndex);
    this.state.map = map;
  }

  handleChunkClick(event) {
    this.isLocked = !this.isLocked;
  }

  handleChunkDoubleClick(event) {
    this.isLocked = true;
    let chunk = event.target.chunk;
    if (!chunk) return;
    this.transitionView.showMaps(chunk.getUniqueTransitions());
  }

  asyncSetTimelineChunkBackground(backgroundTodo) {
    const kIncrement = 100;
    let start = 0;
    let delay = 1;
    while (start < backgroundTodo.length) {
      let end = Math.min(start + kIncrement, backgroundTodo.length);
      setTimeout((from, to) => {
        for (let i = from; i < to; i++) {
          let [chunk, node] = backgroundTodo[i];
          this.setTimelineChunkBackground(chunk, node);
        }
      }, delay++, start, end);
      start = end;
    }
  }

  setTimelineChunkBackground(chunk, node) {
    // Render the types of transitions as bar charts
    const kHeight = chunk.height;
    const kWidth = 1;
    this.backgroundCanvas.width = kWidth;
    this.backgroundCanvas.height = kHeight;
    let ctx = this.backgroundCanvas.getContext('2d');
    ctx.clearRect(0, 0, kWidth, kHeight);
    let y = 0;
    let total = chunk.size();
    let type, count;
    if (true) {
      chunk.getTransitionBreakdown().forEach(([type, count]) => {
        ctx.fillStyle = transitionTypeToColor(type);
        let height = count / total * kHeight;
        ctx.fillRect(0, y, kWidth, y + height);
        y += height;
      });
    } else {
      chunk.items.forEach(map => {
        ctx.fillStyle = transitionTypeToColor(map.getType());
        let y = chunk.yOffset(map);
        ctx.fillRect(0, y, kWidth, y + 1);
      });
    }

    let imageData = this.backgroundCanvas.toDataURL('image/webp', 0.2);
    node.style.backgroundImage = 'url(' + imageData + ')';
  }

  updateOverviewWindow() {
    let indicator = this.timelinePanel_.timelineOverviewIndicator;
    let totalIndicatorWidth =
        this.timelinePanel_.timelineOverview.offsetWidth;
    let div = this.timelinePanel_.timeline;
    let timelineTotalWidth =
        this.timelinePanel_.timelineCanvas.offsetWidth;
    let factor = totalIndicatorWidth / timelineTotalWidth;
    let width = div.offsetWidth * factor;
    let left = div.scrollLeft * factor;
    indicator.style.width = width + 'px';
    indicator.style.left = left + 'px';
  }

  drawOverview() {
    const height = 50;
    const kFactor = 2;
    let canvas = this.backgroundCanvas;
    canvas.height = height;
    canvas.width = window.innerWidth;
    let ctx = canvas.getContext('2d');
    let chunks = this.state.timeline.chunkSizes(canvas.width * kFactor);
    let max = chunks.max();

    ctx.clearRect(0, 0, canvas.width, height);
    ctx.fillStyle = 'white';
    ctx.beginPath();
    ctx.moveTo(0, height);
    for (let i = 0; i < chunks.length; i++) {
      ctx.lineTo(i / kFactor, height - chunks[i] / max * height);
    }
    ctx.lineTo(chunks.length, height);
    ctx.strokeStyle = 'white';
    ctx.stroke();
    ctx.closePath();
    ctx.fill();
    let imageData = canvas.toDataURL('image/webp', 0.2);
    this.timelinePanel_.timelineOverview.style.backgroundImage =
        'url(' + imageData + ')';
  }

  redraw() {
    let canvas = this.timelinePanel_.timelineCanvas;
    canvas.width = (this.chunks.length + 1) * kChunkWidth;
    canvas.height = kChunkHeight;
    let ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, canvas.width, kChunkHeight);
    if (!this.state.map) return;
    //TODO(zcankara) Redraw the IC events on canvas.
    this.drawEdges(ctx);
  }

  setMapStyle(map, ctx) {
    ctx.fillStyle = map.edge && map.edge.from ? 'white' : '#aedc6e';
  }

  setEdgeStyle(edge, ctx) {
    let color = edge.getColor();
    ctx.strokeStyle = color;
    ctx.fillStyle = color;
  }

  markMap(ctx, map) {
    let [x, y] = map.position(this.state.chunks);
    ctx.beginPath();
    this.setMapStyle(map, ctx);
    ctx.arc(x, y, 3, 0, 2 * Math.PI);
    ctx.fill();
    ctx.beginPath();
    ctx.fillStyle = 'white';
    ctx.arc(x, y, 2, 0, 2 * Math.PI);
    ctx.fill();
  }

  markSelectedMap(ctx, map) {
    let [x, y] = map.position(this.state.chunks);
    ctx.beginPath();
    this.setMapStyle(map, ctx);
    ctx.arc(x, y, 6, 0, 2 * Math.PI);
    ctx.strokeStyle = 'white';
    ctx.stroke();
  }

  drawEdges(ctx) {
    // Draw the trace of maps in reverse order to make sure the outgoing
    // transitions of previous maps aren't drawn over.
    const kMaxOutgoingEdges = 100;
    let nofEdges = 0;
    let stack = [];
    let current = this.state.map;
    while (current && nofEdges < kMaxOutgoingEdges) {
      nofEdges += current.children.length;
      stack.push(current);
      current = current.parent();
    }
    ctx.save();
    this.drawOutgoingEdges(ctx, this.state.map, 3);
    ctx.restore();

    let labelOffset = 15;
    let xPrev = 0;
    while (current = stack.pop()) {
      if (current.edge) {
        this.setEdgeStyle(current.edge, ctx);
        let [xTo, yTo] = this.drawEdge(ctx, current.edge, true, labelOffset);
        if (xTo == xPrev) {
          labelOffset += 8;
        } else {
          labelOffset = 15
        }
        xPrev = xTo;
      }
      this.markMap(ctx, current);
      current = current.parent();
      ctx.save();
      // this.drawOutgoingEdges(ctx, current, 1);
      ctx.restore();
    }
    // Mark selected map
    this.markSelectedMap(ctx, this.state.map);
  }

  drawEdge(ctx, edge, showLabel = true, labelOffset = 20) {
    if (!edge.from || !edge.to) return [-1, -1];
    let [xFrom, yFrom] = edge.from.position(this.chunks);
    let [xTo, yTo] = edge.to.position(this.chunks);
    let sameChunk = xTo == xFrom;
    if (sameChunk) labelOffset += 8;

    ctx.beginPath();
    ctx.moveTo(xFrom, yFrom);
    let offsetX = 20;
    let offsetY = 20;
    let midX = xFrom + (xTo - xFrom) / 2;
    let midY = (yFrom + yTo) / 2 - 100;
    if (!sameChunk) {
      ctx.quadraticCurveTo(midX, midY, xTo, yTo);
    } else {
      ctx.lineTo(xTo, yTo);
    }
    if (!showLabel) {
      ctx.strokeStyle = 'white';
      ctx.stroke();
    } else {
      let centerX, centerY;
      if (!sameChunk) {
        centerX = (xFrom / 2 + midX + xTo / 2) / 2;
        centerY = (yFrom / 2 + midY + yTo / 2) / 2;
      } else {
        centerX = xTo;
        centerY = yTo;
      }
      ctx.strokeStyle = 'white';
      ctx.moveTo(centerX, centerY);
      ctx.lineTo(centerX + offsetX, centerY - labelOffset);
      ctx.stroke();
      ctx.textAlign = 'left';
      ctx.fillStyle = 'white';
      ctx.fillText(
          edge.toString(), centerX + offsetX + 2, centerY - labelOffset)
    }
    return [xTo, yTo];
  }

  drawOutgoingEdges(ctx, map, max = 10, depth = 0) {
    if (!map) return;
    if (depth >= max) return;
    ctx.globalAlpha = 0.5 - depth * (0.3 / max);
    ctx.strokeStyle = '#666';

    const limit = Math.min(map.children.length, 100)
    for (let i = 0; i < limit; i++) {
      let edge = map.children[i];
      this.drawEdge(ctx, edge, true);
      this.drawOutgoingEdges(ctx, edge.to, max, depth + 1);
    }
  }
}

class TransitionView {
  constructor(state, node) {
    this.state = state;
    this.container = node;
    this.currentNode = node;
    this.currentMap = undefined;
  }

  selectMap(map) {
    this.currentMap = map;
    this.state.map = map;
  }

  showMap(map) {
    if (this.currentMap === map) return;
    this.currentMap = map;
    this._showMaps([map]);
  }

  showMaps(list, name) {
    this.state.view.isLocked = true;
    this._showMaps(list);
  }

  _showMaps(list, name) {
    // Hide the container to avoid any layouts.
    this.container.style.display = 'none';
    removeAllChildren(this.container);
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
    let node = div('map');
    if (map.edge) node.style.backgroundColor = map.edge.getColor();
    node.map = map;
    node.addEventListener('click', () => this.selectMap(map));
    if (map.children.length > 1) {
      node.innerText = map.children.length;
      let showSubtree = div('showSubtransitions');
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
    let edge = div(classes);
    edge.style.backgroundColor = map.edge.getColor();
    let labelNode = div('transitionLabel');
    labelNode.innerText = map.edge.toString();
    edge.appendChild(labelNode);
    return edge;
  }

  addTransitionTo(map) {
    // transition[ transitions[ transition[...], transition[...], ...]];

    let transition = div('transition');
    if (map.isDeprecated()) transition.classList.add('deprecated');
    if (map.edge) {
      transition.appendChild(this.addTransitionEdge(map));
    }
    let mapNode = this.addMapNode(map);
    transition.appendChild(mapNode);

    let subtree = div('transitions');
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
}

// =========================================================================

function transitionTypeToColor(type) {
  switch (type) {
    case 'new':
      // green
      return '#aedc6e';
    case 'Normalize':
      // violet
      return '#d26edc';
    case 'SlowToFast':
      // orange
      return '#dc9b6e';
    case 'InitialMap':
      // yellow
      return '#EEFF41';
    case 'Transition':
      // pink/violet (primary)
      return '#9B6EDC';
    case 'ReplaceDescriptors':
      // red
      return '#dc6eae';
  }
  // pink/violet (primary)
  return '#9B6EDC';
}

export { State, div, table, tr, td};