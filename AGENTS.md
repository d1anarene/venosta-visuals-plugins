# Venosta Visuals Plugins — repository rules for coding agents

This repository is a collection of **free** plugins for Adobe Premiere Pro (and possibly other
Adobe applications later). Each plugin lives in its own folder under `plugins/` and has its own
`AGENTS.md` with the authoritative product specification for that plugin.

## Repository-wide rules

1. **Keep plugins separate.** Each plugin is self-contained in `plugins/<plugin-name>/` with its
   own build, tests, docs and `PROGRESS.md`. Do not mix code or features between plugins.
2. **Always update the root `README.md` when a plugin is added** (or renamed, removed, or its
   status changes). The README must list every plugin with a one-line description, its current
   status (e.g. "in development", "host-tested", "released"), and a link to the plugin's own
   README. Keep the no-warranty notice and the link to https://venostavisuals.com intact.
3. **Be honest about verification.** Never claim a plugin was tested inside Premiere Pro, Media
   Encoder or any other Adobe host unless it actually was. Mark unit-tested, host-tested and
   unverified behaviour separately.
4. **Do not commit Adobe SDK files** (headers, samples, binaries) or other proprietary material
   unless its license explicitly allows redistribution.
5. Keep `PROGRESS.md` in each plugin folder current after every meaningful milestone.
6. Plugins are free and provided without any warranty (see `LICENSE`).

## Adding a new plugin — checklist

- [ ] Create `plugins/<plugin-name>/` with `AGENTS.md` (spec), `CLAUDE.md` (imports `AGENTS.md`),
      `README.md`, and `PROGRESS.md`.
- [ ] Add the plugin to the **Plugins** table in the root `README.md`.
- [ ] Add any CI/test command for the plugin to the root `README.md` "For developers" section.
