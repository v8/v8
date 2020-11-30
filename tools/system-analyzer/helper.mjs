// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

export const KB = 1024;
export const MB = KB * KB;
export const GB = MB * KB;
export const kMillis2Seconds = 1 / 1000;

export function formatBytes(bytes) {
  const units = ['B', 'KiB', 'MiB', 'GiB'];
  const divisor = 1024;
  let index = 0;
  while (index < units.length && bytes >= divisor) {
    index++;
    bytes /= divisor;
  }
  return bytes.toFixed(2) + units[index];
}

export function formatSeconds(millis) {
  return (millis * kMillis2Seconds).toFixed(2) + 's';
}

export function delay(time) {
  return new Promise(resolver => setTimeout(resolver, time));
}
