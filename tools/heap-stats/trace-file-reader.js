// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

'use strict';

const trace_file_reader_template =
    document.currentScript.ownerDocument.querySelector(
        '#trace-file-reader-template');

class TraceFileReader extends HTMLElement {
  constructor() {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.appendChild(trace_file_reader_template.content.cloneNode(true));
    this.addEventListener('click', e => this.handleClick(e));
    this.addEventListener('dragover', e => this.handleDragOver(e));
    this.addEventListener('drop', e => this.handleChange(e));
    this.$('#file').addEventListener('change', e => this.handleChange(e));
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  updateLabel(text) {
    this.$('#label').innerText = text;
  }

  handleClick(event) {
    this.$('#file').click();
  }

  handleChange(event) {
    // Used for drop and file change.
    event.preventDefault();
    var host = event.dataTransfer ? event.dataTransfer : event.target;
    this.readFile(host.files[0]);
  }

  handleDragOver(event) {
    event.preventDefault();
  }

  connectedCallback() {}

  readFile(file) {
    if (!file) {
      this.updateLabel('Failed to load file.');
      return;
    }

    const result = new FileReader();
    result.onload = (e) => {
      let contents = e.target.result.split('\n');
      contents = contents.map(function(line) {
        try {
          // Strip away a potentially present adb logcat prefix.
          line = line.replace(/^I\/v8\s*\(\d+\):\s+/g, '');
          return JSON.parse(line);
        } catch (e) {
          console.log('unable to parse line: \'' + line + '\'\' (' + e + ')');
        }
        return null;
      });
      const return_data = this.createModel(contents);
      this.updateLabel('Finished loading \'' + file.name + '\'.');
      this.dispatchEvent(new CustomEvent(
          'change', {bubbles: true, composed: true, detail: return_data}));
    };
    result.readAsText(file);
  }

  createModel(contents) {
    // contents is an array of JSON objects that is consolidated into the
    // application model.

    const data = Object.create(null);  // Final data container.
    const keys = Object.create(null);  // Collecting 'keys' per isolate.

    let createOrUpdateEntryIfNeeded = entry => {
      if (!(entry.isolate in keys)) {
        keys[entry.isolate] = new Set();
      }
      if (!(entry.isolate in data)) {
        data[entry.isolate] = {
          non_empty_instance_types: new Set(),
          gcs: {},
          zonetags: [],
          samples: {zone: {}},
          start: null,
          end: null,
          data_sets: new Set()
        };
      }
      const data_object = data[entry.isolate];
      if (('id' in entry) && !(entry.id in data_object.gcs)) {
        data_object.gcs[entry.id] = {non_empty_instance_types: new Set()};
      }
      if (data_object.end === null || data_object.end < entry.time) {
        data_object.end = entry.time;
      }
      if (data_object.start === null || data_object.start > entry.time)
        data_object.start = entry.time;
    };

    for (var entry of contents) {
      if (entry === null || entry.type === undefined) {
        continue;
      }
      if (entry.type === 'zone') {
        createOrUpdateEntryIfNeeded(entry);
        const stacktrace = ('stacktrace' in entry) ? entry.stacktrace : [];
        data[entry.isolate].samples.zone[entry.time] = {
          allocated: entry.allocated,
          pooled: entry.pooled,
          stacktrace: stacktrace
        };
      } else if (
          entry.type === 'zonecreation' || entry.type === 'zonedestruction') {
        createOrUpdateEntryIfNeeded(entry);
        data[entry.isolate].zonetags.push(
            Object.assign({opening: entry.type === 'zonecreation'}, entry));
      } else if (entry.type === 'gc_descriptor') {
        createOrUpdateEntryIfNeeded(entry);
        data[entry.isolate].gcs[entry.id].time = entry.time;
        if ('zone' in entry)
          data[entry.isolate].gcs[entry.id].malloced = entry.zone;
      } else if (entry.type === 'instance_type_data') {
        if (entry.id in data[entry.isolate].gcs) {
          createOrUpdateEntryIfNeeded(entry);
          if (!(entry.key in data[entry.isolate].gcs[entry.id])) {
            data[entry.isolate].gcs[entry.id][entry.key] = {
              instance_type_data: {},
              non_empty_instance_types: new Set(),
              overall: 0
            };
            data[entry.isolate].data_sets.add(entry.key);
          }
          const instanceTypeName = entry.instance_type_name;
          const id = entry.id;
          const key = entry.key;
          keys[entry.isolate].add(key);
          data[entry.isolate]
              .gcs[id][key]
              .instance_type_data[instanceTypeName] = {
            overall: entry.overall,
            count: entry.count,
            histogram: entry.histogram,
            over_allocated: entry.over_allocated,
            over_allocated_histogram: entry.over_allocated_histogram
          };
          data[entry.isolate].gcs[id][key].overall += entry.overall;

          if (entry.overall !== 0) {
            data[entry.isolate].gcs[id][key].non_empty_instance_types.add(
                instanceTypeName);
            data[entry.isolate].gcs[id].non_empty_instance_types.add(
                instanceTypeName);
            data[entry.isolate].non_empty_instance_types.add(instanceTypeName);
          }
        }
      } else if (entry.type === 'bucket_sizes') {
        if (entry.id in data[entry.isolate].gcs) {
          createOrUpdateEntryIfNeeded(entry);
          if (!(entry.key in data[entry.isolate].gcs[entry.id])) {
            data[entry.isolate].gcs[entry.id][entry.key] = {
              instance_type_data: {},
              non_empty_instance_types: new Set(),
              overall: 0
            };
            data[entry.isolate].data_sets.add(entry.key);
          }
          data[entry.isolate].gcs[entry.id][entry.key].bucket_sizes =
              entry.sizes;
        }
      } else {
        console.warning('Unknown entry type: ' + entry.type);
      }
    }

    let checkNonNegativeProperty = (obj, property) => {
      if (obj[property] < 0) {
        console.warning(
            'Property \'' + property + '\' negative: ' + obj[property]);
      }
    };

    for (const isolate of Object.keys(data)) {
      for (const gc of Object.keys(data[isolate].gcs)) {
        for (const key of keys[isolate]) {
          const data_set = data[isolate].gcs[gc][key];
          // 1. Create a ranked instance type array that sorts instance
          // types by memory size (overall).
          data_set.ranked_instance_types =
              [...data_set.non_empty_instance_types].sort(function(a, b) {
                if (data_set.instance_type_data[a].overall >
                    data_set.instance_type_data[b].overall) {
                  return 1;
                } else if (
                    data_set.instance_type_data[a].overall <
                    data_set.instance_type_data[b].overall) {
                  return -1;
                }
                return 0;
              });

          // 2. Create *FIXED_ARRAY_UNKNOWN_SUB_TYPE that accounts for all
          // missing fixed array sub types.
          const fixed_array_data =
              Object.assign({}, data_set.instance_type_data.FIXED_ARRAY_TYPE);
          for (const instanceType in data_set.instance_type_data) {
            if (!instanceType.startsWith('*FIXED_ARRAY')) continue;
            const subtype = data_set.instance_type_data[instanceType];
            fixed_array_data.count -= subtype.count;
            fixed_array_data.overall -= subtype.overall;
            for (let i = 0; i < fixed_array_data.histogram.length; i++) {
              fixed_array_data.histogram[i] -= subtype.histogram[i];
            }
          }

          // Emit log messages for negative values.
          checkNonNegativeProperty(fixed_array_data, 'count');
          checkNonNegativeProperty(fixed_array_data, 'overall');
          for (let i = 0; i < fixed_array_data.histogram.length; i++) {
            checkNonNegativeProperty(fixed_array_data.histogram, i);
          }

          data_set.instance_type_data['*FIXED_ARRAY_UNKNOWN_SUB_TYPE'] =
              fixed_array_data;
          data_set.non_empty_instance_types.add(
              '*FIXED_ARRAY_UNKNOWN_SUB_TYPE');
        }
      }
    }
    console.log(data);
    return data;
  }
}

customElements.define('trace-file-reader', TraceFileReader);
