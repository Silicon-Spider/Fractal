# G1a: ftxi's pin moved to v0.5.2

G1a ruling 2 (`G1a-rulings.md`) asked for ftxi's pin to move from v0.4.10 to v0.5.2, by the procedure in `CONTRIBUTING.md`, as a change of its own, with divergence entries retiring by the procedure and never by hand. This is that change's gate report entry (the procedure's step 10). It was done on 28 November 2026.

Nothing in the engine, its tests, its tools or its trainer changed. The change touches `oracles/` (the harness manual and one of its self-tests included), the ftxi link under `oracles/third_party/`, `PROVENANCE.md` and this file.

## Summary

- **The pin.** ftxi is at `e6cf82403c3580ebad4b385f24d610a87438d322` (tag v0.5.2, committed 20 October 2026, tree `3d989a886169df23a4d9347c4a8b5f6f4ebae8dc`), 59 commits after v0.4.10. Its licence file is unchanged.
- **Both tiers pass.** The commit tier passes with 0 records declined. The nightly tier passes and gates the same two rulesets as before: Standard - Half Reflected (1 record, 21 positions) and Standard - Reversed Royalty (1 record, 122 positions).
- **ftxi moved to the engine's answers, not the other way.** Its check verdict went from check to no check at 131 recorded positions, and its action count rose at 2. At every one of those the engine compares, ftxi now gives the answer the engine gave before. No answer of the engine's moved, and its baseline is as it was.
- **151 divergence entries retired.** Both tiers reported them as no longer occurring, and they were removed. The ten other ftxi entries still occur and carry the new pin.
- **One new disagreement, with two entries.** v0.5.2 reads Standard - Half Reflected, which v0.4.10 did not, and sets it up with White's king and queen exchanged. The rulebook, the notation specification's example game and the other three oracles exchange Black's. Half Reflected is a gen-1 variant.
- **The first recording was thrown away.** My port of the driver wrote boards smaller than 8×8 wrongly, and seven records came back as errors. The recording's own loss notice showed it. The driver was fixed and the recording made again.
- **The harness manual and one of its self-tests follow the pin.** That self-test asserted that ftxi and Chess in 5D disagree on check, and failed once they no longer did. It now asserts that they agree.
- **Not yet done:** the second half of ruling 2, the commit and nightly tiers again on items A to C. That is the next step, and its own report.

## The procedure, step by step

| Step (`CONTRIBUTING.md`, "Changing a pin") | What was done |
| --- | --- |
| 1. Edit the lock entry | `commit`, `tree`, `ref` and `committed` in `oracles/oracles.lock.toml` |
| 2. `sync`, then `git add` the link | Done with `python oracles/harness.py sync`. The index pins `e6cf824` |
| 3. `verify`; read the licence | `pins: ok`. The licence file's hash is unchanged, so there was no new text to read |
| 4. Manifest and `corpus --unlisted` | `corpus: ok`: every listed ftxi record has the same blob at the new pin. 795 records are new and unlisted (below) |
| 5. `build` | `build --oracle ftxi --oracle notation`, with g++ 16.2.0, CMake 4.4.2 and Ninja 1.13.2. The driver needed porting first (below) |
| 6. Check again every entry bound to the old pin | 161 entries. 10 still occur and were re-pinned; 151 no longer occur and were removed (below) |
| 7. `golden --write --repeat 2 --jobs 8`, and read the diff | Run for `ftxi` and `ftxi:naive`, the two sources the pin and the driver feed, on a machine doing nothing else. Recorded twice, because the first recording showed a driver fault (below) |
| 8. The engine's baseline | Not recorded again. No corpus record changed, and both tiers compare 41 records with the baseline of 17 October and find 0 declined |
| 9. `provenance --write`; read the lock file's notes again | Done. The three notes kept from before were checked against the new checkout and still hold. The first note was rewritten for the new pin, and one was added about the new records |
| 10. Record it in the gate report | This file |

## What moved in the recorded answers

Compared with the files this replaces, field by field for every position of every record:

| What | `ftxi` | `ftxi:naive` |
| --- | --- | --- |
| Check verdict, check → no check | 131 positions of 14 records | 86 positions of 13 records |
| Action count | `notation/1.2`: 52 → 54 at position 15, 53 → 55 at position 17 | The same two |
| `cwmtt/test1` (Standard - Half Reflected) | unsupported at the set-up → 9 positions, then action 9 refused | The same |
| Positions recorded | 1,032 → 1,041 | 897 → 906 |
| Anything else | Nothing | Nothing |

"Nothing else" means: no side to move, terminal verdict, board or marker fingerprint, timeline count or board count changed at any position. No outcome changed on the other 40 records. None of the nine `ftxi:naive` runs cut at their budget was cut earlier, and no value became unknown or unstable.

**The check verdicts.** No verdict went the other way. ftxi's commit `f66c4ee` (15 October 2026, "check semantics revisited") changed which boards its check test passes. Its documentation at the pin now says: "Checkmate and stalemate are distinguished by simulating a pass only on active boards at the present. Inactive boards and boards ahead of the present are not advanced for this check" (`docs/usage/tools.md`). That is ruling R1's reading. v0.4.10 passed every board the side to move could play.

The positions, by record (`ftxi`, then `ftxi:naive`):

| Record | Positions moved |
| --- | --- |
| `cwmtt/niceAndrey` | 37, 29 |
| `ftxi/niceAndrey` | 37, 29 |
| `cwmtt/wide` | 15, 0 |
| `cwmtt/many2` | 14, 2 |
| `cwmtt/manyChecks` | 5, 5 |
| `cwmtt/silly` | 5, 5 |
| `cwmtt/exiledKings` | 4, 3 |
| `cwmtt/100_timelines` | 3, 2 |
| `cwmtt/NP`, `cwmtt/NP0`, `cwmtt/standard`, `ftxi/softmate`, `notation/Bg2` | 2, 2 each |
| `notation/game-2` | 1, 1 |

Where `ftxi:naive` shows fewer, its run is cut before the later positions.

**The action counts.** ftxi's commit `18df77d` (14 October 2026) gives an unmoved pawn or brawn a two-timeline step in its forward direction for both colours. v0.4.10 lacked Black's. cwmtt already counted 54 at position 15.

**What each tier requires** rose with the nine new positions, and fell nowhere:

| Tier | Source | Required values, before → now |
| --- | --- | --- |
| commit, nightly | `ftxi` | 8,256 → 8,328 |
| nightly | `ftxi:naive` | 6,556 → 6,708 |
| release (not run, spec §3.5) | `ftxi:naive` | 6,780 → 7,004 |

**Lines of `oracles/DISAGREEMENTS.md`:**

- **Gone:** `ftxi` against Chess in 5D on check at 92 positions, and `ftxi:naive` at 55. Both are now 0. Also the one count on which each ftxi search differed from each cwmtt search.
- **New:** the Half Reflected set-up on `cwmtt/test1`, where both ftxi searches differ from cwmtt, cwmtt's exhaustive search, Chess in 5D and the notation parser.

## The divergence register

`oracles/known-divergences.toml` had 167 entries, 161 of them bound to ftxi's old pin. It has 18 now.

**How the 161 were checked.** A script looked up, for each entry, what the golden file recorded at the new pin says at that record, kind and index, using the harness's own `value_at`, which is what `verify` holds an entry to.

- **10 still occur:** ftxi gives the same answer as before. Their `pin` lines, and nothing else in them, were rewritten: KD-0044, 0046, 0047, 0049, 0052 to 0056 and 0170.
- **151 no longer occur.** They were left untouched until the gate had named them.

**How the 151 were retired.** With those entries still in place, both tiers were run. The commit tier reported 94 of them as `divergence-stale` ("no longer occurs ... remove it, or find out why"): the `ftxi` entries. The nightly tier, which also runs `ftxi:naive`, reported all 151. A second script removed exactly the entries a report named, and only after checking each one again: bound to the old pin, and the golden file now recording the entry's `engine_value` from the oracle. No other byte of the file moved in that step.

**Why they stopped occurring:**

| Entries | What they recorded | What changed in ftxi |
| --- | --- | --- |
| 147: 92 `ftxi`, 55 `ftxi:naive` | A check verdict that R1 rules wrong | `f66c4ee` |
| KD-0040 to KD-0043 | Black's missing two-timeline double step (R4) | `18df77d` |

**The telltale rule is not in play.** The register says never to remove an entry because the engine has come to agree with an oracle's recorded error. Here the oracle moved. The engine's answers are the ones the entries already recorded as `engine_value`, the baseline shows 0 records declined, and no engine code is in this change.

**The final runs are the confirmation.** With the 151 gone, both tiers pass. An entry removed while its disagreement still occurred would show as an unexplained divergence and fail the gate.

**The two new entries, KD-0171 (`ftxi`) and KD-0172 (`ftxi:naive`).** Kind `placement`, at position 0 of `cwmtt/test1`.

- **What ftxi does.** It sets up Standard - Half Reflected as `rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBKQBNR`. Its own documentation says so: "Standard board with White's king on d1 and queen on e1" (`docs/branched-5dpgn.md`). It then refuses action 9 of the record, `5. (0T5)Qd1>>(0T3)f3~`, because it finds no queen on d1.
- **What settles it.** A placement entry must cite the notation specification:
  - Its `Board` header names one of the game's variants.
  - Its own example game on this board (Shad Amethyst v PseudoAbstractMeta, 22 January 2021) can only be played with Black's king on d8 and queen on e8. `4... Qh5` after `1... e5 2... f6` is a queen from e8. `10. (1T6)Be8+ / (1T6)Kxe8` has e8 empty beside Black's king. `16. (0T11)Qd1>(2T11)f3` is White's queen from d1.
  - The rulebook gives the same board: §8, V3, `rnbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR`, at medium confidence.
  - Run to check: the engine plays all 45 actions of that example game, and ftxi at v0.5.2 refuses its `4... Qh5`. ftxi was given the first four moves only, because its reader stops on the full text for a reason that has nothing to do with the set-up.
- **Other evidence.** cwmtt, Chess in 5D and the notation parser hold the engine's boards and play all 20 actions of the record. The record's later moves take Black's king from d8 and Black's queen from e8.
- **What it costs.** Nothing against before. At v0.4.10 ftxi read none of this record. Now nothing of ftxi's on it is compared with the engine. The ruleset is gated in the nightly tier by cwmtt and Chess in 5D, as it was.
- **What it means for gen 1.** Half Reflected is one of the four gen-1 variants (spec §0). Until ftxi changes its set-up, ftxi checks nothing on it. Its Defended Pawn set-up matches the rulebook's, by its documentation; no corpus record uses that variant yet.

The file's header now says it has changed since the copy, and a paragraph of its preamble records the retirement and the two new entries. The retired ids are not reused.

## The driver

`oracles/drivers/ftxi/ftxi_driver.cpp` did not compile against v0.5.2. It was ported in the same change, as the ruling allows.

| What changed in ftxi | What the driver does now |
| --- | --- |
| Its replay tool now tests an action itself: each move applied, the present tested for advancement, the action submitted, then the mover's royals tested for exposure | The driver follows the tool and adds no test of its own. At v0.4.10 the tool submitted without the last test, and the driver applied ftxi's `find_checks()` before submitting |
| Refusals are a typed validation error | Each kind maps to the harness's reason: no matching move, ambiguous move, illegal move, cannot submit. Any other kind goes on as an error, not a verdict |
| A turn is a class, not a pair | Read through its accessors |
| A board is written at the size it is asked for | Asked for at the record's board size |
| A game is built from the parsed record by a named constructor, and a parse error takes a kind and a place | Followed |

The adapted part of the driver follows `tools/position_tools.cpp` at v0.5.2 and stays under ftxi's licence notice in the file (G0's ruling 2). The gate's section "ftxi driver vs stock 5dtools" passes in both tiers.

