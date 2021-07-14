# Copyright 2020 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//definitions.star", "versions")

def branch_descriptor(
        bucket,
        poller_name,
        refs,
        version_tag = None,
        priority = None,
        has_console_name_prefix = True):
    version = None
    if version_tag:
        version = versions[version_tag].replace(".", "\\.")
        refs = [ref % version for ref in refs]
    return struct(
        bucket = bucket,
        version = version,
        poller_name = poller_name,
        create_consoles = branch_console_builder(bucket, version_tag, refs) if has_console_name_prefix else master_console_builder(),
        refs = refs,
        priority = priority,
        console_id_by_name = console_id_resolver(bucket, has_console_name_prefix),
    )

def master_console_builder():
    def builder():
        console_view("main")
        console_view("ports")

    return builder

def branch_console_builder(bucket, version_tag, refs):
    def builder():
        base_name = bucket[3:]
        base_display_name = version_tag + " "
        header = "//consoles/header_branch.textpb"
        console_view(base_name, title = base_display_name + "main", refs = refs, header = header)
        console_view(base_name + ".ports", title = base_display_name + "ports", refs = refs, header = header)

    return builder

def console_id_resolver(bucket, has_console_name_prefix):
    def resolver(console_kind):
        if has_console_name_prefix:
            suffix = ".ports" if console_kind == "ports" else ""
            return bucket[3:] + suffix
        else:
            return console_kind

    return resolver

branch_descriptors = [
    branch_descriptor(
        "ci",
        "v8-trigger",
        ["refs/heads/master"],
        priority = 30,  # default
        has_console_name_prefix = False,
    ),  # master
    branch_descriptor(
        "ci.br.beta",
        "v8-trigger-br-beta",
        ["refs/branch-heads/%s"],
        version_tag = "beta",
        priority = 50,
    ),
    branch_descriptor(
        "ci.br.stable",
        "v8-trigger-br-stable",
        ["refs/branch-heads/%s"],
        version_tag = "stable",
        priority = 50,
    ),
    branch_descriptor(
        "ci.br.extended",
        "v8-trigger-br-extended",
        ["refs/branch-heads/%s"],
        version_tag = "extended",
        priority = 50,
    ),
]

waterfall_acls = [
    acl.entry(
        roles = acl.BUILDBUCKET_TRIGGERER,
        users = [
            "v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
        ],
        groups = ["service-account-v8-bot"],
    ),
]

tryserver_acls = [
    acl.entry(
        roles = acl.BUILDBUCKET_TRIGGERER,
        groups = [
            "service-account-cq",
            "project-v8-tryjob-access",
            "service-account-v8-bot",
        ],
    ),
]

defaults_ci = {
    "executable": "recipe:v8",
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "default"},
    "service_account": "v8-ci-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 7200,
    "build_numbers": True,
}

defaults_ci_br = dict(defaults_ci)
defaults_ci_br["dimensions"]["pool"] = "luci.v8.ci"

defaults_try = {
    "executable": "recipe:v8",
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "default", "pool": "luci.v8.try"},
    "service_account": "v8-try-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 1800,
    "properties": {"builder_group": "tryserver.v8"},
}

defaults_triggered = {
    "executable": "recipe:v8",
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
    "ci.br.extended": defaults_ci_br,
}

GOMA = struct(
    DEFAULT = {
        "server_host": "goma.chromium.org",
        "rpc_extra_params": "?prod",
        "use_luci_auth": True,
    },
    ATS = {
        "server_host": "goma.chromium.org",
        "enable_ats": True,
        "rpc_extra_params": "?prod",
        "use_luci_auth": True,
    },
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


def _reclient_properties(use_rbe):
    if use_rbe == None:
        return {}

    return {
      "$build/reclient": dict(use_rbe),
      "use_rbe": True,
    }


# These settings enable overwriting variables in V8's DEPS file.
GCLIENT_VARS = struct(
    INSTRUMENTED_LIBRARIES = {"checkout_instrumented_libraries": "True"},
    ITTAPI = {"checkout_ittapi": "True"},
    V8_HEADER_INCLUDES = {"check_v8_header_includes": "True"},
    GCMOLE = {"download_gcmole": "True"},
    JSFUNFUZZ = {"download_jsfunfuzz": "True"},
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
    if kwargs.pop("close_tree", False):
        notifies = kwargs.pop("notifies", [])
        notifies.append("v8 tree closer")
        kwargs["notifies"] = notifies
    parent_builder = kwargs.pop("parent_builder", None)
    if parent_builder:
        resolve_parent_tiggering(kwargs, bucket_name, parent_builder)
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
        kwargs.pop("use_goma", GOMA.NONE),
        kwargs.pop("goma_jobs", None),
    ))
    properties.update(_reclient_properties(kwargs.pop("use_rbe", None)))
    properties.update(_gclient_vars_properties(kwargs.pop("gclient_vars", [])))
    kwargs["properties"] = properties
    kwargs = fix_args(defaults, **kwargs)
    luci.builder(**kwargs)

