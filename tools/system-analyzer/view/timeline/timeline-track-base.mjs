// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {delay} from '../../helper.mjs';
import {kChunkHeight, kChunkWidth} from '../../log/map.mjs';
import {SelectionEvent, SelectTimeEvent, SynchronizeSelectionEvent, ToolTipEvent,} from '../events.mjs';
import {CSSColor, DOM, SVG, V8CustomElement} from '../helper.mjs';

export class TimelineTrackBase extends V8CustomElement {
  _timeline;
  _nofChunks = 500;
  _chunks;
  _selectedEntry;
  _timeToPixel;
  _timeStartOffset;
  _legend;

  _chunkMouseMoveHandler = this._handleChunkMouseMove.bind(this);
  _chunkClickHandler = this._handleChunkClick.bind(this);
  _chunkDoubleClickHandler = this._handleChunkDoubleClick.bind(this);
  _flameMouseOverHandler = this._handleFlameMouseOver.bind(this);

  constructor(templateText) {
    super(templateText);
    this._selectionHandler = new SelectionHandler(this);
    this._legend = new Legend(this.$('#legendTable'));
    this._legend.onFilter = (type) => this._handleFilterTimeline();
    this.timelineNode.addEventListener(
        'scroll', e => this._handleTimelineScroll(e));
    this.timelineNode.ondblclick = (e) =>
        this._selectionHandler.clearSelection();
    this.timelineChunks.onmousemove = this._chunkMouseMoveHandler;
    this.isLocked = false;
  }

  static get observedAttributes() {
    return ['title'];
  }

  attributeChangedCallback(name, oldValue, newValue) {
    if (name == 'title') {
      this.$('#title').innerHTML = newValue;
    }
  }

  _handleFilterTimeline(type) {
    this._updateChunks();
  }

  set data(timeline) {
    this._timeline = timeline;
    this._legend.timeline = timeline;
    this.$('.content').style.display = timeline.isEmpty() ? 'none' : 'relative';
    this._updateChunks();
  }

  set timeSelection(selection) {
    this._selectionHandler.timeSelection = selection;
    this.updateSelection();
  }

  updateSelection() {
    this._selectionHandler.update();
    this._legend.update();
  }

  // Maps the clicked x position to the x position on timeline canvas
  positionOnTimeline(pagePosX) {
    let rect = this.timelineNode.getBoundingClientRect();
    let posClickedX = pagePosX - rect.left + this.timelineNode.scrollLeft;
    return posClickedX;
  }

  positionToTime(pagePosX) {
    let posTimelineX =
        this.positionOnTimeline(pagePosX) + this._timeStartOffset;
    return posTimelineX / this._timeToPixel;
  }

  timeToPosition(time) {
    let relativePosX = time * this._timeToPixel;
    relativePosX -= this._timeStartOffset;
    return relativePosX;
  }

  get currentTime() {
    const centerOffset = this.timelineNode.getBoundingClientRect().width / 2;
    return this.positionToTime(this.timelineNode.scrollLeft + centerOffset);
  }

  set currentTime(time) {
    const centerOffset = this.timelineNode.getBoundingClientRect().width / 2;
    this.timelineNode.scrollLeft = this.timeToPosition(time) - centerOffset;
  }

  get timelineCanvas() {
    return this.$('#timelineCanvas');
  }

  get timelineChunks() {
    if (this._timelineChunks === undefined) {
      this._timelineChunks = this.$('#timelineChunks');
    }
    return this._timelineChunks;
  }

  get timelineSamples() {
    if (this._timelineSamples === undefined) {
      this._timelineSamples = this.$('#timelineSamples');
    }
    return this._timelineSamples;
  }

  get timelineNode() {
    if (this._timelineNode === undefined) {
      this._timelineNode = this.$('#timeline');
    }
    return this._timelineNode;
  }

  get timelineAnnotationsNode() {
    return this.$('#timelineAnnotations');
  }

  get timelineMarkersNode() {
    return this.$('#timelineMarkers');
  }

