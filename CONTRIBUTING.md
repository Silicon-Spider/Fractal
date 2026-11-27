# Contributing

The rules are in `CLAUDE.md` and `AGENTS.md`, and they bind people and agents alike. This file adds the procedures around the oracle harness that the rules refer to. The harness was copied from Temporal-5D with the rules engine; its manual is `oracles/README.md`, and these procedures restate Temporal-5D's for Fractal.

## Why the oracles are here

Everything exact that the engine claims — a legal turn, a check, a mate — comes from the rules engine. A fault there doesn't lose a game; it produces false facts, and a search and a net trained on its games inherit them. So the rules engine is compared, position by position, with implementations written by other people, and those are pinned in this repository so that the comparison can't be skipped when it is inconvenient.

An oracle is worth what its independence is worth. Every rule about copying, porting and the boundary protects that.

## Every change

1. Build, then run the checks under "Before you report a change as done" in `AGENTS.md`.
2. Record what changed where the project keeps its records: a measurement in `docs/MEASUREMENTS.md`, a change to a copied file in `PROVENANCE.md`, a result or a proposal in the gate report under `docs/gates/`.
3. `docs/SPEC.md` and `docs/DECISIONS.md` change only at Fergus's request. If a change contradicts a committed decision, or you can't tell what the spec should say, write the proposal in the gate report and stop.

## When the engine and an oracle disagree

1. The rulebook decides (`docs/rules/README.md`): not the oracle, not the engine, and never the official game client.
2. If the engine is wrong, fix the engine.
3. If the oracle is wrong, add an entry to `oracles/known-divergences.toml` with the rulebook citation that settles it, and report it upstream. An entry excuses one observation exactly. It stops matching if either side's answer changes, if the oracle's pin moves, or if the disagreement stops occurring.
4. An oracle that stops because it can't read a move's notation, where the engine plays on, is cited against the notation specification instead. If the oracle really holds the move illegal, it is a rules matter and needs the rulebook.
5. A disagreement that isn't recorded fails the gate. There is no list of expected failures and no way to skip a record.

## The engine's baseline

The commit tier gates no ruleset, but it fails an engine that declines a record it read on the last green run. What the engine read then is `oracles/engine-baseline.json`, pinned in `oracles/gate.toml`. It is written only by `python oracles/harness.py baseline --write`, from a green run of the commit tier, and never edited by hand. A new baseline that lists a `LOSS`, a record the engine now reads less of, is a loss of checking: fix the engine or raise the question, and never record one to make a run pass.

## Changing a pin

A pin isn't moved routinely. When one has to move, it is a change of its own, with its own gate report entry:

1. Edit the entry in `oracles/oracles.lock.toml`: `commit`, `ref`, `committed`.
2. `python oracles/harness.py sync`, then `git add oracles/third_party/<id>` so that the link in the index follows.
3. `python oracles/harness.py verify`. It prints the new commit's tree id and, if the licence file changed, its new hash. Read the new licence before you copy its hash into the lock file.
4. Update `oracles/corpus/manifest.toml` for every record of that source whose blob changed, and look at `python oracles/harness.py corpus --unlisted` for new ones.
5. `python oracles/harness.py build`.
6. Check again every entry in `oracles/known-divergences.toml` bound to the old pin.
7. `python oracles/harness.py golden --write --repeat 2 --jobs 8`, on a quiet machine, and read the diff. Apart from each file's signature line, a changed line under `oracles/golden/` is an answer that changed.
8. If a commit-tier record changed, record the engine's baseline again and read its diff.
9. `python oracles/harness.py provenance --write`, and read again the notes about that oracle in the lock file.
10. Record the bump in the gate report: the answers that moved, and any line of `oracles/DISAGREEMENTS.md` that appeared or went.

Editing a driver, an adapter, or the `base.py`, `fingerprint.py` and `headers.py` the adapters share makes the golden files stale in the same way, even when the edit is to a comment. Steps 7 to 10 apply.

## Adding game records

The corpus is a manifest, not a folder of copies. A third-party record is named by its pinned source, path, blob id and hash, and read in place.

- A record already in a pinned source: `python oracles/harness.py corpus --unlisted` prints its manifest entry. Add the licence it is used under, a tier and any notes, then record what the oracles say about it with `golden --write`. A commit-tier record needs a new engine baseline too.
- A record of Fractal's own goes under `oracles/corpus/records/` with `source = "local"`. That directory is exempt from line-ending conversion, because a record is pinned by the hash of its bytes.
- Check the licence of any third-party source of records before adding it, and give it a row in `PROVENANCE.md`.

## Review

`.github/CODEOWNERS` puts `oracles/`, `.github/` and `.claude/` under their owner's review: the harness holds the recorded answers, the workflows decide which checks run, and `.claude/settings.json` holds the deny list that keeps agents from force-pushing, merging past review and changing branch protection. It binds once branch protection on `main` requires a review from a code owner and the CI jobs as status checks. Until then it only requests one, so run the checks yourself.
