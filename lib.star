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
defaults_ci_br["dimensions"]["pool"] = "ci"

defaults_try = {
    "executable": {"name": "v8", "cipd_package": "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build", "cipd_version": "refs/heads/master"},
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "default"},
    "service_account": "v8-try-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 1800,
    "properties": {"mastername": "tryserver.v8"},
}
defaults_triggered = {
    "executable": {"name": "v8", "cipd_package": "infra/recipe_bundles/chromium.googlesource.com/chromium/tools/build", "cipd_version": "refs/heads/master"},
    "swarming_tags": ["vpython:native-python-wrapper"],
    "dimensions": {"host_class": "multibot", "pool": "luci.v8.try"},
    "service_account": "v8-try-builder@chops-service-accounts.iam.gserviceaccount.com",
    "execution_timeout": 3600,
    "properties": {"mastername": "tryserver.v8"},
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

goma_props = {"$build/goma": {"server_host": "goma.chromium.org", "rpc_extra_params": "?prod"}}

multibot_caches = [
    swarming.cache(
        path = "builder",
        name = "v8_builder_cache_nowait",
    ),
]

def v8_builder(**kv_args):
    bucket_name = kv_args["bucket"]
    v8_basic_builder(defaults_dict[bucket_name], **kv_args)

def v8_basic_builder(defaults, **kv_args):
    kv_args = fix_args(defaults, **kv_args)
    luci.builder(**kv_args)

def v8_branch_coverage_builder(**kv_args):
    for bucket_name in ["ci", "ci.br.beta", "ci.br.stable"]:
        args = dict(kv_args)
        v8_basic_builder(defaults_ci, bucket = bucket_name, **args)

def v8_try_ng_pair(name, **kv_args):
    triggered_timeout = kv_args.pop("triggered_timeout", None)
    kv_args["properties"]["triggers"] = [name + "_ng_triggered"]
    v8_basic_builder(defaults_try, name = name + "_ng", bucket = "try", **kv_args)
    v8_basic_builder(
        defaults_triggered,
        name = name + "_ng_triggered",
        bucket = "try.triggered",
        execution_timeout = triggered_timeout,
    )

def v8_auto(name, recipe, cipd_package = None, cipd_version = None, execution_timeout = None, **properties):
    properties = dict(properties.items() + goma_props.items())
    executable = dict()
    executable["name"] = recipe
    if cipd_package:
        executable["cipd_package"] = cipd_package
    if cipd_version:
        executable["cipd_version"] = cipd_version
    v8_basic_builder(
        defaults_ci,
        name = name,
        bucket = "ci",
        executable = executable,
        dimensions = {"os": "Ubuntu-16.04", "cpu": "x86-64"},
        service_account = "v8-ci-autoroll-builder@chops-service-accounts.iam.gserviceaccount.com",
        execution_timeout = execution_timeout,
        properties = properties,
    )

def fix_args(defaults, **kv_args):
    args = dict(kv_args)
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

    return args

def override_defaults(defaults, args, key):
    args[key] = args.pop(key, None) or defaults.get(key, None)

def merge_defaults(defaults, args, key):
    args[key] = dict(clean_dict_items(defaults, key) + clean_dict_items(args, key))

def clean_dict_items(dictionary, key):
    return {k: v for k, v in dictionary.get(key, {}).items() if v != None}.items()