  _update() {
    this._updateTimeline();
    this._legend.update();
  }

  _handleFlameMouseOver(event) {
    const codeEntry = event.target.data;
    this.dispatchEvent(new ToolTipEvent(codeEntry.logEntry, event.target));
  }

  set nofChunks(count) {
    this._nofChunks = count;
    this._updateChunks();
  }

  get nofChunks() {
    return this._nofChunks;
  }

  _updateChunks() {
    this._chunks =
        this._timeline.chunks(this.nofChunks, this._legend.filterPredicate);
    this.requestUpdate();
  }

  get chunks() {
    return this._chunks;
  }

  set selectedEntry(value) {
    this._selectedEntry = value;
    this.drawAnnotations(value);
  }

  get selectedEntry() {
    return this._selectedEntry;
  }

  set scrollLeft(offset) {
    this.timelineNode.scrollLeft = offset;
  }

  handleEntryTypeDoubleClick(e) {
    this.dispatchEvent(new SelectionEvent(e.target.parentNode.entries));
  }

  timelineIndicatorMove(offset) {
    this.timelineNode.scrollLeft += offset;
  }

  _handleTimelineScroll(e) {
    let horizontal = e.currentTarget.scrollLeft;
    this.dispatchEvent(new CustomEvent(
        'scrolltrack', {bubbles: true, composed: true, detail: horizontal}));
  }

  _updateTimeline() {
    const chunks = this.chunks;
    const start = this._timeline.startTime;
    const end = this._timeline.endTime;
    const duration = end - start;
    const width = chunks.length * kChunkWidth;
    let oldWidth = width;
    if (this.timelineChunks.style.width) {
      oldWidth = parseInt(this.timelineChunks.style.width);
    }

    this._timeToPixel = width / duration;
    this._timeStartOffset = start * this._timeToPixel;
    this.timelineChunks.style.width = `${width}px`;
    this.timelineMarkersNode.style.width = `${width}px`;
    this.timelineAnnotationsNode.style.width = `${width}px`;

    this._drawMarkers();
    this._drawContent();
    this._drawAnnotations(this.selectedEntry);
  }

  async _drawContent() {
    const chunks = this.chunks;
    const max = chunks.max(each => each.size());
    let buffer = '';
    for (let i = 0; i < chunks.length; i++) {
      const chunk = chunks[i];
      const height = (chunk.size() / max * kChunkHeight);
      chunk.height = height;
      if (chunk.isEmpty()) continue;
      buffer += '<g>';
      buffer += this._drawChunk(i, chunk);
      buffer += '</g>'
    }
    this.timelineChunks.innerHTML = buffer;
  }

  _drawChunk(chunkIndex, chunk) {
    const groups = chunk.getBreakdown(event => event.type);
    let buffer = '';
    const kHeight = chunk.height;
    let lastHeight = 200;
    for (let i = 0; i < groups.length; i++) {
      const group = groups[i];
      if (group.count == 0) break;
      const height = (group.count / chunk.size() * kHeight) | 0;
      lastHeight -= height;
      const color = this._legend.colorForType(group.key);
      buffer += `<rect x=${chunkIndex * kChunkWidth} y=${lastHeight} height=${
          height} `
      buffer += `width=6 fill=${color} />`
    }
    return buffer;
  }

  _drawMarkers() {
    const fragment = new DocumentFragment();
    // Put a time marker roughly every 20 chunks.
    const expected = this._timeline.duration() / this.chunks.length * 20;
    let interval = (10 ** Math.floor(Math.log10(expected)));
    let correction = Math.log10(expected / interval);
    correction = (correction < 0.33) ? 1 : (correction < 0.75) ? 2.5 : 5;
    interval *= correction;

    const start = this._timeline.startTime;
    let time = start;
    while (time < this._timeline.endTime) {
      const timeNode = DOM.div('timestamp');
      timeNode.innerText = `${((time - start) / 1000) | 0} ms`;
      timeNode.style.left = `${((time - start) * this._timeToPixel) | 0}px`;
      fragment.appendChild(timeNode);
      time += interval;
    }
    DOM.removeAllChildren(this.timelineMarkersNode);
    this.timelineMarkersNode.appendChild(fragment);
  }

