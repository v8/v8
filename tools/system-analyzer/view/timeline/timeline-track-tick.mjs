// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {delay} from '../../helper.mjs';
import {TickLogEntry} from '../../log/tick.mjs';
import {Timeline} from '../../timeline.mjs';
import {SelectTimeEvent} from '../events.mjs';
import {CSSColor, DOM, SVG} from '../helper.mjs';

import {TimelineTrackBase} from './timeline-track-base.mjs'

const kFlameHeight = 8;

class Flame {
  constructor(time, entry, depth, id) {
    this.time = time;
    this.entry = entry;
    this.depth = depth;
    this.id = id;
    this.duration = -1;
    this.parent = undefined;
    this.children = [];
  }

  static add(time, entry, stack, flames) {
    const depth = stack.length;
    const id = flames.length;
    const newFlame = new Flame(time, entry, depth, id)
    if (depth > 0) {
      const parent = stack[depth - 1];
      newFlame.parent = parent;
      parent.children.push(newFlame);
    }
    flames.push(newFlame);
    stack.push(newFlame);
  }

  stop(time) {
    this.duration = time - this.time
  }

  get start() {
    return this.time;
  }

  get end() {
    return this.time + this.duration;
  }

  get type() {
    return TickLogEntry.extractCodeEntryType(this.entry);
  }
}

DOM.defineCustomElement('view/timeline/timeline-track', 'timeline-track-tick',
                        (templateText) =>
                            class TimelineTrackTick extends TimelineTrackBase {
  _flames = new Timeline();
  _originalContentWidth = 0;

  constructor() {
    super(templateText);
    this._annotations = new Annotations(this);
  }

  _updateChunks() {
    // We don't need to update the chunks here.
    this._updateDimensions();
    this.requestUpdate();
  }

  set data(timeline) {
    super.data = timeline;
    this._contentWidth = 0;
    this._updateFlames();
  }

  _handleDoubleClick(event) {
    if (event.button !== 0) return;
    this._selectionHandler.clearSelection();
    const flame = this._getFlameForEvent(event);
    if (flame === undefined) return;
    event.stopImmediatePropagation();
    this.dispatchEvent(new SelectTimeEvent(flame.start, flame.end));
    return false;
  }

  _getFlameDepthForEvent(event) {
    return Math.floor(event.layerY / kFlameHeight) - 1;
  }

  _getFlameForEvent(event) {
    const depth = this._getFlameDepthForEvent(event);
    const time = this.positionToTime(event.pageX);
    const index = this._flames.find(time);
    for (let i = index - 1; i > 0; i--) {
      const flame = this._flames.at(i);
      if (flame.depth != depth) continue;
      if (flame.end < time) continue;
      return flame;
    }
    return undefined;
  }

  _getEntryForEvent(event) {
    const depth = this._getFlameDepthForEvent(event);
    const time = this.positionToTime(event.pageX);
    const index = this._timeline.find(time);
    const tick = this._timeline.at(index);
    let stack = tick.stack;
    if (index > 0 && tick.time > time) {
      stack = this._timeline.at(index - 1).stack;
    }
    // tick.stack = [top, ...., bottom];
    const logEntry = stack[stack.length - depth - 1]?.logEntry ?? false;
    // Filter out raw pc entries.
    if (typeof logEntry == 'number' || logEntry === false) return false;
    this.toolTipTargetNode.style.left = `${event.layerX}px`;
    this.toolTipTargetNode.style.top = `${(depth + 2) * kFlameHeight}px`;
    return logEntry;
  }

  _updateFlames() {
    const tmpFlames = [];
    // flameStack = [bottom, ..., top];
    const flameStack = [];
    const ticks = this._timeline.values;
    let maxDepth = 0;

    for (let tickIndex = 0; tickIndex < ticks.length; tickIndex++) {
      const tick = ticks[tickIndex];
      maxDepth = Math.max(maxDepth, tick.stack.length);
      // tick.stack = [top, .... , bottom];
      for (let stackIndex = tick.stack.length - 1; stackIndex >= 0;
           stackIndex--) {
        const entry = tick.stack[stackIndex];
        const flameStackIndex = tick.stack.length - stackIndex - 1;
        if (flameStackIndex < flameStack.length) {
          if (flameStack[flameStackIndex].entry === entry) continue;
          for (let k = flameStackIndex; k < flameStack.length; k++) {
            flameStack[k].stop(tick.time);
          }
          flameStack.length = flameStackIndex;
        }
        Flame.add(tick.time, entry, flameStack, tmpFlames);
      }
      if (tick.stack.length < flameStack.length) {
        for (let k = tick.stack.length; k < flameStack.length; k++) {
          flameStack[k].stop(tick.time);
        }
        flameStack.length = tick.stack.length;
      }
    }
    const lastTime = ticks[ticks.length - 1].time;
    for (let k = 0; k < flameStack.length; k++) {
      flameStack[k].stop(lastTime);
    }
    this._flames = new Timeline(Flame, tmpFlames);
    this._annotations.flames = this._flames;
    // Account for empty top line
    maxDepth++;
    this._adjustHeight(maxDepth * kFlameHeight);
  }

  _scaleContent(currentWidth) {
    if (this._originalContentWidth == 0) return;
    // Instead of repainting just scale the flames
    const ratio = currentWidth / this._originalContentWidth;
    this._scalableContentNode.style.transform = `scale(${ratio}, 1)`;
    this.style.setProperty('--txt-scale', `scale(${1 / ratio}, 1)`);
  }

  async _drawContent() {
    if (this._originalContentWidth > 0) return;
    this._originalContentWidth = parseInt(this.timelineMarkersNode.style.width);
    this._scalableContentNode.innerHTML = '';
    let buffer = '';
    const add = () => {
      const svg = SVG.svg();
      svg.innerHTML = buffer;
      this._scalableContentNode.appendChild(svg);
      buffer = '';
    };
    const rawFlames = this._flames.values;
    for (let i = 0; i < rawFlames.length; i++) {
      if ((i % 3000) == 0) {
        add();
        await delay(50);
      }
      buffer += this.drawFlame(rawFlames[i], i);
    }
    add();
  }

  drawFlame(flame, i, outline = false) {
    const x = this.timeToPosition(flame.time);
    const y = (flame.depth + 1) * kFlameHeight;
    let width = flame.duration * this._timeToPixel;
    if (outline) {
      return `<rect x=${x} y=${y} width=${width} height=${
          kFlameHeight - 1} class=flameSelected />`;
    }
    let color = this._legend.colorForType(flame.type);
    if (i % 2 == 1) {
      color = CSSColor.darken(color, 20);
    }
    return `<rect x=${x} y=${y} width=${width} height=${
        kFlameHeight - 1} fill=${color} class=flame />`;
  }

  drawFlameText(flame) {
    let type = flame.type;
    const kHeight = 9;
    const x = this.timeToPosition(flame.time);
    const y = flame.depth * (kHeight + 1);
    let width = flame.duration * this._timeToPixel;
    width -= width * 0.1;

    let buffer = '';
    if (width < 15 || type == 'Other') return buffer;
    const rawName = flame.entry.getName();
    if (rawName.length == 0) return buffer;
    const kChartWidth = 5;
    const maxChars = Math.floor(width / kChartWidth)
    const text = rawName.substr(0, maxChars);
    buffer += `<text x=${x + 1} y=${y - 3} class=txt>${text}</text>`
    return buffer;
  }

  _drawAnnotations(logEntry, time) {
    if (time === undefined) {
      time = this.relativePositionToTime(this._timelineScrollLeft);
    }
    this._annotations.update(logEntry, time);
  }
})

