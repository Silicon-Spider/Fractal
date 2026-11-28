# Measurements

An append-only log of every speed and strength result, newest last. Each entry gives the machine, the commit, the build and the exact command, so anyone can re-run it (BUILD_PROMPT.md, *Records*).

Conventions:

- **Machine.** CPU model, cores and logical processors, RAM and OS, and whether the machine was otherwise quiet.
- **Build.** Compiler and version, build type and flags, as the tool prints them.
- **Commit.** The Fractal commit the binaries were built from.
- **Rates.** Medians over interleaved rounds, with the slowest and fastest round in brackets, unless an entry says otherwise. Percentiles are nearest rank.
- **Comparison.** Temporal-5D's figures quoted in the spec came from the same CPU model (Ryzen 9 9900X), so the Phase 0 entries compare against them directly.
- **Names.** The project was called Chronos until 15 November 2026, and this log now gives the current names throughout. At a commit from before the rename, the tools are `chronos-bench` and `chronos-relhist`, and the CMake option is `CHRONOS_WERROR`.

## 1. Phase 0 baselines: movegen, turn generation, the 5-timeline hypercuboid case (2026-11-15)

The copied rules engine, unchanged apart from the capi removal, measured before any §3.3 change (G0).

- **Machine:** AMD Ryzen 9 9900X 12-Core Processor (12 cores, 24 logical processors), 31.1 GiB (32 GB) RAM, Windows 11 Pro 10.0.26300.
  - The machine was quiet: 1.2% busy just before the run (PowerShell `Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 2 -MaxSamples 3`, averaged). The bench itself doesn't sample load during the run.
  - One thread, pinned to logical processor 2, priority above normal.
- **Build:** GNU g++ 16.2.0 (MSYS2 ucrt64), Release, `-O2 -DNDEBUG`, statically linked.
- **Commit:** Fractal `6364eb0` on `phase-0-copy`; engine copied from Temporal-5D `c8a9c49`.
- **Commands:**

  ```
  cmake -S . -B build/engine -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DFRACTAL_WERROR=ON
  cmake --build build/engine
  python -I data/t0database/to_5dpgn.py build/t0db
  build/engine/fractal-bench --records build/t0db --json build/g0/bench.json
  python -I tools/bench/turngen_table.py build/g0/bench.json
  ```

- **Defaults:**
  - movegen: a warm-up round, then 15 interleaved rounds, each with one 200 ms sample per class and bucket (8 samples);
  - synthetic positions: 16 games per class, 6 one-move children per base position;
  - turn generation: 3 passes, at most 1,000 turns per generator, a 2,000,000-node budget.

  Total run time 137 s.

### Pseudo-legal movegen, one thread

Each sample calls `generate_moves` on a stream of positions, cycled to the sample's deadline.

- **Synthetic classes.** Temporal-5D M9's definition: seeded random play from Standard to N boards with T active timelines (seed = 1 + 1,000,003 N + 10,007 T + 37 g). The stream is 6 one-move children of each of the 16 games' end positions.
- **Records rows.** Every distinct turn start of the 716 t0database records that replay to the end in parse mode (no royal-safety test at submission), by board count.

| Positions | Positions/s, median [slowest, fastest round] | Thread CPU ns per position (pooled) | Moves | Boards | Active timelines | Temporal-5D M9 (same CPU, `2740e66`) |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| N100 T6, synthetic | **157,707** [155,442, 159,630] | 6,319 | 448.5 | 104.1 | 6.35 | 171,804 |
| N160 T7, synthetic | 128,031 [126,642, 128,769] | 7,822 | 635.2 | 164.8 | 7.34 | 139,053 |
| N240 T8, synthetic | 100,813 [99,590, 101,339] | 9,933 | 817.8 | 246.3 | 8.16 | 102,263 |
| N400 T10, synthetic | 76,990 [74,894, 77,667] | 12,995 | 1,196.9 | 406.9 | 10.23 | 80,213 |
| N600 T12, synthetic | **57,279** [56,562, 57,664] | 17,577 | 1,622.0 | 606.2 | 12.19 | 58,002 |
| t0database, under 50 boards (19,410 positions) | 611,419 [577,093, 644,353] | 1,624 | 71.5 | 25.1 | 1.40 | - |
| t0database, 50-99 boards (3,162) | 398,444 [380,178, 402,087] | 2,515 | 130.3 | 63.6 | 2.70 | - |
| t0database, 100-199 boards (189) | 286,479 [277,764, 288,504] | 3,518 | 218.9 | 123.7 | 4.24 | - |

