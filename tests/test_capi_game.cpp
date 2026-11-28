// tests/test_capi_game.cpp - Fractal's own tests of the C API's game calls (spec 3.6).
//
// New code, written for Fractal. The copied engine/tests/test_capi_game.cpp cannot be
// built here: it includes the expansion and the square labels, which were not copied
// (spec 3.2), and calls the three entry points G0 removed. Nothing else that is built
// exercises t5d_game_*, t5d_layout or the round trip of a ruleset's encoding. So the
// cases of that file that are about copied code are covered here, each written again
// from the C API's header (engine/include/t5d/capi.h) and the rule ids it cites.
//
// Left out, because the code was not copied: every array of an expansion, the relation
// bits, the square labels, and applying a move by its index in an expansion.
//
// Every expected value is one of three things: the engine's own answer by a second
// route (a C++ GameState playing the same moves, held equal by fingerprint), a status
// the header promises, or a position small enough to read off the board. The starting
// boards of the four gen-1 variants are written out from the rulebook
// (docs/rules/README.md section 8, V3).

#include <cstddef>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "synthetic.hpp"
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
#include "test_rulesets.hpp"

using namespace t5d;

namespace {

// ---- owning the handles ------------------------------------------------------------------

struct Game {
  t5d_game* p = nullptr;
  Game() = default;
  Game(const Game&) = delete;
  Game& operator=(const Game&) = delete;
  ~Game() { t5d_game_destroy(p); }
};

struct State {
  t5d_state* p = nullptr;
  State() = default;
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  ~State() { t5d_state_destroy(p); }
};

struct Text {
  char* p = nullptr;
  Text() = default;
  Text(const Text&) = delete;
  Text& operator=(const Text&) = delete;
  ~Text() { t5d_free(p); }
  bool has(std::string_view part) const {
    return p != nullptr && std::string_view(p).find(part) != std::string_view::npos;
  }
};

// ---- reading a game ------------------------------------------------------------------------

std::string fingerprint_of(const t5d_game* game) {
  State s;
  if (t5d_game_state(game, &s.p) != T5D_OK) return "<no state>";
  t5d_fingerprint f{};
  if (t5d_state_fingerprint(s.p, &f) != T5D_OK) return "<no fingerprint>";
  return f.fingerprint;
}

std::string fingerprint_of(const GameState& s) { return fingerprint(s.multiverse()).fingerprint; }

// The game's boards in canonical order, as 5DFEN board strings without the unmoved marks.
std::vector<std::string> boards_of(const t5d_game* game) {
  std::vector<std::string> out;
  State s;
  std::size_t count = 0;
  if (t5d_game_state(game, &s.p) != T5D_OK || t5d_state_board_count(s.p, &count) != T5D_OK) return out;
  for (std::size_t i = 0; i < count; ++i) {
    Text fen;
    if (t5d_state_board(s.p, i, nullptr, nullptr, &fen.p) != T5D_OK) return {};
    std::string text;
    for (const char* c = fen.p; *c != '\0'; ++c) {
      if (*c != '*') text.push_back(*c);
    }
    out.push_back(text);
  }
  return out;
}

t5d_game_summary summary_of(const t5d_game* game) {
  t5d_game_summary info{};
  REQUIRE_EQ(t5d_game_info(game, &info), T5D_OK);
  return info;
}

// What t5d_game_apply reads of a move: its source, its destination and its promotion.
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

// A move on one board: from one square to another, at half-turn `h` of `timeline`.
t5d_move board_move(int timeline, int h, int from_file, int from_rank, int to_file, int to_rank) {
  t5d_move r{};
  r.from_timeline = timeline;
  r.from_half_turn = h;
  r.from_square = static_cast<std::int8_t>(make_square(from_file, from_rank));
  r.to_timeline = timeline;
  r.to_half_turn = h;
  r.to_square = static_cast<std::int8_t>(make_square(to_file, to_rank));
  return r;
}

// Every field of the summary against the C++ state it stands for. `pseudo_legal`: the
// game tests submissions for the present's shift only, so it may submit exactly when
// the obligations are met.
void check_summary(const t5d_game* game, const GameState& s, int moves_made, bool pseudo_legal) {
  const t5d_game_summary info = summary_of(game);
  const Multiverse& mv = s.multiverse();
  CHECK_EQ(info.boards, static_cast<std::uint64_t>(mv.board_count()));
  CHECK_EQ(info.width, mv.board_size().width);
  CHECK_EQ(info.height, mv.board_size().height);
  CHECK_EQ(info.even, mv.even() ? 1 : 0);
  CHECK_EQ(info.to_move, index_of(s.to_move()));
  CHECK_EQ(info.present, s.present());
  CHECK_EQ(info.timelines, mv.timeline_count());
  int active = 0;
  for (const TimelineRef& t : mv.timelines()) active += s.active(t->position()) ? 1 : 0;
  CHECK_EQ(info.active_timelines, active);
  CHECK_EQ(info.obligations_met, s.obligations_met() ? 1 : 0);
  CHECK_EQ(info.may_move, s.may_move() ? 1 : 0);
  if (pseudo_legal) CHECK_EQ(info.may_submit, s.obligations_met() ? 1 : 0);
  CHECK_EQ(info.ruleset, mv.ruleset());
  CHECK_EQ(info.rules, s.rules().encode());
  CHECK_EQ(info.moves_made, moves_made);
}

// A seeded pseudo-legal game of the copied synthetic player, with branches.
synthetic::Game sample_game() {
  synthetic::Spec spec;
  spec.seed = 2026;
  spec.target_boards = 60;
  spec.target_active = 4;
  spec.branch_percent = 50;
  return synthetic::play(spec);
}

const char* const kStandardBoard = "[r*nbqk*bnr*/p*p*p*p*p*p*p*p*/8/8/8/8/P*P*P*P*P*P*P*P*/R*NBQK*BNR*:0:1:w]";

}  // namespace

