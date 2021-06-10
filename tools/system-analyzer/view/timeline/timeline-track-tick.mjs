// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Profile} from '../../../profile.mjs'
import {delay} from '../../helper.mjs';
import {Timeline} from '../../timeline.mjs';
import {CSSColor, DOM, SVG, V8CustomElement} from '../helper.mjs';

import {TimelineTrackBase} from './timeline-track-base.mjs'

class Flame {
  constructor(time, entry, depth, id) {
    this.time = time;
    this.entry = entry;
    this.depth = depth;
    this.id = id;
    this.duration = -1;
  }
  stop(time) {
    this.duration = time - this.time
  }
}

const kFlameHeight = 10;

DOM.defineCustomElement('view/timeline/timeline-track', 'timeline-track-tick',
                        (templateText) =>
                            class TimelineTrackTick extends TimelineTrackBase {
  _flames = new Timeline();
  _originalContentWidth = 0;

  constructor() {
    super(templateText);
    this._annotations = new Annotations(this);
    this.timelineNode.style.overflowY = 'scroll';
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

  _getEntryForEvent(event) {
    let logEntry = false;
    const target = event.target;
    const id = event.target.getAttribute('data-id');
    if (id) {
      const codeEntry = this._flames.at(id).entry;
      if (codeEntry.logEntry) {
        logEntry = codeEntry.logEntry;
      }
    }
    return {logEntry, target};
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
        const newFlame =
            new Flame(tick.time, entry, flameStack.length, tmpFlames.length);
        tmpFlames.push(newFlame);
        flameStack.push(newFlame);
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
    if (this._originalContentWidth > 0) {
      // Instead of repainting just scale the flames
      const ratio = currentWidth / this._originalContentWidth;
      this._scalableContentNode.style.transform = `scale(${ratio}, 1)`;
    }
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
      buffer += this.drawFlame(rawFlames[i]);
    }
    add();
  }

  drawFlame(flame, outline = false) {
    const x = this.timeToPosition(flame.time);
    const y = (flame.depth + 1) * kFlameHeight;
    let width = flame.duration * this._timeToPixel;

    if (outline) {
      return `<rect x=${x} y=${y} width=${width} height=${
          kFlameHeight} class=flameSelected />`;
    }

    let type = 'native';
    if (flame.entry?.state) {
      type = Profile.getKindFromState(flame.entry.state);
    }
    const color = this._legend.colorForType(type);
    return `<rect x=${x} y=${y} width=${width} height=${kFlameHeight} fill=${
        color} data-id=${flame.id} class=flame />`;
  }

  drawFlameText(flame) {
    let type = 'native';
    if (flame.entry?.state) {
      type = Profile.getKindFromState(flame.entry.state);
    }
    const kHeight = 9;
    const x = this.timeToPosition(flame.time);
    const y = flame.depth * (kHeight + 1);
    let width = flame.duration * this._timeToPixel;
    width -= width * 0.1;

    let buffer = '';
    if (width < 15 || type == 'native') return buffer;
    const rawName = flame.entry.getRawName();
    if (rawName.length == 0) return buffer;
    const kChartWidth = 5;
    const maxChars = Math.floor(width / kChartWidth)
    const text = rawName.substr(0, maxChars);
    buffer += `<text x=${x + 1} y=${y - 3} class=txt>${text}</text>`
    return buffer;
  }

  _drawAnnotations(logEntry, time) {
    if (time === undefined) {
      time = this.relativePositionToTime(this.timelineNode.scrollLeft);
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
    for (let range = 0; range < this._flames.length; range += 10000) {
      this._markFlames(start - range, start - offset);
      this._markFlames(start + offset, start + range);
      offset = range;
      await delay(50);
      // Abort if we started another update asynchronously.
      if (this._logEntry != logEntry) return;
    }
  }

  _markFlames(start, end) {
    const rawFlames = this._flames.values;
    if (start < 0) start = 0;
    if (end > rawFlames.length) end = rawFlames.length;
    const code = this._logEntry.entry;
    for (let i = start; i < end; i++) {
      const flame = rawFlames[i];
      if (flame.entry != code) continue;
      this._buffer += this._track.drawFlame(flame, true);
    }
    this._drawBuffer();
  }

  _drawBuffer() {
    if (this._buffer.length == 0) return;
    const svg = SVG.svg();
    svg.innerHTML = this._buffer;
    this._node.appendChild(svg);
    this._buffer = '';
  }
}