**The fault in my first port.** I left the board writer's call as it was. At v0.4.10 it took no size; at v0.5.2 its size defaults to 8×8, and squares outside a smaller board are written as walls. On the seven records with smaller boards the adapter refused the driver's state line, and ftxi came back as `error: malformed_output` with no positions.

- **How it showed.** The first recording ended with the harness's notice that it no longer held 7 things: ftxi's mate or stalemate verdicts on `cwmtt/ctp1`, `local/checkmate`, `local/stalemate` and `local/stalemate-with-a-past`, and `ftxi:naive`'s on the three local records. Reading the diff showed the seven records with their outcomes gone.
- **The fix.** One call, which now passes the record's board size.
- **The second recording** is the one in this change. It loses nothing against the committed files (the comparison is described above). Its own notice has one line, because it compares with the files it replaces, which were the first recording's: `ftxi:naive` on `cwmtt/ctp1` is cut at its budget at position 16 again, as in the committed file.

## The manual and its self-test

`python oracles/harness.py selftest` failed one test of 687 after the recording: `test_every_check_disagreement_is_at_an_optional_timeline`, in `oracles/tests/test_repository.py`. CI's `gate` job runs `selftest` with ftxi built, so it would have failed there too.

- **What the test was for.** The manual (`oracles/README.md`) explained the check disagreements between ftxi and Chess in 5D by timelines the side to move may play but need not. The test held the manual to that, "when a pin or the corpus moves". It asserted that at least one disagreement exists, with the message "no check disagreement is left: update oracles/README.md".
- **What the pin did.** No disagreement is left: 92 before, 0 now, at the 857 positions where both give a verdict about the same boards.
- **The manual.** Three passages changed, and only those the pin made false:
  - what "accepted" means for ftxi, now that its own tools test royal exposure;
  - the check disagreement, as history, with what ftxi changed;
  - the upstream drafts for the entries that retired.
