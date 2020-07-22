// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const KB = 1024;
const MB = KB * KB;
const GB = MB * KB;
const kMillis2Seconds = 1 / 1000;

function formatBytes(bytes) {
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  const divisor = 1024;
  let index = 0;
  while (index < units.length && bytes >= divisor) {
    index++;
    bytes /= divisor;
  }
  return bytes.toFixed(2) + units[index];
}

function formatSeconds(millis) {
  return (millis * kMillis2Seconds).toFixed(2) + 's';
}

function defineCustomElement(path, generator) {
  let name = path.substring(path.lastIndexOf("/") + 1, path.length);
  path = path + '-template.html';
  fetch(path)
      .then(stream => stream.text())
      .then(
          templateText => customElements.define(name, generator(templateText)));
}

// DOM Helpers
function removeAllChildren(node) {
  let range = document.createRange();
  range.selectNodeContents(node);
  range.deleteContents();
}

function $(id) {
  return document.querySelector(id)
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

class V8CustomElement extends HTMLElement {
  constructor(templateText) {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
  }
  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  div(classes) {return div(classes)}

  table(className) {
    let node = document.createElement('table')
    if (className) node.classList.add(className)
    return node;
  }

  td(textOrNode) {
    let node = document.createElement('td');
    if (typeof textOrNode === 'object') {
      node.appendChild(textOrNode);
    } else {
      node.innerText = textOrNode;
    }
    return node;
  }

  tr(){
    return document.createElement('tr');
  }

  removeAllChildren(node) { return removeAllChildren(node); }
}

export {defineCustomElement, V8CustomElement, removeAllChildren, $, div};
