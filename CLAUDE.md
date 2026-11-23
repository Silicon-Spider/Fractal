# Fractal: rules for every agent session

Fractal is a standalone, CPU-only engine for 5D Chess With Multiverse Time Travel, in C++20: alpha-beta over whole turns, scored by an NNUE that reads the boards. The design is `docs/SPEC.md`, the decisions are `docs/DECISIONS.md`, and the build plan is `docs/BUILD_PROMPT.md`.

- Nothing in the engine's build or in the running engine depends on Temporal-5D, Kairos or any other engine. The harness's pinned oracles are test fixtures under `oracles/third_party/`, run as separate programs in tests only; a clone without submodules still builds the engine and passes its tests.
- Temporal-5D's rules engine is copied in once (spec §3.1), with every file's provenance in `PROVENANCE.md` and in a header where the file can carry one; nothing from §3.2.
- No code from Kairos, Stockfish, Lc0, KataGo, dlshogi, YaneuraOu or the oracle engines in the engine, its tools, its tests or its trainer, copied or ported: retyping another engine's logic, or pasting it with the names changed, is copying. Ideas only. Read another engine to understand an algorithm, write the idea down in your own words with its source in the gate report, and code from that note and the rulebook.
- Nothing outside `oracles/` includes, imports, links, loads or names a path into it, and the engine never runs the harness or an oracle. `python oracles/harness.py boundary` checks this and looks for pasted oracle code; it can't see a port, so you are the check on that.
- When the engine and an oracle disagree, the rulebook (`docs/rules/README.md`) decides. A wrong oracle gets an entry in `oracles/known-divergences.toml` with the rule that settles it; without a citation, stop and report.
- Never make a check pass by weakening it: no deleted or demoted corpus record, lowered budget, waiver, hand-edited golden file, pin, baseline, pinned hash, test inventory or skip list. Each is recorded again only on purpose, by its own command, with the reason in the gate report.
- Copied code passes its own tests and the gate's commit tier after each change, and the nightly tier before a change to it merges. Change it one item at a time; mark the file's header and add its row to `PROVENANCE.md`; a new file in a copied directory gets a row there too.
- When a faster routine takes over from a copied one, the copied one stays as the reference and a test holds the two equal, case by case. A refactor never moves a pin (`tests/test_legality.cpp`, `engine/tests/test_detectors.cpp`).
- Don't touch `oracles/third_party/`, the oracle pins, `oracles/golden/` or the adapters and drivers without being asked, and never update submodules recursively: use `python oracles/harness.py sync`.
- No hand-crafted evaluation of any kind. The net is the only evaluator; unweighted rule-fact counts appear only in arm NX2.
- Nets train only on Fractal's own searches and results, never on another engine's evaluations.
- The search never calls per-move exact legality. A turn is legal because the turn generator yielded it, the depth-first detector returned it, or it was replayed whole and passed the submission test. A stopped or failed generator never proves mate, and no mate is reported unless every legal turn was searched.
- Rules logic lives only in the rules engine. Store games and positions, never features.
- Incremental updates equal from-scratch rebuilds; the integer eval is bit-identical across targets; a node-limited single-thread search is deterministic.
- Don't change a committed decision without a written proposal in a gate report. Don't edit `docs/SPEC.md` or `docs/DECISIONS.md`: they change only at Fergus's request, and each revision is recorded in `docs/gates/`.
- Record gate results in `docs/gates/`, measurements in `docs/MEASUREMENTS.md` and nets in `NETS.md`. Every gate report ends with the rulings it needs.
- Git: one branch per phase or gate part, a pull request into `main` at each gate, never force-push `main`, and keep data and nets out of git.
- Compute: the desktop by default and always for training. GitHub Actions runs CI and adds spare capacity for test matches and self-play shards, within GitHub's terms and the budget in spec §11; every Actions job also runs locally.
- Build: CMake and Ninja, C++20, g++ 13+ or clang; Windows through MSYS2 ucrt64. Tests need Python 3.12+ and git.
- Before reporting a change as done, run the checks `AGENTS.md` lists and report what they printed.

The rules about other engines, the oracles and copied code in full, with the commands to run:

@AGENTS.md
