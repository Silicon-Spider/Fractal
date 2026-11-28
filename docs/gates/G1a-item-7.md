# G1a: item 7, the turn generator's move buffers

Item 7 of the review checklist (spec §3.3): move lists in buffers reserved once and reused. This is its first step, the one G0's review called 7a, done on 28 November 2026. It is the fourth change to copied code. The measurement is entry 4 of `docs/MEASUREMENTS.md`.

## Summary

- **The change is about twenty lines of `engine/src/hypercuboid.cpp`.** The generator keeps its move buffers for its life and reserves them once.
- **Allocations, on t0database turn starts under 50 boards:** construction 36.88 → 18.24, and a node that builds a generator and takes eight turns 318.16 → 286.36. The targets are 18 and 51.
- **No answer changed.** Both pins of `tests/test_legality.cpp` hold, both suites pass, legality by second routes agrees on all 22,761 turn starts, and both gate tiers pass.
- **A new check in CI holds the counts:** `fractal-legality --ceilings` fails a generator that allocates more than 19 at construction or 290 for the eight-turn node. Run on the engine before the change, it fails.
- **The times are not settled.** The machine was a third busy with other projects' jobs when they were taken. Construction reads 3% to 13% faster. The runs are to be repeated on a quiet machine.
- **A profile of every allocation in the generator was made first.** It says where items 6b to 6d should aim, that 6d as worded has nothing left to act on, and that about 70 allocations would remain in the eight-turn node after every remedy it lists, against the target of 51.

## Where the allocations were

Before changing anything, every allocation in the three generator measures was attributed to its call site. A subagent did it in a separate worktree, with a throwaway copy of the bench tool that records a short call stack in its `operator new` and resolves it to source lines. Nothing from that worktree is in this change. Its rows add up to the tool's totals. Its figures for the three construction sites changed here put the saving at about 19 allocations, and 18.6 was measured.

Under 50 boards, per position, before the change:

| Where | build | K turns | Paid |
| --- | ---: | ---: | --- |
| The candidate move list, a local that grows | 7.3 | 7.3 | Once per generator |
| The move table, growing | 7.4 | 7.4 | Once per generator |
| The axes' values, growing | 9.2 | 9.2 | Once per generator |
| The generator's other tables (axes, heads, counts, scores, the full box) | 11.0 | 11.0 | Once per generator |
| The generator object and its copy of the turn start | 2.0 | 2.0 | Once per generator |
| The pseudo-legality test's scratch list, growing at first use | | 5.4 | Once per generator |
| The abstract check's tables | | 8.5 | Once per generator |
| Small vectors rebuilt at every step: regions, the point, `seen`, `played`, the plan, the sequence, `done`, the subtraction | | 168 | Per step, point judged or point realised |
| The successors: boards and timelines made by `apply` | | 30.8 | Per move applied |
| State copies in realisation | | 12.0 | Per point realised |
| Recording an action: the successor copied, map nodes, vectors growing | | 39.8 | Per action recorded |
| The turn handed back: its moves and its successor | | 16.7 | Per turn emitted |

## What changed

Four edits, all in `engine/src/hypercuboid.cpp`:

1. **The candidate list is the generator's one move buffer.** Construction listed each axis's candidates in a local list. It now lists them in the member the pseudo-legality test already used as scratch at realisation, and reserves it once at 256 moves. Construction ends before realisation begins, so the two uses never overlap.
2. **The move table is reserved from the list** before an axis's moves are added. A move adds at most two entries: itself, and for a jump its J6 twin.
3. **Each axis's values are reserved the same way,** by the same bound.
4. **The sequence a point's realisation plays is a member,** cleared for each point, in the ordered and the free-order realisations. A point is realised to its end before the next begins, and the visit copies the sequence when it records an action.

One mean position has 71.5 candidate moves on 1.25 playable boards, so 256 covers most positions in one allocation; a position with more grows the buffer, as before. The largest in t0database has at least 626.

**What the generator returns is unchanged,** and so is the work it does: the same nodes and the same points judged in every bucket, before and after.

**The memory it reports changes.** `TurnGenerator::memory_bytes()` counts the tables by capacity, and reserved capacity is larger than grown capacity was. Nothing reads that figure yet. The budget that can stop a generator counts the queue and the recorded states, not the tables, so no stop moves.

**Left for item 6,** as G0 proposed: the other small vectors of a step, and working memory kept per thread across generators.

## Results

Heap allocations per position; the full tables with times are in `docs/MEASUREMENTS.md` entry 4.

