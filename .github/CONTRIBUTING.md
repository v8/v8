# Welcome to V8-RISCV

We are working hard to port the V8 Javascript engine to RISC-V.

## Code of Conduct

As this port will eventually be upstreamed to the [core v8 project](https://github.com/v8/v8), and that project is a part of the [Chromium project](https://github.com/chromium/chromium), we follow the [Chromium Code of
Conduct](https://chromium.googlesource.com/chromium/src/+/master/CODE_OF_CONDUCT.md) for all communication relating to this project.

## Getting Started

Our [wiki](https://github.com/v8-riscv/v8/wiki) contains lots of useful information for developers wishing to contribute to this work. You can start off by getting the code (not as straight-forward as you may hope) by following along with ["Get the Source"](https://github.com/v8-riscv/v8/wiki/get-the-source).

## Communication

We are an open team and strongly encourage communication. The primary place for interaction with the team will be through the [issues](https://github.com/v8-riscv/v8/issues). This is the best place because it is totally public and it is permanent.

Contributing developers are also welcome to [join our Slack group](https://forms.office.com/Pages/ResponsePage.aspx?id=8o_uD7KjGECcdTodVZH-3OiciJKG_BJHrqMNgnsFFqtUNlRUNEQ5QUgxNk0wVEVaTjJBTDNOMDNIQS4u) for more informal discussions.

## Contributing

### Workflow

* Identify an issue to work on, or create a new issue to track the work
* Assign the issue to yourself so that the community knows that you are workin on it (if an issue is already assigned but stale, contact the current developer to discuss taking it over).
* Ensure you are starting with the latest version (checkout `riscv-porting-dev` and pull the latest)
* Create a new branch, with a name starting with the issue number (ex. `12-fix-arithmetic-bug`)
* Make changes and commit code in logical units
* Ensure commit messages follow the [recommended guidelines](https://github.com/v8-riscv/v8/wiki/using-git#committing)
* [Use `clang-format`](https://clang.llvm.org/docs/ClangFormat.html) to ensure that your changes follow the Google style
* Run the testsuite locally to ensure your changes do not cause any new failures:
  ```bash
  ./v8-riscv-tools/test-riscv.sh
  ```
* Push your branch to a personal fork of the repository
* [Submit a pull request](https://github.com/v8-riscv/v8/compare) to v8-riscv
* The PR must pass the CI jobs and be reviewed by an owner before being merged.
