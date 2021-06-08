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
    this.start = time;
    this.end = this.start;
    this.entry = entry;
    this.depth = depth;
    this.id = id;
  }
  stop(time) {
    this.end = time;
    this.duration = time - this.start
  }
}

DOM.defineCustomElement('view/timeline/timeline-track', 'timeline-track-tick',
                        (templateText) =>
                            class TimelineTrackTick extends TimelineTrackBase {
  _flames = new Timeline();
  _originalContentWidth = 0;

  constructor() {
    super(templateText);
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
    const stack = [];
    const ticks = this._timeline.values;

    for (let tickIndex = 0; tickIndex < ticks.length; tickIndex++) {
      const tick = ticks[tickIndex];
      for (let stackIndex = 0; stackIndex < tick.stack.length; stackIndex++) {
        const entry = tick.stack[stackIndex];
        if (stack.length <= stackIndex) {
          const newFlame =
              new Flame(tick.time, entry, stackIndex, tmpFlames.length);
          tmpFlames.push(newFlame);
          stack.push(newFlame);
        } else {
          if (stack[stackIndex].entry !== entry) {
            for (let k = stackIndex; k < stack.length; k++) {
              stack[k].stop(tick.time);
            }
            stack.length = stackIndex;
            const replacementFlame =
                new Flame(tick.time, entry, stackIndex, tmpFlames.length);
            tmpFlames.push(replacementFlame);
            stack[stackIndex] = replacementFlame;
          }
        }
      }
    }
    const lastTime = ticks[ticks.length - 1].time;
    for (let stackIndex = 0; stackIndex < stack.length; stackIndex++) {
      stack[stackIndex].stop(lastTime);
    }
    this._flames = new Timeline(Flame, tmpFlames);
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

  drawFlame(flame) {
    let type = 'native';
    if (flame.entry?.state) {
      type = Profile.getKindFromState(flame.entry.state);
    }
    const kHeight = 9;
    const x = this.timeToPosition(flame.start);
    const y = flame.depth * (kHeight + 1);
    let width = flame.duration * this._timeToPixel;
    width -= width * 0.1;
    const color = this._legend.colorForType(type);

    return `<rect x=${x} y=${y} width=${width} height=${kHeight} fill=${
        color} data-id=${flame.id} />`;
  }

  drawFlameText(flame) {
    let type = 'native';
    if (flame.entry?.state) {
      type = Profile.getKindFromState(flame.entry.state);
    }
    const kHeight = 9;
    const x = this.timeToPosition(flame.start);
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
})