  _handleChunkMouseMove(event) {
    if (this.isLocked) return false;
    if (this._selectionHandler.isSelecting) return false;
    let target = event.target;
    if (target === this.timelineChunks) return false;
    target = target.parentNode;
    const time = this.positionToTime(event.pageX);
    const chunkIndex = (time - this._timeline.startTime) /
        this._timeline.duration() * this._nofChunks;
    const chunk = this.chunks[chunkIndex | 0];
    if (!chunk || chunk.isEmpty()) return;
    const relativeIndex =
        Math.round((200 - event.layerY) / chunk.height * (chunk.size() - 1));
    if (relativeIndex > chunk.size()) return;
    const logEntry = chunk.at(relativeIndex);
    this.dispatchEvent(new ToolTipEvent(logEntry, target));
    this._drawAnnotations(logEntry);
  }

  _drawAnnotations(logEntry) {
    // Subclass responsibility.
  }

  _handleChunkClick(event) {
    this.isLocked = !this.isLocked;
  }

  _handleChunkDoubleClick(event) {
    const chunk = event.target.chunk;
    if (!chunk) return;
    event.stopPropagation();
    this.dispatchEvent(new SelectTimeEvent(chunk.start, chunk.end));
  }
};

class SelectionHandler {
  // TODO turn into static field once Safari supports it.
  static get SELECTION_OFFSET() {
    return 10
  };

  _timeSelection = {start: -1, end: Infinity};
  _selectionOriginTime = -1;

  constructor(timeline) {
    this._timeline = timeline;
    this._timelineNode.addEventListener(
        'mousedown', e => this._handleTimeSelectionMouseDown(e));
    this._timelineNode.addEventListener(
        'mouseup', e => this._handleTimeSelectionMouseUp(e));
    this._timelineNode.addEventListener(
        'mousemove', e => this._handleTimeSelectionMouseMove(e));
  }

  update() {
    if (!this.hasSelection) {
      this._selectionNode.style.display = 'none';
      return;
    }
    this._selectionNode.style.display = 'inherit';
    const startPosition = this.timeToPosition(this._timeSelection.start);
    const endPosition = this.timeToPosition(this._timeSelection.end);
    this._leftHandleNode.style.left = startPosition + 'px';
    this._rightHandleNode.style.left = endPosition + 'px';
    const delta = endPosition - startPosition;
    const selectionNode = this._selectionBackgroundNode;
    selectionNode.style.left = startPosition + 'px';
    selectionNode.style.width = delta + 'px';
  }

  set timeSelection(selection) {
    this._timeSelection.start = selection.start;
    this._timeSelection.end = selection.end;
  }

  clearSelection() {
    this._timeline.dispatchEvent(new SelectTimeEvent());
  }

  timeToPosition(posX) {
    return this._timeline.timeToPosition(posX);
  }

  positionToTime(posX) {
    return this._timeline.positionToTime(posX);
  }

  get isSelecting() {
    return this._selectionOriginTime >= 0;
  }

  get hasSelection() {
    return this._timeSelection.start >= 0 &&
        this._timeSelection.end != Infinity;
  }

  get _timelineNode() {
    return this._timeline.$('#timeline');
  }

  get _selectionNode() {
    return this._timeline.$('#selection');
  }

  get _selectionBackgroundNode() {
    return this._timeline.$('#selectionBackground');
  }

  get _leftHandleNode() {
    return this._timeline.$('#leftHandle');
  }

  get _rightHandleNode() {
    return this._timeline.$('#rightHandle');
  }

  get _leftHandlePosX() {
    return this._leftHandleNode.getBoundingClientRect().x;
  }

  get _rightHandlePosX() {
    return this._rightHandleNode.getBoundingClientRect().x;
  }

  _isOnLeftHandle(posX) {
    return Math.abs(this._leftHandlePosX - posX) <=
        SelectionHandler.SELECTION_OFFSET;
  }