def multibranch_builder(**kwargs):
    added_builders = []
    close_tree = kwargs.pop("close_tree", True)
    for branch in branch_descriptors:
        args = dict(kwargs)
        parent_builder = args.pop("parent_builder", None)
        if parent_builder:
            args["triggered_by_gitiles"] = False
            resolve_parent_tiggering(args, branch.bucket, parent_builder)
        triggered_by_gitiles = args.pop("triggered_by_gitiles", True)
        first_branch_version = args.pop("first_branch_version", None)
        if triggered_by_gitiles:
            args.setdefault("triggered_by", []).append(branch.poller_name)
            args["use_goma"] = args.get("use_goma", GOMA.DEFAULT)
        else:
            args["dimensions"] = {"host_class": "multibot"}
        args["priority"] = branch.priority
        if branch.bucket == "ci":
            if close_tree:
                notifies = args.pop("notifies", [])
                notifies.append("v8 tree closer")
                args["notifies"] = notifies
        else:
            args["notifies"] = ["beta/stable notifier"]
            if _builder_is_not_supported(branch.bucket, first_branch_version):
                continue
        v8_basic_builder(defaults_ci, bucket = branch.bucket, **args)
        added_builders.append(branch.bucket + "/" + kwargs["name"])
    return added_builders

def resolve_parent_tiggering(args, bucket_name, parent_builder):
    # By the time the generators are executed the parent_builder property
    # is no longer present on the builder struct, so we add it here in
    # the properties and it will get removed by a generator
    args.setdefault("properties", {})["parent_builder"] = parent_builder
    # Disambiguate the scheduler job names, because they are not
    # nested by bucket, while builders are.
    args.setdefault("triggered_by", []).append(
        bucket_name + "/" + parent_builder)

def _builder_is_not_supported(bucket_name, first_branch_version):
    # do we need to skip the builder in this bucket?
    if first_branch_version:
        branch_id = bucket_name.split(".")[2]
        branch_version = _to_int_tuple(versions[branch_id])
        builder_first_version = _to_int_tuple(first_branch_version)
        return branch_version < builder_first_version
    return False

def _to_int_tuple(version_str):
    return [int(n) for n in version_str.split(".")]

def fix_args(defaults, **kwargs):
    args = dict(kwargs)
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
    mergeable_keys = ["dimensions", "properties"]
    for key in mergeable_keys:
        merge_defaults(defaults, args, key)
    args["execution_timeout"] = args["execution_timeout"] * time.second
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

def in_branch_console(console_name, *builder_sets):
    def in_category(category_name, *builder_sets):
        for builder_set in builder_sets:
            for builder in builder_set:
                branch_name = builder.split("/")[0]
                luci.console_view_entry(
                    console_view = branch_by_name(branch_name).console_id_by_name(console_name),
                    builder = builder,
                    category = category_name,
                )

    return in_category

def branch_by_name(name):
    for branch in branch_descriptors:
        if branch.bucket == name:
            return branch

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

greedy_batching_of_1 = scheduler.policy(
    kind = scheduler.GREEDY_BATCHING_KIND,
    max_batch_size = 1,
)

def console_view(name, title = None, repo = None, refs = None, exclude_ref = None, header = None):
    luci.console_view(
        name = name,
        title = title or name,
        repo = repo or "https://chromium.googlesource.com/v8/v8",
        refs = refs or ["refs/heads/master"],
        exclude_ref = exclude_ref,
        favicon = "https://storage.googleapis.com/chrome-infra-public/logo/v8.ico",
        header = header or "//consoles/header_main.textpb",
    )
