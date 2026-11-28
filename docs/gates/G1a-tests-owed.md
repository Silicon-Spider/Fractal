# G1a: the tests owed

Spec §3.6 lists tests G1a still owes. This report records them as they land. The first part, of 28 November 2026, is the copied-code cases of two copied test files that cannot be built. Two more groups are still owed; the last sections say what and why.

No copied code changed in this part, and no pin moved.

## Summary

- **Two new test files in `tests/`:** `test_capi_game.cpp` (11 tests) and `test_turn_state.cpp` (2 tests in a default run, 3 with `--long`).
- **`fractal-tests` goes from 8 tests to 21,** and its inventory is recorded again by its own command.
- **The new tests fail when a fault is planted.** Eleven faults were put into the copied code one at a time, and every one fails at least one new test. One failed nothing at first, which showed a gap, and a test was written for it.
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

## The inventory

`tools/inventory/fractal-tests.tsv` was recorded again with `tools/run_tests.py --write-inventory`. It held one suite of 8 tests and now holds three suites of 21: the two new files. No suite lost a test.

## The checks, and what they printed

On Windows, g++ 16.2.0, with warnings as errors.

| Check | Result |
| --- | --- |
| `cmake --build build/engine` | Built, no warnings |
| `run_tests.py`, `t5d-tests` | 366 passed, 0 failed, 3 skipped; 29 suites as recorded |
| `run_tests.py`, `fractal-tests` | 21 passed, 0 failed; 3 suites as recorded. With `--long`: 22 passed |
| `tools/check_provenance.py` | 221 copied files; 199 unchanged, 22 changed as recorded, 0 failing |
| `tools/check_docs.py` | 0 problems |
| `harness.py verify` | All eight sections ok |
| `harness.py gate --tier commit` | Passed; 41 records against the baseline, 0 declined |

**Not checked here:** the clang build and the Linux build of the two new files. CI builds both with warnings as errors.

## Still owed

1. **The two tests of spec §3.3 item 8** (G1a ruling 1): check status read in place equal to the line scan on the passed multiverse, (i) at 65 to 256 timelines and (ii) with Q8 or Q20 off its default, each shown to fail with its fault planted. Fergus said they would supply `fuzz_check.cpp` for these; it is not in the tree yet.
2. **The allocation test** of §3.3 item 6: construction at 18 heap allocations or fewer, and the eight-turn node at 51 or fewer. The figures stand at 37 and 318 (MEASUREMENTS entry 3), so the test can only pass once items 6b to 6d have landed, and it lands with them.
3. **Part b's test** that relations equal an independent reading through movegen.

## Rulings needed

1. **`fuzz_check.cpp`.** The two tests of item 8 and their planted faults are fully specified in the spec, so I can write them without the file. Which do you want?
   - **Recommended:** I write them now from §3.3 item 8, and you compare them with `fuzz_check.cpp` when it is to hand. They are a condition for closing G1a, and they guard code that is already in the tree.
   - The alternative is to wait for the file, and to start item 7 in the meantime.
