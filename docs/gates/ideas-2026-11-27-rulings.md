# Rulings on the ideas pass of 27 November 2026, and the third spec revision

`ideas-2026-11-27.md` is an ideas pass Fergus asked for on 27 November 2026. It covers Stockfish's main branch since Stockfish 19, its trainer and the other engines in development, read through their commit notes and documents, and looks for ideas Fractal should test. It was written in Fergus's review session, outside the repository and without being run against the code, and is filed here as it stands.

On 28 November Fergus ruled on its three questions and asked for its rows to be merged as a spec revision of its own, after the one that carries the G1a rulings, with its figures checked against the spec along the way.

This file records the rulings, what they changed and that check. A committed decision changes only on a written proposal in a gate report: the note is the proposal, and this file is the record. The register's reversals log has a row for each change to a decision.

## The rulings

| # | Question | Ruling | Where |
| --- | --- | --- | --- |
| 1 | The arms: add NX28 to NX34, and the notes on NX13 and NX14 | **Yes** | Spec §15.1; the register's Arms |
| 2 | Room for search-tree samples in the record, where ND1 is committed and committed-class | **(a).** The format reserves an optional list of tree samples per searched position now, left empty in gen 1. Whether to fill it stays NX31's question at G4 | Spec §9, §9.1; ND1 |
| 3 | The correction to §13.3's "Stockfish 18–19" row: pawn pairs were added, not dropped | **Yes** | Spec §13.3 |

## What changed in the spec

| Section | Now says |
| --- | --- |
| §0 | The third revision, and where the ideas pass and its rulings are |
| §9, §9.1 | Each searched position has room for a list of search-tree samples, empty in gen 1 (ruling 2). The wording is taken from NX31's "What is stored" in the note |
| §13.3 | The "Stockfish 18–19" row in the note's corrected wording (ruling 3). Its seven rows, after the Stockfish rows |
| §13.4 | Its twelve sources on Stockfish's main branch, the trainer and Viridithas |
| §14.3 | The ideas pass and this file |
| §15.1 | NX28 to NX34. NX13's "Why" and NX14's "Arm" extended. NX28, NX29, NX32 and NX30 placed in the screen order, NX31 and NX33 at G4 outside it, and NX34 in the match order. G2's first four screens are unchanged |

The rows, links and changed cells were taken from the note by a script rather than retyped, so they stand as written. That includes the three passages the check below questions; their fixes wait for a ruling.

## What changed in the register

- **Arms.** NX28 to NX34 added. The header and the Arms section say 34 arms, and the Arms section says NX31 and NX33 are screened at G4. The decision count is unchanged: 109, 68 of them committed.
- **ND1** gains the room for tree samples (ruling 2).
- **Open items.** Fergus's living docs need all three revisions.
- **Reversals log.** Two rows dated 28 November 2026.

## The figures, checked against the spec

What the note reports of Stockfish and the other engines is outside evidence: the spec doesn't carry it, and it was not checked again here. Nor were §13.3's feature counts (79,856 → 59,808), which the note also leaves as they were. What the note takes or derives from Fractal's own documents was checked.

**What holds:**

| The note says | Against | Check |
| --- | --- | --- |
| v1 picks one of five output stacks by active-timeline count | §8: 1, 2, 3–4, 5–8, 9+ | Holds |
| 85% of distinct turn starts have under 50 boards, at a mean of 1.4 active timelines, so most positions land in the first two stacks | §8; MEASUREMENTS entry 1 (1.40) | Holds. With a floor of 1 and a mean of 1.40, at most a fifth of those positions have three or more, so at least 68% of all distinct turn starts fall in the first two stacks |
| A full stack is about 95,000 8-bit weights; form (c) adds about a thousand per class | §8: 2,944 → 32 → 32 → 1 | Holds: 94,208 + 1,024 + 32 = 95,264, and 1,056 in the later layers |
| §7.5 names the stack selector as the one use of a global count | §7.5 | Holds |
| The gen-1 piece table is 0.6 MB at 16 bits, against a 3.4 MB net file | §7.2: 1,152 live rows; §8: 256 wide, 3.4 MB | Holds: 589,824 bytes |
| Relation rows are already 8-bit; NN11 is committed; ND1 is committed and committed-class | §8.4; the register | Holds |
| A human game has about 39 turns | §9.5 | Holds |
| A capped game's positions train on their scores alone | §9.2 | Holds |
| ±2.5 K is a win probability of about 92% | §8, §9.3: a logistic of score / K, K = 400 | Holds: sigmoid(2.5) = 0.924. It stands in for the trainer's 1,000 units by reading them on Fractal's scale; the arm uses Fractal's scale, so the threshold is Fractal's own |
| Games in 5D almost always end in mate | §13.2: 99.65% of Kairos's test games | Holds |
| In a one-timeline position at most 416 of the 1,472 inputs per view can be non-zero | §8: 2 × 176 + 2 × 32 | Holds: at a turn start the one head board is mandatory |
| About 21.5 relation rows per position are added at each evaluation | §7.3 | Holds |
| Verifying 21 key bits instead of 16 makes false hits 32 times rarer | Arithmetic | Holds: 2⁵ |
| Of the seven arms, four are screens on stored data, two wait for G4 and one is a match | The seven rows | Holds |
| Kairos is at `194e426`, with no commits since | §13.2 | Holds |
| ftxi is at `664277d`, four commits on | ftxi's repository | Holds. `git ls-remote` gives `664277d` as `main`, and GitHub's comparison puts it four commits after `7e8bbca`, the head this clone last fetched: an optional-moves report, an Elo display and two merges |

