#!/usr/bin/env vpython3
# Copyright 2026 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
import os
import json
import sys
import glob


def analyze_brain(brain_id):
  home_dir = os.path.expanduser("~")
  brain_dir = f"{home_dir}/.gemini/jetski/brain/{brain_id}"
  if not os.path.exists(brain_dir):
    print(f"Brain directory {brain_dir} not found.")
    return

  message_files = glob.glob(f"{brain_dir}/.system_generated/messages/*.json")
  if not message_files:
    print("No messages found in brain.")
    return

  print(
      f"Analyzing {len(message_files)} messages for potential shortcuts or flaws..."
  )

  keywords = [
      "shortcut", "skip", "lazy", "ignore", "failed", "error", "timeout",
      "hardcoded", "workaround"
  ]

  findings = []
  for msg_file in sorted(message_files):
    try:
      with open(msg_file, 'r') as f:
        msg = json.load(f)
        content = msg.get("content", "").lower()
        for kw in keywords:
          if kw in content:
            findings.append(
                f"- Found '{kw}' in message {msg['id']} from {msg['sender']}: {msg['content'][:200]}..."
            )
            break
    except Exception as e:
      pass

  if findings:
    print("\n Potential shortcuts or issues detected:")
    for finding in findings:
      print(finding)
  else:
    print("\nNo obvious shortcuts or issues detected via keywords.")


if __name__ == "__main__":
  if len(sys.argv) < 2:
    print("Usage: analyze_brain.py <brain_id>")
    sys.exit(1)
  analyze_brain(sys.argv[1])