The synthetic rates are 1-8% below M9's. The seeds, the class definition and the timing loop are the same. Four things differ:

- the children chosen from each position;
- the engine commit (M9 measured `2740e66`; this copy is `c8a9c49`, whose movegen and state code changed in between);
- what shares a round (M9 interleaved its one-thread samples with multi-thread and expansion samples);
- pinning and priority.

The records rows time the positions themselves rather than their children, so they don't compare with M9's records. No t0database position reaches 200 boards.

### Turn generation, one thread

- **What runs.** The copied hypercuboid `TurnGenerator` (unit `Successors`) at each base position, pulled until it is done or has given 1,000 turns.
- **Scorer.** It has no chess knowledge. Each option scores a deterministic hash of the move, in [0.001, 1), and leaving a board scores 0.
- **Times.** Medians over the 3 passes per position, then over positions.
- **Columns.** "Up to 27 turns" times min(27, available) turns, until the generator is done if there are fewer; Temporal-5D's MCTS pulled about 27 turns per search node. "µs per turn, average to done" is (time until done or 1,000 turns) / turns. "µs per further turn" excludes the first: (done − first) / (turns − 1).
- **Positions with no legal turn.** The generator ends exhausted with none, which proves mate or stalemate. Most such t0database positions end their game. They are shown apart, because they cost a proof instead of turns.

| Positions | With a legal turn | First turn (construction + first `next()`), µs, median [p90] | Up to 27 turns, µs | µs per turn, average to done | µs per further turn | Turns/s (pooled) | No legal turn: count, median µs |
| --- | ---: | --- | ---: | ---: | ---: | ---: | --- |
| t0database, under 50 boards | 19,023 | **16.6** [52.1] | 91.5 | 3.10 | 2.83 | 167,434 | 387, 41.6 |
| t0database, 50-99 boards | 2,965 | 40.0 [125.9] | 174.9 | 5.13 | 4.73 | 139,620 | 197, 56.7 |
| t0database, 100-199 boards | 176 | 73.5 [354.2] | 309.5 | 7.13 | 6.88 | 120,081 | 13, 45.2 |
| N100 T6, synthetic | 14 | 703.7 [948.7] | 854.0 | 14.68 | 14.04 | 60,364 | 2, 508.0 |
| N160 T7, synthetic | 16 | 887.5 [1,882.3] | 1,265.2 | 19.20 | 18.40 | 34,159 | 0 |
| N240 T8, synthetic | 15 | 1,919.9 [3,083.5] | 3,096.5 | 27.48 | 25.99 | 34,388 | 1, 1,020.6 |
| N400 T10, synthetic | 12 | 3,938.8 [11,710.4] | 3,941.8 | 34.17 | 28.97 | 29,003 | 4, 3,608.8 |
| N600 T12, synthetic | 9 | 15,501.6 [101,041.7] | 15,504.9 | 54.80 | 37.39 | 13,199 | 7, 7,375.0 |

Notes:

- **Up to 27 turns.** Among positions with a legal turn, the time ran until the generator was done for 1,386 of 19,023, 418 of 2,965 and 10 of 176 in the three t0database buckets, which have fewer than 27 legal turns.
- **Synthetic rows.** They rest on 9-16 positions each, and random play is pseudo-legal, so it often ends without a legal turn. At N600 T12 the first turn takes between 3.9 ms and 101 ms.
- No generator was stopped by its node budget.

### The 5-timeline hypercuboid case

- **Position.** The near-mate family from `test_detectors.cpp` (`detectors_the_hypercuboid_stays_small_near_mate`): timelines -n..+n on 5x5 boards, all mandatory, a mate on timeline 0, and on every other board white rooks a1 and b1 and a black knight whose moves can't help. n = 2 is the 5-timeline case.
- **Measure.** `detect_mate`, median time per call. The hypercuboid figures come from thousands of calls. The naive search ran 3, 3 and 1 calls for n = 1, 2 and 3, so its n = 3 figure is a single run.

