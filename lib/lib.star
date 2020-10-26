# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//definitions.star", "branch_names", "versions")

waterfall_acls = [
    acl.entry(
        roles = acl.BUILDBUCKET_TRIGGERER,
        users = [
            "luci-scheduler@appspot.gserviceaccount.com",
            "v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        ],
        groups = ["service-account-v8-bot"],
    ),
]

tryserver_acls = [
    acl.entry(
        roles = acl.BUILDBUCKET_TRIGGERER,
        users = [
            "luci-scheduler@appspot.gserviceaccount.com",
        ],
        groups = [
            "service-account-cq",
            "project-v8-tryjob-access",
            "service-account-v8-bot",
        ],
    ),
]

defaults_ci = {
    "executable": {"name": "v8", "cipd_package": "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build", "cipd_version": "refs/heads/master"},
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "default"},
    "service_account": "v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 7200,
    "build_numbers": True,
}

defaults_ci_br = dict(defaults_ci)
defaults_ci_br["dimensions"]["pool"] = "luci.v8.ci"

defaults_try = {
    "executable": {"name": "v8", "cipd_package": "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build", "cipd_version": "refs/heads/master"},
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "default", "pool": "luci.v8.try"},
    "service_account": "v8-try-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 1800,
    "properties": {"builder_group": "tryserver.v8"},
}

defaults_triggered = {
    "executable": {"name": "v8", "cipd_package": "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build", "cipd_version": "refs/heads/master"},
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "multibot", "pool": "luci.v8.try"},
    "service_account": "v8-try-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 3600,
    "properties": {"builder_group": "tryserver.v8"},
    "caches": [
        swarming.cache(
            path = "builder",
            name = "v8_builder_cache_nowait",
        ),
    ],
}

defaults_dict = {
    "ci": defaults_ci,
    "try": defaults_try,
    "try.triggered": defaults_triggered,
    "ci.br.beta": defaults_ci_br,
    "ci.br.stable": defaults_ci_br,
}

trigger_dict = {
    "ci": "v8-trigger",
    "ci.br.beta": "v8-trigger-br-beta",
    "ci.br.stable": "v8-trigger-br-stable",
}

