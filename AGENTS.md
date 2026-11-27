# Rules for coding agents

These rules bind every agent session and every human contributor. `CLAUDE.md` lists the project's rules in brief. This file is the part about other engines, the oracles and the copied code: the places where a rule broken once can't be seen afterwards.

Most of it is carried over from Temporal-5D's rules for its oracle harness, which Fractal copied along with the harness (`oracles/README.md` is its manual). `python oracles/harness.py verify` checks much of it mechanically. It can't check all of it, and where it can't, you are the check.

## Other engines are for ideas, never for code

"Another engine" means Kairos, ftxi's 5dchess_engine, cwmtt, Chess in 5D, the notation repository's parser, Stockfish, Lc0, KataGo, dlshogi, YaneuraOu, and every part of Temporal-5D that `PROVENANCE.md` doesn't list (spec §3.2).

- **Never copy code from another engine** into Fractal: not into the engine, its tools, its tests or its trainer [NF11, NF12].
- **Never port it.** Retyping another engine's logic in C++, or pasting it with the names changed, is copying. It carries that engine's licence and its bugs across. Where the engine is an oracle, it also leaves nothing to compare against: a check detector ported from ftxi can't be tested against ftxi, because both now share its mistakes.
- This covers move generation, check detection, the legal-turn and mate searches (the hypercuboid method included), the turn state machine, notation readers, search heuristics, evaluation code, data tables, tests and test positions.
- **You may read another engine to understand an algorithm, and cite it.** Then close it. Write the idea down in your own words, with where it came from, in the gate report. Write the code from that note, the rulebook and the spec. If you can't describe a design without the source open, you don't have an idea yet; you have someone else's code.
- **A subagent sent to read another engine reports in prose,** with file and line references and no code. Say so in its brief, and don't paste what it returns into the tree.
- Rules logic is written from `docs/rules/README.md`. The notation reader is written from the notation specification's text. Test positions are written from the rulebook, never taken from another implementation's tests.
- The moment you most want to reuse an oracle is when you're stuck on a subtle rules bug, which is when the oracle is most needed as an independent check. If you're stuck, say so.

## The boundary

> Everything under `oracles/` may use the engine. Nothing outside `oracles/` may use anything under it.

- Outside `oracles/`, never import, include, link or load anything from it, and never name a path into it. A comment or a document may cite anything, and code may name the manual, `oracles/README.md`.
- Don't name an oracle's repository or package in a build file, a script or a workflow. CI may run the harness; it may not fetch or name an oracle.
- The engine never runs the harness or an oracle, by any route, a subprocess included. The gate's engine adapter (`t5d-gate`) lives with the engine and asks the engine, never a recorded answer.
- Nothing is committed under `oracles/third_party/` except the five submodule links.
- `python oracles/harness.py boundary` checks all of this. Its copy check finds six consecutive lines of logic, or one long line, that match a pinned oracle's source. It can't find a port, a paste with renamed identifiers, or code from an engine that isn't pinned there.
- If the boundary check stops you and you believe it is wrong, the way through is a reviewed waiver in `oracles/gate.toml`, never a restructuring that hides the dependency. Ask first.

## When the engine and an oracle disagree

- The rulebook decides: not the oracle, not the engine, and never the official game client.
- If the engine is wrong, fix the engine.
- If the oracle is wrong, add an entry to `oracles/known-divergences.toml` with the rulebook citation that settles it. If you can't cite the rule, you haven't shown the oracle is wrong: stop and report the disagreement.
- The oracles disagree with each other in places (`oracles/DISAGREEMENTS.md`). That is data. Don't "fix" it.

## Never make a check pass by weakening it

Don't get to green by any of these. If one looks necessary, stop and ask.

- Deleting a corpus record, moving it to a later tier, or lowering a tier's budget, `min_checked` or `proof_share`.
- Adding a boundary waiver or a divergence entry you can't justify.
- Editing by hand a golden file, a cost file, `MANIFEST`, the engine baseline, the rulebook pin or an oracle pin.
- Editing a pinned hash in a test (`tests/test_legality.cpp`, `engine/tests/test_detectors.cpp`), a test inventory (`tools/inventory/`), a skip list, or the output CI expects.
- Making the engine adapter report `unsupported` or leave a field out.