| n (timelines) | Hypercuboid | Naive | Temporal-5D (engine README) |
| --- | --- | --- | --- |
| 1 (3) | 126 nodes, 50.7 µs | 2,407 nodes, 0.75 ms | 126 against 2,407 nodes |
| **2 (5)** | **184 nodes, 75.4 µs** | **817,875 nodes, 0.589 s** | 184 nodes in under 1 ms, against 817,875 nodes in 0.6 s |
| 3 (7) | 248 nodes, 102.0 µs | stopped by the memory ceiling after 1,007,435 nodes, 0.705 s: unknown | 248 against 1,007,435 nodes and a stop |

All proven verdicts are checkmate, as the test expects. A `TurnGenerator` on the n = 2 position ends exhausted with no turn after 796 nodes, 464 µs.

## 2. Phase 0 outputs read again: turn width, relation families, live rows, Δt buckets, game lengths (2026-11-15)

Nothing was run again for this entry. It reads further figures out of the files Phase 0 wrote, for the spec's revision of 15 November 2026 (`docs/gates/G0-rulings.md`).

- **Inputs:** entry 1's `build/g0/bench.json`, and the outputs of G0's relation run on the same machine and build:

  ```
  build/engine/fractal-relhist --records build/t0db --games build/g0/start-games.txt --records-out build/g0/records.tsv --out build/g0/start.json
  build/engine/fractal-relhist --probe build/g0/probe.json
  ```

  `start-games.txt` lists the 507 start games of `data/splits/t0database-nd8.tsv`. The table step of entry 1 (`tools/bench/turngen_table.py`) first appears in commit `2346376`, not `6364eb0`.
- **Command:**

  ```
  python -I tools/phase0_figures.py --bench build/g0/bench.json --hist build/g0/start.json --probe build/g0/probe.json --records build/g0/records.tsv --split data/splits/t0database-nd8.tsv
  ```

### Turn starts with at least 1,000 legal turns

Entry 1's turn-generation run stopped each generator at 1,000 turns. These are the t0database turn starts that reached that cap.

| Positions | Turn starts | With a legal turn | At the cap | Share of those with a legal turn |
| --- | ---: | ---: | ---: | ---: |
| Under 50 boards | 19,410 | 19,023 | 2,302 | 12.1% |
| 50-99 boards | 3,162 | 2,965 | 1,376 | 46.4% |
| 100-199 boards | 189 | 176 | 124 | 70.5% |

That run's scorer gave every move a higher score than leaving its board, so with several playable boards its first turns play all of them.

### Relations by family, kind and where the victim stands

The start games: 2,496,465 relations over 15,387 distinct turn starts, 162.2 per position. An attack reaches an enemy piece and a defence an own one. Figures are per position.

| Family | Attacks onto heads | Attacks onto history | Defences onto heads | Defences onto history | All |
| --- | ---: | ---: | ---: | ---: | ---: |
| Same board | 6.90 | 0.00 | 53.11 | 0.00 | 60.00 (37.0%) |
| Timeline only | 0.84 | 1.18 | 7.69 | 11.82 | 21.54 (13.3%) |
| Time only | 0.00 | 5.19 | 0.00 | 44.17 | 49.36 (30.4%) |
| Both | 0.24 | 1.59 | 4.81 | 24.70 | 31.34 (19.3%) |
| **All** | 7.98 | 7.96 | 65.61 | 80.70 | 162.25 |

- Defences onto history boards are 1,241,663 relations, 49.7% of all.
- Attacks, and defences onto head boards, are 1,254,802: 81.5 per position, 60.0 of them on the attacker's own board and 21.5 leaving it.
- 1,478,807 relations (59.2%) are read on a passed copy.
- A victim on a passed copy counts as on its timeline's head.

### Live relation rows with defences kept only onto head boards

The same count as G0's (77,136 live rows, 20,376 for the gen-1 kinds), less the time-only defence rows: a time-only relation always lands on a history board.

| Rows | All twelve kinds | The six gen-1 kinds |
| --- | ---: | ---: |
| On the attacker's own board | 28,752 | 8,472 |
| Across boards | 39,552 | 9,600 |
| **Live** | **68,304** | **18,072** |
| Time-only defence rows, live at G0 and dropped here | 8,832 | 2,304 |

### Δt to the present in whole turns

G0's histogram of 25,202 head boards, with a half-turn distance rounded up to whole turns.

