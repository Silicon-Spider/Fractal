# G1a: the tests owed

Spec §3.6 lists tests G1a still owes. This report records them as they land. Two parts landed on 28 November 2026: the copied-code cases of two copied test files that cannot be built, and the two tests of the set scan that G1a ruling 1 made a condition of closing G1a. Two more groups are still owed; the last sections say what and why.

No copied code changed, and no pin moved.

## Summary

- **Two new test files in `tests/`:** `test_capi_game.cpp` (11 tests) and `test_turn_state.cpp` (2 tests in a default run, 3 with `--long`).
- **Two new tests in `tests/test_legality.cpp`,** the ones ruling 1 asks for: check status read in place equals the line scan at 65 to 256 timelines, and with Q8 or Q20 off its default. Each fails with its fault planted, and no other test does.
- **`fractal-tests` goes from 8 tests to 23,** and its inventory is recorded again by its own command.
- **The new tests fail when a fault is planted.** Fourteen faults were put into the copied code one at a time, and every one fails at least one new test. One failed nothing at first, which showed a gap, and a test was written for it.
- **Nothing wrong was found in the copied code.**

## What the two copied files tested, and what covers it now

`engine/tests/test_capi_game.cpp` and `engine/tests/test_turn_state.cpp` are copied and cannot be built: both include the expansion, which was not copied (spec §3.2), and the first also calls the three entry points G0 removed. Nothing else that is built calls `t5d_game_*` or `t5d_layout`. The new tests are written from the C API's header and the rulebook, not adapted from the copied files' text.

**The C API's game calls** (12 copied cases):

| Copied case | Covered by | Left out |
| --- | --- | --- |
| A record replays and expands as the engine | `capi_game_replays_a_record_as_the_engine_does` | The expansion's arrays |
| Applies moves and submits | `capi_game_applies_moves_and_submits` | Applying a move by its index in an expansion |
| From 5DFEN and from a variant | `capi_game_from_fen_and_from_a_variant` | The expansion's move count |
| Errors | `capi_game_refuses_what_it_cannot_read_or_play` | The expansion's option errors |
| Pin and check relation bits | Nothing | All of it: the expansion's relations |
| Square labels | Nothing | All of it: the labels were not copied |
| The promotion asked for | `capi_game_applies_the_promotion_asked_for` | The by-index half |
| Submissions are legal by default | `capi_game_submissions_are_legal_by_default` | Nothing |
| Who acts, and whether submitting is legal | The summary's `obligations_met` and `may_submit`, in the case above and the one below | The expansion's three fields |
| The count of moves in the action | `capi_game_counts_the_moves_in_its_action` | The expansion's counts |
| The struct layouts | `capi_layout_reports_the_structs_as_compiled` | The two removed structs, now asserted to have no layout |
| A ruleset's encoding round-trips | `rule_options_decode_inverts_encode` | Nothing |

Two cases are new:

- **`capi_game_sets_up_the_gen1_variants_as_the_rulebook_gives_them`** writes out the starting boards of the four gen-1 variants from the rulebook (§8, V3) and holds the engine to them. ftxi's set-up of Half Reflected, found at the pin change, is why this one is written from the rulebook and not from the engine's own table.
- **`capi_game_counts_the_active_timelines`** plays two branches by White and none by Black, and reads the result off the rules: the second timeline is inactive (R2), and the present ignores it (T4).

**The turn state** (3 copied cases):

| Copied case | Covered by |
| --- | --- |
| The counts equal a recount under every ruleset | `turn_state_equals_a_recount_under_every_ruleset` |
| The same, longer | `long_turn_state_equals_a_recount_under_every_ruleset` |
| The spec's cases by construction | `turn_state_read_off_the_board` |

The copied file read the counts through the expansion. The new one reads what `GameState` itself keeps (the present, the mandatory and playable timelines, whether the obligations are met, whether a move may be made) and the same through a `t5d_game` handle playing the same moves.

## How the new tests know the answer

Every expected value is one of four things:

- **The engine's own answer by a second route.** A C++ `GameState` plays the same moves as the handle, and the two are held equal by fingerprint and field by field.
- **A status the header promises.**
- **A position small enough to read off the board,** with the rule that settles it named in the test.
- **A recount from the rulebook's definitions** (L5, T3 to T6, T8, and the readings Q4 to Q6 as the engine's rule options word them). The engine keeps the present and the mandatory count incrementally, as boards are added; the recount starts from nothing at every state.