- **The test.** Replaced by `test_ftxi_and_chessin5d_give_the_same_check_verdicts`. It asserts three things:
  - the two give a verdict about the same boards at more than 500 positions;
  - some of those verdicts are check;
  - they disagree at none.

  Run against the old recordings, its condition fails with the 92 disagreements. It stops comparing a record where the two hold different boards, which is `cwmtt/test1` from its first position.
- **Is that weakening a check?** I don't think so. The old test's subject is gone, and the new one fails if any disagreement comes back. But it is a copied test rewritten in the same change that would otherwise fail it, so it is ruling 5 below.

Both files' headers and `PROVENANCE.md` record the change.

## Reading ftxi

To port the driver and to find out why answers moved, I read ftxi's commit notes between the two tags, its documents at the new pin, the declarations the driver calls and its replay tool. All of that stays under `oracles/`, in a driver that already adapts ftxi's code under its licence. Nothing read went into the engine, its tests or its tools, and this change contains no file outside `oracles/` apart from `PROVENANCE.md` and this report. No idea was taken for the engine.

## The checks, and what they printed

Run on the finished change, on Windows with g++ 16.2.0 and Python 3.12. `verify`, both tiers and `selftest` were run once more after the last edit, to the manual and its test, and the table gives those runs.

| Check | Result |
| --- | --- |
| `cmake --build build/engine` | `ninja: no work to do.` |
| `run_tests.py`, `t5d-tests` | 366 passed, 0 failed, 3 skipped of 369; 29 suites as recorded |
| `run_tests.py`, `fractal-tests` | 8 passed, 0 failed; 1 suite as recorded |
| `tools/check_provenance.py` | 221 copied files; 201 unchanged, 20 changed as recorded, 0 failing |
| `tools/check_docs.py` | 109 decisions (68 committed, 41 provisional), 34 arms, 13 refusals, 9 predictions; 0 problems |
| `harness.py verify` | All eight sections ok. The 17 standing findings about the release tier's required sets are the same 17 as before; `ftxi:naive`'s share there rose from 82.1% to 84.1% |
| `harness.py selftest` | 687 tests, OK, 5 skipped. Before the manual and its test were changed: 1 failure, the same 5 skipped |
| `harness.py gate --tier commit` | Passed. Oracle layer ok; `ftxi` 8,328 of 8,328 required values and 41 of 41 outcomes; notation 3,012 of 3,012; 41 records against the baseline, 0 declined |
| `harness.py gate --tier nightly` | Passed. Oracle layer ok; every source reproduced all its required values (`ftxi` 8,328, `ftxi:naive` 6,708, cwmtt 6,272, `cwmtt:naive` 4,755, Chess in 5D 6,461, notation 3,012); 0 declined; two rulesets gated |