// ---- a record, replayed --------------------------------------------------------------------

TEST(capi_game_replays_a_record_as_the_engine_does) {
  const synthetic::Game g = sample_game();
  REQUIRE(g.stop == synthetic::Stop::Target);
  REQUIRE(g.actions.size() >= 12);
  const std::string record = synthetic::record(g);
  Game game;
  Text error;
  REQUIRE_EQ(t5d_game_new_from_pgn(record.data(), record.size(), T5D_RULES_STANDARD, &game.p, &error.p), T5D_OK);
  // Synthetic play is pseudo-legal, so it is replayed as such.
  REQUIRE_EQ(t5d_game_set_legality(game.p, T5D_PSEUDO_LEGAL, &error.p), T5D_OK);

  std::size_t total = 0;
  std::size_t played = 99;
  std::int32_t truncated = 9;
  CHECK_EQ(t5d_game_record_actions(game.p, &total, &played, &truncated), T5D_OK);
  CHECK_EQ(total, g.actions.size());
  CHECK_EQ(played, std::size_t{0});
  CHECK_EQ(truncated, 0);

  // The same game in C++, action by action: the same position at every turn start.
  GameState s = g.start;
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
  check_summary(game.p, s, 0, true);
  bool branched = false;
  for (std::size_t a = 0; a < total; ++a) {
    REQUIRE_EQ(t5d_game_replay_next(game.p, nullptr), T5D_OK);
    for (const Move& m : g.actions[a]) {
      branched = branched || m.branching();
      s.apply(m);
    }
    s.submit(CheckRuling::none());
    CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
    check_summary(game.p, s, 0, true);
    CHECK_EQ(t5d_game_record_actions(game.p, nullptr, &played, nullptr), T5D_OK);
    CHECK_EQ(played, a + 1);
  }
  CHECK(branched);  // the record held time travel, not one board's game
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(g.state));
  const t5d_game_summary info = summary_of(game.p);
  CHECK(info.timelines > 1);
  CHECK_EQ(info.may_move, 1);
  CHECK_EQ(info.may_submit, 0);  // a turn start: the present is on the mover's colour (T5)
  CHECK_EQ(info.rules, std::uint64_t{0});

  // Nothing is left to replay, and saying so changes nothing.
  CHECK_EQ(t5d_game_replay_next(game.p, &error.p), T5D_ERR_ARGUMENT);
  CHECK(error.p != nullptr);
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(g.state));
}

