# Venosta Visuals Plugins — repository rules for coding agents

This repository is a collection of **free** plugins for Adobe Premiere Pro (and possibly other
Adobe applications later). Each plugin lives in its own folder under `plugins/` and has its own
`AGENTS.md` with the authoritative product specification for that plugin.

## Repository-wide rules

1. **Plugins are separate; common code lives in the shared engine.** Each plugin lives in
   `plugins/<plugin-name>/` with its own effect code, parameters, tests, docs and `PROGRESS.md`.
   Do not put one plugin's features into another. Code that several plugins need (image types,
   blur pyramid, colour/noise maths, GPU framework, Premiere/Resolve adapters, installer and CI
   templates) belongs in `shared/engine/` (owner decision, 2026-11-12) — never copy it between
   plugins. A change to the shared engine must keep every plugin's tests green.
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

## Branches and releases

- Develop on the **`dev`** branch (or short-lived branches merged into `dev`). Do not commit
  directly to `main`.
- Merge `dev` into `main` with a pull request when CI is green. Every push to `main` creates a
  GitHub release automatically (`.github/workflows/release.yml`), tagged `v<VERSION>.<run number>`.
- `VERSION` (repository root) holds the major.minor version. Versions starting with `0.` are
  published as pre-releases. Bump it when a plugin reaches a new stage (e.g. `1.0` after host
  testing).
- Add new downloadable artifacts (e.g. built plugin files) to the release workflow when a plugin
  can be built in CI.

## Adding a new plugin — checklist

- [ ] Create `plugins/<plugin-name>/` with `AGENTS.md` (spec), `CLAUDE.md` (imports `AGENTS.md`),
      `README.md`, and `PROGRESS.md`.
- [ ] Add the plugin to the **Plugins** table in the root `README.md`.
- [ ] Add any CI/test command for the plugin to the root `README.md` "For developers" section.
- [ ] Add the plugin's downloadable files to `.github/workflows/release.yml` (if any).