| Bucket | Behind | 0 | 1 | 2 | 3-4 | 5+ |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Head boards | 611 | 17,611 | 1,663 | 1,375 | 1,853 | 2,089 |
| Share | 2.4% | 69.9% | 6.6% | 5.5% | 7.4% | 8.3% |

### How the 692 usable human games end

| Timelines at the end | 1 | 2 | 3-4 | 5-8 | 9+ |
| --- | ---: | ---: | ---: | ---: | ---: |
| Games | 214 (30.9%) | 141 (20.4%) | 239 (34.5%) | 91 (13.2%) | 7 (1.0%) |

- Turns: 27,097 in all; mean 39.2, median 37, longest 99.
- Boards at the end: median 44, 95th percentile 85, most 186.

## 3. Legality after three changes to the copied code: the set scan, construction, two savings per turn (2026-11-17)

The first changes to copied code (`docs/gates/G1a-rules-review.md`), each measured against the engine as copied with the same tool on the same day.

- **Machine:** the desktop of entry 1. It was less quiet: 10.5% busy just before the runs (the same counter as entry 1, which read 1.2%). One thread, pinned to logical processor 2, priority above normal.
- **Build:** GNU g++ 16.2.0 (MSYS2 ucrt64), Release, `-O2 -DNDEBUG`, statically linked.
- **Commit:** none when measured. The changes were an uncommitted working tree on branch `phase-1a-rules`, on top of `e354801`. They were committed on 2026-11-22 as `ea9419e` (A), `629e898` (B) and `2604d61` (C); each of those commits has the late fix to `check.cpp` and one renamed variable there, which the binaries behind the tables did not (see the notes). Four binaries of `fractal-legality` were built, one per state of the engine:
  - *as copied:* `e354801`'s engine, with the tool's source added;
  - *A:* that, with this tree's `engine/src/check.cpp` (the set scan);
  - *B:* A, with this tree's `state.cpp` and `move_edits.hpp`, and its `hypercuboid.cpp` less item C's two hunks;
  - *C:* this tree.
- **Commands:**

  ```
  python -I data/t0database/to_5dpgn.py build/t0db
  fractal-legality --records build/t0db --passes 5 --json FILE        (each binary, twice, in the order copied, A, B, C)
  fractal-legality --records build/t0db --passes 5 --pool --json FILE (C only)
  python -I tools/bench/legality_table.py LABEL=FILE ...
  build/engine/fractal-bench --records build/t0db --json build/g1a/bench.json
  python -I tools/bench/turngen_table.py build/g1a/bench.json
  build/engine/fractal-legality --records build/t0db --verify --turns 12
  ```