TEST(capi_game_applies_moves_and_submits) {
  const synthetic::Game g = sample_game();
  REQUIRE(g.actions.size() >= 12);
  const std::string record = synthetic::record(g, 10);
  Game game;
  REQUIRE_EQ(t5d_game_new_from_pgn(record.data(), record.size(), T5D_RULES_STANDARD, &game.p, nullptr), T5D_OK);
  REQUIRE_EQ(t5d_game_set_legality(game.p, T5D_PSEUDO_LEGAL, nullptr), T5D_OK);
  for (int a = 0; a < 10; ++a) REQUIRE_EQ(t5d_game_replay_next(game.p, nullptr), T5D_OK);
  GameState s = synthetic::replay(g.start, g.actions, 10);
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));

  // The game's eleventh action, one move at a time.
  const std::vector<Move>& action = g.actions[10];
  REQUIRE(!action.empty());
  Text error;
  int made = 0;
  for (const Move& m : action) {
    const t5d_move r = record_of(m);
    REQUIRE_EQ(t5d_game_apply(game.p, &r, &error.p), T5D_OK);
    s.apply(m);
    ++made;
    CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
    check_summary(game.p, s, made, true);
  }
  // A move applied by hand leaves the record: it cannot be replayed further.
  CHECK_EQ(t5d_game_replay_next(game.p, &error.p), T5D_ERR_ARGUMENT);
  CHECK(error.p != nullptr);
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));

  REQUIRE_EQ(t5d_game_submit(game.p, &error.p), T5D_OK);
  s.submit(CheckRuling::none());
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
  check_summary(game.p, s, 0, true);

  // At the next turn start a submission is refused (T5), and a made-up move is no move.
  // Neither changes the game.
  CHECK_EQ(t5d_game_submit(game.p, &error.p), T5D_ERR_ILLEGAL);
  CHECK(error.has("T5"));
  t5d_move bogus{};  // from timeline 0, half-turn 0, square a1: no board of this game
  CHECK_EQ(t5d_game_apply(game.p, &bogus, &error.p), T5D_ERR_ILLEGAL);
  CHECK(error.p != nullptr);
  CHECK_EQ(t5d_game_apply(game.p, nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_apply(nullptr, &bogus, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_submit(nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
  check_summary(game.p, s, 0, true);
}

// ---- other ways to make a game ---------------------------------------------------------------

TEST(capi_game_from_fen_and_from_a_variant) {
  const char* boards[] = {kStandardBoard};
  Game fen;
  REQUIRE_EQ(t5d_game_new_from_fen(boards, 1, 0, 0, T5D_PARITY_INFER, T5D_TO_MOVE_INFER, T5D_RULES_STANDARD, &fen.p,
                                   nullptr),
             T5D_OK);
  Game standard;
  REQUIRE_EQ(t5d_game_new_variant("Standard", T5D_RULES_STANDARD, &standard.p, nullptr), T5D_OK);
  CHECK_EQ(fingerprint_of(fen.p), fingerprint_of(standard.p));

  // A game that was not made from a record has no record.
  std::size_t total = 9;
  std::size_t played = 9;
  std::int32_t truncated = 9;
  CHECK_EQ(t5d_game_record_actions(standard.p, &total, &played, &truncated), T5D_OK);
  CHECK_EQ(total, std::size_t{0});
  CHECK_EQ(played, std::size_t{0});
  CHECK_EQ(truncated, 0);
  CHECK_EQ(t5d_game_replay_next(standard.p, nullptr), T5D_ERR_ARGUMENT);

  // A ruleset is given by its encoding. Bit 3 is Q4's Guide-literal reading; moves
  // after the shift are the default since R8, so their flag is the default's 0.
  CHECK_EQ(T5D_RULES_MOVES_AFTER_SHIFT, T5D_RULES_STANDARD);
  CHECK_EQ(T5D_RULES_NO_MOVES_AFTER_SHIFT, std::uint64_t{1} << 3);
  Game literal;
  REQUIRE_EQ(t5d_game_new_variant("standard - two timelines", T5D_RULES_NO_MOVES_AFTER_SHIFT, &literal.p, nullptr),
             T5D_OK);  // a variant's name, whatever its case
  t5d_game_summary info = summary_of(literal.p);
  CHECK_EQ(info.rules, T5D_RULES_NO_MOVES_AFTER_SHIFT);
  CHECK(info.ruleset != 0u);
  CHECK_EQ(info.even, 1);
  CHECK_EQ(info.timelines, 2);
  CHECK_EQ(info.active_timelines, 2);
  CHECK_EQ(info.boards, std::uint64_t{2});
  Game ruled;
  REQUIRE_EQ(t5d_game_new_variant("Standard - Two Timelines", T5D_RULES_MOVES_AFTER_SHIFT, &ruled.p, nullptr), T5D_OK);
  info = summary_of(ruled.p);
  CHECK_EQ(info.rules, std::uint64_t{0});
  CHECK_EQ(info.ruleset, 0u);
  CHECK_EQ(fingerprint_of(ruled.p), fingerprint_of(literal.p));  // the same boards under either reading

  // Every ruleset the engine offers goes in by its encoding and comes back by it.
  for (const RuleOptions& rules : t5d_test::every_ruleset()) {
    Game g;
    REQUIRE_EQ(t5d_game_new_variant("Standard", rules.encode(), &g.p, nullptr), T5D_OK);
    CHECK_EQ(summary_of(g.p).rules, rules.encode());
  }

  // A copy is independent of its original.
  Game copy;
  REQUIRE_EQ(t5d_game_copy(standard.p, &copy.p), T5D_OK);
  CHECK_EQ(fingerprint_of(copy.p), fingerprint_of(standard.p));
  const t5d_move e4 = board_move(0, 2, 4, 1, 4, 3);  // (0T1)e2e4
  REQUIRE_EQ(t5d_game_apply(copy.p, &e4, nullptr), T5D_OK);
  CHECK(fingerprint_of(copy.p) != fingerprint_of(standard.p));
  CHECK_EQ(fingerprint_of(standard.p), fingerprint_of(fen.p));
  CHECK_EQ(summary_of(copy.p).moves_made, 1);
  CHECK_EQ(summary_of(standard.p).moves_made, 0);
}

TEST(capi_game_sets_up_the_gen1_variants_as_the_rulebook_gives_them) {
  // docs/rules/README.md section 8, V3. Standard - Turn Zero holds the Standard
  // placement twice: on (0T0) with Black to move, a history board from the start, and
  // on (0T1) with White to move. Half Reflected exchanges Black's king and queen.
  // Defended Pawn exchanges each side's queen and queen's knight.
  struct Expected {
    const char* name;
    std::vector<std::string> boards;
  };
  const std::string standard = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR";
  const std::vector<Expected> expected = {
      {"Standard", {"[" + standard + ":0:1:w]"}},
      {"Standard - Turn Zero", {"[" + standard + ":0:0:b]", "[" + standard + ":0:1:w]"}},
      {"Standard - Defended Pawn", {"[rqbnkbnr/pppppppp/8/8/8/8/PPPPPPPP/RQBNKBNR:0:1:w]"}},
      {"Standard - Half Reflected", {"[rnbkqbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR:0:1:w]"}},
  };
  for (const Expected& e : expected) {
    Game game;
    Text error;
    REQUIRE_EQ(t5d_game_new_variant(e.name, T5D_RULES_STANDARD, &game.p, &error.p), T5D_OK);
    CHECK(boards_of(game.p) == e.boards);
    const t5d_game_summary info = summary_of(game.p);
    CHECK_EQ(info.width, 8);
    CHECK_EQ(info.height, 8);
    CHECK_EQ(info.even, 0);
    CHECK_EQ(info.timelines, 1);
    CHECK_EQ(info.active_timelines, 1);
    CHECK_EQ(info.to_move, 0);  // White moves first, on T1
    CHECK_EQ(info.present, 2);  // T1, White to move
    CHECK_EQ(info.obligations_met, 0);
    CHECK_EQ(info.may_move, 1);
    CHECK_EQ(info.may_submit, 0);
    CHECK_EQ(info.moves_made, 0);
    CHECK_EQ(info.rules, std::uint64_t{0});

    // The engine's own start of the variant, and a record that names it.
    const Variant* variant = find_variant(e.name);
    REQUIRE(variant != nullptr);
    CHECK_EQ(fingerprint_of(game.p), fingerprint_of(variant_start(*variant)));
    const std::string record = std::string("[Board \"") + e.name + "\"]\n[Mode \"5D\"]\n";
    Game from_record;
    REQUIRE_EQ(t5d_game_new_from_pgn(record.data(), record.size(), T5D_RULES_STANDARD, &from_record.p, &error.p),
               T5D_OK);
    CHECK_EQ(fingerprint_of(from_record.p), fingerprint_of(game.p));
  }
}

TEST(capi_game_refuses_what_it_cannot_read_or_play) {
  Game g;
  Text error;
  // A set-up that reads, and move text that does not: the game is made, the record
  // is marked as stopping early, and the replay says why.
  const std::string garbage = "1. (0T1)Qz9";
  REQUIRE_EQ(t5d_game_new_from_pgn(garbage.data(), garbage.size(), T5D_RULES_STANDARD, &g.p, &error.p), T5D_OK);
  std::size_t total = 9;
  std::int32_t truncated = 0;
  CHECK_EQ(t5d_game_record_actions(g.p, &total, nullptr, &truncated), T5D_OK);
  CHECK_EQ(total, std::size_t{0});
  CHECK_EQ(truncated, 1);
  CHECK_EQ(t5d_game_replay_next(g.p, &error.p), T5D_ERR_NOTATION);
  t5d_game_destroy(g.p);
  g.p = nullptr;

  // A game the engine does not play, and a variant it does not know.
  const std::string invasion = "[Board \"Misc - Timeline Invasion\"]\n1. e4\n";
  CHECK_EQ(t5d_game_new_from_pgn(invasion.data(), invasion.size(), T5D_RULES_STANDARD, &g.p, &error.p),
           T5D_ERR_UNSUPPORTED);
  CHECK(g.p == nullptr);
  CHECK_EQ(t5d_game_new_variant("No Such Variant", T5D_RULES_STANDARD, &g.p, &error.p), T5D_ERR_UNSUPPORTED);
  CHECK(g.p == nullptr);

  // A number that is no ruleset's encoding.
  CHECK_THROWS_AS(RuleOptions::decode(std::uint64_t{1} << 21), std::invalid_argument);
  CHECK_EQ(t5d_game_new_variant("Standard", std::uint64_t{1} << 21, &g.p, &error.p), T5D_ERR_ARGUMENT);
  CHECK(g.p == nullptr);
  CHECK(error.p != nullptr);

  // NULL where a pointer is needed.
  CHECK_EQ(t5d_game_new_variant(nullptr, T5D_RULES_STANDARD, &g.p, &error.p), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_new_variant("Standard", T5D_RULES_STANDARD, nullptr, &error.p), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_info(nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_copy(nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_state(nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_record_actions(nullptr, nullptr, nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_replay_next(nullptr, nullptr), T5D_ERR_ARGUMENT);
  t5d_game_destroy(nullptr);  // ignored
}

// ---- moves and submissions ---------------------------------------------------------------------

TEST(capi_game_applies_the_promotion_asked_for) {
  // A moved white pawn on b7 and the promotion set Q R B N (S8, as a Promotions header
  // gives it). b8 can be reached four ways, and t5d_game_apply plays the one it is asked for.
  RuleOptions rules;
  rules.promotions = PromotionSet::of({PieceKind::Queen, PieceKind::Rook, PieceKind::Bishop, PieceKind::Knight});
  const char* boards[] = {"[4k3/1P6/8/8/8/8/8/4K3:0:1:w]"};
  const auto fresh = [&](Game& g) {
    REQUIRE_EQ(t5d_game_new_from_fen(boards, 1, 0, 0, T5D_PARITY_ODD, T5D_TO_MOVE_INFER, rules.encode(), &g.p,
                                     nullptr),
               T5D_OK);
  };
  const auto promote_to = [](PieceKind kind) {
    t5d_move m = board_move(0, 2, 1, 6, 1, 7);  // (0T1)b7b8
    m.promotion = static_cast<std::uint8_t>(index_of(kind) + 1);
    return m;
  };
  const std::pair<PieceKind, const char*> wanted[] = {{PieceKind::Knight, "[1N2k3/"},
                                                      {PieceKind::Bishop, "[1B2k3/"},
                                                      {PieceKind::Rook, "[1R2k3/"},
                                                      {PieceKind::Queen, "[1Q2k3/"}};
  for (const auto& [kind, top_rank] : wanted) {
    Game g;
    fresh(g);
    const t5d_move m = promote_to(kind);
    REQUIRE_EQ(t5d_game_apply(g.p, &m, nullptr), T5D_OK);
    const std::vector<std::string> after = boards_of(g.p);
    REQUIRE_EQ(after.size(), std::size_t{2});
    CHECK_EQ(after[0], std::string("[4k3/1P6/8/8/8/8/8/4K3:0:1:w]"));  // the board played from is history
    CHECK_EQ(after[1].substr(0, 7), std::string(top_rank));
    CHECK_EQ(after[1].substr(7), std::string("8/8/8/8/8/8/4K3:0:1:b]"));
  }
  // A piece the ruleset does not offer, and no piece at all, are no move (S8: a pawn
  // that reaches the last rank promotes).
  Game g;
  fresh(g);
  const std::string before = fingerprint_of(g.p);
  t5d_move m = promote_to(PieceKind::Unicorn);
  CHECK_EQ(t5d_game_apply(g.p, &m, nullptr), T5D_ERR_ILLEGAL);
  m.promotion = 0;
  CHECK_EQ(t5d_game_apply(g.p, &m, nullptr), T5D_ERR_ILLEGAL);
  CHECK_EQ(fingerprint_of(g.p), before);
  CHECK_EQ(summary_of(g.p).moves_made, 0);
}

TEST(capi_game_submissions_are_legal_by_default) {
  // 1. e4 f6 2. Qh5 puts Black's king in check along h5-e8, and 2... a6 leaves it
  // there. A submission that leaves a royal capturable is refused (C4, R1) unless the
  // game was asked to test the present's shift only.
  const std::string record = "1. e4 / f6 2. Qh5 / a6\n";
  for (const std::int32_t legality : {T5D_LEGAL, T5D_PSEUDO_LEGAL}) {
    Game game;
    Text error;
    REQUIRE_EQ(t5d_game_new_from_pgn(record.data(), record.size(), T5D_RULES_STANDARD, &game.p, &error.p), T5D_OK);
    if (legality == T5D_PSEUDO_LEGAL) REQUIRE_EQ(t5d_game_set_legality(game.p, legality, nullptr), T5D_OK);
    for (int a = 0; a < 3; ++a) REQUIRE_EQ(t5d_game_replay_next(game.p, nullptr), T5D_OK);
    const std::string before = fingerprint_of(game.p);
    const t5d_status last = t5d_game_replay_next(game.p, &error.p);
    std::size_t played = 0;
    CHECK_EQ(t5d_game_record_actions(game.p, nullptr, &played, nullptr), T5D_OK);
    if (legality == T5D_LEGAL) {
      CHECK_EQ(last, T5D_ERR_ILLEGAL);
      CHECK(error.has("C4"));
      CHECK_EQ(played, std::size_t{3});
      CHECK_EQ(fingerprint_of(game.p), before);  // a refused action leaves the game as it was
      const t5d_game_summary info = summary_of(game.p);
      CHECK_EQ(info.to_move, 1);
      CHECK_EQ(info.moves_made, 0);
    } else {
      CHECK_EQ(last, T5D_OK);
      CHECK_EQ(played, std::size_t{4});
      CHECK(fingerprint_of(game.p) != before);
    }
  }

  // The same by hand: the move is played, and it is the submission that is refused.
  Game game;
  Text error;
  const std::string opening = "1. e4 / f6 2. Qh5\n";
  REQUIRE_EQ(t5d_game_new_from_pgn(opening.data(), opening.size(), T5D_RULES_STANDARD, &game.p, nullptr), T5D_OK);
  for (int a = 0; a < 3; ++a) REQUIRE_EQ(t5d_game_replay_next(game.p, nullptr), T5D_OK);
  t5d_game_summary info = summary_of(game.p);
  CHECK_EQ(info.to_move, 1);
  CHECK_EQ(info.present, 5);  // T2, Black to move
  CHECK_EQ(info.may_submit, 0);
  const t5d_move a6 = board_move(0, 5, 0, 6, 0, 5);  // (0T2)a7a6
  REQUIRE_EQ(t5d_game_apply(game.p, &a6, &error.p), T5D_OK);
  info = summary_of(game.p);
  CHECK_EQ(info.obligations_met, 1);  // the present has shifted (T5)
  CHECK_EQ(info.may_submit, 0);       // but the king can be taken (C4)
  CHECK_EQ(info.moves_made, 1);
  CHECK_EQ(t5d_game_submit(game.p, &error.p), T5D_ERR_ILLEGAL);
  CHECK(error.has("C4"));
  CHECK_EQ(summary_of(game.p).moves_made, 1);
  REQUIRE_EQ(t5d_game_set_legality(game.p, T5D_PSEUDO_LEGAL, nullptr), T5D_OK);
  CHECK_EQ(summary_of(game.p).may_submit, 1);
  REQUIRE_EQ(t5d_game_submit(game.p, &error.p), T5D_OK);
  CHECK_EQ(summary_of(game.p).to_move, 0);

  // A block of the check may be submitted under either setting: 2... g6.
  Game blocked;
  REQUIRE_EQ(t5d_game_new_from_pgn(opening.data(), opening.size(), T5D_RULES_STANDARD, &blocked.p, nullptr), T5D_OK);
  for (int a = 0; a < 3; ++a) REQUIRE_EQ(t5d_game_replay_next(blocked.p, nullptr), T5D_OK);
  const t5d_move g6 = board_move(0, 5, 6, 6, 6, 5);  // (0T2)g7g6
  REQUIRE_EQ(t5d_game_apply(blocked.p, &g6, nullptr), T5D_OK);
  CHECK_EQ(summary_of(blocked.p).may_submit, 1);
  CHECK_EQ(t5d_game_submit(blocked.p, nullptr), T5D_OK);

  // Only the two settings exist.
  CHECK_EQ(t5d_game_set_legality(game.p, 7, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_game_set_legality(nullptr, T5D_LEGAL, nullptr), T5D_ERR_ARGUMENT);
}

TEST(capi_game_counts_the_moves_in_its_action) {
  // The moves made in the action in progress are the handle's count, since no state
  // records them: 0 at every set-up, one more for each move applied, 0 again after a
  // submission or a replayed action, and carried by a copy.
  //
  // White has timeline 0 at T1, which is in the present and so mandatory, and timeline
  // +1 at T2, which is ahead of the present and so optional (T4, T6).
  const char* boards[] = {"[3k/4/4/K3:0:1:w]", "[3k/4/4/K3:+1:2:w]"};
  const t5d_move on_zero = board_move(0, 2, 0, 0, 0, 1);  // (0T1)Ka1a2
  const t5d_move on_one = board_move(1, 4, 0, 0, 1, 0);   // (+1T2)Ka1b1
  for (const std::uint64_t rules : {T5D_RULES_STANDARD, T5D_RULES_NO_MOVES_AFTER_SHIFT}) {
    const bool moves_after_shift = rules == T5D_RULES_STANDARD;
    Game game;
    REQUIRE_EQ(t5d_game_new_from_fen(boards, 2, 4, 4, T5D_PARITY_INFER, T5D_TO_MOVE_INFER, rules, &game.p, nullptr),
               T5D_OK);
    t5d_game_summary info = summary_of(game.p);
    CHECK_EQ(info.moves_made, 0);
    CHECK_EQ(info.present, 2);
    CHECK_EQ(info.obligations_met, 0);
    CHECK_EQ(info.may_move, 1);

    // A refused submission and a refused move count nothing.
    CHECK_EQ(t5d_game_submit(game.p, nullptr), T5D_ERR_ILLEGAL);
    t5d_move bogus = on_zero;
    bogus.to_square = 63;
    CHECK_EQ(t5d_game_apply(game.p, &bogus, nullptr), T5D_ERR_ILLEGAL);
    CHECK_EQ(summary_of(game.p).moves_made, 0);

    // The mandatory board played: the present shifts to Black's T1 (T5).
    REQUIRE_EQ(t5d_game_apply(game.p, &on_zero, nullptr), T5D_OK);
    info = summary_of(game.p);
    CHECK_EQ(info.moves_made, 1);
    CHECK_EQ(info.present, 3);
    CHECK_EQ(info.obligations_met, 1);
    CHECK_EQ(info.may_submit, 1);
    CHECK_EQ(info.may_move, moves_after_shift ? 1 : 0);  // Q4: by default a move may follow the shift (R8)

    if (moves_after_shift) {
      // A copy carries the count, and from there each handle counts its own moves.
      Game copy;
      REQUIRE_EQ(t5d_game_copy(game.p, &copy.p), T5D_OK);
      CHECK_EQ(summary_of(copy.p).moves_made, 1);
      REQUIRE_EQ(t5d_game_apply(copy.p, &on_one, nullptr), T5D_OK);  // the optional board, after the shift
      CHECK_EQ(summary_of(copy.p).moves_made, 2);
      CHECK_EQ(summary_of(game.p).moves_made, 1);
      REQUIRE_EQ(t5d_game_apply(game.p, &on_one, nullptr), T5D_OK);
      CHECK_EQ(summary_of(game.p).moves_made, 2);
      CHECK_EQ(fingerprint_of(game.p), fingerprint_of(copy.p));
    } else {
      // Under the Guide-literal reading nothing may be played once the present has shifted.
      const std::string before = fingerprint_of(game.p);
      CHECK_EQ(t5d_game_apply(game.p, &on_one, nullptr), T5D_ERR_ILLEGAL);
      CHECK_EQ(summary_of(game.p).moves_made, 1);
      CHECK_EQ(fingerprint_of(game.p), before);
    }

    // Submitted: Black's turn start.
    REQUIRE_EQ(t5d_game_submit(game.p, nullptr), T5D_OK);
    info = summary_of(game.p);
    CHECK_EQ(info.moves_made, 0);
    CHECK_EQ(info.to_move, 1);
    CHECK_EQ(info.obligations_met, 0);
  }

  // A record's replay plays whole actions, so the count is 0 after each. A move by hand
  // counts, and leaves the record.
  const std::string record = "1. e4 / e5 2. Nf3\n";
  Game game;
  REQUIRE_EQ(t5d_game_new_from_pgn(record.data(), record.size(), T5D_RULES_STANDARD, &game.p, nullptr), T5D_OK);
  for (int a = 0; a < 3; ++a) {
    REQUIRE_EQ(t5d_game_replay_next(game.p, nullptr), T5D_OK);
    CHECK_EQ(summary_of(game.p).moves_made, 0);
  }
  const t5d_move nc6 = board_move(0, 5, 1, 7, 2, 5);  // (0T2)Nb8c6
  REQUIRE_EQ(t5d_game_apply(game.p, &nc6, nullptr), T5D_OK);
  CHECK_EQ(summary_of(game.p).moves_made, 1);
  CHECK_EQ(t5d_game_replay_next(game.p, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(summary_of(game.p).moves_made, 1);
}

TEST(capi_game_counts_the_active_timelines) {
  // R2 (T10's count rule): a created timeline is inactive if its creator already had at
  // least one more timeline than the opponent when creating it. White branches twice
  // and Black never, so White's first timeline is active and its second is not, and
  // the present is the earliest head among the active ones only (T4).
  //
  // Half-turns: T1 White 2, T1 Black 3, T2 White 4, T2 Black 5.
  Game game;
  REQUIRE_EQ(t5d_game_new_variant("Standard", T5D_RULES_STANDARD, &game.p, nullptr), T5D_OK);
  GameState s = variant_start(*find_variant("Standard"));
  // Plays one move on the handle and the same generated move on the C++ state.
  const auto play = [&](int l, int h, int file, int rank, int to_l, int to_h, int to_file, int to_rank) {
    t5d_move r{};
    r.from_timeline = l;
    r.from_half_turn = h;
    r.from_square = static_cast<std::int8_t>(make_square(file, rank));
    r.to_timeline = to_l;
    r.to_half_turn = to_h;
    r.to_square = static_cast<std::int8_t>(make_square(to_file, to_rank));
    Text error;
    REQUIRE_EQ(t5d_game_apply(game.p, &r, &error.p), T5D_OK);
    MoveList moves;
    generate_moves(s, moves);
    bool found = false;
    for (const Move& m : moves) {
      if (!found && m.from_timeline == l && m.from_half_turn == h && m.from_square == r.from_square &&
          m.to_timeline == to_l && m.to_half_turn == to_h && m.to_square == r.to_square) {
        s.apply(m);
        found = true;
      }
    }
    REQUIRE(found);
    CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
  };
  const auto submit = [&] {
    REQUIRE_EQ(t5d_game_submit(game.p, nullptr), T5D_OK);
    s.submit();
    CHECK_EQ(fingerprint_of(game.p), fingerprint_of(s));
  };

  play(0, 2, 6, 0, 0, 2, 5, 2);  // 1. (0T1)Ng1f3
  submit();
  play(0, 3, 6, 7, 0, 3, 5, 5);  // 1... (0T1)Ng8f6
  submit();
  // 2. (0T2)Nf3>>(0T1)f5: a jump onto a history board, so a branch (J3). White's first
  // timeline, +1, starts at T1 with Black to move.
  play(0, 4, 5, 2, 0, 2, 5, 4);
  t5d_game_summary info = summary_of(game.p);
  CHECK_EQ(info.timelines, 2);
  CHECK_EQ(info.active_timelines, 2);
  CHECK_EQ(info.present, 3);  // the new board is the earliest head
  CHECK_EQ(info.obligations_met, 1);
  CHECK_EQ(info.boards, std::uint64_t{5});
  submit();
  play(1, 3, 6, 7, 1, 3, 5, 5);  // 2... (+1T1)Ng8f6: Black's one mandatory board
  CHECK_EQ(summary_of(game.p).present, 4);
  submit();
  // 3. (+1T2)Nb1>(0T2)b3: onto timeline 0's T2 board, which is history, so a second
  // branch. White already has one timeline more than Black: +2 is inactive.
  play(1, 4, 1, 0, 0, 4, 1, 2);
  info = summary_of(game.p);
  CHECK_EQ(info.timelines, 3);
  CHECK_EQ(info.active_timelines, 2);
  CHECK_EQ(info.present, 5);
  CHECK_EQ(info.obligations_met, 1);
  CHECK_EQ(info.may_submit, 1);
  CHECK_EQ(info.boards, std::uint64_t{8});
  CHECK(s.active(0));
  CHECK(s.active(1));
  CHECK(!s.active(2));
  check_summary(game.p, s, 1, false);
  submit();
  // Black's turn: timelines 0 and +1 are mandatory. +2 is playable but inactive, so the
  // action may be submitted with it unplayed (T6).
  CHECK_EQ(s.mandatory_count(), 2);
  play(0, 5, 0, 6, 0, 5, 0, 5);  // (0T2)a7a6
  CHECK_EQ(summary_of(game.p).obligations_met, 0);
  play(1, 5, 0, 6, 1, 5, 0, 5);  // (+1T2)a7a6
  info = summary_of(game.p);
  CHECK_EQ(info.obligations_met, 1);
  CHECK_EQ(info.may_submit, 1);
  CHECK_EQ(info.moves_made, 2);
  CHECK_EQ(info.active_timelines, 2);
}

// ---- what a binding checks at load ---------------------------------------------------------------

#define FRACTAL_LAYOUT_AT(T, F)                               \
  do {                                                        \
    std::size_t at = 12345;                                   \
    REQUIRE_EQ(t5d_layout(#T, #F, &at), T5D_OK);              \
    CHECK_EQ(at, offsetof(T, F));                             \
  } while (false)

#define FRACTAL_LAYOUT_SIZE(T)                                \
  do {                                                        \
    std::size_t size = 12345;                                 \
    REQUIRE_EQ(t5d_layout(#T, nullptr, &size), T5D_OK);       \
    CHECK_EQ(size, sizeof(T));                                \
  } while (false)

TEST(capi_layout_reports_the_structs_as_compiled) {
  FRACTAL_LAYOUT_SIZE(t5d_fingerprint);
  FRACTAL_LAYOUT_AT(t5d_fingerprint, fingerprint);
  FRACTAL_LAYOUT_AT(t5d_fingerprint, placement);
  FRACTAL_LAYOUT_AT(t5d_fingerprint, boards);
  FRACTAL_LAYOUT_AT(t5d_fingerprint, pieces);

  FRACTAL_LAYOUT_SIZE(t5d_info);
  FRACTAL_LAYOUT_AT(t5d_info, boards);
  FRACTAL_LAYOUT_AT(t5d_info, pieces);
  FRACTAL_LAYOUT_AT(t5d_info, width);
  FRACTAL_LAYOUT_AT(t5d_info, height);
  FRACTAL_LAYOUT_AT(t5d_info, even);
  FRACTAL_LAYOUT_AT(t5d_info, to_move);
  FRACTAL_LAYOUT_AT(t5d_info, min_timeline);
  FRACTAL_LAYOUT_AT(t5d_info, max_timeline);
  FRACTAL_LAYOUT_AT(t5d_info, created_by_white);
  FRACTAL_LAYOUT_AT(t5d_info, created_by_black);
  FRACTAL_LAYOUT_AT(t5d_info, ruleset);

  FRACTAL_LAYOUT_SIZE(t5d_move);
  FRACTAL_LAYOUT_AT(t5d_move, from_timeline);
  FRACTAL_LAYOUT_AT(t5d_move, from_half_turn);
  FRACTAL_LAYOUT_AT(t5d_move, to_timeline);
  FRACTAL_LAYOUT_AT(t5d_move, to_half_turn);
  FRACTAL_LAYOUT_AT(t5d_move, from_board);
  FRACTAL_LAYOUT_AT(t5d_move, to_board);
  FRACTAL_LAYOUT_AT(t5d_move, from_square);
  FRACTAL_LAYOUT_AT(t5d_move, to_square);
  FRACTAL_LAYOUT_AT(t5d_move, from_cell);
  FRACTAL_LAYOUT_AT(t5d_move, to_cell);
  FRACTAL_LAYOUT_AT(t5d_move, piece);
  FRACTAL_LAYOUT_AT(t5d_move, captured);
  FRACTAL_LAYOUT_AT(t5d_move, promotion);
  FRACTAL_LAYOUT_AT(t5d_move, flags);
  // A move record is six 32-bit numbers and eight bytes, with no padding.
  CHECK_EQ(sizeof(t5d_move), std::size_t{32});
  CHECK_EQ(offsetof(t5d_move, flags), std::size_t{31});

  FRACTAL_LAYOUT_SIZE(t5d_game_summary);
  FRACTAL_LAYOUT_AT(t5d_game_summary, boards);
  FRACTAL_LAYOUT_AT(t5d_game_summary, width);
  FRACTAL_LAYOUT_AT(t5d_game_summary, height);
  FRACTAL_LAYOUT_AT(t5d_game_summary, even);
  FRACTAL_LAYOUT_AT(t5d_game_summary, to_move);
  FRACTAL_LAYOUT_AT(t5d_game_summary, present);
  FRACTAL_LAYOUT_AT(t5d_game_summary, timelines);
  FRACTAL_LAYOUT_AT(t5d_game_summary, active_timelines);
  FRACTAL_LAYOUT_AT(t5d_game_summary, obligations_met);
  FRACTAL_LAYOUT_AT(t5d_game_summary, may_move);
  FRACTAL_LAYOUT_AT(t5d_game_summary, may_submit);
  FRACTAL_LAYOUT_AT(t5d_game_summary, ruleset);
  FRACTAL_LAYOUT_AT(t5d_game_summary, rules);
  FRACTAL_LAYOUT_AT(t5d_game_summary, moves_made);
  // The count of moves was appended after the ruleset's encoding, so every older offset stands.
  CHECK(offsetof(t5d_game_summary, moves_made) >= offsetof(t5d_game_summary, rules) + sizeof(std::uint64_t));

  // What has no layout: the opaque handles, the two structs of the expansion that G0
  // removed with it, an unknown field, and NULL. A failure leaves the output as it was.
  std::size_t v = 7;
  CHECK_EQ(t5d_layout("t5d_state", nullptr, &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout("t5d_game", nullptr, &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout("t5d_expansion", nullptr, &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout("t5d_expand_options", nullptr, &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout("t5d_game_summary", "no_such_field", &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout(nullptr, nullptr, &v), T5D_ERR_ARGUMENT);
  CHECK_EQ(t5d_layout("t5d_move", nullptr, nullptr), T5D_ERR_ARGUMENT);
  CHECK_EQ(v, std::size_t{7});
}

#undef FRACTAL_LAYOUT_AT
#undef FRACTAL_LAYOUT_SIZE

// ---- a ruleset's encoding --------------------------------------------------------------------------

TEST(rule_options_decode_inverts_encode) {
  // The defaults, every alternative alone, the other rule family, a promotion set, and
  // several at once.
  std::vector<RuleOptions> all = t5d_test::every_ruleset();
  {
    RuleOptions r;
    r.family = RuleFamily::ReversedRoyalty;
    all.push_back(r);
    r = RuleOptions{};
    r.promotions = PromotionSet::of({PieceKind::Queen, PieceKind::Knight, PieceKind::Princess});
    all.push_back(r);
    r = RuleOptions{};
    r.moves_after_shift = false;
    r.past_self_blocks = false;
    r.family = RuleFamily::ReversedRoyalty;
    r.promotions = PromotionSet::of({PieceKind::RoyalQueen});
    all.push_back(r);
  }
  std::set<std::uint64_t> encodings;
  for (const RuleOptions& r : all) {
    CHECK(RuleOptions::decode(r.encode()) == r);
    CHECK_EQ(RuleOptions::decode(r.encode()).encode(), r.encode());
    encodings.insert(r.encode());
  }
  CHECK_EQ(encodings.size(), all.size());  // different rulesets, different numbers
  CHECK(RuleOptions::decode(0) == RuleOptions{});
  CHECK_EQ(RuleOptions{}.encode(), std::uint64_t{0});

  // The other way: a number is either a ruleset's encoding, and decodes to the ruleset
  // that encodes to it, or it is refused. Every single bit is tried, and both happen.
  int accepted = 0;
  int refused = 0;
  for (int bit = 0; bit < 64; ++bit) {
    const std::uint64_t encoding = std::uint64_t{1} << bit;
    try {
      const RuleOptions r = RuleOptions::decode(encoding);
      CHECK_EQ(r.encode(), encoding);
      ++accepted;
    } catch (const std::invalid_argument&) {
      ++refused;
    }
  }
  CHECK(accepted > 20);
  CHECK(refused > 0);
  CHECK_THROWS_AS(RuleOptions::decode(std::uint64_t{1} << 21), std::invalid_argument);
}