A pin, an inventory or a baseline is recorded again only on purpose, by its own command, with the reason in the gate report. A gate run restricted with `--record` or `--source` is never a tier's result. A green gate means the oracle layer is healthy and the engine had no finding against it; read the last lines of its output to see which rulesets it gated, if any.

## Changing copied code

- **One change at a time** (spec §3.3). After each: both test suites with their inventories, the provenance check, the gate's commit tier, `fractal-legality --verify`, and a speed measurement in `docs/MEASUREMENTS.md`. Before a change merges: the nightly tier.
- **Mark it.** The file's header `state:` line says what changed. `PROVENANCE.md` gets a row under "Changes since the copy" with the hash that `python tools/check_provenance.py --hash PATH` prints. A new file in a directory that holds copied files gets a row under "Fractal's own files".
- **Keep the slow path.** When a faster routine takes over from a copied one, the copied one stays as the reference, and a test holds the two equal: on random positions, on recorded games, and case by case with each case isolated, so that one right answer can't hide another's absence. The set scan and the line scan in `engine/src/check.cpp` are the model.
- **A refactor doesn't move a pin.** If the generator's output hash, its node counts or the check verdicts change, the change altered behaviour. Find out why before anything else.
- Copied tests aren't edited to make them pass. The one trim so far, in `test_search.cpp`, is recorded in `PROVENANCE.md`.
- Later Temporal-5D fixes are merged by hand, never through a dependency, and recorded in `PROVENANCE.md` with their source commit.

## Things you must not touch without being asked

- The contents of `oracles/third_party/`. They are read-only, and `verify` fails if a submodule is modified.
- The pins in `oracles/oracles.lock.toml`. A pin bump is a reviewed change with its own procedure (`CONTRIBUTING.md`).
- `oracles/golden/*` and `oracles/DISAGREEMENTS.md`, by hand.
- The adapters and drivers (`oracles/oracle_harness/adapters/`, `headers.py`, `fingerprint.py`, `oracles/drivers/`), in passing. Editing one, even a comment, makes the golden files stale, and recording them again takes about half an hour on a quiet machine.
- Never run `git submodule update --init --recursive`: the oracles' own submodules aren't needed, and one is declared with an SSH URL. Use `python oracles/harness.py sync`.
- The release tier. It fails on findings inherited from Temporal-5D's recordings, and no budget fixes them (spec §3.5).
- Anything under `.claude/`. `.claude/settings.json` denies agents force-pushes, GitHub writes that bypass review, `gh pr merge --admin`, and changes to branch protection and rulesets (G1a ruling 6). Its owner sets it, as the owner sets branch protection; an agent never edits it to get a command through.

## Before you report a change as done

Run these and report what they printed. On Windows the interpreter may be `py -3.12` rather than `python`.

```
cmake --build build/engine
python tools/run_tests.py --exe build/engine/t5d-tests --inventory tools/inventory/t5d-tests.tsv
python tools/run_tests.py --exe build/engine/fractal-tests --tests-dir tests --inventory tools/inventory/fractal-tests.tsv
python tools/check_provenance.py
python tools/check_docs.py
python oracles/harness.py verify
python oracles/harness.py gate --tier commit
```

For a change to copied code, also:

```
python -I data/t0database/to_5dpgn.py build/t0db        # once: the records the next two read
build/engine/fractal-legality --records build/t0db --verify
build/engine/fractal-legality --records build/t0db      # on a quiet machine, for docs/MEASUREMENTS.md
python oracles/harness.py gate --tier nightly           # before it merges
```

The oracle checkouts and builds they need: `python oracles/harness.py sync`, then `python oracles/harness.py build --oracle ftxi --oracle notation` for the commit tier, and `python oracles/harness.py build --bootstrap --oracle cwmtt --oracle chessin5d` for the nightly tier's. Name the oracles: Kairos is pinned for the copy check and is never built (spec §3.5), and a bare `build` would build it.