Which timelines are active is taken from the engine in the recount. The copied suite tests activation, and `capi_game_counts_the_active_timelines` reads one case of it off the board.

**What the random play reaches,** in a default run: 14,413 states over 27 rulesets, 6 starting positions and 2 games each. 4,616 of them have an inactive timeline, 6,870 an optional board, and 144 have the obligations met with no move allowed. Of the moves, 4,453 play one mandatory board, 569 play two and 2,423 play none; 2,755 branch. The long run reaches 162,818 states in 12 seconds. The play is seeded with the copied generator, so the counts are the same on every target, and the test asserts that each kind was reached.

## The faults planted

Each fault was written into the copied code, `fractal-tests` was built and run, and the file was put back with `git checkout`. `tools/check_provenance.py` confirms the copied code is as it was.

| Fault | In | New tests that fail |
| --- | --- | --- |
| A submission leaves the count of moves as it was | `capi.cpp` | `capi_game_applies_moves_and_submits`, `capi_game_counts_the_moves_in_its_action`, `capi_game_counts_the_active_timelines`, and the random play |
| `may_submit` ignores royal safety | `capi.cpp` | `capi_game_submissions_are_legal_by_default` |
| `t5d_game_apply` ignores the promotion asked for | `capi.cpp` | `capi_game_applies_the_promotion_asked_for` |
| The summary's present is one too high | `capi.cpp` | Six of the C API's eleven, and the random play |
| `active_timelines` counts every timeline | `capi.cpp` | `capi_game_counts_the_active_timelines`, and the random play |
| The present counts inactive timelines | `state.cpp` | Both turn-state tests, and `capi_game_counts_the_active_timelines` |
| A head added in the present's column is not counted | `state.cpp` | The random play only |
| A head leaving the present's column is not counted | `state.cpp` | Both turn-state tests, and five of the C API's |
| The default reading counts every head of the mover's as mandatory | `state.cpp` | Both turn-state tests, and two of the C API's |
| `mandatory(p)` ignores the present | `state.cpp` | Both turn-state tests |
| `may_move` is always true | `state.hpp` | Both turn-state tests, and `capi_game_counts_the_moves_in_its_action` |

"The random play" is `turn_state_equals_a_recount_under_every_ruleset`.

- **The gap.** When the C API's tests were written, and before the turn-state file existed, the fifth fault failed nothing: no test reached a position with an inactive timeline. `capi_game_counts_the_active_timelines` was written for it, from the rules. The random play now reaches such positions too.
- **The seventh fault** fails only the random play. No hand-built position here has two boards arriving in the present's column in the order that shows it.
- **The tests of legality that were already here** (`tests/test_legality.cpp`) also fail on four of the five `state.cpp` faults, each time through a pin.

## The two tests of §3.3 item 8

Ruling 1 ratified the set scan on a condition: before G1a closes, two tests hold check status read in place equal to the line scan on the passed multiverse, and each is shown to fail with a named fault planted. Fergus said they would supply `fuzz_check.cpp` to adapt. It is not in the tree, and the spec states both tests and both faults in full, so they are written here from the spec. Both are in `tests/test_legality.cpp`, beside the tests of the set scan they extend.

In both, `in_check` reads the passed boards in place, and the reference is the copied line scan (`find_royal_capture`) on `passed_multiverse`, which builds the passed boards one by one.

**(i) `legality_check_status_read_in_place_at_65_to_256_timelines`.** The set scan marks passed timelines in a table of four 64-bit words, one bit for each timeline by its index.

- **By construction, 178 positions.** For 13 timeline counts from 65 to 256 and every index of 64 or more that each holds from a list of 12 (the word boundaries among them), two positions:
  - A white king on the timeline at that index, which is passed, and a black rook on the same square of the timeline beside it. The rook takes the king on the passed copy: check. The timeline whose bit a first-word-only read would return is not passed.
  - The same pieces, with the king's timeline ahead of the present and so not passed: no check. The timeline whose bit a first-word-only read would return is passed.
- **At random, 80 multiverses** of 65 to 256 timelines, with pieces only on timelines of index 64 and above. The test asserts that more than 1,000 passed timelines had such an index and that the verdict went both ways.

**(ii) `legality_check_status_read_in_place_with_q8_or_q20_off`.** By default every board is a destination. Under Q8's other reading a history board at turn zero is none, and under Q20's nor is a history board of an inactive timeline. So under either the scan must know which boards are history, and a pass changes that.