| | As copied | After 6a | After item 7 | Target |
| --- | ---: | ---: | ---: | ---: |
| Under 50 boards: build | 181.0 | 36.88 | 18.24 | 18 |
| Under 50 boards: eight-turn node | 518.8 | 318.16 | 286.36 | 51 |
| 50-99 boards: build | 314.9 | 47.60 | 25.61 | |
| 50-99 boards: eight-turn node | 1,076.7 | 735.98 | 697.94 | |
| 100-199 boards: build | 584.7 | 65.63 | 38.95 | |
| 100-199 boards: eight-turn node | 1,740.7 | 1,111.30 | 1,064.16 | |

**Times.** Two runs of each binary, alternating, on a machine that was 33% busy with jobs of other projects. Construction is faster in every pair, by 3% to 13%. The eight-turn node under 50 boards is 2% to 4% faster. At 50 boards and more it reads 0.5% to 2% slower in all four pairs, which is inside the spread between runs but has the same sign each time. I don't know yet whether that is real. The runs are to be repeated on a quiet machine and appended to entry 4.

## The ceilings

G0 asked for an exact allocation-count test with this step, and the spec asks for the target of item 6 to be asserted by test. Both now have a place.

- **`fractal-legality --ceilings`** measures as usual and then fails if, under 50 boards, construction makes more than 19 allocations or the eight-turn node more than 290. The counts are the program's own, so they don't depend on the machine.
- **The ceilings are a ratchet.** They sit just above today's figures (18.24 and 286.36), with room for a standard library that grows a vector differently, and are lowered on purpose as items land, toward 18 and 51. They are constants in `tools/bench/fractal_legality.cpp`, with their history beside them.
- **CI runs it** in both engine jobs, after legality by second routes, on the same t0database turn starts.
- **It fails when it should.** With `hypercuboid.cpp` as it was before the change, it reports 36.88 and 318.16 and exits 1.

## The checks, and what they printed

On Windows, g++ 16.2.0, warnings as errors.

| Check | Result |
| --- | --- |
| `cmake --build build/engine` | Built, no warnings |
| `run_tests.py`, `t5d-tests` | 366 passed, 0 failed, 3 skipped; 29 suites as recorded |
| `run_tests.py`, `fractal-tests` | 23 passed, 0 failed; 3 suites as recorded. Both pins unmoved |
| `tools/check_provenance.py` | 221 copied files; 199 unchanged, 22 changed as recorded, 0 failing |
| `tools/check_docs.py` | 0 problems |
| `fractal-legality --verify` | 22,761 positions; 435,884 scans compared; 172,420 turns replayed; 0 differences |
| `fractal-legality --ceilings` | build 18.24 of 19, K turns 286.36 of 290: ok |
| `harness.py verify` | All eight sections ok |
| `harness.py gate --tier commit` | Passed; 41 records against the baseline, 0 declined |
| `harness.py gate --tier nightly` | Passed; 0 declined; the same two rulesets gated |

The gate's calibration probe took 1.5 to 1.6 times its recorded time, which is the load on the machine; both tiers allow for that and passed.

**Not checked here:** the ceilings on Linux and with clang. The counts should be the same there, since all three builds use the same standard library, but CI is the test.

`PROVENANCE.md` has the file's new hash and a sentence on the change in its row, and the file's header says it.

## What the profile says about items 6b to 6d

These are findings for the next steps, not changes.

1. **Most of what is left is small vectors rebuilt at every step:** about 160 of the 286 under 50 boards, and about 540 of 698 at 50 to 99 boards. Keeping that working memory in the generator, or per thread, is item 6b, and it is where nearly all of the gain is.
2. **Item 6d has nothing to reuse.** It says "boards built at construction reused when a turn is realised". Since 6a, construction builds no board: it reads each candidate's changes from `move_edits`. Every board the generator makes is made when a point is realised, and belongs to a successor.
3. **States are copied three or four times for each turn emitted:** at the start of a realisation, at each creation tried, into the record, and into the turn handed back. That is 28 allocations in the eight-turn node. Moving the realised state into the record, in place of copying it, removes about 11 without touching the copied `Multiverse`.
4. **About 70 would remain** in the eight-turn node after every remedy the profile lists: the successors' own boards and timelines (31), each action's moves held twice (15), map nodes (9) and the generator object. The target is 51. Ruling 1 of 27 November says what happens then: the report on 6b to 6d brings the measured figure and proposes a target.
5. **Construction can go well under 18.** With its tables kept per thread it would make about 2 allocations.

## Rulings needed

1. **Item 6d.** As worded it has nothing to act on (finding 2).
   - **Proposed:** 6d becomes "the realised state moved into the record, not copied", which is what is left of the same idea: don't build twice what was built once.
   - **Recommended:** yes.
2. **The times.** They were taken on a busy machine.
   - **Proposed:** this change stands on its counts, its pins and its checks, and the times are repeated on a quiet machine before item 6b is measured, since 6b needs a clean baseline anyway.
   - **Recommended:** yes. If the repeat shows the eight-turn node slower at 50 boards and more, I report it before going on.