class Annotations {
  _flames;
  _logEntry;
  _buffer;

  constructor(track) {
    this._track = track;
  }

  set flames(flames) {
    this._flames = flames;
  }

  get _node() {
    return this._track.timelineAnnotationsNode;
  }

  async update(logEntry, time) {
    if (this._logEntry == logEntry) return;
    this._logEntry = logEntry;
    this._node.innerHTML = '';
    if (logEntry === undefined) return;
    this._buffer = '';
    const start = this._flames.find(time);
    let offset = 0;
    // Draw annotations gradually outwards starting form the given time.
    let deadline = performance.now() + 500;
    for (let range = 0; range < this._flames.length; range += 10000) {
      this._markFlames(start - range, start - offset);
      this._markFlames(start + offset, start + range);
      offset = range;
      if ((navigator?.scheduling?.isInputPending({includeContinuous: true}) ??
           false) ||
          performance.now() >= deadline) {
        // Yield if we have to handle an input event, or we're out of time.
        await delay(50);
        // Abort if we started another update asynchronously.
        if (this._logEntry != logEntry) return;

        deadline = performance.now() + 500;
      }
      this._drawBuffer();
    }
    this._drawBuffer();
  }

  _markFlames(start, end) {
    const rawFlames = this._flames.values;
    if (start < 0) start = 0;
    if (end > rawFlames.length) end = rawFlames.length;
    const logEntry = this._logEntry;
    // Also compare against the function, if any.
    const func = logEntry.entry?.func;
    for (let i = start; i < end; i++) {
      const flame = rawFlames[i];
      if (!flame.entry) continue;
      if (flame.entry.logEntry !== logEntry &&
          (!func || flame.entry.func !== func)) {
        continue;
      }
      this._buffer += this._track.drawFlame(flame, i, true);
    }
  }

  _drawBuffer() {
    if (this._buffer.length == 0) return;
    const svg = SVG.svg();
    svg.innerHTML = this._buffer;
    this._node.appendChild(svg);
    this._buffer = '';
  }
}