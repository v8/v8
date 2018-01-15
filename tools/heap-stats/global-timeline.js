// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

const KB = 1024;
const MB = KB * KB;

const global_timeline_template =
    document.currentScript.ownerDocument.querySelector(
        '#global-timeline-template');

class GlobalTimeline extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.appendChild(global_timeline_template.content.cloneNode(true));
  }

  set data(value) {
    this._data = value;
    this.stateChanged();
  }

  get data() {
    return this._data;
  }

  set selection(value) {
    this._selection = value;
    this.stateChanged();
  }

  get selection() {
    return this._selection;
  }

  isValid() {
    return this.data && this.selection;
  }

  stateChanged() {
    if (this.isValid()) this.drawChart();
  }

  drawChart() {
    console.assert(this.data, 'invalid data');
    console.assert(this.selection, 'invalid selection');

    const categories = Object.keys(this.selection.categories)
                           .map(k => this.selection.category_names.get(k));
    const labels = ['Time', ...categories];
    const chart_data = [labels];
    const isolate_data = this.data[this.selection.isolate];
    for (let k of Object.keys(isolate_data.gcs)) {
      const gc_data = isolate_data.gcs[k];
      const data_set = gc_data[this.selection.data_set].instance_type_data;
      const data = [];
      data.push(gc_data.time);
      for (let [category, instance_types] of Object.entries(
               this.selection.categories)) {
        let overall = 0;
        for (let instance_type of instance_types) {
          overall += data_set[instance_type].overall;
        }
        data.push(overall / KB);
      }
      chart_data.push(data);
    }

    const data = google.visualization.arrayToDataTable(chart_data);
    const options = {
      isStacked: true,
      hAxis: {
        title: 'Time [ms]',
      },
      vAxis: {title: 'Memory consumption [KBytes]'},
      chartArea: {width: '85%', height: '80%'},
      legend: {position: 'top', maxLines: '1'},
      pointsVisible: true,
      pointSize: 5,
    };
    const chart = new google.visualization.AreaChart(
        this.shadowRoot.querySelector('#chart'));
    this.shadowRoot.querySelector('#container').style.display = 'block';
    chart.draw(data, google.charts.Line.convertOptions(options));
  }
}

customElements.define('global-timeline', GlobalTimeline);