- **By construction, four positions under each of four readings** (the defaults, Q8 off, Q20 off, both), each verdict read off the board:
  - A passed copy at turn zero, attacked from the timeline beside it: check under every reading, because the copy is the playable board.
  - A king on a history board at turn zero, attacked from one turn later: check unless Q8 is off.
  - The same with a pass at turn zero beside it.
  - A king on a history board of an inactive timeline: check unless Q20 is off.
- **At random, 3,000 small multiverses,** a third under each of the three readings, some starting at turn zero and some with all but one created timeline inactive. The test asserts that more than 500 had a pass at turn zero, more than 500 an inactive timeline, and that at more than 20 the verdict differs from the default readings' on the same boards.

**The faults planted**, each in `engine/src/check.cpp`:

| Fault | Tests that fail, of the 23 |
| --- | --- |
| (i) The pass table is read from word 0 only | `legality_check_status_read_in_place_at_65_to_256_timelines` alone: 146 positions differ |
| (ii) The passed copy of a board is flagged as history | `legality_check_status_read_in_place_with_q8_or_q20_off` alone: 6 positions differ, the first constructed position among them |
| The pass table is written to word 0 only | `legality_check_status_read_in_place_at_65_to_256_timelines` alone |

With either named fault planted, the copied suite passes in full, in its default run and its long one (366 and 372 tests), and so do all of Fractal's other tests, long ones included. Only the new test fails. So before these two tests neither fault failed anything, which is what ruling 1 was about.

- **Q20 alone cannot show fault (ii).** Only active timelines are passed (R1), and Q20 is about inactive ones. So under Q20 off the passed copy's history flag is never read, and the fault shows under Q8 off, where a passed copy can stand at turn zero. The test covers both readings; the fault fails it through Q8.
- **The other half of the pass, the passed board itself becoming history, changes no verdict.** The opponent's pieces land only on boards where the opponent is to move, and the passed board has the mover to move. No test can be made to fail on it, and none here claims to.

## The inventory

`tools/inventory/fractal-tests.tsv` was recorded again with `tools/run_tests.py --write-inventory`. It was recorded twice, once for each part. It held one suite of 8 tests and now holds three suites of 23: the two new files, and `test_legality.cpp` at 10. No suite lost a test.

## The checks, and what they printed

On Windows, g++ 16.2.0, with warnings as errors.

| Check | Result |
| --- | --- |
| `cmake --build build/engine` | Built, no warnings |
| `run_tests.py`, `t5d-tests` | 366 passed, 0 failed, 3 skipped; 29 suites as recorded |
| `run_tests.py`, `fractal-tests` | 23 passed, 0 failed; 3 suites as recorded. With `--long`: 24 passed |
| `tools/check_provenance.py` | 221 copied files; 199 unchanged, 22 changed as recorded, 0 failing |
| `tools/check_docs.py` | 0 problems |
| `harness.py verify` | All eight sections ok |
| `harness.py gate --tier commit` | Passed; 41 records against the baseline, 0 declined |

**Not checked here:** the clang build and the Linux build of the two new files. CI builds both with warnings as errors.

## Still owed

1. **The allocation test** of §3.3 item 6: construction at 18 heap allocations or fewer, and the eight-turn node at 51 or fewer. The figures stand at 37 and 318 (MEASUREMENTS entry 3), so the test can only pass once items 6b to 6d have landed, and it lands with them.
2. **Part b's test** that relations equal an independent reading through movegen.

## Proposed doc changes

For Fergus to apply or not; I have not edited either file.

1. **Spec §3.3 item 8.** "Before G1a closes, two more tests hold ..." becomes a statement that the two tests are in `tests/test_legality.cpp`, each shown to fail with its fault planted.
2. **Spec §3.6.** "with the two tests that item still owes" goes, and the C API's game calls and the turn state are listed as covered by `tests/test_capi_game.cpp` and `tests/test_turn_state.cpp`.
3. **The register's open items** drop the two tests of §3.3 item 8 and the tests of §3.6 for the two copied files.

## Rulings needed

1. **The two tests of item 8 were written without `fuzz_check.cpp`.** Do they meet ruling 1's condition?
   - **Recommended:** yes, and compare them with `fuzz_check.cpp` when it is to hand. If it covers a case these don't, I add it.
2. **The three proposed doc changes** above.