- **What `fractal-legality` times.** Every distinct turn start of the 716 t0database records that replay to the end in parse mode, in entry 1's buckets. Each measure is five passes over a bucket's positions, and the median pass is reported in nanoseconds per position, with the heap allocations per position in brackets (the program's own `operator new` counts them).
  - *in_check:* R1's check status.
  - *scan succ:* `royal_capture_exists` on the first generated turn's successor, which is the submission test's scan on a position the search would reach.
  - *build:* a `TurnGenerator` constructed and destroyed. *first:* constructed, and one turn taken. *K turns:* constructed, and up to 8 turns taken.
  - *copy+apply:* a `GameState` copied and its first non-branching move applied.
- **Scorer.** The bands of spec §5.3 with no chess knowledge inside a band: a move on a mandatory board scores in [1, 2), leaving an optional board 0, a move on an optional board in [−2, −1), leaving a mandatory board −3. The score inside a band is a ten-step hash of the move, about 30 ns a move, and that is part of *build*. Entry 1's turn-generation figures used another scorer, in which every move outscores leaving; they are repeated below for comparison.

### Each state of the engine, second run

| Under 50 boards (19,410 positions; 71.5 moves and 1.25 playable boards on average; 7.63 turns taken of 8) | As copied | A | B | C | C with `--pool` |
| --- | ---: | ---: | ---: | ---: | ---: |
| movegen | 1,577 | 1,621 | 1,542 | 1,619 | 1,684 |
| copy+apply | 204 (3.2) | 205 (3.2) | 200 (3.2) | 204 (3.2) | 140 (3.2) |
| in_check | 1,581 (4.4) | 157 | 141 | 140 | 150 |
| scan succ | 2,523 | 382 | 500 | 344 | 322 |
| build | 15,108 (181.0) | 15,159 (181.0) | 5,177 (36.9) | 5,169 (36.9) | 4,638 (36.9) |
| first | 23,398 (247.1) | 21,022 (247.1) | 11,031 (103.0) | 10,756 (95.2) | 8,852 (95.2) |
| K turns | 51,975 (518.8) | 40,126 (518.8) | 30,252 (374.7) | 28,268 (318.2) | 22,424 (318.2) |
| First turn after the build | 8,290 (66.1) | 5,863 (66.1) | 5,853 (66.1) | 5,586 (58.3) | 4,214 (58.3) |
| Each further turn | 4,311 (41.0) | 2,882 (41.0) | 2,899 (41.0) | 2,642 (33.6) | 2,047 (33.6) |

| 50-99 boards (3,162 positions; 130.3 moves, 2.09 playable boards; 7.25 turns) | As copied | A | B | C | C with `--pool` |
| --- | ---: | ---: | ---: | ---: | ---: |
| in_check | 2,329 (5.3) | 259 | 264 | 263 | 266 |
| scan succ | 3,199 | 394 | 396 | 398 | 401 |
| build | 27,395 (314.9) | 27,290 (314.9) | 8,301 (47.6) | 8,354 (47.6) | 7,598 (47.6) |
| first | 48,423 (462.4) | 43,768 (462.4) | 25,795 (195.1) | 25,759 (182.9) | 22,016 (182.9) |
| K turns | 111,849 (1,076.7) | 94,180 (1,076.7) | 76,873 (809.3) | 74,748 (736.0) | 60,130 (736.0) |
| Each further turn | 10,140 (98.2) | 8,060 (98.2) | 8,166 (98.2) | 7,832 (88.4) | 6,093 (88.4) |

| 100-199 boards (189 positions; 218.9 moves, 3.67 playable boards; 7.38 turns) | As copied | A | B | C | C with `--pool` |
| --- | ---: | ---: | ---: | ---: | ---: |
| in_check | 3,326 (7.3) | 281 | 283 | 281 | 285 |
| scan succ | 4,325 | 430 | 428 | 430 | 442 |
| build | 48,866 (584.7) | 48,739 (584.7) | 13,292 (65.6) | 13,290 (65.6) | 12,366 (65.6) |
| first | 86,526 (820.4) | 77,098 (820.4) | 41,443 (301.3) | 40,833 (275.8) | 35,440 (275.8) |
| K turns | 193,250 (1,740.7) | 167,883 (1,740.7) | 133,153 (1,221.7) | 131,371 (1,111.3) | 109,651 (1,111.3) |
| Each further turn | 16,725 (144.2) | 14,228 (144.2) | 14,372 (144.2) | 14,189 (130.9) | 11,630 (130.9) |

Notes:

- **Two runs each.** The tables give each binary's second run. The first runs agree within 4% on every generator row. The first run of all, the engine as copied, read higher on the small rows (in_check 1,775, copy+apply 280), as a first run does. The scan rows last a few milliseconds a pass and are noisy: over both runs of A, B and C, *scan succ* under 50 boards read 427, 308, 323, 382, 500 and 344.
- **The generator did the same work in every state:** the same nodes and the same points judged per bucket, in all nine runs. Under 50 boards that is 79.1 nodes and 8.70 points judged for 7.63 turns.
- **`--pool` is an experiment in the measuring program,** not a change to the engine: freed blocks of up to 512 bytes are kept on free lists by size and handed back, in place of `malloc`.
- **No successor failed the scan** in any run.
- **One late fix, measured again.** The tables were taken before a guard was added to the set scan for multiverses of more than 256 timelines (four lines of `check.cpp`). The final binary, run twice more, agrees with column C within 2% on every generator row: under 50 boards, *K turns* 28,188 and 28,412, each further turn 2,639 and 2,682.
- **How far to trust the small rows.** The machine was less settled by then. Six alternating runs of the binary before the guard and the one after, nine passes each, read *in_check* under 50 boards at 219, 284 and 147 ns before and 165, 147 and 143 after, and *scan succ* at 610, 589 and 362 before and 424, 306 and 258 after: the spread between runs is larger than anything between the two binaries. Read the scan rows as 0.15 to 0.3 µs for check status and 0.3 to 0.6 µs for the submission scan, against 1.6 and 2.5 µs on the engine as copied.

### Entry 1's turn-generation table again, on state C

`fractal-bench`, as in entry 1: its scorer, medians over 3 passes per position and then over positions, among positions with a legal turn. Entry 1's figures are in brackets.

| Positions | First turn, µs, median [p90] | Up to 27 turns, µs | µs per further turn | Turns/s (pooled) | No legal turn: median µs to prove none |
| --- | --- | ---: | ---: | ---: | ---: |
| t0database, under 50 boards | **6.7** [25.2] (16.6 [52.1]) | 52.0 (91.5) | 1.72 (2.83) | 312,798 (167,434) | 25.7 (41.6) |
| t0database, 50-99 boards | 18.4 [68.4] (40.0 [125.9]) | 96.0 (174.9) | 2.70 (4.73) | 258,586 (139,620) | 35.3 (56.7) |
| t0database, 100-199 boards | 36.0 [225.8] (73.5 [354.2]) | 165.6 (309.5) | 3.73 (6.88) | 232,020 (120,081) | 28.1 (45.2) |
| N100 T6, synthetic | 342.6 [653.1] (703.7 [948.7]) | 410.0 (854.0) | 5.25 (14.04) | 155,756 (60,364) | 331.2 (508.0) |
| N160 T7, synthetic | 458.6 [1,456.6] (887.5 [1,882.3]) | 693.2 (1,265.2) | 6.88 (18.40) | 70,172 (34,159) | - |
| N240 T8, synthetic | 1,617.7 [2,710.8] (1,919.9 [3,083.5]) | 2,107.9 (3,096.5) | 13.09 (25.99) | 72,093 (34,388) | 728.3 (1,020.6) |
| N400 T10, synthetic | 2,779.0 [10,550.3] (3,938.8 [11,710.4]) | 2,781.8 (3,941.8) | 11.42 (28.97) | 57,900 (29,003) | 3,143.2 (3,608.8) |
| N600 T12, synthetic | 14,285.9 [96,250.0] (15,501.6 [101,041.7]) | 14,289.1 (15,504.9) | 11.93 (37.39) | 20,675 (13,199) | 6,412.8 (7,375.0) |

- Pseudo-legal movegen is unchanged, as it should be: 631,302 / 397,795 / 285,046 positions/s in the three t0database buckets (entry 1: 611,419 / 398,444 / 286,479).
- The 5-timeline near-mate case: the hypercuboid detector takes 59.4 µs at the same 184 nodes (entry 1: 75.4 µs); a `TurnGenerator` ends exhausted after the same 796 nodes in 433 µs (464 µs).
- The whole bench ran in 89 s, where entry 1's took 137 s.

### Checks run on state C

- `fractal-legality --verify`, 2.3 s: 22,761 positions; 604,020 comparisons of the set scan with the line scan, 3,198 of them with a capture; 3,184 positions in check; 256,488 generated turns replayed through the state machine; 597 positions with no turn, 596 checkmate and 1 stalemate by the detector; none undecided within 2,000,000 nodes; 0 differences. The engine as copied gives the same line, figure for figure.
- The copied tests: 366 pass, 3 skipped, 21 s. Fractal's own: 8 pass, 1.3 s.
- The harness: `selftest` 687 run, 5 skipped (4 need Kairos built, which it never is; 1 needs symlinks), 466 s; `gate --tier commit` 33 s; `gate --tier nightly` 7 minutes.

## 4. The turn generator's move buffers, reserved once and reused: item 7 (2026-11-28)

The fourth change to copied code (spec §3.3 item 7; `docs/gates/G1a-item-7.md`), measured against the engine just before it with the same tool on the same day.

- **Machine:** the desktop of entry 1. **It was not quiet.** Two jobs of other projects were running on about six of its 24 logical processors, and the counter of entry 1 read 33% busy just before the runs. One thread, pinned, priority above normal.
- **Build:** GNU g++ 16.2.0 (MSYS2 ucrt64), Release, `-O2 -DNDEBUG`, statically linked.
- **Commit:** *before* is `8e82eb3`, whose engine is entry 3's state C. *After* is the commit that adds this entry. The two binaries differ only in `engine/src/hypercuboid.cpp`.
- **Commands:**

  ```
  fractal-legality --records build/t0db --passes 5 --json FILE        (each binary, twice, in the order before, after, before, after)
  python -I tools/bench/legality_table.py LABEL=FILE ...
  fractal-legality --records build/t0db --ceilings --passes 1 --no-pin
  fractal-legality --records build/t0db --verify
  ```

- **What changed.** The generator lists its candidate moves in one buffer that it keeps, reserved once at 256 moves, and realisation's pseudo-legality test reuses that buffer. The move table and each axis's values are reserved from the candidate list before they are filled. The sequence a point's realisation plays is a member, cleared for each point.

Nanoseconds per position, with heap allocations per position in brackets, as in entry 3.

| Under 50 boards (19,410 positions) | Before, run 1 | After, run 1 | Before, run 2 | After, run 2 |
| --- | ---: | ---: | ---: | ---: |
| build | 6,022 (36.9) | 5,241 (18.2) | 5,677 (36.9) | 5,486 (18.2) |
| first | 12,876 (95.2) | 11,105 (72.2) | 11,602 (95.2) | 11,712 (72.2) |
| K turns | 30,564 (318.2) | 29,928 (286.4) | 31,101 (318.2) | 29,786 (286.4) |
| First turn after the build | 6,854 (58.3) | 5,863 (53.9) | 5,925 (58.3) | 6,227 (53.9) |
| Each further turn | 2,668 (33.6) | 2,839 (32.3) | 2,941 (33.6) | 2,726 (32.3) |

| 50-99 boards (3,162 positions) | Before, run 1 | After, run 1 | Before, run 2 | After, run 2 |
| --- | ---: | ---: | ---: | ---: |
| build | 9,595 (47.6) | 9,297 (25.6) | 10,204 (47.6) | 9,172 (25.6) |
| first | 27,399 (182.9) | 26,722 (154.7) | 27,479 (182.9) | 27,664 (154.7) |
| K turns | 80,279 (736.0) | 81,151 (697.9) | 80,737 (736.0) | 82,018 (697.9) |
| Each further turn | 8,454 (88.4) | 8,702 (86.9) | 8,515 (88.4) | 8,690 (86.9) |

| 100-199 boards (189 positions) | Before, run 1 | After, run 1 | Before, run 2 | After, run 2 |
| --- | ---: | ---: | ---: | ---: |
| build | 14,673 (65.6) | 13,831 (38.9) | 14,395 (65.6) | 13,847 (38.9) |
| first | 45,359 (275.8) | 44,231 (241.8) | 44,107 (275.8) | 45,537 (241.8) |
| K turns | 144,835 (1,111.3) | 145,533 (1,064.2) | 141,676 (1,111.3) | 144,547 (1,064.2) |
| Each further turn | 15,589 (130.9) | 15,876 (128.9) | 15,291 (130.9) | 15,516 (128.9) |

Notes:

- **The allocation counts are exact,** and the same in both runs: they are the program's own count and do not depend on the machine. Under 50 boards construction goes from 36.88 to 18.24 allocations, and a node that builds a generator and takes eight turns from 318.16 to 286.36. Against the engine as copied that is 181 to 18.24 and 519 to 286.36. The targets are 18 and 51.
- **The times are not a clean measurement,** because of the load. What they support: construction is faster, by 3% to 13% in every pair of runs; the whole node under 50 boards is 2% to 4% faster; and at 50 boards and more the node reads 0.5% to 2% slower in all four pairs. That is inside the spread between runs here, but it has the same sign each time, so it is not settled. The before runs here read 8% to 10% above entry 3's column C on *K turns* under 50 boards, which is the load. The runs are to be repeated when the machine is quiet, and the result appended here.
- **The generator did the same work:** the same nodes and the same points judged per bucket in all four runs, and no successor failed the scan.
- **Before, by another reading:** the before binary's counts are entry 3's column C to the last digit (36.9, 95.2 and 318.2 under 50 boards).

Checks:

- `fractal-legality --ceilings`: build 18.24 against a ceiling of 19, *K turns* 286.36 against 290. Run on the engine before the change, it fails on both (36.88 and 318.16).
- `fractal-legality --verify`, with the default eight turns: 22,761 positions; 435,884 comparisons of the set scan with the line scan, 3,195 of them with a capture; 3,184 positions in check; 172,420 generated turns replayed through the state machine; 597 positions with no turn, 596 checkmate and 1 stalemate; no difference.
- The copied tests: 366 pass, 3 skipped. Fractal's own: 23 pass. The pins of `tests/test_legality.cpp` did not move.
