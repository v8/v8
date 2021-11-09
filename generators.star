# Copyright 2021 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

def aggregate_builder_tester_console(ctx):
    """
    This callback collects and groups all builders by parent name in separate
    categories containing both the parent and the children to later add this
    categories to the `builder/tester` console. The purpose of the console is
    to make the parent/child relationship evident.

    Warning! The callback needs to run before the `parent_builder` property
    gets removed, before `ensure_forward_triggering_properties` callback.
    """
    build_bucket = ctx.output["cr-buildbucket.cfg"]
    categories = {}
    for bucket in build_bucket.buckets:
        if bucket.name == "ci":
            for builder in bucket.swarming.builders:
                if builder.properties:
                    properties = json.decode(builder.properties)
                    parent = properties.pop("parent_builder", None)
                    if parent:
                        contents = categories.get(parent, [])
                        if contents:
                            contents.append(builder.name)
                        else:
                            categories[parent] = [parent, builder.name]
    milo = ctx.output["luci-milo.cfg"]
    for console in milo.consoles:
        if console.name == "builder-tester":
            for category, contents in categories.items():
                for builder in contents:
                    cbuilder = dict(
                        name = "buildbucket/luci.v8.ci/" + builder,
                        category = category,
                    )
                    console.builders.append(cbuilder)

def is_artifact_builder(builder):
    return builder.name.endswith("builder")

def is_release_builder(builder):
    return "debug" not in builder.name

BUILDER_TESTER_SPLICER = struct(
    decision_func = is_artifact_builder,
    left_category = "builder",
    right_category = "tester",
)

RELEASE_DEBUG_SPLICER = struct(
    decision_func = is_release_builder,
    left_category = "rel",
    right_category = "dbg",
)

def builder_with_category(builder, add_sub_cat, splicer):
    category = builder.category
    if splicer.decision_func(builder):
        subcat = splicer.left_category
    else:
        subcat = splicer.right_category
    if add_sub_cat:
        category += ("|" + subcat)
    return dict(
        name = builder.name,
        category = category,
    )

def splice_by_categories(builders, splicer):
    # Key builder lists by categories.
    categories = dict()
    for builder in builders:
        categories.setdefault(builder.category, []).append(builder)

    # Analyze each category separately for subcategories.
    result = []
    for category, contents in categories.items():
        left_builders = [b for b in contents if splicer.decision_func(b)]
        right_builders = [b for b in contents if not splicer.decision_func(b)]

        # Create subcategories only with enough builders of each kind.
        add_sub_cat = len(left_builders) > 1 and len(right_builders) > 1
        for builder in left_builders + right_builders:
            result.append(builder_with_category(builder, add_sub_cat, splicer))
    return result

def mirror_console(original_console, dev_console):
    dev_console.builders = splice_by_categories(
        original_console.builders,
        BUILDER_TESTER_SPLICER,
    )
    dev_console.builders = splice_by_categories(
        dev_console.builders,
        RELEASE_DEBUG_SPLICER,
    )

def mirror_dev_consoles(ctx):
    """
    Mirror main and ports as main-dev and ports-dev with builders and testers
    grouped under subcategories.
    """
    consoles = dict()
    milo = ctx.output["luci-milo.cfg"]
    for console in milo.consoles:
        consoles[console.name] = console
    mirror_console(consoles["main"], consoles["main-dev"])
    mirror_console(consoles["ports"], consoles["ports-dev"])

def ensure_forward_triggering_properties(ctx):
    """
    This callback collects (and removes) `parent_builder` properties from the
    builders, then reverse the relationship and set `triggers` property on the
    corresponding builders.
    """
    build_bucket = ctx.output["cr-buildbucket.cfg"]
    for bucket in build_bucket.buckets:
        triggers = dict()
        for builder in bucket.swarming.builders:
            if builder.properties:
                properties = json.decode(builder.properties)
                parent = properties.pop("parent_builder", None)
                if parent:
                    triggers.setdefault(parent, []).append(builder.name)
                builder.properties = json.encode(properties)
        for builder in bucket.swarming.builders:
            tlist = triggers.get(builder.name, [])
            if tlist:
                properties = json.decode(builder.properties)
                properties["triggers"] = tlist
                builder.properties = json.encode(properties)

lucicfg.generator(aggregate_builder_tester_console)

lucicfg.generator(mirror_dev_consoles)

lucicfg.generator(ensure_forward_triggering_properties)
