# G1a rulings and the spec revision of 27 November 2026

G1a's first part report (`G1a-rules-review.md`) ended with six rulings. On 27 November 2026 Fergus ruled on all six, adopted the report's nine proposed doc changes with two more, set how the allocation target is judged, and set the order of the remaining work. This file records the rulings and what they changed in the spec and the register. It is the written record that the rule "don't change a committed decision without a written proposal in a gate report" asks for, and the register's reversals log has one row per change.

**G1a has not passed.** Pull request #2 stays open until it does.

## The rulings

| # | Question | Ruling | Where |
| --- | --- | --- | --- |
| 1 | The order of work: the royal-capture scan was on no checklist, and item 6's first step went ahead of item 7 | **Ratified:** the set scan (`ea9419e`) becomes a checklist item, and the order as done stands. **Condition, before G1a closes:** two tests hold check status read in place equal to the line scan on the passed multiverse: (i) 65 to 256 timelines, with passed timelines at table index 64 and above; (ii) passed boards with Q8 or Q20 off its default. Each must fail with its fault planted: (i) the pass table read from word 0 only; (ii) the passed copy of a board flagged as history. Fergus will supply `fuzz_check.cpp`, which covers both and can be adapted | Spec §3.3 item 8, §3.6, §14; NR3, NR7 |
| 2 | The ftxi pin | **Yes, and Fergus asks for the pin change:** ftxi to v0.5.2 (commit `e6cf824`), by `CONTRIBUTING.md`'s procedure, as its own change and before any other change to copied code. Driver edits needed to build against it belong to the same change. Divergence entries retire by the procedure, never by hand. Then the commit and nightly tiers run again on items A to C, and the report says what moved | Spec §3.5, §14.2; NR6 |
| 3 | The rewritten Temporal-5D commit ids | **(a).** The headers stay. The tree ids in `PROVENANCE.md` are the pins of record | Spec §3.4, §13.1; NR1; `PROVENANCE.md` |
| 4 | Engines the copy check can't see | **(c),** not the (a) the report recommended. A Fractal-owned copy check under `tools/`, in place before part b's first commit. Its sources sit at pinned commits in a cache outside the tree, read by the tool and never by an agent: Stockfish, YaneuraOu, Lc0, KataGo, dlshogi, and Temporal-5D's §3.2 parts. It matches exact lines and token runs with identifiers ignored. Any commit a reading pass opens is checked too. Nothing under `oracles/` changes for this | Spec §3.5, §14.2, §15.2; NR8, NF12 |
| 5 | Dead copied files | **Keep all of them until G1 closes,** then remove what is still unused in one recorded change, with their `PROVENANCE.md` rows marked removed | Spec §3.1 |
| 6 | Two of Temporal-5D's guards | **Yes to `.claude/settings.json`** with Temporal-5D's deny list, plus `gh pr merge --admin` and `gh api` writes to branch protection and rulesets. `/.claude/` goes into `CODEOWNERS` and into `AGENTS.md`'s do-not-touch list. **No changelog rule.** Fergus sets branch protection on `main` | Done in `d0344cd` |

Also ruled:

- **Docs.** Proposed changes 1 to 9 apply to `SPEC.md` and `DECISIONS.md`, and so do the copy check of ruling 4 and the two tests of ruling 1.
- **The allocation target.** 90% is judged on construction and on the eight-turn node: 181 heap allocations to 18 or fewer, and 519 to 51 or fewer (MEASUREMENTS entry 3: t0database turn starts under 50 boards), asserted by test. If items 6b to 6d land short, the report brings the measured figure and a proposed target.
- **The order of what is left:** the rulings and docs; the pin bump; the reruns; the tests owed; items 7, 6b to 6d, and 1.

## How ruling 6 was carried out

`.claude/settings.json` holds a deny list and nothing else (`d0344cd`):

- **From Temporal-5D's `.claude/settings.json` at the pin,** its eleven deny rules unchanged: `git push` with `--force`, `-f` or a `+` refspec, from Bash and from PowerShell, and the three GitHub tools that write files without a pull request (`create_or_update_file`, `push_files`, `delete_file`). Its hooks, status line and allow list belong to its own tooling and did not come across.
- **Added:** `gh pr merge` with `--admin`, from either shell. `gh api` calls that name `protection` or `rulesets` and carry a method (`-X`, `--method`) or a body (`-f`, `-F`, `--input`), in either order, and GraphQL calls that name the mutations on `BranchProtectionRule` or `RepositoryRuleset`. A plain `gh api` read of either stays allowed, so an agent can still report whether `main` is protected.
- `CODEOWNERS`, `AGENTS.md`, `CONTRIBUTING.md` and `PROVENANCE.md` say where the list came from and who owns it.

## What changed in the spec

| Proposal | Section | Now says |
| --- | --- | --- |
| — | §0 | The second revision, and where G1a's reports are |
| 1 | §3.1, the check row | The set scan answers under the default rules, held equal to the copied line scan, which answers under the alternatives and wherever a witness is wanted |
| Ruling 5 | §3.1 | Unused copied files stay until G1 closes, then go in one recorded change |
| 2 | §3.3 | Item 8, the royal-capture scan, done, with ruling 1's two tests owed. Item 6: 6a done by `move_edits` rather than stack builders, its allocation counts, and the target as ruled. The order of work as ratified |
| 3 | §3.4, §13.1 | The rewrite of Temporal-5D's history; the tree ids are the pins of record |
| 4 | §3.5 | The checkouts in place; the `gate` job; what each tier gates; the removed sentence "Until the checkouts are in place CI compares the engine with an oracle at no position"; ftxi's pin bump |
| Ruling 4 | §3.5 | The Fractal-owned copy check [NR8] |
| 5 | §3.6 | `test_search` recovered; the pins and the inventory in place; `tests/` and `fractal-tests` named; what is still owed |
| 6 | §5.2 | G1a's figures beside G0's |
| 7 | §14.2, §14.3, `docs/BUILD_PROMPT.md` | Step 1 done; the order of what is left; the copy check before part b; `AGENTS.md` and `CONTRIBUTING.md` under *Read first*; the nightly tier's oracles named in *Environment*, so that `build` doesn't also build Kairos |
| Ruling 1 | §14 | G1a's criteria include the two tests of §3.3 item 8 |
| 8 | §13.3, §15.2, `docs/BUILD_PROMPT.md` | NF12 covers the tests too, copied or ported |

## What changed in the register

- **New rows.** NR7: a faster routine keeps the copied one as its reference, held equal by test (proposal 9). NR8: the Fractal-owned copy check (ruling 4). The register now has 109 decisions, 68 of them committed.
- **Changed rows.** NR1 names the tree that is the pin of record (ruling 3). NR3 carries the ratified order (ruling 1). NF12 adds the tests and "copied or ported", and rests on NR8 as well (proposal 8).
- **Open items.** Updated for the work that is left, branch protection, the ftxi pin, the copy check and the unused files.
- **Reversals log.** Seven rows dated 27 November 2026.
