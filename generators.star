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
        if bucket.name == 'ci':
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
        if console.name == 'builder-tester':
            for category, contents in categories.items():
                for builder in contents:
                    cbuilder = dict(
                            name="buildbucket/luci.v8.ci/" + builder,
                            category=category)
                    console.builders.append(cbuilder)


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

lucicfg.generator(ensure_forward_triggering_properties)
