# Claude Code instructions — Film Effects

@AGENTS.md

Read and follow `AGENTS.md` in full. It is the authoritative product, technical, workflow, and acceptance specification for this repository.

## Additional Claude Code guidance

- Begin with an environment/SDK feasibility audit and a minimal **native Premiere Pro effect**, not a UXP-only extension or image mockup.
- Keep `PROGRESS.md` updated as a durable checkpoint after every meaningful milestone.
- Work autonomously through feasible implementation stages, run the available build/tests, and report only verified capabilities.
- If an Adobe SDK, Premiere installation, or Apple Metal/Xcode environment is missing, clearly identify the missing dependency and continue building independently testable code without claiming the native plugin was tested.
- Do not accidentally mix this project with **Track Range Exporter**; they are separate Premiere Pro plugins.
