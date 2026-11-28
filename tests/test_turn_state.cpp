// tests/test_turn_state.cpp - Fractal's own tests of the turn state (spec 3.6).
//
// New code, written for Fractal. The copied engine/tests/test_turn_state.cpp cannot be
// built here: it reads the turn state through the expansion, which was not copied
// (spec 3.2). What it tests of copied code is covered here: the turn state a GameState
// keeps - the present, the boards the mover must still play, whether the obligations
// are met, whether a move may be made - and the same through a t5d_game handle playing
// the same moves.
//
// The expected values are a recount written here from the rulebook's definitions
// (docs/rules/README.md: L5, T3 to T6, T8, with the readings Q4, Q5 and Q6 as the
// engine's rule options word them), at every state of seeded random play under every
// ruleset, and three positions small enough to read off the board. Which timelines are
// active (T10) is taken from the engine: the copied suite tests that, and the last
// case here reads one activation off the board.

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "t5d/capi.h"
#include "t5d/check.hpp"
#include "t5d/fingerprint.hpp"
#include "t5d/move.hpp"
#include "t5d/movegen.hpp"
#include "t5d/multiverse.hpp"
#include "t5d/records.h"
#include "t5d/rules.hpp"
#include "t5d/state.hpp"
#include "t5d/types.hpp"
#include "t5d/variants.hpp"
#include "test_framework.hpp"
#include "test_positions.hpp"
#include "test_rulesets.hpp"
#include "test_support.hpp"

using namespace t5d;

