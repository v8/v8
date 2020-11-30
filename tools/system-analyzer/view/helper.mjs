// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

class CSSColor {
  static _cache = new Map();

  static get(name) {
    let color = this._cache.get(name);
    if (color !== undefined) return color;
    const style = getComputedStyle(document.body);
    color = style.getPropertyValue(`--${name}`);
    if (color === undefined) {
      throw new Error(`CSS color does not exist: ${name}`);
    }
    this._cache.set(name, color.trim());
    return color;
  }
  static reset() {
    this._cache.clear();
  }

  static get backgroundColor() {
    return this.get('background-color');
  }
  static get surfaceColor() {
    return this.get('surface-color');
  }
  static get primaryColor() {
    return this.get('primary-color');
  }
  static get secondaryColor() {
    return this.get('secondary-color');
  }
  static get onSurfaceColor() {
    return this.get('on-surface-color');
  }
  static get onBackgroundColor() {
    return this.get('on-background-color');
  }
  static get onPrimaryColor() {
    return this.get('on-primary-color');
  }
  static get onSecondaryColor() {
    return this.get('on-secondary-color');
  }
  static get defaultColor() {
    return this.get('default-color');
  }
  static get errorColor() {
    return this.get('error-color');
  }
  static get mapBackgroundColor() {
    return this.get('map-background-color');
  }
  static get timelineBackgroundColor() {
    return this.get('timeline-background-color');
  }
  static get red() {
    return this.get('red');
  }
  static get green() {
    return this.get('green');
  }
  static get yellow() {
    return this.get('yellow');
  }
  static get blue() {
    return this.get('blue');
  }
  static get orange() {
    return this.get('orange');
  }
  static get violet() {
    return this.get('violet');
  }
  static at(index) {
    return this.list[index % this.list.length];
  }
  static get list() {
    if (!this._colors) {
      this._colors = [
        this.green,
        this.violet,
        this.orange,
        this.yellow,
        this.primaryColor,
        this.red,
        this.blue,
        this.yellow,
        this.secondaryColor,
      ];
    }
    return this._colors;
  }
}

class DOM {
  static element(type, classes) {
    const node = document.createElement(type);
    if (classes === undefined) return node;
    if (typeof classes === 'string') {
      node.className = classes;
    } else {
      classes.forEach(cls => node.classList.add(cls));
    }
    return node;
  }

  static text(string) {
    return document.createTextNode(string);
  }

  static div(classes) {
    return this.element('div', classes);
  }

  static span(classes) {
    return this.element('span', classes);
  }

  static table(classes) {
    return this.element('table', classes);
  }

  static tbody(classes) {
    return this.element('tbody', classes);
  }

  static td(textOrNode, className) {
    const node = this.element('td');
    if (typeof textOrNode === 'object') {
      node.appendChild(textOrNode);
    } else if (textOrNode) {
      node.innerText = textOrNode;
    }
    if (className) node.className = className;
    return node;
  }

  static tr(classes) {
    return this.element('tr', classes);
  }

  static removeAllChildren(node) {
    let range = document.createRange();
    range.selectNodeContents(node);
    range.deleteContents();
  }

  static defineCustomElement(path, generator) {
    let name = path.substring(path.lastIndexOf('/') + 1, path.length);
    path = path + '-template.html';
    fetch(path)
        .then(stream => stream.text())
        .then(
            templateText =>
                customElements.define(name, generator(templateText)));
  }
}

function $(id) {
  return document.querySelector(id)
}

class V8CustomElement extends HTMLElement {
  _updateTimeoutId;
  _updateCallback = this._update.bind(this);

  constructor(templateText) {
    super();
    const shadowRoot = this.attachShadow({mode: 'open'});
    shadowRoot.innerHTML = templateText;
    this._updateCallback = this._update.bind(this);
  }

  $(id) {
    return this.shadowRoot.querySelector(id);
  }

  querySelectorAll(query) {
    return this.shadowRoot.querySelectorAll(query);
  }

  update(useAnimation = false) {
    if (useAnimation) {
      window.cancelAnimationFrame(this._updateTimeoutId);
      this._updateTimeoutId =
          window.requestAnimationFrame(this._updateCallback);
    } else {
      // Use timeout tasks to asynchronously update the UI without blocking.
      clearTimeout(this._updateTimeoutId);
      const kDelayMs = 5;
      this._updateTimeoutId = setTimeout(this._updateCallback, kDelayMs);
    }
  }

  _update() {
    throw Error('Subclass responsibility');
  }
}

class LazyTable {
  constructor(table, rowData, rowElementCreator) {
    this._table = table;
    this._rowData = rowData;
    this._rowElementCreator = rowElementCreator;
    const tbody = table.querySelector('tbody');
    table.replaceChild(document.createElement('tbody'), tbody);
    table.querySelector('tfoot td').onclick = (e) => this._addMoreRows();
    this._addMoreRows();
  }

  _nextRowDataSlice() {
    return this._rowData.splice(0, 100);
  }

  _addMoreRows() {
    const fragment = new DocumentFragment();
    for (let row of this._nextRowDataSlice()) {
      const tr = this._rowElementCreator(row);
      fragment.appendChild(tr);
    }
    this._table.querySelector('tbody').appendChild(fragment);
  }
}

export * from '../helper.mjs';
export {
  DOM,
  $,
  V8CustomElement,
  CSSColor,
  LazyTable,
};
