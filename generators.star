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

lucicfg.generator(ensure_forward_triggering_properties)