namespace {

// ---- the recount ---------------------------------------------------------------------------
//
// T4: the present is the column of the earliest playable board among the active
//     timelines. Under Q6's Deferred reading a deferred timeline is left out as well.
// L5: a board is playable when it is the latest of its timeline; the mover may move
//     from it when the mover is to move on it.
// T6: the mover's playable boards in the present, on active timelines, are mandatory.
//     Under Q5's AllActive reading every active one is, wherever it lies.
//     Every other playable board of the mover's is optional.

struct Recount {
  int present = INT_MAX;
  std::set<int> counting;   // timelines that count for the present
  std::set<int> playable;   // timelines whose latest board has the mover to move
  std::set<int> mandatory;
};

Recount recount(const GameState& s) {
  Recount r;
  std::set<int> deferred;
  for (const GameState::Deferral& d : s.deferred_timelines()) deferred.insert(d.timeline);
  for (const TimelineRef& t : s.multiverse().timelines()) {
    const int p = t->position();
    if (!s.active(p) || deferred.count(p) != 0) continue;
    r.counting.insert(p);
    r.present = std::min(r.present, t->last_half_turn());
  }
  const bool everywhere = s.rules().mandatory == MandatoryBoards::AllActive;
  for (const TimelineRef& t : s.multiverse().timelines()) {
    const int p = t->position();
    if (to_move_at(t->last_half_turn()) != s.to_move()) continue;
    r.playable.insert(p);
    if (r.counting.count(p) != 0 && (everywhere || t->last_half_turn() == r.present)) r.mandatory.insert(p);
  }
  return r;
}

int size_of(const std::set<int>& s) { return static_cast<int>(s.size()); }

struct Tally {
  long long states = 0;
  long long turn_starts = 0;
  long long mid_action = 0;
  long long met_and_may_move = 0;     // the obligations met and another move allowed (R8)
  long long met_and_may_not = 0;      // ... and none allowed (Q4's Guide-literal reading)
  long long with_optional = 0;        // states with a playable board that is not mandatory
  long long with_inactive = 0;        // states with an inactive timeline
  long long one_played = 0;           // moves that played one mandatory board
  long long two_played = 0;           // jumps between two mandatory boards
  long long none_played = 0;          // moves on optional boards only
  long long branches = 0;
  long long rewinds = 0;              // branches after which the obligations were met at once (T8)
  long long handle_states = 0;
};

// The GameState's turn state at `s` against the recount.
void check_state(const GameState& s, bool turn_start, Tally& tally) {
  const Recount r = recount(s);
  REQUIRE(!r.counting.empty());
  CHECK_EQ(s.present(), r.present);
  CHECK_EQ(s.mandatory_count(), size_of(r.mandatory));
  CHECK(s.mandatory_timelines() == std::vector<int>(r.mandatory.begin(), r.mandatory.end()));
  CHECK(s.playable_timelines() == std::vector<int>(r.playable.begin(), r.playable.end()));
  for (const TimelineRef& t : s.multiverse().timelines()) {
    const int p = t->position();
    CHECK_EQ(s.mandatory(p), r.mandatory.count(p) != 0);
    CHECK_EQ(s.playable_by_mover(p), r.playable.count(p) != 0);
  }
  // T5: the obligations are met when no mandatory board is left. Under the default
  // reading that is exactly when the present lies on the opponent's colour.
  CHECK_EQ(s.obligations_met(), r.mandatory.empty());
  CHECK_EQ(s.present_shifted(), to_move_at(r.present) != s.to_move());
  if (s.rules().mandatory == MandatoryBoards::InPresent) CHECK_EQ(s.obligations_met(), s.present_shifted());
  // Q4: by default a move may be made until the action is submitted (R8); under the
  // Guide-literal reading only while a mandatory board is left.
  CHECK_EQ(s.may_move(), s.rules().moves_after_shift || !r.mandatory.empty());
  // A turn starts with something to do.
  if (turn_start) CHECK(!r.mandatory.empty());
  // L5: every generated move starts on a playable board of the mover's, at its
  // timeline's latest board. None is generated when none may be made.
  MoveList moves;
  generate_moves(s, moves);
  for (const Move& m : moves) {
    CHECK(r.playable.count(m.from_timeline) != 0);
    const Timeline* from = s.multiverse().timeline(m.from_timeline);
    REQUIRE(from != nullptr);
    CHECK_EQ(m.from_half_turn, from->last_half_turn());
  }
  if (!s.may_move()) CHECK(moves.empty());

  ++tally.states;
  ++(turn_start ? tally.turn_starts : tally.mid_action);
  if (r.mandatory.empty()) ++(s.may_move() ? tally.met_and_may_move : tally.met_and_may_not);
  if (size_of(r.playable) > size_of(r.mandatory)) ++tally.with_optional;
  if (!s.inactive_timelines().empty()) ++tally.with_inactive;
}

// T3 and T6: a move is made on every board it starts or ends on, and a mandatory board
// is satisfied when it is moved from or when a piece arrives on it. So a move that
// makes no new timeline, and changes no timeline's standing, leaves one mandatory board
// fewer for each mandatory board it plays - whichever reading of Q5 is in force.
void check_move(const GameState& s, const Move& m, const GameState& t, Tally& tally) {
  const Recount a = recount(s);
  const Recount b = recount(t);
  if (m.branching()) {
    ++tally.branches;
    // T8: a branch may put the present back on the opponent's colour at once.
    if (!a.mandatory.empty() && b.mandatory.empty() && a.mandatory.count(m.from_timeline) == 0) ++tally.rewinds;
    return;
  }
  bool standing_changed = false;
  for (const TimelineRef& tl : s.multiverse().timelines()) {
    const int p = tl->position();
    if ((a.counting.count(p) != 0) != (b.counting.count(p) != 0)) standing_changed = true;
  }
  if (standing_changed) return;
  const bool from_mandatory = a.mandatory.count(m.from_timeline) != 0;
  const bool onto_mandatory = m.jump() && a.mandatory.count(m.to_timeline) != 0;
  const int played = (from_mandatory ? 1 : 0) + (onto_mandatory ? 1 : 0);
  CHECK_EQ(size_of(b.mandatory), size_of(a.mandatory) - played);
  // The boards it did not play are still to play.
  for (const int p : a.mandatory) {
    const bool was_played = p == m.from_timeline || (m.jump() && p == m.to_timeline);
    CHECK_EQ(b.mandatory.count(p) != 0, !was_played);
  }
  ++(played == 0 ? tally.none_played : played == 1 ? tally.one_played : tally.two_played);
}

// ---- the C API's game handle, playing the same moves ----------------------------------------

struct Handle {
  t5d_game* p = nullptr;
  Handle() = default;
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  ~Handle() { t5d_game_destroy(p); }
  void take_copy_of(const t5d_game* other) {
    t5d_game* copy = nullptr;
    REQUIRE_EQ(t5d_game_copy(other, &copy), T5D_OK);
    t5d_game_destroy(p);
    p = copy;
  }
};

t5d_move record_of(const Move& m) {
  t5d_move r{};
  r.from_timeline = m.from_timeline;
  r.from_half_turn = m.from_half_turn;
  r.from_square = m.from_square;
  r.to_timeline = m.to_timeline;
  r.to_half_turn = m.to_half_turn;
  r.to_square = m.to_square;
  r.promotion = static_cast<std::uint8_t>(m.promotes() ? index_of(m.promotion) + 1 : 0);
  return r;
}

// The handle's summary at `s`, with `moves_made` moves made in the action. The game
// tests submissions for the present's shift only, so it may submit exactly when the
// obligations are met.
void check_handle(const t5d_game* game, const GameState& s, int moves_made, Tally& tally) {
  const Recount r = recount(s);
  t5d_game_summary info{};
  REQUIRE_EQ(t5d_game_info(game, &info), T5D_OK);
  CHECK_EQ(info.to_move, index_of(s.to_move()));
  CHECK_EQ(info.present, r.present);
  CHECK_EQ(info.obligations_met, r.mandatory.empty() ? 1 : 0);
  CHECK_EQ(info.may_move, (s.rules().moves_after_shift || !r.mandatory.empty()) ? 1 : 0);
  CHECK_EQ(info.may_submit, r.mandatory.empty() ? 1 : 0);
  CHECK_EQ(info.moves_made, moves_made);
  CHECK_EQ(info.timelines, s.multiverse().timeline_count());
  int active = 0;
  for (const TimelineRef& t : s.multiverse().timelines()) active += s.active(t->position()) ? 1 : 0;
  CHECK_EQ(info.active_timelines, active);
  CHECK_EQ(info.boards, static_cast<std::uint64_t>(s.multiverse().board_count()));
  ++tally.handle_states;
}

// ---- random play ------------------------------------------------------------------------------

// The four gen-1 variants, and two starts with more than one timeline.
const char* const kStarts[] = {
    "Standard",          "Standard - Turn Zero",     "Standard - Defended Pawn",
    "Standard - Half Reflected", "Standard - Two Timelines", "Misc - Timeline Battleground",
};

// Plays `actions` actions of random pseudo-legal moves from a variant's start under
// `rules`, on a GameState and on a handle. An action is random moves until the
// obligations are met; then, while another move is allowed, one more with probability
// one half; then the submission. An action that runs out of moves with its obligations
// unmet is tried again from its turn start, and a turn that finds no action ends the game.
void random_game(const char* variant_name, const RuleOptions& rules, std::uint64_t seed, int actions, Tally& tally) {
  const Variant* variant = find_variant(variant_name);
  REQUIRE(variant != nullptr);
  t5d_test::Rng rng(seed);
  GameState s = variant_start(*variant, rules);
  Handle game;
  REQUIRE_EQ(t5d_game_new_variant(variant_name, rules.encode(), &game.p, nullptr), T5D_OK);
  REQUIRE_EQ(t5d_game_set_legality(game.p, T5D_PSEUDO_LEGAL, nullptr), T5D_OK);
  {
    t5d_state* state = nullptr;
    t5d_fingerprint f{};
    REQUIRE_EQ(t5d_game_state(game.p, &state), T5D_OK);
    const t5d_status status = t5d_state_fingerprint(state, &f);
    t5d_state_destroy(state);
    REQUIRE_EQ(status, T5D_OK);
    REQUIRE_EQ(std::string(f.fingerprint), fingerprint(s.multiverse()).fingerprint);
  }
  MoveList moves;
  for (int a = 0; a < actions; ++a) {
    const GameState turn_start = s;
    Handle at_turn_start;
    at_turn_start.take_copy_of(game.p);
    bool done = false;
    for (int attempt = 0; attempt < 6 && !done; ++attempt) {
      s = turn_start;
      game.take_copy_of(at_turn_start.p);
      int made = 0;
      for (int step = 0; step < 40; ++step) {
        check_state(s, made == 0, tally);
        check_handle(game.p, s, made, tally);
        if (s.obligations_met() && (!s.may_move() || rng.chance(50))) {
          done = true;
          break;
        }
        generate_moves(s, moves);
        if (moves.empty()) {
          done = s.obligations_met();
          break;
        }
        const Move m = moves[static_cast<std::size_t>(rng.below(static_cast<int>(moves.size())))];
        GameState next = s;
        next.apply(m);
        check_move(s, m, next, tally);
        const t5d_move r = record_of(m);
        REQUIRE_EQ(t5d_game_apply(game.p, &r, nullptr), T5D_OK);
        s = next;
        ++made;
      }
    }
    if (!done) return;
    s.submit(CheckRuling::none());
    REQUIRE_EQ(t5d_game_submit(game.p, nullptr), T5D_OK);
  }
  check_state(s, true, tally);
  check_handle(game.p, s, 0, tally);
}

Tally play_under_every_ruleset(int games, int actions, std::uint64_t seed) {
  Tally tally;
  for (const RuleOptions& rules : t5d_test::every_ruleset()) {
    for (const char* start : kStarts) {
      for (int g = 0; g < games; ++g) random_game(start, rules, seed++, actions, tally);
    }
  }
  return tally;
}

void print(const char* name, const Tally& t) {
  std::fprintf(stderr,
               "  %s: %lld states (%lld turn starts, %lld mid-action; %lld with the obligations met and a move "
               "allowed, %lld with none allowed; %lld with an optional board; %lld with an inactive timeline); "
               "moves that played one mandatory board %lld, two %lld, none %lld; %lld branches (%lld met the "
               "obligations at once); %lld states through the C API\n",
               name, t.states, t.turn_starts, t.mid_action, t.met_and_may_move, t.met_and_may_not, t.with_optional,
               t.with_inactive, t.one_played, t.two_played, t.none_played, t.branches, t.rewinds, t.handle_states);
}

// The generated move of `s` with these coordinates, applied to it.
void play(GameState& s, int l, int h, int file, int rank, int to_l, int to_h, int to_file, int to_rank) {
  MoveList moves;
  generate_moves(s, moves);
  for (const Move& m : moves) {
    if (m.from_timeline == l && m.from_half_turn == h && m.from_square == make_square(file, rank) &&
        m.to_timeline == to_l && m.to_half_turn == to_h && m.to_square == make_square(to_file, to_rank)) {
      s.apply(m);
      return;
    }
  }
  REQUIRE(false);  // no such move
}

}  // namespace

