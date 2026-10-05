#!/usr/bin/env python3
# Copyright 2026 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from pathlib import Path
import tempfile
import unittest

from agents.skills.clusterfuzz.scripts.fetch_testcase import (
    find_cookies_file,
    is_html_login_response,
    map_job_to_local_out,
    parse_metadata_response,
    parse_target,
)


class FetchTestcaseTest(unittest.TestCase):

  def test_parse_target_numeric_id(self):
    tc_id, url = parse_target("5197100195282944")
    self.assertEqual(tc_id, "5197100195282944")
    self.assertEqual(
        url, "https://clusterfuzz.com/download?testcase_id=5197100195282944")

  def test_parse_target_public_url(self):
    url_in = "https://clusterfuzz.com/testcase-detail/5197100195282944"
    tc_id, url = parse_target(url_in)
    self.assertEqual(tc_id, "5197100195282944")
    self.assertEqual(
        url, "https://clusterfuzz.com/download?testcase_id=5197100195282944")

  def test_parse_target_public_query_param(self):
    url_in = "https://clusterfuzz.com/download?testcase_id=12345"
    tc_id, url = parse_target(url_in)
    self.assertEqual(tc_id, "12345")
    self.assertEqual(url, url_in)

  def test_map_job_to_local_out(self):
    self.assertEqual(map_job_to_local_out("linux_asan_d8"), "out/x64.asan/d8")
    self.assertEqual(map_job_to_local_out("linux_msan_d8"), "out/x64.msan/d8")
    self.assertEqual(map_job_to_local_out("linux_tsan_d8"), "out/x64.tsan/d8")
    self.assertEqual(
        map_job_to_local_out("linux_ubsan_vptr_d8"), "out/x64.ubsan/d8")
    self.assertEqual(map_job_to_local_out("linux_d8_dbg"), "out/x64.debug/d8")
    self.assertEqual(
        map_job_to_local_out("linux_d8_optdebug"), "out/x64.optdebug/d8")
    self.assertEqual(map_job_to_local_out("linux_d8_rel"), "out/x64.release/d8")
    self.assertEqual(map_job_to_local_out("v8_foozzie"), "out/x64.release/d8")

  def test_is_html_login_response(self):
    self.assertTrue(
        is_html_login_response(b"<!DOCTYPE html><html>Login</html>"))
    self.assertTrue(is_html_login_response(b"<html><head>"))
    self.assertTrue(
        is_html_login_response(b"window.location = '/session-login'"))
    self.assertFalse(is_html_login_response(b"print('hello world');"))
    self.assertFalse(is_html_login_response(b"\x00asm\x01\x00\x00\x00"))

  def test_parse_metadata_response_valid(self):
    raw = b')]}\'\n{"testcase": {"id": 12345, "job_type": "linux_asan_d8"}}'
    data = parse_metadata_response(raw)
    self.assertEqual(data["testcase"]["id"], 12345)
    self.assertEqual(data["testcase"]["job_type"], "linux_asan_d8")

  def test_parse_metadata_response_unauthorized(self):
    raw = b'{"status": 401, "type": "UnauthorizedError"}'
    with self.assertRaises(PermissionError):
      parse_metadata_response(raw)

  def test_find_cookies_file_specified(self):
    with tempfile.NamedTemporaryFile() as tmp:
      found = find_cookies_file(tmp.name)
      self.assertEqual(found, Path(tmp.name).resolve())

    with self.assertRaises(FileNotFoundError):
      find_cookies_file("/non/existent/path/cookies.txt")

  def test_regression_html_stripping(self):
    import re
    regression = 'V8: <a target="_blank" href="https://example.com">81576:81577</a><br />Common: 12345'
    clean = regression
    if "<" in regression and ">" in regression:
      clean = re.sub(r"<br\s*/?>", "\n" + " " * 20, regression)
      clean = re.sub(r"<[^>]+>", "", clean).strip()
    self.assertEqual(clean,
                     "V8: 81576:81577\n                    Common: 12345")


if __name__ == "__main__":
  unittest.main()
