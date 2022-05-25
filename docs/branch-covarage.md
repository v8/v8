Branches build coverage
======================

This is a description of V8 infrastructure for Beta and Stable branches.

Builder coverage for Beta/Stable branches is organised in the following consoles that reflect the same schema used for the main branch:
 - [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console)
 - [beta.ports](https://ci.chromium.org/p/v8/g/br.beta.ports/console)
 - [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console)
 - [stable.ports](https://ci.chromium.org/p/v8/g/br.stable.ports/console)

These consoles are the beta/stable branch counterparts of the [main](https://ci.chromium.org/p/v8/g/main/console) and [ports](https://ci.chromium.org/p/v8/g/ports/console) consoles for the main branch.


Monitoring
======================

V8 sheriffs will be notified on any failures in builders under [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console) and [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console) consoles

Updating the Beta/Stable branch references
======================

To update branch references the folowing locations in infra/config files need to be update:
 - [luci-milo.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/luci-milo.cfg)
   - consoles[id: "br.beta"]/refs
   - consoles[id: "br.beta.ports"]/refs
   - consoles[id: "br.stable"]/refs
   - consoles[id: "br.stable.ports"]/refs
 - [luci-scheduler.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/luci-scheduler.cfg)
   - trigger[id: "v8-trigger-br-beta"]/gitiles/refs
   - trigger[id: "v8-trigger-br-stable"]/gitiles/refs


Adding new builders
======================

To propagate a new builder addition under main/ports consoles you need to:
 - add a builder definition in beta and stable buckets in [cr-buildbucket.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/cr-buildbucket.cfg)
   - buckets[name: "luci.v8.ci.br.beta"]
   - buckets[name: "luci.v8.ci.br.stable"]
   - use the **same name** for the builder in all buckets
 - create BETA and STABLE jobs and triggers in [luci-scheduler.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/luci-scheduler.cfg)
   - trigger[id: "v8-trigger-br-beta"]/triggers: "BETA new_builder_name"
   - job { id: "BETA new_builder_name" ... }
   - trigger[id: "v8-trigger-br-stable"]/triggers: "STABLE new_builder_name"
   - job { id: "STABLE new_builder_name" ... } 
 - add the new builder references under corresponding Beta/Stable consoles in [luci-milo.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/luci-milo.cfg)
   - consoles[id: "br.beta..."]
   - consoles[id: "br.stable..."]
 - additionaly for builders under main console add a reference for notification in [luci-notify.cfg](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/luci-notify.cfg)
 - backmerge mb_config.pyl and builders.pyl file changes to Beta/Stable branch

Developer branches
======================

On this foundation for branch builder coverage it is possible to cover more than just Beta/Stable branches. Developer branches might require full or partial (no ports) builder coverage. To achieve that, one needs to replicate the Beta/Stable configuration in infra/config branch.
