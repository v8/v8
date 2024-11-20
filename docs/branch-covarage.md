Branches build coverage
======================

This is a description of V8 infrastructure for Beta, Stable and Extended branches.

Builder coverage for Beta/Stable/Extended branches is organized in the following consoles that reflect the same schema used for the main branch:
 - [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console)
 - [beta.memory](https://ci.chromium.org/p/v8/g/br.beta.memory/console)
 - [beta.ports](https://ci.chromium.org/p/v8/g/br.beta.ports/console)
 - [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console)
 - [stable.memory](https://ci.chromium.org/p/v8/g/br.stable.memory/console)
 - [stable.ports](https://ci.chromium.org/p/v8/g/br.stable.ports/console)
 - [extended.main](https://ci.chromium.org/p/v8/g/br.extended/console)
 - [extended.memory](https://ci.chromium.org/p/v8/g/br.extended.memory/console)
 - [extended.ports](https://ci.chromium.org/p/v8/g/br.extended.ports/console)

These consoles are the beta/stable/extended branch counterparts of the [main](https://ci.chromium.org/p/v8/g/main/console), [memory](https://ci.chromium.org/p/v8/g/memory/console) and [ports](https://ci.chromium.org/p/v8/g/ports/console) consoles for the main branch.


Monitoring
======================

V8 sheriffs will be notified on any failures in builders under [beta.main](https://ci.chromium.org/p/v8/g/br.beta/console), [stable.main](https://ci.chromium.org/p/v8/g/br.stable/console) and [extended.main](https://ci.chromium.org/p/v8/g/br.stable/console) consoles

Updating the Beta/Stable/Extended branch references
======================

To update branch references, update the [versions](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/definitions.star) and regenerate the lucicfg configs by running `./main.cfg`.


Adding new builders
======================

To propagate a new builder addition under main/memory/ports consoles you need to:
 - add a builder definition in one of the [multibranch consoles](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/builders/multibranch/) using the `multibranch_builder` abstraction.
 - specify the `first_branch_version` attribute - typically the current version on main
 - generate the configurations by running `./main.cfg`
 - backmerging mb_config.pyl and builders.pyl file changes to the respective branches is only required if the `first_branch_version` was not specified to the current main branch