The two runs that named the stale entries came before these, at 19:23 and 19:32. They failed, as they should have, on three things only: the 151 entries (reported as bound to the old pin, as contradicting the golden files, and as stale), the provenance inventory not yet rewritten, and the Half Reflected set-up not yet entered.

**Not checked here: the Linux build.** CI's `gate` job builds ftxi on Ubuntu 24.04 with the runner's g++, and I have only built v0.5.2 with g++ 16.2 on Windows. v0.5.2 asks for C++23 and CMake 3.20, as v0.4.10 did, and the standard headers it adds are old ones. The first CI run on the pull request is the test.

## Records the corpus doesn't list

`corpus --unlisted` shows 795 new records in ftxi's checkout, under `test/pgn/online`. None was added: a corpus change is its own change.

- **What they are.** 775 Standard - Turn Zero, 15 Standard - Defended Pawn, 3 Standard - Two Timelines and 2 custom positions.
- **Where they come from.** 739 are t0database's games, converted by ftxi from the same `Data.csv` Fractal holds (the same SHA-256), under t0database's MIT licence. 56 are from a second collection whose README names its collector and states no licence.
- **Why it matters.** The commit tier gates no ruleset, and no gen-1 variant but Half Reflected is gated at all (finding 3 of `G1a-rules-review.md`; spec §3.5's open item). These are games of the variants gen 1 plays.

## Proposed doc changes

For Fergus to apply or not; I have not edited either file.

1. **Spec §3.5, "ftxi's pin".** "moves from v0.4.10 to v0.5.2" becomes "moved on 28 November 2026", with what it did: 151 entries retired, two added for ftxi's Half Reflected set-up, and the record in this file.
2. **Spec §14.2, step 3.** "Next, ftxi's pin moves to v0.5.2 and both tiers run again on those items" becomes "ftxi's pin is at v0.5.2; next, both tiers run again on those items".
3. **The register's open items.** "The ftxi oracle pin moves to v0.5.2" becomes done, and the item on phase 1, part a drops the pin bump.
4. **Spec §3.5 or §13.4, a new line.** ftxi at v0.5.2 checks nothing on Standard - Half Reflected, a gen-1 variant, because of its set-up.

## Rulings needed

1. **The two new entries.** KD-0171 and KD-0172 rest on the rulebook's Half Reflected row, which is at medium confidence, and on the notation specification's example game.
   - **Recommended:** keep them. Three oracles and two games played on that board agree with the rulebook, and ftxi's set-up can play neither game.
2. **Reporting it to ftxi.** The entries say "unreported". ftxi documents its set-up, so this is a question to its author with the sources above, and it is yours to file.
   - **Recommended:** file it. A fix upstream would give gen 1 a second oracle on Half Reflected at the commit tier.
   - The two questions drafted in Temporal-5D for ftxi's author, on the check test and on the double step, have nothing left to ask: ftxi made both changes itself.
3. **The 795 unlisted records.**
   - **Recommended:** add the 739 t0database games in a change of their own, after the tests owed, starting in the nightly tier. Leave the 56 out until their licence is known. Recording them across five oracles will take hours, so the size of the first batch is a cost you may want to set.
4. **The four proposed doc changes** above.
5. **The rewritten self-test.** `test_every_check_disagreement_is_at_an_optional_timeline` is replaced, not edited to pass: its claim is now false, and its own message asks for the manual to change.
   - **Recommended:** accept the replacement. The alternative is to delete the test, which would leave the manual's statement about check unguarded.
