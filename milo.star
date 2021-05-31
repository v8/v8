# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//definitions.star", "beta_re", "stable_re", "extended_re")

def console_view(name, title, repo, refs, exclude_ref = None, header = "//consoles/header_main.textpb"):
    luci.console_view(
        name = name,
        title = title,
        repo = repo,
        refs = refs,
        exclude_ref = exclude_ref,
        favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
        header = header,
    )

def master_console_view(name, title, repo = "https://chromium.googlesource.com/v8/v8"):
    console_view(
        name = name,
        title = title,
        repo = repo,
        refs = ["refs/heads/master"],
    )

def branch_console_view(name, title, version):
    if name.endswith("ports"):
        console_name = "br.ports"
        bucket_name = "ci." + name[:-6]
    else:
        console_name = "br"
        bucket_name = "ci." + name
    console_view(
        name = name,
        title = title,
        repo = "https://chromium.googlesource.com/v8/v8",
        refs = ["refs/branch-heads/" + version],
        header = "//consoles/header_branch.textpb",
    )

def list_view(name, title):
    luci.list_view(
        name = name,
        title = title,
        favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
    )

luci.milo(
    logo = "https://storage.googleapis.com/chrome-infra-public/logo/v8.svg",
)

master_console_view("main", "Main")
master_console_view("ports", "Ports")
master_console_view("experiments", "Experiments")
master_console_view("integration", "Integration")
master_console_view("clusterfuzz", "ClusterFuzz")
master_console_view("chromium", "Chromium", "https://chromium.googlesource.com/chromium/src")

console_view(
    name = "official",
    title = "Official",
    repo = "https://chromium.googlesource.com/v8/v8",
    refs = ["refs/branch-heads/\\d+\\.\\d+", "refs/heads/\\d+\\.\\d+\\.\\d+"],
    exclude_ref = "refs/heads/master",
)


branch_console_view("br.beta", "Beta Main", beta_re)
branch_console_view("br.beta.ports", "Beta Ports", beta_re)
branch_console_view("br.stable", "Stable Main", stable_re)
branch_console_view("br.stable.ports", "Stable Ports", stable_re)
branch_console_view("br.extended", "Extended Main", extended_re)
branch_console_view("br.extended.ports", "Extended Ports", extended_re)

list_view("infra", "Infra")
list_view("tryserver", "Tryserver")

luci.console_view_entry(builder = "ci/Auto-tag", category = "Tag", console_view = "br.stable")
luci.console_view_entry(builder = "ci/Auto-tag", category = "Tag", console_view = "br.beta")
