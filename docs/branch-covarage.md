Branches build coverage
======================

This is a description of V8 infrastructure for Beta, Stable and Extended branches.

Builder coverage for Beta/Stable/Extended branches is organized in the following consoles that reflect the same schema used for the main branch:
 - [15.1.main](https://ci.chromium.org/p/v8/g/15.1/console)
 - [15.1.memory](https://ci.chromium.org/p/v8/g/15.1.memory/console)
 - [15.1.ports](https://ci.chromium.org/p/v8/g/15.1.ports/console)
 - [15.0.main](https://ci.chromium.org/p/v8/g/15.0/console)
 - [15.0.memory](https://ci.chromium.org/p/v8/g/15.0.memory/console)
 - [15.0.ports](https://ci.chromium.org/p/v8/g/15.0.ports/console)
 - [15.0-extended.main](https://ci.chromium.org/p/v8/g/15.0-extended/console)
 - [15.0-extended.memory](https://ci.chromium.org/p/v8/g/15.0-extended.memory/console)
 - [15.0-extended.ports](https://ci.chromium.org/p/v8/g/15.0-extended.ports/console)

These consoles are the beta/stable/extended branch counterparts of the [main](https://ci.chromium.org/p/v8/g/main/console), [memory](https://ci.chromium.org/p/v8/g/memory/console) and [ports](https://ci.chromium.org/p/v8/g/ports/console) consoles for the main branch.


Monitoring
======================

V8 sheriffs will be notified on any failures in builders under branch main consoles.

Updating the Beta/Stable/Extended branch references
======================

To update branch references, update the constants in [definitions.star](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/definitions.star) and regenerate the lucicfg configs by running `./main.star`.


Adding new builders
======================

To propagate a new builder addition under main/memory/ports consoles you need to:
 - add a builder definition in one of the [multibranch consoles](https://chromium.googlesource.com/v8/v8/+/refs/heads/infra/config/builders/multibranch/) using the `multibranch_builder` abstraction.
 - specify the `first_branch_version` attribute - typically the current version on main
 - generate the configurations by running `./main.star`
 - backmerging mb_config.pyl and builders.pyl file changes to the respective branches is only required if the `first_branch_version` was not specified to the current main branch
