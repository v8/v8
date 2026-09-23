# Prompts

This directory contains the common prompt for V8 and template prompts to teach
agents about specific tools. Everything is intended to work with `jetski-cli`.

## Creating the System Instruction Prompt

Run `vpython3 agents/scripts/install_for_jetski.py` to generate your root
`GEMINI.md` file from `agents/prompts/templates/modular.md` and link `.agents/`
rules and skills.

## Contributing

Changes to `common.md` and `templates/modular.md` should be done *carefully* as
they are meant to be used broadly across V8 workspaces.