TEST(turn_state_equals_a_recount_under_every_ruleset) {
  const Tally t = play_under_every_ruleset(2, 12, 0x5d5d0001);
  print("turn state", t);
  // The play reached what the checks are about.
  CHECK(t.states > 5000);
  CHECK_EQ(t.handle_states, t.states);
  CHECK(t.turn_starts > 1000);
  CHECK(t.mid_action > 1000);
  CHECK(t.met_and_may_move > 0);
  CHECK(t.met_and_may_not > 0);
  CHECK(t.with_optional > 0);
  CHECK(t.with_inactive > 0);
  CHECK(t.one_played > 1000);
  CHECK(t.two_played > 0);
  CHECK(t.none_played > 0);
  CHECK(t.branches > 0);
  CHECK(t.rewinds > 0);
}

TEST(long_turn_state_equals_a_recount_under_every_ruleset) {
  const Tally t = play_under_every_ruleset(5, 24, 0x5d5d1001);
  print("turn state, long", t);
  CHECK(t.states > 100000);
  CHECK_EQ(t.handle_states, t.states);
  CHECK(t.with_inactive > 0);
  CHECK(t.two_played > 0);
  CHECK(t.rewinds > 0);
}

TEST(turn_state_read_off_the_board) {
  // White has timeline 0 at T1 and timeline +1 at T2. Half-turns: T1 White 2,
  // T1 Black 3, T2 White 4. The present is T1 (T4), so timeline 0 is mandatory and
  // timeline +1, ahead of the present, is optional (T6).
  const std::vector<std::string> boards = {"[3k/4/4/K3:0:1:w]", "[3k/4/4/K3:+1:2:w]"};
  {
    GameState s = t5d_test::game(boards, BoardSize{4, 4});
    CHECK_EQ(s.present(), 2);
    CHECK(s.mandatory_timelines() == std::vector<int>({0}));
    CHECK(s.playable_timelines() == std::vector<int>({0, 1}));
    CHECK(!s.obligations_met());
    CHECK(!s.present_shifted());
    CHECK(s.may_move());
    CHECK(s.submission_status(CheckRuling::none()) == SubmitStatus::ObligationsUnmet);
    // The optional board played first changes nothing that is owed.
    GameState optional_first = s;
    play(optional_first, 1, 4, 0, 0, 1, 4, 1, 0);  // (+1T2)Ka1b1
    CHECK_EQ(optional_first.present(), 2);
    CHECK_EQ(optional_first.mandatory_count(), 1);
    CHECK(!optional_first.obligations_met());
    // The mandatory board played: the present is Black's T1 (T5).
    play(s, 0, 2, 0, 0, 0, 2, 0, 1);  // (0T1)Ka1a2
    CHECK_EQ(s.present(), 3);
    CHECK_EQ(s.mandatory_count(), 0);
    CHECK(s.obligations_met());
    CHECK(s.present_shifted());
    CHECK(s.playable_timelines() == std::vector<int>({1}));
    CHECK(s.may_move());  // R8: until submission
    CHECK(s.submission_status(CheckRuling::none()) != SubmitStatus::ObligationsUnmet);
    play(s, 1, 4, 0, 0, 1, 4, 1, 0);  // the optional board, after the shift
    CHECK(s.obligations_met());
    CHECK(s.playable_timelines().empty());
    s.submit(CheckRuling::none());
    // Black's turn: timeline 0 at T1 is in the present; timeline +1 at T2 is ahead of it.
    CHECK(s.to_move() == Colour::Black);
    CHECK_EQ(s.present(), 3);
    CHECK(s.mandatory_timelines() == std::vector<int>({0}));
    CHECK(s.playable_timelines() == std::vector<int>({0, 1}));
  }
  {
    // Q4's Guide-literal reading: no move once the present has shifted.
    RuleOptions literal;
    literal.moves_after_shift = false;
    GameState s = t5d_test::game(boards, BoardSize{4, 4}, literal);
    CHECK(s.may_move());
    play(s, 0, 2, 0, 0, 0, 2, 0, 1);
    CHECK(s.obligations_met());
    CHECK(!s.may_move());
    MoveList moves;
    generate_moves(s, moves);
    CHECK(moves.empty());
  }
  {
    // Q5's other reading: every active timeline the mover can play is mandatory, so
    // the present shifts before the obligations are met.
    RuleOptions all;
    all.mandatory = MandatoryBoards::AllActive;
    GameState s = t5d_test::game(boards, BoardSize{4, 4}, all);
    CHECK(s.mandatory_timelines() == std::vector<int>({0, 1}));
    play(s, 0, 2, 0, 0, 0, 2, 0, 1);
    CHECK(s.present_shifted());
    CHECK(!s.obligations_met());
    CHECK(s.mandatory_timelines() == std::vector<int>({1}));
    play(s, 1, 4, 0, 0, 1, 4, 1, 0);
    CHECK(s.obligations_met());
  }
  {
    // A branch and an inactive timeline, from the Standard start. Half-turns: T1 White
    // 2, T1 Black 3, T2 White 4, T2 Black 5.
    GameState s = variant_start(*find_variant("Standard"));
    play(s, 0, 2, 6, 0, 0, 2, 5, 2);  // 1. (0T1)Ng1f3
    s.submit();
    play(s, 0, 3, 6, 7, 0, 3, 5, 5);  // 1... (0T1)Ng8f6
    s.submit();
    // 2. (0T2)Nf3>>(0T1)f5 lands on a history board, so it branches. White's first
    // timeline is active (R2), and its first board, at T1 with Black to move, is earlier
    // than the present: the present moves back to it and White may submit (T8).
    CHECK_EQ(s.present(), 4);
    play(s, 0, 4, 5, 2, 0, 2, 5, 4);
    CHECK(s.active(1));
    CHECK_EQ(s.present(), 3);
    CHECK(s.obligations_met());
    s.submit();
    // Black owes the new board only: timeline 0 is at T2, ahead of the present.
    CHECK(s.mandatory_timelines() == std::vector<int>({1}));
    CHECK(s.playable_timelines() == std::vector<int>({0, 1}));
    play(s, 1, 3, 6, 7, 1, 3, 5, 5);  // 2... (+1T1)Ng8f6
    CHECK_EQ(s.present(), 4);
    CHECK(s.obligations_met());
    s.submit();
    // 3. (+1T2)Nb1>(0T2)b3 branches again. White already has one timeline more than
    // Black, so +2 is inactive (R2) and does not move the present (T4).
    CHECK(s.mandatory_timelines() == std::vector<int>({1}));
    play(s, 1, 4, 1, 0, 0, 4, 1, 2);
    CHECK(!s.active(2));
    CHECK(s.inactive_timelines() == std::vector<int>({2}));
    CHECK_EQ(s.present(), 5);
    CHECK(s.obligations_met());
    s.submit();
    // Black owes timelines 0 and +1. Timeline +2 is playable and never mandatory (T6).
    CHECK(s.mandatory_timelines() == std::vector<int>({0, 1}));
    CHECK(s.playable_timelines() == std::vector<int>({0, 1, 2}));
    CHECK(!s.mandatory(2));
    play(s, 0, 5, 0, 6, 0, 5, 0, 5);  // (0T2)a7a6
    play(s, 1, 5, 0, 6, 1, 5, 0, 5);  // (+1T2)a7a6
    CHECK(s.obligations_met());
    CHECK(s.playable_timelines() == std::vector<int>({2}));
  }
}