  _isOnRightHandle(posX) {
    return Math.abs(this._rightHandlePosX - posX) <=
        SelectionHandler.SELECTION_OFFSET;
  }

  _handleTimeSelectionMouseDown(e) {
    let xPosition = e.clientX
    // Update origin time in case we click on a handle.
    if (this._isOnLeftHandle(xPosition)) {
      xPosition = this._rightHandlePosX;
    }
    else if (this._isOnRightHandle(xPosition)) {
      xPosition = this._leftHandlePosX;
    }
    this._selectionOriginTime = this.positionToTime(xPosition);
  }

  _handleTimeSelectionMouseMove(e) {
    if (!this.isSelecting) return;
    const currentTime = this.positionToTime(e.clientX);
    this._timeline.dispatchEvent(new SynchronizeSelectionEvent(
        Math.min(this._selectionOriginTime, currentTime),
        Math.max(this._selectionOriginTime, currentTime)));
  }

  _handleTimeSelectionMouseUp(e) {
    this._selectionOriginTime = -1;
    const delta = this._timeSelection.end - this._timeSelection.start;
    if (delta <= 1 || isNaN(delta)) return;
    this._timeline.dispatchEvent(new SelectTimeEvent(
        this._timeSelection.start, this._timeSelection.end));
  }
}

class Legend {
  _timeline;
  _typesFilters = new Map();
  _typeClickHandler = this._handleTypeClick.bind(this);
  _filterPredicate = this.filter.bind(this);
  onFilter = () => {};

  constructor(table) {
    this._table = table;
  }

  set timeline(timeline) {
    this._timeline = timeline;
    const groups = timeline.getBreakdown();
    this._typesFilters = new Map(groups.map(each => [each.key, true]));
    this._colors =
        new Map(groups.map(each => [each.key, CSSColor.at(each.id)]));
  }

  get selection() {
    return this._timeline.selectionOrSelf;
  }

  get filterPredicate() {
    for (let visible of this._typesFilters.values()) {
      if (!visible) return this._filterPredicate;
    }
    return undefined;
  }

  colorForType(type) {
    return this._colors.get(type);
  }

  filter(logEntry) {
    return this._typesFilters.get(logEntry.type);
  }

  update() {
    const tbody = DOM.tbody();
    const missingTypes = new Set(this._typesFilters.keys());
    this.selection.getBreakdown().forEach(group => {
      tbody.appendChild(this._addTypeRow(group));
      missingTypes.delete(group.key);
    });
    missingTypes.forEach(key => tbody.appendChild(this._row('', key, 0, '0%')));
    if (this._timeline.selection) {
      tbody.appendChild(
          this._row('', 'Selection', this.selection.length, '100%'));
    }
    tbody.appendChild(this._row('', 'All', this._timeline.length, ''));
    this._table.tBodies[0].replaceWith(tbody);
  }

  _row(color, type, count, percent) {
    const row = DOM.tr();
    row.appendChild(DOM.td(color));
    row.appendChild(DOM.td(type));
    row.appendChild(DOM.td(count.toString()));
    row.appendChild(DOM.td(percent));
    return row
  }

  _addTypeRow(group) {
    const color = this.colorForType(group.key);
    const colorDiv = DOM.div('colorbox');
    if (this._typesFilters.get(group.key)) {
      colorDiv.style.backgroundColor = color;
    } else {
      colorDiv.style.borderColor = color;
      colorDiv.style.backgroundColor = CSSColor.backgroundImage;
    }
    let percent = `${(group.count / this.selection.length * 100).toFixed(1)}%`;
    const row = this._row(colorDiv, group.key, group.count, percent);
    row.className = 'clickable';
    row.onclick = this._typeClickHandler;
    row.data = group.key;
    return row;
  }

  _handleTypeClick(e) {
    const type = e.currentTarget.data;
    this._typesFilters.set(type, !this._typesFilters.get(type));
    this.onFilter(type);
  }
}
