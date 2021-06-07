// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {Profile} from '../../../profile.mjs'
import {delay} from '../../helper.mjs';
import {CSSColor, DOM, SVG, V8CustomElement} from '../helper.mjs';

import {TimelineTrackBase} from './timeline-track-base.mjs'

class Flame {
  constructor(time, entry) {
    this.start = time;
    this.end = this.start;
    this.entry = entry;
  }
  stop(time) {
    this.end = time;
    this.duration = time - this.start
  }
}

DOM.defineCustomElement('view/timeline/timeline-track', 'timeline-track-tick',
                        (templateText) =>
                            class TimelineTrackTick extends TimelineTrackBase {
  constructor() {
    super(templateText);
  }

  async _drawContent() {
    this.timelineChunks.innerHTML = '';
    const stack = [];
    let buffer = '';
    const kMinPixelWidth = 1
    const kMinTimeDelta = kMinPixelWidth / this._timeToPixel;
    let lastTime = 0;
    let flameCount = 0;
    const ticks = this._timeline.values;
    for (let tickIndex = 0; tickIndex < ticks.length; tickIndex++) {
      const tick = ticks[tickIndex];
      // Skip ticks beyond visible resolution.
      if ((tick.time - lastTime) < kMinTimeDelta) continue;
      lastTime = tick.time;
      if (flameCount > 1000) {
        const svg = SVG.svg();
        svg.innerHTML = buffer;
        this.timelineChunks.appendChild(svg);
        buffer = '';
        flameCount = 0;
        await delay(15);
      }
      for (let stackIndex = 0; stackIndex < tick.stack.length; stackIndex++) {
        const entry = tick.stack[stackIndex];
        if (stack.length <= stackIndex) {
          stack.push(new Flame(tick.time, entry));
        } else {
          const flame = stack[stackIndex];
          if (flame.entry !== entry) {
            for (let k = stackIndex; k < stack.length; k++) {
              stack[k].stop(tick.time);
              buffer += this.drawFlame(stack[k], k);
              flameCount++
            }
            stack.length = stackIndex;
            stack[stackIndex] = new Flame(tick.time, entry);
          }
        }
      }
    }
    const svg = SVG.svg();
    svg.innerHTML = buffer;
    this.timelineChunks.appendChild(svg);
  }

  drawFlame(flame, depth) {
    let type = 'native';
    if (flame.entry?.state) {
      type = Profile.getKindFromState(flame.entry.state);
    }
    const kHeight = 9;
    const x = this.timeToPosition(flame.start);
    const y = depth * (kHeight + 1);
    const width = (flame.duration * this._timeToPixel - 0.5);
    const color = this._legend.colorForType(type);

    let buffer =
        `<rect x=${x} y=${y} width=${width} height=${kHeight} fill=${color} />`;
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