**What doesn't hold, or holds only in part:**

1. **NX34: what the scan knows.**
   - **The claim.** The note says the scan "has already run when the generator is built (§6.1), and it knows which of the mover's pieces an enemy relation reaches". The merged row says "The scan is already paid for at the node (§6.1)".
   - **What the spec says.** The scan at each evaluation covers only the relations that leave a board, 21.5 per position (§7.3). A board's same-board relations are summed once and cached in its record (§7.3, §8.2). Of each colour, the record notes only whether a royal is attacked (§8.2). The same-board relations are 60.0 of the 81.5 per position.
   - **The gap.** On a cached board nothing says which pieces are attacked, by what, or whether they are defended. For each colour, the arm needs the record to keep:
     - the squares the other side's relations reach;
     - the lowest attacker's ordering value on each;
     - the squares the colour's own relations defend.

     The alternative is to rescan the board. That is a few 64-bit masks and a small table, but it is not in §8.2.
2. **NX30: how facing pawns are indexed.**
   - **The index.** The note has facing pawns "indexed like any other relation". v1's index holds the victim's square and not the attacker's (§7.3), so a facing pawn's row would be the same row as a pawn attacking an enemy pawn, or defending an own pawn, on that square.
   - **The definition.** A blocked step is not a capture geometry, so §7.3's definition of a relation doesn't cover it. The arm needs rows of its own, such as a tenth displacement class.
   - **Two forward steps.** A pawn in 5D steps forward along its rank or across timelines (rulebook §4.6). The arm doesn't say whether a pawn on the next timeline that blocks the timeline step counts.
   - **NI8.** §7.5 commits to excluding pawn-structure terms [NI8]. A facing pawn is an unweighted rule fact that the net weighs, which P1 allows on this reading. The note doesn't say so, and the row's Changes column names only NI3.
3. **§13.3's new row, "§6.1 and NF13 already work this way".**
   - **The part that holds:** legal-only generation. The generator yields legal turns, and the search never tests a move's legality [NT2, NF13].
   - **The part that doesn't:** "a kept map of attacked squares". Fractal keeps no such map. Legality comes from the generator, the depth-first detector and the submission test. The relation scan is an evaluation input, and its same-board part is cached as a sum (finding 1).
4. **NX14's second form, "at most 64 rows".**
   - **The count.** A from-scratch build of a piece accumulator adds a row for each piece and another for each unmoved king, rook, pawn or brawn (§7.2). That is up to 128 rows in the index, and 54 at most in the gen-1 set-ups (32 pieces and 22 unmoved flags).
   - **The conclusion still holds:** §8.2 already builds a board with no parent from scratch.
   - In the note only.
5. **NX14's second form, "from 0.6 MB to 9.6 MB".**
   - **The arithmetic.** The table is 1,152 live rows × 256 × 2 bytes = 589,824 bytes. Sixteen copies are 9,437,184 bytes, which is 9.4 MB. The note multiplied the rounded 0.6.
   - In the note only.
6. **Ruling 2 has no place in the note's merge list.**
   - **The omission.** "How to merge" names §13.3, §13.4, §15.1 and the register, not §9.1 or ND1.
   - **What was done.** Ruling 2 (a) changes the record format, so it went into §9's ND1 row, §9.1 and the register's ND1. ND1 is committed, so it also has a reversals row.
7. **"Stockfish main (19 August)".**
   - **The inconsistency.** The note's summary dates Stockfish 19 to 5 September, so the AdamW change of 19 August is in Stockfish 19, not in its main branch since.
   - Nothing merged depends on it.
8. **The note on NX13: "In v1's index the two rows are not duplicates".**
   - **The qualification.** v1 already drops one row of each same-kind, same-board enemy pair, even though the two rows hold different squares. Each view records the pair once, as own attacks enemy (§7.3: 624 rows).
   - **What the note adds.** Its line extends that to pairs of different kinds whose geometries nest, which the attacker's square makes exact.

Findings 4, 5, 7 and 8 are in the note alone, which stays as it stands. Nothing in the spec repeats them, so they need no ruling.

## Rulings needed

1. **NX34's "Why".**
   - **Proposed:** replace "The scan is already paid for at the node (§6.1)" with "The scan runs at the node (§6.1); the board record would also keep, per colour, the squares the other side's relations reach and the lowest attacker on each (§8.2)".
   - **Recommended:** yes. Otherwise the row promises a free signal that the record doesn't hold.
2. **NX30: rows of its own, both forward steps, and NI8.**
   - **Proposed "Arm":** "Facing pawns on relation rows of their own: a pawn whose forward step, along its rank or across timelines, is blocked by a pawn of either colour".
   - **Proposed addition to "Why":** "An unweighted rule fact, as P1 allows, and not one of NI8's pawn-structure terms".
   - **Recommended:** yes. If the timeline step should wait, the row can name the rank step alone.
3. **§13.3's "legal-only move generation" row.**
   - **Proposed "Used here":** "Legal turns only, from the generator [NT2, NF13]; NX34 for the ordering", in place of "§6.1 and NF13 already work this way; NX34".
   - **Recommended:** yes.