GOMA = struct(
    DEFAULT = {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"},
    ATS = {"server_host": "goma.chromium.org", "enable_ats": True, "rpc_extra_params": "?prod"},
    NO = {"use_goma": False},
    NONE = {},
)

GOMA_JOBS = struct(
    J150 = 150,
)

def _goma_properties(use_goma, goma_jobs):
  if use_goma == GOMA.NONE or use_goma == GOMA.NO:
    return use_goma

  properties = dict(use_goma)

  if goma_jobs:
    properties["jobs"] = goma_jobs

  return {"$build/goma": properties}

# These settings enable overwriting variables in V8's DEPS file.
GCLIENT_VARS = struct(
    INSTRUMENTED_LIBRARIES = {"checkout_instrumented_libraries": "True"},
    ITTAPI = {"checkout_ittapi": "True"},
    V8_HEADER_INCLUDES = {"check_v8_header_includes": "True"},
    GCMOLE = {"download_gcmole": "True"},
    JSFUNFUZZ = {"download_jsfunfuzz": "True"},
    XCODE12_BETA = {"mac_xcode_version": "xcode_12_beta"},
)

def _gclient_vars_properties(props):
  gclient_vars = {}
  for prop in props:
    gclient_vars.update(prop)
  if gclient_vars:
    return {"gclient_vars": gclient_vars}
  else:
    return {}

multibot_caches = [
    swarming.cache(
        path = "builder",
        name = "v8_builder_cache_nowait",
    ),
]

def v8_builder(defaults = None, **kwargs):
    bucket_name = kwargs["bucket"]
    in_console = kwargs.pop("in_console", None)
    in_list = kwargs.pop("in_list", None)
    defaults = defaults or defaults_dict[bucket_name]
    v8_basic_builder(defaults, **kwargs)
    if in_console:
        splited = in_console.split("/")
        console_name = splited[0]
        category_name = None
        if len(splited) > 1:
            category_name = splited[1]
        luci.console_view_entry(
            console_view = console_name,
            builder = "%s/%s" % (bucket_name, kwargs["name"]),
            category = category_name,
        )
    if in_list:
        luci.list_view_entry(
            list_view = in_list,
            builder = kwargs["name"],
        )
    return kwargs["name"]

def v8_basic_builder(defaults, **kwargs):
    cq_properties = kwargs.pop("cq_properties", None)
    if cq_properties:
        luci.cq_tryjob_verifier(
            kwargs["name"],
            cq_group = "v8-cq",
            **cq_properties
        )
    properties = dict(kwargs.pop("properties", {}))
    properties.update(_goma_properties(
        kwargs.pop("use_goma", GOMA.NONE), kwargs.pop("goma_jobs", None)))
    properties.update(_gclient_vars_properties(kwargs.pop("gclient_vars", [])))
    kwargs["properties"] = properties

    properties["$recipe_engine/isolated"] = {
        "server": "https://isolateserver.appspot.com/",
    }
    kwargs = fix_args(defaults, **kwargs)
    luci.builder(**kwargs)

branch_console_dict = {
    ("ci", "main"): "main",
    ("ci", "ports"): "ports",
    ("ci.br.beta", "main"): "br.beta",
    ("ci.br.beta", "ports"): "br.beta.ports",
    ("ci.br.stable", "main"): "br.stable",
    ("ci.br.stable", "ports"): "br.stable.ports",
}

def multibranch_builder(**kwargs):
    added_builders = []
    for bucket_name in branch_names:
        args = dict(kwargs)
        triggered_by_gitiles = args.pop("triggered_by_gitiles", True)
        first_branch_version = args.pop("first_branch_version", None)
        if triggered_by_gitiles:
            args["triggered_by"] = [trigger_dict[bucket_name]]
            args["use_goma"] = args.get("use_goma", GOMA.DEFAULT)
        else:
            args["dimensions"] = {"host_class": "multibot"}
        if bucket_name != "ci":
            args["notifies"] = ["beta/stable notifier"]
            if _builder_is_not_supported(bucket_name, first_branch_version):
                continue
        v8_basic_builder(defaults_ci, bucket = bucket_name, **args)
        added_builders.append(bucket_name + "/" + kwargs["name"])
    return added_builders

def _builder_is_not_supported(bucket_name, first_branch_version):
    # do we need to skip the builder in this bucket?
    if first_branch_version:
        branch_id = bucket_name.split(".")[2]
        branch_version = versions[branch_id].replace(".", "")
        builder_first_version = first_branch_version.replace(".", "")
        return int(branch_version) < int(builder_first_version)
    return False

def perf_builder(**kwargs):
    properties = {"triggers_proxy": True, "build_config": "Release", "builder_group": "client.v8.perf"}
    extra_properties = kwargs.pop("properties", None)
    if extra_properties:
        properties.update(extra_properties)
    v8_builder(
        bucket = "ci",
        triggered_by = ["v8-trigger"],
        triggering_policy = scheduler.policy(
            kind = scheduler.GREEDY_BATCHING_KIND,
            max_batch_size = 1,
        ),
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        properties = properties,
        use_goma = GOMA.DEFAULT,
        **kwargs
    )

def fix_args(defaults, **kwargs):
    args = dict(kwargs)
    recipe_name = args.pop("recipe_name", None)
    if recipe_name:
        args["executable"] = {"name": recipe_name}
    overridable_keys = [
        "executable",
        "swarming_tags",
        "service_account",
        "execution_timeout",
        "build_numbers",
        "priority",
        "caches",
    ]
    for key in overridable_keys:
        override_defaults(defaults, args, key)
    mergeable_keys = ["dimensions", "properties", "executable"]
    for key in mergeable_keys:
        merge_defaults(defaults, args, key)
    args["execution_timeout"] = args["execution_timeout"] * time.second
    args["properties"] = dict(args["properties"].items() +
                              args["executable"].get("properties_j", {}).items())
    args["executable"] = luci.recipe(**args.get("executable"))
    if args["bucket"] in ["ci.br.beta", "ci.br.stable"]:
        args["dimensions"]["pool"] = "luci.v8.ci"
    if args.get("dimensions", {}).get("host_class", "") == "multibot":
        args["caches"] = multibot_caches
    if args.get("properties", {}).get("triggers", None):
        bucket_name = args["bucket"]
        if bucket_name == "try":
            bucket_name = "try.triggered"
        args["triggers"] = [bucket_name + "/" + t for t in args["properties"]["triggers"]]
    return args

def override_defaults(defaults, args, key):
    args[key] = args.pop(key, None) or defaults.get(key, None)

def merge_defaults(defaults, args, key):
    args[key] = dict(clean_dict_items(defaults, key) + clean_dict_items(args, key))

def clean_dict_items(dictionary, key):
    return {k: v for k, v in dictionary.get(key, {}).items() if v != None}.items()

def in_branch_console(console_id, *builder_sets):
    def in_category(category_name, *builder_sets):
        for builder_set in builder_sets:
            for builder in builder_set:
                branch = builder.split("/")[0]
                luci.console_view_entry(
                    console_view = branch_console_dict[branch, console_id],
                    builder = builder,
                    category = category_name,
                )

    return in_category

def in_console(console_id, *builders):
    def in_category(category_name, *builders):
        for builder in builders:
            luci.console_view_entry(
                console_view = console_id,
                builder = builder,
                category = category_name,
            )

    return in_category

FAILED_STEPS_EXCLUDE = [
    "bot_update",
    "isolate tests",
    "package build",
    "extract build",
    "cleanup_temp",
    "gsutil upload",
    "taskkill",
    "Failure reason",
    "steps",
    ".* \\(flakes\\)",
    ".* \\(retry shards with patch\\)",
    ".* \\(with patch\\)",
    ".* \\(without patch\\)",
]

def v8_notifier(**kwargs):
    luci.notifier(
        on_new_status = ["FAILURE", "INFRA_FAILURE"],
        **kwargs
    )
