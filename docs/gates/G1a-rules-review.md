# G1a, part 1: the copied rules engine, reviewed for an alpha-beta search

On 17 November 2026 Fergus asked for the rules engine and everything else imported from Temporal-5D to be reviewed again, now as parts of an alpha-beta engine: whether each piece belongs in Fractal, and whether it can be improved for it. The example he gave was legality checking, which runs far more often here than under Temporal-5D's search. He also asked for a look at what Kairos and ftxi's engine do, for Temporal-5D's rule against copying code and rules like it to be written into this project, and for Temporal-5D's checks to be carried over where they help.

This report records what was found, what was changed and what is left. It is a part report: **G1a has not passed.** The work was done on branch `phase-1a-rules` and committed on 22 November as a series, one commit for each change to copied code. Pull request #3 merged it into `fractal-spec-revision` on 23 November, after its CI run passed on all four jobs; pull request #2 takes that branch into `main`.

## Summary

- **Everything in spec §3.1 fits Fractal and stays.** Four things are dead weight rather than faults: `certificate` (can't build), `attacks` (nothing calls it), the 21 test files for modules that were never copied, and `legal`'s per-move tiers, which only tests and tools call.
- **Legality cost too much in two places, and both are now several times cheaper.** Check status takes 0.15 to 0.3 µs where it took 1.6 µs on human positions under 50 boards, and the submission test's scan 0.3 to 0.6 µs where it took 2.5; the ranges are the spread between runs. Building a turn generator takes 5.2 µs where it took 15.1, with 37 heap allocations where it made 181. A node that builds a generator and pulls eight turns costs 28 µs where it cost 52. No answer changed: the generator's output, its node counts and the check verdicts are pinned, and the pins did not move.
- **The safety net of §3.5 and §3.6 now exists.** The five oracle checkouts are submodules, `verify` passes all eight sections for the first time in Fractal, and the commit tier runs: the engine as copied and the changed engine both come through its 41 records with no finding and none declined. The nightly tier passes on the code as committed, and gates two rulesets, neither of them Standard or Turn Zero. `test_search` is recovered, and the test inventory is pinned.
- **The rule against copying is enforced where a machine can enforce it and written down where it can't.** The copy check now compares every file with the five pinned sources, Kairos among them. `AGENTS.md` carries Temporal-5D's rules across, and `CLAUDE.md` has the short form.
- **Six rulings are needed.** The first ratifies a departure from the committed order of work: the largest change here, the faster royal-capture scan, was not on §3.3's checklist.

## G1a's criteria (spec §14)

| Criterion | Result | Evidence |
| --- | --- | --- |
| NM1 holds after every item | **Met for the three items done**, with one blemish | After each: 366 copied tests and Fractal's own pass, and the commit tier's engine section is clean. The first item's gate run failed its oracle layer on a boundary finding in a tool I had just edited, not in the engine; see *The gate* |
| The commit tier in CI | **Met** | CI's `gate` job ran on `2102b73` on 23 November: `verify` with every section ok and nothing excused, then the commit tier, 41 records and 0 declined. See *The CI run* |
| The nightly tier on the desktop | **Met**, on the code as committed | Run on 17 November after the three items and after each of two late edits to the first, and again on 22 November on the commits. Reports under `build/reports/g1a/`, which git ignores |
| Every pin unchanged or re-recorded on purpose | **Met** | The detector hash `0x5366691BCAF734AB` and the two new pins are unchanged across all three items |
| The allocation target of §3.3 item 6 (90% fewer per node) | **Not met** | Construction makes 80% fewer. A node that pulls eight turns makes 39% fewer. See *What is left* |
| Speed recorded after each item | **Met** | `docs/MEASUREMENTS.md`, entry 3 |

Not done from §14.2 part a: tests covering the copied-code cases of `test_turn_state` and `test_capi_game`; checklist items 7, 6b to 6d, and 1.

## Does each imported piece belong in an alpha-beta engine?

Temporal-5D searches with MCTS and a transformer. A node there costs a network evaluation, so its rules engine was written to be exact and inspectable, and its speed mattered little. Fractal's node is meant to cost tens of microseconds in all. The review below asks of each piece what an alpha-beta search asks of it and what it costs.

### Where a node's legality time went

Measured on every distinct turn start of the t0database games (22,761 positions), one thread, with `fractal-legality`, a tool written for this review. Figures are nanoseconds per position on the engine as copied, under 50 boards, where 85% of human turn starts lie.

| Step | Cost as copied | What it did |
| --- | ---: | --- |
| Pseudo-legal moves | 1,577 | Direction-table walks, 72 moves a position |
| Check status (`in_check`) | 1,581 | Built a multiverse with the passed boards (4.4 heap allocations), then walked every line of every enemy piece |
| Generator construction | 15,108 | Applied every non-branching candidate to a copy of the position, only to read which squares it changes: 181 heap allocations |
| First turn, after construction | 8,290 | One point judged and recorded: 66 allocations |
| Each further turn | 4,311 | 41 allocations; of this time, about 2,500 ns is the scan that proves no royal can be captured |

Two things stand out. The royal-capture scan was more than half the cost of each further turn, and it runs once per turn tested, once per node for check status and once per replayed hint. And construction cost as much as three and a half further turns before a node had seen one.

### The pieces

| Piece | Fits? | What an alpha-beta search needs of it | Verdict |
| --- | --- | --- | --- |
| types, geometry, error | Yes | Nothing new | Keep as copied |
| board | Yes | Bitboards per colour and kind beside the square codes: enough for set-wise scans without any new field | Keep as copied |
| timeline, multiverse | Yes, at a price | A position is a value and there is no unmake. A copy costs 40 ns and one allocation; a copy with one move applied costs 200 ns and three. Kairos, which makes and unmakes moves on one mutable state, pays no allocation per move | Keep (NB3 is committed and the net's per-board cache rests on it). Pools are measured below |
| zobrist | Yes | Incremental, and the search keys on it | Keep as copied |
| rules, variants, state | Yes | `apply` checks a move's structure against the state every time; the checks are comparisons and cost little | Changed: `apply` builds its boards from `move_edits` |
| move, movegen | Yes | 1.5 µs a position is now a third of construction. The relation scan of §7.3 will be a pass of the same kind for both colours at every evaluation | Keep as copied for now. The set-wise method used for the scan below is the candidate for §3.3 item 4's "dedicated scan" |
| attacks | No use | Incremental royal-attack maps. Spec §3.3 item 5 already leaves them unused; G0 found them slower than a plain scan as copied, and the plain scan is now several times faster | Keep as copied, unused. A candidate for removal (ruling 5) |
| check | Yes, but slow | Asked at every node and for every turn tested | **Changed: the set scan** |
| search, legal | Yes | The detectors are the fallback of §5.4 and the proof of NM5. `legal`'s tiers are refused in the search (NF13) | Changed in one detail (`search.cpp`); `legal` kept as copied |
| hypercuboid, turngen | Yes, with waste | Built for a search that pulled about 27 turns a node and kept every successor. An alpha-beta cut node wants the first turn or two cheaply | **Changed: construction.** The cost per turn is the main work left |
| fen, pgn | Yes | There is still no 5DPGN writer; the protocol and the game records need one (NE1, ND1) | Keep as copied; the writer is new work in part c |
| fingerprint, md5 | Yes | The gate adapter uses them | Keep as copied |
| certificate | No | Can't build without the uncopied prover | Dead file. Ruling 5 |
| gate | Yes | The adapter the harness runs; it calls `in_check`, the counts and the detector, so it exercises all three changes here | Keep as copied |
| capi, records | Yes, untested | The trainer's bridge in Phase 2. No test that builds calls the game functions or `t5d_layout` | Keep; tests still owed (§3.6) |
| The 64 test files | 37 build | `test_search` is recovered. Of the 23 test sources that still can't build, two test copied code through the uncopied expansion (`test_turn_state`, `test_capi_game`) and are owed new tests; 21 test modules that will never exist here | Keep the 37 and those two. Ruling 5 on the 21 |
| The harness | Yes | See *Temporal-5D's checks* | Now running |
| The rulebook, t0database | Yes | Unchanged | Keep as copied |

## What changed in the copied code

Three items, one at a time. Each kept the engine's answers exactly, by test.

### Item A: a set scan for royal captures (`engine/src/check.cpp`)

**Before.** `royal_capture_exists` asked the question the way the move generator would: for each of the attacker's playable boards, each piece, each of its lines (80 for a queen), one square at a time, looking up a board at every step. I call this the line scan.

**After.** A second routine, the set scan, asks from the other end, a board at a time. Within a board it starts at each royal: the leapers' patterns from that square, and the first piece along each of the eight lines. Between boards it uses the fact that a move's board displacement is one of eight directions repeated n times, and that after n steps a slider is n squares along each spatial axis it moves on. So for the n-th board along a direction, the only squares a capture of that board's royal can start from are the royal's own square, the four at distance n along its file and rank, and the four at distance n along its diagonals; which kinds may stand there follows from how many axes the move uses. Those squares come from tables. The sliders that keep their square walk along a direction together as one set of bits, losing the squares that the boards they cross occupy. `in_check` no longer builds the passed multiverse: it reads the passed boards in place.

**What stayed.** The line scan is untouched and still answers every entry point that wants a witness (`find_royal_capture`, `for_each_royal_capture`, `royal_capture_from`), which is what the hypercuboid search uses to cut regions. It also answers both questions under the non-default readings of Q18 and Q19 (sliders crossing void, past selves not blocking), which the set scan does not read.

**How the two are held equal.**

- The copied `test_check.cpp` already compares `royal_capture_exists` with the move generator's own royal captures on 1,500 random multiverses under seven rulesets, and check status with a reference on random play. Both pass unchanged.
- `tests/test_legality.cpp` adds 16,200 comparisons on random multiverses up to 8×8 with timelines up to 26 boards long, under nine option sets. Then it takes each capture the line scan lists (27,770 of them) and leaves the capturing piece alone on its side's playable boards: the set scan must still find a capture, and must find none once that piece is removed too. One right answer can't hide a missing case that way. Every piece kind is the capturing piece at least 287 times.
- `fractal-legality --verify` compares the two on every t0database turn start, its passed multiverse and its generated successors: 604,020 comparisons, no difference.
- The check verdicts over 243 sampled positions and 1,942 random actions are pinned by hash, recorded before the change.

**One fault of my own, found on a second reading.** The first version read its table of passed timelines for every multiverse, and the table holds 256. A multiverse with more timelines would have read past its end. Nothing in the tests or the recorded games comes near that many, so every check above passed with the fault in. The table is now read only once a timeline has been passed, a pass is made only at 256 timelines or fewer, and a test scans a multiverse of 300. The gate runs and the measurements were repeated on the corrected code.

### Item B: construction without applying every candidate (`state.cpp`, `hypercuboid.cpp`, new `move_edits.hpp`)

**Before.** To learn which squares each candidate changes, the generator copied the position and applied the candidate to the copy, once per non-branching move. This is §3.3 item 6's finding.

**After.** The squares a move changes on the boards it plays are computed by one function, `detail::move_edits`. `GameState::apply` now builds its new boards from it, and the generator reads the same edits directly. One function serves both, so the two can't disagree about what a move does, and every test of `apply` (the special-move tests, the replays, the gate) is now also a test of what the generator reads. A node of the budget is still spent per candidate, so node counts are unchanged.

### Item C: two savings per turn (`search.cpp`, `search_internal.hpp`, `hypercuboid.cpp`)

- The generator kept each new successor's action as canonical text, built with string formatting, in case a second action reached the same state and the two had to be ordered. The text is now written only when that happens.
- A successor was copied once more than needed on its way into the set of positions; a function object was allocated per point judged; a small vector was allocated per board per point even when no branch was involved.

This item is small: 2,899 ns a turn became 2,642. It is listed apart because it changes the memory estimate a generator reports by a few bytes a turn (a move's text is now counted at 24 bytes instead of its length). Nothing reads that estimate except the 2 GiB ceiling.

### Results

One thread of a Ryzen 9 9900X, g++ 16.2 at -O2, every distinct t0database turn start; `docs/MEASUREMENTS.md` entry 3 has the conditions and the other buckets. Nanoseconds per position, and heap allocations in brackets.

| Under 50 boards (19,410 positions) | As copied | After A | After B | After C |
| --- | ---: | ---: | ---: | ---: |
| Check status | 1,581 (4.4) | 157 | 141 | 140 |
| Submission scan on a successor | 2,523 | 382 | 500 | 344 |
| Generator construction | 15,108 (181) | 15,159 (181) | 5,177 (37) | 5,169 (37) |
| Construction and the first turn | 23,398 (247) | 21,022 (247) | 11,031 (103) | 10,756 (95) |
| Each further turn | 4,311 (41) | 2,882 (41) | 2,899 (41) | 2,642 (34) |
| Construction and eight turns | 51,975 (519) | 40,126 (519) | 30,252 (375) | 28,268 (318) |

- **The table gives each binary's second run, and the two scan rows are noisy.** Each pass of them lasts a few milliseconds. Over every run of the changed engines that day, check status under 50 boards read between 140 and 284 ns and the submission scan between 258 and 610 ns, and the differences between items in those rows are not real. The generator rows repeat within 4%.
- **Item A is the scan; item B is construction.** Check status is 6 to 11 times cheaper under 50 boards, depending on the run, and 9 and 12 times in the two larger buckets, which repeat better. Construction is 2.9 times cheaper under 50 boards and 3.3 and 3.7 times in the larger buckets, where a position has more candidates.
- **By G0's own measure** (`fractal-bench`, a scorer that puts every move above leaving, medians over positions) the first legal turn takes 6.7 / 18.4 / 36.0 µs in the three buckets where entry 1 has 16.6 / 40.0 / 73.5, and each further turn 1.72 / 2.70 / 3.73 µs where it has 2.83 / 4.73 / 6.88. On the synthetic positions of 100 to 600 boards a further turn is 2.0 to 3.1 times cheaper.
- **Wide positions are now limited by the generator's bookkeeping, not by legality.** At 100 to 199 boards a further turn still costs 14 µs and 131 allocations under the banded scorer, and the scan is 0.4 µs of that.
- **The proof of mate got cheaper too:** the 5-timeline near-mate case takes 59 µs where it took 75, at the same 184 nodes.

## The safety net (spec §3.5, §3.6; G0's ruling 1)

- **Submodules.** The five pinned repositories are submodules under `oracles/third_party/` at the commits `oracles/oracles.lock.toml` locks. `verify` passes all eight sections; at G0 three of them could only fail.
- **The copy check is live.** It compares every tracked and untracked file outside `oracles/` with the five sources' code at their pins. It passes, on the new code too.
- **The commit tier runs.** On the engine as copied and after each item it reproduces ftxi's 8,256 recorded values and the notation parser's 3,012, and compares the engine with them on 41 records: no finding, and none declined against the baseline Temporal-5D recorded on 17 October. It takes under a minute on the desktop. The harness's self-test now runs 687 tests with 5 skipped, where 49 or 50 were skipped at G0 for want of an oracle.
- **The nightly tier.** Run after the three items, and again after the late fix to item A: ftxi and cwmtt with their naive searches, Chess in 5D and the notation parser all reproduce their recordings, and the engine differential passes with none declined. Seven minutes on the desktop. The GHC it needs came from the archive Temporal-5D's harness had already downloaded; the harness checked it against the lock file's checksum before unpacking it. Kairos is not built, as §3.5 rules, so its four live tests are the self-test's skips, with one that needs symlinks.
- **`test_search` is recovered** by the trim §3.6 names: the include of `t5d/expand.hpp` and the half of one test that calls the expansion, 34 lines. Its 22 tests pass, the three that hold "never a mate on a budget stop" among them. The copied suite is now 366 passing tests in 29 files, with the same 3 skipped.
- **Pins, recorded before any copied code changed.** `tests/test_legality.cpp` hashes the generator's output on 243 positions from nine variants under two scorers (10,454 turns with their moves, successor keys, scores and the search's own counts) and the check verdicts on the same positions and 1,942 random actions. The positions come from the copied synthetic player and the scores are integers, and the hashes are the same on every CI target: g++ 13.3 and clang 18.1 on Linux, and g++ 16.2 on Windows (*The CI run*).
- **Fractal's own tests.** Eight, in a second runner beside the copied one: the two pins; the scans against each other; generator exhaustion against the depth-first detector; a generator stopped mid-run against the full run; and the three tests of item A (the scans on random multiverses, check status read in place, and a multiverse of 300 timelines).
- **The test inventory is pinned.** `tools/run_tests.py --inventory` fails a run whose passed and skipped counts per file differ from `tools/inventory/*.tsv`, so a test can't drop out of the build unnoticed.
- **CI.** The two existing jobs stay free of submodules and gain the inventories, Fractal's tests and `fractal-legality --verify`. They no longer treat `verify`'s checkout findings as expected output; the new `gate` job runs `verify` with nothing excused, then the commit tier. I ran the two kinds of job's harness steps on the desktop in both states: with the checkouts and builds present, and with the submodules declared but neither checked out nor built, which is what a job without submodules sees. In the second state the self-test passes with 50 skipped and the restricted gate's engine section is clean, as at G0.

### The gate

| Run | Engine | Oracle layer | Engine against the oracles |
| --- | --- | --- | --- |
| Commit tier | As copied | ok | Clean: 41 records, 0 declined |
| Commit tier | After item A | **Failed**: one `references-harness` finding against `tools/check_provenance.py`, which I had edited to name the harness directory | Clean, 0 declined |
| Commit tier | After items A and B | ok | Clean, 0 declined |
| Commit tier | After A, B and C | ok | Clean, 0 declined |
| Nightly tier | After A, B and C | ok | Passed, 0 declined; two rulesets gated |
| Commit and nightly tiers | The same, with item A's late fix | ok | Clean and passed, 0 declined; the same two rulesets gated |
| Commit and nightly tiers | The tree as it stood on 17 November: that, with one variable renamed in the set scan | ok | Clean and passed, 0 declined; the same two rulesets gated |
| Commit tier | 22 November, each commit checked out alone: the safety net on the engine as copied (`badc73c`), item A (`ea9419e`), item B (`629e898`), item C (`2604d61`) | ok, four times | Clean each time: 41 records, 0 declined |
| Nightly tier | 22 November, the code as committed | ok | Passed, 0 declined; the same two rulesets gated |

The rows of 17 November ran on the working tree before it was committed; the last of them used the adapter `build/engine/t5d-gate.exe` with SHA-256 `0b6e5ec3…c4f0d0`. The failed run is the boundary check doing its job on the first day it could. The tool now reads the copied directories from `PROVENANCE.md` and names none of them.

**The commits, checked one at a time.** The work was committed five days after it was done, and the series' intermediate commits are not the states gated on the 17th: each already has item A's late fix and the renamed variable, and the tests of the set scan sit in the safety net's commit, where they compare the line scan with itself. So on 22 November the safety net's commit and each item's commit was checked out alone and rebuilt, and each passed everything `AGENTS.md` lists: 366 copied tests with 3 skipped and Fractal's 8, both to their inventories; the provenance and docs checks; `verify`, all eight sections; the commit tier; and `fractal-legality --verify`, with no difference on 22,761 positions. The nightly tier then passed on the code as committed. That code is what the runs of the 17th gated: the hashes in `PROVENANCE.md` did not change, and the build of the 17th was still current when the commits were made. The adapter's own hash is not the same from one build to the next, since the linker stamps the link time into it, so it identifies a binary and not the code.

**The commit tier gates no ruleset.** It is a smoke test, as its own last lines say: it fails an engine on a finding or on a record it used to read and now declines. The nightly tier gates two rulesets: Standard - Half Reflected (one record, 21 positions) and Standard - Reversed Royalty (one record, 122 positions). Standard is not gated, because the engine can't read one of its records (`ftxi/softmate`). Standard - Turn Zero is not gated, because only one oracle follows one of its records as far as the engine goes. Standard - Defended Pawn has no record at all.

### The CI run

**The CI run behind this report:** [45133969227](https://github.com/Silicon-Spider/Fractal/actions/runs/45133969227), the pull request's run on `2102b73`, all four jobs green; the push run of the same commit, [45133963518](https://github.com/Silicon-Spider/Fractal/actions/runs/45133963518), is green too. It was the first time the new code was built anywhere but the desktop. Later commits change only this report.

- **The jobs without submodules:** linux with g++ 13.3, linux with clang 18.1, and windows with MSYS2 g++ 16.2. Each gave 366 copied tests passing with 3 skipped and Fractal's 8 passing, both to their inventories, so the two pins hold on all three targets. `fractal-legality --verify` found no difference on 22,761 positions. The provenance check found 221 copied files, 10 changed as recorded, 1 of Fractal's own and none failing, and the docs check was clean. The self-test passed with 49 skipped, the restricted gate found nothing against the engine, and the near-mate smoke test took 184 nodes.
- **The `gate` job,** with g++ 13.3: `verify` with every section ok and nothing excused, the self-test with 687 tests and 20 skipped, and the commit tier, oracle layer ok, 41 records, 0 declined. The desktop skips only 5 in the self-test because the nightly tier's oracles are built there too; CI builds only the commit tier's two.
- **One warning, not an error:** GitHub runs `actions/checkout@v4` and `actions/setup-python@v5` on Node.js 24 because they target Node.js 20, which it has deprecated.

## Temporal-5D's checks

An inventory of every mechanical check and standing rule in Temporal-5D was made at the pin and in its live tree. What Fractal does with each:

| Check | In Temporal-5D | In Fractal now |
| --- | --- | --- |
| Boundary: nothing outside `oracles/` reaches into it or names an oracle | `verify`, in CI | Copied at G0; all rules now run with checkouts present |
| Copy check: six lines of logic, or one long line, matching a pinned source | `verify`, in CI | **Live for the first time.** At G0 it was skipped five times over and CI expected that |
| Pins, corpus, known divergences, rulebook, baseline, golden files | `verify` | **All pass for the first time**; at G0 pins and corpus could only fail |
| Commit tier on every push | Its own workflow | **Carried over** as CI's `gate` job, green on its first run |
| Nightly tier | A scheduled workflow | On the desktop, before a change to copied code merges and at each gate report, as spec §3.5 rules. Not scheduled |
| Release tier | By hand or on a tag | Not used (§3.5) |
| Engine built with no submodules | Its own workflow | Kept: two jobs |
| The build writes nothing under the harness | A CI step | Kept, in all three jobs |
| Two detectors agree; pinned detector hash; scan equals generator; generator equals brute force | Engine tests | Copied and passing. **Added:** pins of the generator's output and of the check verdicts |
| The 22 tests of legal actions and mate, H37 among them | `test_search` | **Recovered** |
| Warnings as errors | CI | Kept, on three toolchains where Temporal-5D has two |
| A changelog entry with every change | A written rule, unchecked | Not carried over: the gate reports, `PROVENANCE.md` and `MEASUREMENTS.md` already hold what it would. Ruling 6 |
| The spec is never edited by an agent | A written rule | Already Fractal's |
| Review owners for `oracles/` and `.github/` | `CODEOWNERS` | **Carried over.** It binds only with branch protection, which is Fergus's to turn on |
| Force-push and GitHub writes denied to agents | `.claude/settings.json` | **Not carried over:** a permission setting is Fergus's to make. Ruling 6 |
| A code signature compiled into tools that record data | CMake | Not yet: nothing records data until Phase 2. Worth having for self-play shards and `NETS.md` |
| Allocation counting and fault injection in tests | `test_alloc` | The counting is in `fractal-legality`. Fault injection is owed with the search (see *Findings*) |
| Sanitiser and fuzz builds | None | None yet; NM17 asks for sanitisers at G1b |

**Improved on the way across:**

- **Nothing in a copied directory without a row.** `tools/check_provenance.py` used to check only the files `PROVENANCE.md` lists, so a file pasted in beside them was invisible to it. It now fails on any file, tracked or not, in a directory that holds copied files unless the file is a copied one or is listed under "Fractal's own files". It also fails when a file's header and `PROVENANCE.md` disagree about whether the file changed.
- **The link check finds new documents itself.** `tools/check_docs.py` checked a fixed list of seven files; it now checks every Markdown file at the root, in `docs/` and in `docs/gates/`.
- **Legality by second routes over the whole corpus.** `fractal-legality --verify` replays every generated turn through the state machine, compares the two scans, and holds generator exhaustion to the detector, on all 22,761 turn starts in 2.3 s. It runs in CI.
- **CI no longer converts a failure into expected output.** The step that required exactly 47 findings is gone.

**What no check covers.** The copy check knows only the five pinned sources. Nothing mechanical compares Fractal with Stockfish, Lc0, KataGo, dlshogi, YaneuraOu or the parts of Temporal-5D that §3.2 leaves out, and no check anywhere can see a port. Those rest on the written rule. A pin for a further source is possible in the harness's design (a source that is never built, as Kairos is), but it means editing copied harness files and re-rendering the inventory, and I have not tried it. Ruling 4.

## The rule against copying

Fractal already refused other engines' code (NF11, NF12). What Temporal-5D's rules add, and what is now written into `AGENTS.md` and, in short, `CLAUDE.md`:

- **Porting is copying.** Retyping another engine's logic, or pasting it with the names changed, is the same offence. The check can't see it.
- **How to take an idea.** Read, close the source, write the idea down with where it came from, and code from the note and the rulebook. A subagent that reads another engine reports in prose.
- **The boundary** in full, and that the engine never runs the harness or an oracle.
- **The rulebook decides** a disagreement, and an oracle is only called wrong with a citation.
- **Never make a check pass by weakening it**, with the list of ways.
- **Keep the slow path** when a faster one takes over, and hold the two equal case by case.
- **What not to touch** without being asked, and the commands to run before reporting a change done.

`CONTRIBUTING.md` restates the procedures the copied harness refers to and Fractal lacked: a disagreement, the engine's baseline, a pin bump, a new record.

### Ideas taken in this work, and from where

The new rule asks for this list.

| Idea | Where it is used elsewhere | In Fractal | Written from |
| --- | --- | --- | --- |
| Answer "can a royal be captured" a board at a time from bitboards, starting at the royals, instead of generating the attacker's moves | Both ftxi and Kairos scan this way (`check_position` in each) | The set scan | The rulebook's movement rules as the line scan states them. I worked out the squares-at-distance-n form before the reading passes came back, and did not open either engine's source at any point: two subagents read them and reported in prose |
| Sliders that keep their square advance together as one set of bits | Both | The walk along each board direction | The same |
| Read the passed boards in place instead of building a position | Both ("overlay") | `in_check` | The same |
| Don't apply a candidate to learn what it changes | ftxi stores a result board per option; Kairos likewise | `move_edits` | G0's finding for §3.3 item 6, which predates this review |

## What Kairos and ftxi do

Read on 17 November 2026 at Kairos `194e426` and ftxi `7e8bbca`, by two reading passes that returned prose with file and line references. Every speed and Elo figure below is that project's own claim; none was reproduced.

**Both**

- Scan for royal captures a board at a time and stop at the first. ftxi reports the change as 4.2 to 9.4 times faster per check query; Fractal's set scan measures 6 to 12 times here.
- Never build a position to test a candidate turn. Each option carries the board it would leave, made once, and a candidate is tested against the base position plus one borrowed board per moved timeline.
- Decide same-board safety once, when a board's options are listed: an option that leaves an own royal attacked on its own board is never offered, so the per-turn test looks only between boards.
- Turn a failed test into a removed box, as the copied generator does.

**Kairos**, the one alpha-beta engine among them

- Makes and unmakes moves on one mutable state, with no heap allocation per applied action, and pools every per-node buffer.
- Tries the remembered turn and two killers by replaying each whole and running one scan, before it builds anything (+20 Elo by its test). Spec §6.1 step 5 already does this for the hint.
- Walks a lone playable board's moves directly, without the box machinery.
- Remembers up to 32 failed regions per node, and falls back to a depth-first pass when best-first goes 256 steps without a turn (its figure: a stalled node from 318 ms to 2 ms). Spec §5.4's detector fallback covers the same failure.
- Caches check status by position hash and hands a child's status down from its parent.
- Tried generating only check evasions and took it out: −12 Elo on many-timeline positions.

**ftxi**

- After a legal turn, enumerates its siblings that differ only in physical moves, rerunning only the check test (its figure: 40 to 44% off full enumeration).
- Reports 15 to 17 µs on average to find one action on typical positions. Fractal's construction and first turn now take 10.8 µs on human positions under 50 boards, and took 23.4.
- Its own profile still puts the allocator at about 19% of search time.

**Neither** runs its tests in CI.

**What Fractal should take next, in order of expected gain here**

1. **Working memory that outlives one generator.** A node's first turn costs about twice a later one, mostly in containers growing from empty. Kairos pools these per ply. The pool experiment below estimates what allocation alone is worth.
2. **Same-board safety at construction.** It removes whole classes of failing candidates before they are judged. Its value here is in positions in check, 14% of human turn starts, where most candidates fail.
3. **A lone-board path.** 85% of human turn starts under 50 boards have one playable board.
4. **Check status in the TT entry**, beside the static eval.

Not for Fractal: make and unmake. NB3 commits to positions as values, and the net's per-board cache depends on boards never changing.

## Findings

1. **None of the Temporal-5D commit ids Fractal cites is on a branch upstream any more.** Temporal-5D's history was rewritten after the copy to replace an e-mail address, which gave every commit a new id and left its tree alone. Fractal cites three: the pin `c8a9c49`, now `03eca7a`; `2cb107d`, the commit the spec first named, now `d8a54d3`; and `2740e66`, the commit Temporal-5D's M9 measured, now `b5c1176`. Each was matched to a commit on upstream's `main` with the same tree (the pin's is `ce35e1e5…`), so every copied byte matches, and `PROVENANCE.md` now carries the three pairs with their trees. The 182 headers, the provenance check, the spec, the register and G0's reports still name the old ids. GitHub still answers for an old id asked for directly, but a new clone doesn't have them. Ruling 3.
2. **The commit tier's rules oracle is behind its own upstream on rules that matter.** ftxi is pinned at v0.4.10, of 15 September. Three later commits, all in its v0.5.2, bear on the rules: `18df77d` (14 October, "correct movegen for pawns (again)"), `f66c4ee` (15 October, "check semantics revisited") and `52469a2` (20 October, "revised functional boundary for can_submit()"). By the reading pass's account, the first fixes pawn and brawn generation for Black (at the pin, Black's unmoved two-timeline pawn step and Black's extra brawn captures never fire), the second makes its check test pass only the active boards in the present, which is ruling R1's reading, and the third makes a checked submission require royal safety. I confirmed the commits, their dates and their subjects, not the code. G0's rulings already listed the second as open. A pin bump is a reviewed change that re-records the golden files on a quiet machine. Ruling 2.
3. **No gen-1 variant is gated by the commit tier, and the nightly tier gates only Half Reflected among them.** Growing the corpus from t0database is still an open item of §3.5, and it is the one that would make the gate bite on the variant Fractal plays.
4. **A proof of "no legal turn" can outrun any test budget.** Two of the 243 sampled positions, wide ones from pseudo-legal play, were undecided by both the generator and the detector at 200,000 nodes, and the first version of the test, with no budget, ran for more than ten minutes. Spec §5.4 is right to make a stopped generator prove nothing; the generator budget of NT5 needs setting with positions like these in the set.
5. **A generator that throws.** Temporal-5D's build log records two things since the pin that bear on NT5. Its issue 535: once `next()` throws, the generator may only be destroyed, and a later call may report an exhaustion that never happened. Spec §5.4 already says this. Its issue 603: g++ 16.2, the desktop's compiler, breaks `std::deque`'s exception safety under its default `-fassume-sane-operators-new-delete`. I have not reproduced either. Part c's search should be tested with allocation failures injected.
6. **Upstream has moved without touching the rules engine.** On 22 November Temporal-5D's `main` is eighteen commits past the pin, and none changes a file under `engine/src`, `engine/include` or `engine/tools`. Twenty of the 221 copied files differ there. Three are tests: `test_zobrist.cpp` gains tests of a copied module, which could be merged by hand, and the other two test modules that were never copied. Seventeen are harness files: by the commit subjects and the lock file's new `lineage` fields, the harness now counts ftxi and Kairos as one lineage for the two-oracle bar and has a parse mode for Kairos waiting to be enabled. Fractal's copy predates both, and neither matters while Kairos is never built here. The five oracle pins are the same in both. Three branches not yet merged there change copied engine files (`legal.{hpp,cpp}`, `certificate.cpp`, `turngen.hpp`); by their commit subjects they add deadlines and exception-safety notes, not rules fixes. I read the subjects, not the code.
7. **What a pool would buy.** `fractal-legality --pool` recycles freed blocks of up to 512 bytes through free lists, in the measuring program only. It takes 22% off each further turn, 25% off the first and 10% off construction. That is an estimate of what §3.3 item 1's pooled allocation is worth before non-atomic handles, made without changing the engine.
8. **Where a turn's remaining cost is.** After the three items a further turn costs 2,642 ns and 34 allocations: about 300 ns for the scan, about 200 for the successor's board and timeline, and the rest in the box bookkeeping, which walks every option of every board several times per turn, and in small containers.

## What is left in G1a

- New tests for the copied-code cases of `test_turn_state` and `test_capi_game` (§3.6).
- §3.3 item 7 (reused move lists) and items 6b to 6d (pooled working memory, prebuilt boards). The allocation target is not met.
- §3.3 item 1 (per-thread roots, pools).
- `main` needs branch protection with the four CI jobs as required checks.
- **The speed figures come from one machine and one compiler,** g++ 16.2 on the desktop. CI shows the code builds and answers the same on two more compilers, not how fast it runs there.
- A pin of allocation counts per turn as a test. The counts are measured by the tool, not asserted.
- Nothing is in `main` yet. The work reaches it with pull request #2, one commit for each item as §3.3 asks changes to land.

## Proposed doc changes

`docs/SPEC.md` and `docs/DECISIONS.md` are not edited. If the rulings go as recommended, they would say:

1. **§3.1, the check row.** "Check status (R1) and submission legality, by a set scan from the royals under the default rules, held equal to the copied line scan, which answers under the alternatives and wherever a witness is wanted."
2. **§3.3.** A new item for the royal-capture scan, done. Item 6: its first step done, by `move_edits` rather than stack builders; the allocation count after it.
3. **§3.4 and §13.1.** The pin as a tree id beside the commit id.
4. **§3.5.** Carried out: the submodules, the `gate` job, what the nightly tier gates. Remove "Until the checkouts are in place CI compares the engine with an oracle at no position."
5. **§3.6.** `test_search` recovered; the pins and the inventory in place; name `tests/` and `fractal-tests`.
6. **§5.2, the measurements table.** The new figures beside G0's.
7. **§14.2 step 1 and `docs/BUILD_PROMPT.md`.** Done; add `AGENTS.md` and `CONTRIBUTING.md` to *Read first*. Its *Environment* names a bare `build --bootstrap` for the nightly tier's oracles, which would also build Kairos: name the two oracles instead, as `AGENTS.md` does.
8. **NF12.** Add "its tests" and "copied or ported".
9. **A new register row** for "a faster routine keeps the copied one as its reference, held equal by test".

## Rulings needed

1. **The order of work.** NR3 commits to §3.3's checklist in G0's order: the safety net, then items 7, 6 and 1. I did the safety net first, then the royal-capture scan, which is on no list, then item 6's first step ahead of item 7. I took your request to speed up legality checking as the instruction to do so. Nothing is merged, and the scan is one commit of its own (`ea9419e`). **Recommended:** ratify the scan as a checklist item and the order as done. The alternative is to back that commit out until the checklist is revised; the tests, pins and tool stand either way.
2. **The ftxi pin.** Move it to a release that has the three fixes of finding 2, by the procedure in `CONTRIBUTING.md`, with the golden files re-recorded on a quiet machine. **Recommended:** yes, as its own change before G1a closes, since every later change to copied code is judged against this oracle.
3. **The rewritten commit ids.** (a) Leave the headers and documents naming `c8a9c49`, `2cb107d` and `2740e66`, and let `PROVENANCE.md` carry each one's new id and tree, as it now does. (b) Rewrite the 182 headers, the provenance check and the documents to the new ids; the spec and the register would change at your request only. **Recommended:** (a), unless the old commits are to be removed from GitHub altogether: their ids would then resolve nowhere, and (b) is the better record. The tree id identifies the bytes either way. (b) touches every copied file for no change in content. Two of those files are harness adapters, and the harness ties its golden files to the adapters' code, so (b) may also mean recording the golden files again; I have not tested that.
4. **Engines the copy check can't see.** (a) Leave Stockfish, Lc0 and the rest to the written rule. (b) Pin them as never-built sources in the harness, which edits copied harness files and adds several large submodules. (c) A Fractal-owned check under `tools/` with its own list of sources. **Recommended:** (a) for now. The rules engine has little in common with any of them that a six-line match could catch, and the search and net code that could resemble them doesn't exist yet. Revisit at G1c.
5. **Dead files.** Remove `certificate.{hpp,cpp}` and the 21 test files for uncopied modules, with the four headers only they include, or keep them as the record of what was copied. `attacks` builds and passes its tests but nothing calls it. **Recommended:** keep all of them until G1 closes, then remove what is still unused in one recorded change.
6. **Two of Temporal-5D's guards that are yours to set.** A `.claude/settings.json` that denies agents force-pushes and GitHub writes, and a changelog rule. **Recommended:** the first, since `CLAUDE.md` already forbids force-pushing `main` and a setting enforces it; not the second.

**Ruled on 27 November 2026.** The rulings, and the revision of the spec and the register they made, are in `G1a-rulings.md`.
