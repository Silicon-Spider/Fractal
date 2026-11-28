// tests/test_legality.cpp - Fractal's own tests of the copied legality code: the
// royal-capture scan, check status and the best-first turn generator (spec 3.6).
// New code, written for Fractal: it reads the copied engine through its public
// headers and the copied test helpers, and holds no rule of chess or of 5D movement -
// every expected value here is either the engine's own answer by a second route or a
// pin recorded from the engine as copied.
//
// THE PINS. Two hashes recorded from the engine exactly as copied from Temporal-5D
// (c8a9c49), before any change to copied code: the turn generator's output with its
// node counts, and the check verdicts. A change to copied code that moves either is a
// change in behaviour, not a refactor: either it is a fault, or the pin is recorded
// again on purpose, with the reason, in the gate report (spec 14, G1a). They are the
// same on every CI target: the positions come from the copied synthetic player
// (its own xorshift64*), the scores are integers, and nothing here reads a clock.

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "synthetic.hpp"
#include "t5d/check.hpp"
#include "t5d/move.hpp"
#include "t5d/movegen.hpp"
#include "t5d/multiverse.hpp"
#include "t5d/rules.hpp"
#include "t5d/search.hpp"
#include "t5d/state.hpp"
#include "t5d/turngen.hpp"
#include "t5d/types.hpp"
#include "t5d/variants.hpp"
#include "test_framework.hpp"
#include "test_positions.hpp"
#include "test_support.hpp"

using namespace t5d;

namespace {

// ---- the positions ------------------------------------------------------------------------

struct Sample {
  std::string name;
  GameState state;
};

// Turn starts of seeded pseudo-legal games (the copied synthetic player) from the four
// gen-1 variants and five others - a second royal kind, brawns, smaller boards, two
// and more starting timelines - at several sizes each. Pseudo-legal play leaves royals
// attacked often, so the set holds many positions in check and many with no legal turn.
const std::vector<Sample>& samples() {
  static const std::vector<Sample> all = [] {
    std::vector<Sample> out;
    const char* const variants[] = {
        "Standard",                  "Standard - Turn Zero",     "Standard - Defended Pawn",
        "Standard - Half Reflected", "Standard - Two Timelines", "Standard - Reversed Royalty",
        "Focused - Just Brawns",     "Misc - Timeline Formations", "Small",
    };
    int index = 0;
    for (const char* variant : variants) {
      if (find_variant(variant) == nullptr) {
        throw std::runtime_error(std::string("the engine does not set up ") + variant);
      }
      for (int g = 0; g < 3; ++g) {
        synthetic::Spec spec;
        spec.variant = variant;
        spec.seed = 7001 + 131 * static_cast<std::uint64_t>(index) + 17 * static_cast<std::uint64_t>(g);
        spec.target_active = 1 + (index + g) % 4;
        spec.branch_percent = 30;
        spec.active_branches_only = g != 2;
        spec.target_boards = 40;
        spec.snapshots = {3, 6, 10, 15, 21, 28, 34};
        spec.max_actions = 400;
        const synthetic::Game game = synthetic::play_until_reached(spec, 8);
        const std::string base = std::string(variant) + " seed " + std::to_string(game.seed);
        out.push_back(Sample{base + " start", game.start});
        for (const synthetic::Snapshot& snap : game.snapshots) {
          out.push_back(Sample{base + " at " + std::to_string(snap.target) + " boards", snap.state});
        }
        out.push_back(Sample{base + " end", game.state});
        ++index;
      }
    }
    return out;
  }();
  return all;
}

// ---- a hash of what was seen -------------------------------------------------------------

struct Fnv {
  std::uint64_t h = 0xCBF29CE484222325ull;
  void feed(std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
      h ^= (v >> (8 * i)) & 0xFFu;
      h *= 0x100000001B3ull;
    }
  }
  void feed_int(long long v) noexcept { feed(static_cast<std::uint64_t>(v)); }
};

void feed_move(Fnv& f, const Move& m) {
  f.feed_int(m.from_timeline);
  f.feed_int(m.from_half_turn);
  f.feed_int(m.to_timeline);
  f.feed_int(m.to_half_turn);
  f.feed_int(m.from_square);
  f.feed_int(m.to_square);
  f.feed_int(m.aux_square);
  f.feed_int(m.piece.code());
  f.feed_int(m.captured.code());
  f.feed_int(index_of(m.promotion));
  f.feed_int(m.flags);
}

std::uint64_t mix(std::uint64_t z) noexcept {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

// ---- scorers: integers, in the bands of spec 5.3 --------------------------------------------

int move_rank(const Move& m, int timeline, std::uint64_t seed) {
  std::uint64_t h = seed;
  const auto feed = [&h](long long v) { h = mix(h ^ static_cast<std::uint64_t>(v)); };
  feed(m.from_timeline);
  feed(m.from_half_turn);
  feed(m.to_timeline);
  feed(m.to_half_turn);
  feed(m.from_square);
  feed(m.to_square);
  feed(m.promotes() ? index_of(m.promotion) + 1 : 0);
  feed(m.branching() ? 1 : 0);
  feed(timeline);
  return static_cast<int>(mix(h) % 500);
}

// A move on a mandatory board in [1000, 1500), leaving an optional board 0, a move on
// an optional board in [-2000, -1500), leaving a mandatory board -3000, the arrival
// half of a jump 0.
class BandScorer final : public TurnScorer {
 public:
  BandScorer(const GameState& s, std::uint64_t seed) : s_(s), seed_(seed) {}
  double leave(int timeline) const override { return s_.mandatory(timeline) ? -3000.0 : 0.0; }
  double move(const Move& m, int timeline) const override {
    if (timeline != m.from_timeline) return 0.0;
    const int rank = move_rank(m, timeline, seed_);
    return s_.mandatory(timeline) ? 1000.0 + rank : -2000.0 + rank;
  }

 private:
  const GameState& s_;
  std::uint64_t seed_;
};

// Small integers that tie often: every option in [-3, 0].
class TieScorer final : public TurnScorer {
 public:
  explicit TieScorer(std::uint64_t seed) : seed_(seed) {}
  double leave(int timeline) const override {
    return -static_cast<double>(mix(seed_ ^ static_cast<std::uint64_t>(timeline + 1000)) % 4);
  }
  double move(const Move& m, int timeline) const override {
    return -static_cast<double>(move_rank(m, timeline, seed_) % 4);
  }

 private:
  std::uint64_t seed_;
};

struct Emitted {
  std::vector<std::vector<Move>> moves;
  std::vector<std::uint64_t> keys;
  std::vector<double> scores;
  bool exhausted = false;
  bool stopped = false;
  double bound = 0;
  SearchStats stats;
};

Emitted emit(const GameState& s, const TurnScorer& scorer, std::size_t cap, const SearchBudget& budget) {
  Emitted e;
  TurnGenerator gen(s, scorer, TurnUnit::Successors, budget);
  while (e.moves.size() < cap) {
    std::optional<GeneratedTurn> turn = gen.next();
    if (!turn) break;
    e.moves.push_back(turn->moves);
    e.keys.push_back(turn->result.zobrist());
    e.scores.push_back(turn->score);
  }
  e.exhausted = gen.exhausted();
  e.stopped = gen.stopped();
  e.bound = gen.bound();
  e.stats = gen.stats();
  return e;
}

void feed_emitted(Fnv& f, const Emitted& e) {
  f.feed_int(static_cast<long long>(e.moves.size()));
  for (std::size_t i = 0; i < e.moves.size(); ++i) {
    f.feed_int(static_cast<long long>(e.moves[i].size()));
    for (const Move& m : e.moves[i]) feed_move(f, m);
    f.feed(e.keys[i]);
    f.feed_int(static_cast<long long>(e.scores[i]));
  }
  f.feed_int(e.exhausted ? 1 : 0);
  f.feed_int(e.stopped ? 1 : 0);
  f.feed(e.stats.nodes);
  f.feed(e.stats.points);
  f.feed(e.stats.regions);
  f.feed(e.stats.actions_tested);
  f.feed(e.stats.unrealised);
}

// One random pseudo-legal action from `start`, not submitted; nullopt at a dead end.
std::optional<GameState> random_action(const GameState& start, t5d_test::Rng& rng) {
  MoveList moves;
  for (int attempt = 0; attempt < 6; ++attempt) {
    GameState s = start;
    while (!s.obligations_met()) {
      generate_moves(s, moves);
      if (moves.empty()) break;
      s.apply(moves[static_cast<std::size_t>(rng.below(static_cast<int>(moves.size())))]);
    }
    if (s.obligations_met()) return s;
  }
  return std::nullopt;
}

}  // namespace

// ---- the pins ------------------------------------------------------------------------------

TEST(legality_pin_the_turn_generators_output_and_node_counts) {
  // Every sample, under the banded scorer and under one that ties often: up to 24
  // turns - their moves, successor keys and scores, in the order emitted - then
  // whether the generator was exhausted or stopped and what its search counted.
  Fnv f;
  std::size_t turns = 0;
  std::size_t none = 0;
  const SearchBudget budget = SearchBudget::nodes(200000);
  for (const Sample& sample : samples()) {
    const GameState& s = sample.state;
    f.feed(s.zobrist());
    const BandScorer bands(s, 11);
    const Emitted a = emit(s, bands, 24, budget);
    feed_emitted(f, a);
    const TieScorer ties(13);
    const Emitted b = emit(s, ties, 24, budget);
    feed_emitted(f, b);
    turns += a.moves.size() + b.moves.size();
    if (a.moves.empty()) ++none;
    // The two orders list the same turns where both ran out.
    if (a.exhausted && b.exhausted) CHECK_EQ(a.moves.size(), b.moves.size());
  }
  CHECK_EQ(samples().size(), std::size_t{243});
  CHECK_EQ(turns, std::size_t{10454});
  CHECK_EQ(none, std::size_t{9});
  CHECK_EQ(f.h, std::uint64_t{255262815562744191ull});
}

TEST(legality_pin_the_check_verdicts) {
  // Check status at every sample, and the submission test on eight random
  // pseudo-legal actions from each.
  Fnv f;
  t5d_test::Rng rng(20261117);
  std::size_t in_check_count = 0;
  std::size_t actions = 0;
  std::size_t unsafe = 0;
  for (const Sample& sample : samples()) {
    const GameState& s = sample.state;
    const bool check = in_check(s);
    f.feed_int(check ? 1 : 0);
    if (check) ++in_check_count;
    for (int k = 0; k < 8; ++k) {
      const std::optional<GameState> after = random_action(s, rng);
      if (!after) {
        f.feed_int(2);
        continue;
      }
      const bool safe = royals_safe(*after);
      f.feed_int(safe ? 1 : 0);
      ++actions;
      if (!safe) ++unsafe;
    }
  }
  CHECK_EQ(in_check_count, std::size_t{42});
  CHECK_EQ(actions, std::size_t{1942});
  CHECK_EQ(unsafe, std::size_t{356});
  CHECK_EQ(f.h, std::uint64_t{12584223856404732645ull});
}

// ---- one answer by two routes ---------------------------------------------------------------

TEST(legality_the_scans_agree_on_every_sample) {
  // royal_capture_exists is the search's question; find_royal_capture and
  // for_each_royal_capture answer it with a witness. They must agree - on each
  // sample's passed multiverse (check status) and after random actions (submission).
  t5d_test::Rng rng(5);
  std::size_t compared = 0;
  std::size_t captures = 0;
  const auto agree = [&](const Multiverse& mv, const RuleOptions& rules, Colour attacker,
                         std::span<const int> inactive) {
    const bool exists = royal_capture_exists(mv, rules, attacker, inactive);
    const std::optional<RoyalCapture> witness = find_royal_capture(mv, rules, attacker, inactive);
    CHECK_EQ(exists, witness.has_value());
    bool any = false;
    for_each_royal_capture(mv, rules, attacker, inactive, [&](const RoyalCapture&) {
      any = true;
      return true;
    });
    CHECK_EQ(exists, any);
    ++compared;
    if (exists) ++captures;
  };
  for (const Sample& sample : samples()) {
    const GameState& s = sample.state;
    const Multiverse passed = passed_multiverse(s);
    for (const Colour c : {Colour::White, Colour::Black}) {
      agree(s.multiverse(), s.rules(), c, s.inactive_timelines());
      agree(passed, s.rules(), c, s.inactive_timelines());
    }
    CHECK_EQ(in_check(s), royal_capture_exists(passed, s.rules(), opposite(s.to_move()),
                                              s.inactive_timelines()));
    CHECK_EQ(in_check(s), check_witness(s).has_value());
    for (int k = 0; k < 6; ++k) {
      const std::optional<GameState> after = random_action(s, rng);
      if (!after) continue;
      for (const Colour c : {Colour::White, Colour::Black}) {
        agree(after->multiverse(), after->rules(), c, after->inactive_timelines());
      }
      CHECK_EQ(royals_safe(*after), !submission_witness(*after).has_value());
    }
  }
  CHECK(compared > 2000);
  CHECK(captures > 200);
}

TEST(legality_generator_exhaustion_equals_the_detector) {
  // A generator that runs out with no turn proves the mover has none: the
  // depth-first detector must say mate or stalemate there, and nowhere else. Both
  // run under a node budget - on a wide position without a legal turn the proof can
  // take longer than a test may - and a search its budget stopped is compared with
  // nothing: it proves nothing (spec 5.4).
  std::size_t none = 0;
  std::size_t mates = 0;
  std::size_t undecided = 0;
  const SearchBudget budget = SearchBudget::nodes(200000);
  for (const Sample& sample : samples()) {
    const GameState& s = sample.state;
    const BandScorer bands(s, 3);
    const Emitted e = emit(s, bands, 1, budget);
    const MateResult mate = detect_mate(s, Detector::Hypercuboid, budget);
    CHECK_EQ(mate.verdict == Outcome::Unknown, mate.stats.stopped_by_budget);
    if (e.stopped || mate.verdict == Outcome::Unknown) {
      // Neither may claim a proof it did not finish.
      CHECK(!e.exhausted || !e.stopped);
      if (e.stopped) CHECK(e.moves.empty());
      ++undecided;
      continue;
    }
    if (e.moves.empty()) {
      ++none;
      CHECK(e.exhausted);
      CHECK(mate.verdict == Outcome::Checkmate || mate.verdict == Outcome::Stalemate);
      CHECK_EQ(mate.verdict == Outcome::Checkmate, in_check(s));
      if (mate.verdict == Outcome::Checkmate) ++mates;
    } else {
      CHECK(mate.verdict == Outcome::Ongoing);
      // The generator's turn is a legal action by replay: its moves pseudo-legal in
      // order, the submission safe, the successor the one it carries.
      GameState replayed = s;
      for (const Move& m : e.moves.front()) {
        REQUIRE(is_pseudo_legal(replayed, m));
        replayed.apply(m);
      }
      REQUIRE(replayed.submission_status() == SubmitStatus::Safe);
      replayed.submit();
      CHECK_EQ(replayed.zobrist(), e.keys.front());
    }
  }
  // As copied: 7 samples are proven mate, and on 2 the budget stops both searches.
  CHECK_EQ(none, std::size_t{7});
  CHECK_EQ(mates, std::size_t{7});
  CHECK_EQ(undecided, std::size_t{2});
}

TEST(legality_a_generator_stopped_mid_run_emitted_a_prefix) {
  // Under a node budget too small to finish, a generator is stopped, never
  // exhausted; what it emitted is the start of the full run, and its bound is at
  // least the score of the turn it would have emitted next.
  std::size_t stops = 0;
  std::size_t stops_after_a_turn = 0;
  std::size_t index = 0;
  for (const Sample& sample : samples()) {
    if (index++ % 6 != 0) continue;
    const GameState& s = sample.state;
    const BandScorer bands(s, 29);
    const Emitted full = emit(s, bands, 200, SearchBudget::unlimited());
    for (const std::uint64_t nodes : {1ull, 8ull, 40ull, 90ull, 160ull, 300ull, 700ull}) {
      const Emitted part = emit(s, bands, 200, SearchBudget::nodes(nodes));
      CHECK(!(part.stopped && part.exhausted));
      REQUIRE(part.moves.size() <= full.moves.size());
      for (std::size_t i = 0; i < part.moves.size(); ++i) {
        CHECK(part.moves[i] == full.moves[i]);
        CHECK_EQ(part.keys[i], full.keys[i]);
        CHECK_EQ(part.scores[i], full.scores[i]);
      }
      if (!part.stopped) {
        CHECK_EQ(part.moves.size(), full.moves.size());
        continue;
      }
      ++stops;
      if (!part.moves.empty()) ++stops_after_a_turn;
      CHECK(part.stats.stopped_by_budget);
      if (part.moves.size() < full.moves.size()) CHECK(part.bound >= full.scores[part.moves.size()]);
    }
  }
  CHECK(stops > 30);
  CHECK(stops_after_a_turn > 5);
}

// ---- the set scan against the line scan ---------------------------------------------------

namespace {

// Which pieces a random multiverse favours.
enum class Mix { Any, Pawns, Lines };

// A random multiverse for the scans: one to six timelines of up to `longest` boards,
// few pieces - so that long lines stay open - of every kind, royals plentiful. `mix`
// turns half the other pieces into pawns and brawns, or into rooks, bishops, queens
// and princesses.
Multiverse random_multiverse(t5d_test::Rng& rng, BoardSize size, int longest, int density,
                             Mix mix = Mix::Any) {
  const bool even = rng.chance(50);
  Multiverse mv(size, even);
  const int count = rng.between(1, 6);
  const int first = -rng.below(count);
  for (int i = 0; i < count; ++i) {
    NewTimeline nt;
    nt.position = first + i;
    nt.first_half_turn = rng.between(0, 5);
    const int length = rng.between(1, longest);
    for (int k = 0; k < length; ++k) {
      BoardBuilder b(size);
      for (int sq = 0; sq < kSquareCount; ++sq) {
        if (!size.contains_square(sq) || !rng.chance(density)) continue;
        Piece piece = t5d_test::random_piece(rng);
        if (rng.chance(20)) {
          piece = Piece(rng.chance(70) ? PieceKind::King : PieceKind::RoyalQueen, piece.colour());
        } else if (mix == Mix::Pawns && rng.chance(50)) {
          piece = Piece(rng.chance(50) ? PieceKind::Pawn : PieceKind::Brawn, piece.colour());
        } else if (mix == Mix::Lines && rng.chance(50)) {
          constexpr PieceKind kLines[] = {PieceKind::Rook, PieceKind::Bishop, PieceKind::Queen,
                                          PieceKind::Princess};
          piece = Piece(kLines[rng.below(4)], piece.colour());
        }
        b.put(sq, piece);
      }
      nt.boards.push_back(b.publish());
    }
    if (nt.position != 0 && !(even && nt.position == -1)) {
      nt.creator = nt.position > 0 ? Colour::White : Colour::Black;
      nt.creation_order = nt.position > 0 ? nt.position : -nt.position;
    }
    mv.add_timeline(std::move(nt));
  }
  return mv;
}

// `mv` with every piece of the attacker's taken off the attacker's playable boards,
// but the one on (timeline, square): what is left can capture only from there.
Multiverse only_attacker(const Multiverse& mv, Colour attacker, int timeline, int square) {
  Multiverse out = mv;
  for (const TimelineRef& t : mv.timelines()) {
    if (to_move_at(t->last_half_turn()) != attacker) continue;
    BoardBuilder b(*t->head());
    for (std::uint64_t bits = t->head()->pieces(attacker); bits != 0; bits &= bits - 1) {
      const int sq = std::countr_zero(bits);
      if (t->position() != timeline || sq != square) b.remove(sq);
    }
    out.replace_head(t->position(), b.publish());
  }
  return out;
}

}  // namespace

TEST(legality_the_set_scan_equals_the_line_scan_on_random_multiverses) {
  // royal_capture_exists (the set scan, under the default Q18 and Q19) against
  // find_royal_capture and for_each_royal_capture (the line scan, piece by piece):
  // the same answer on every multiverse, for both colours, under every option either
  // reads - and, capture by capture, with the capturing piece left alone on its
  // side's playable boards, so that no other capture can stand in for one the set
  // scan misses. Boards up to 8x8 and timelines long enough for every distance a
  // slider can cover across boards.
  std::vector<RuleOptions> rulesets(9);
  rulesets[1].turn_zero_reachable = false;
  rulesets[2].inactive_history_is_destination = false;
  rulesets[3].brawn_rank_timeline_capture = false;
  rulesets[4].earlier_playable_jump_branches = true;
  rulesets[5].turn_zero_reachable = false;
  rulesets[5].inactive_history_is_destination = false;
  rulesets[6].sliders_cross_void = true;  // the line scan answers
  rulesets[7].past_self_blocks = false;   // the line scan answers
  // rulesets[8]: the defaults again, so they take two rounds in nine
  t5d_test::Rng rng(20261117);
  long long multiverses = 0;
  long long with_capture = 0;
  long long without = 0;
  long long isolated = 0;
  std::array<long long, kPieceKindCount> by_kind{};
  long long physical = 0;
  long long pawn_jumps = 0;    // a pawn's or brawn's capture onto another board
  long long far_straight = 0;  // a slider on its own square, eight boards away or more
  long long far_compound = 0;  // a slider moving in space too, four boards away or more
  long long branching = 0;
  for (int round = 0; round < 8100; ++round) {
    const RuleOptions& rules = rulesets[static_cast<std::size_t>(round) % rulesets.size()];
    const BoardSize size =
        round % 3 == 0 ? BoardSize{rng.between(3, 8), rng.between(3, 8)} : BoardSize{8, 8};
    const int longest = round % 2 == 0 ? 26 : 9;
    const Mix mix = round % 4 == 1 ? Mix::Pawns : round % 4 == 2 ? Mix::Lines : Mix::Any;
    const int density = round % 5 == 0 ? 22 : mix == Mix::Lines ? rng.between(2, 4) : rng.between(3, 10);
    const Multiverse mv = random_multiverse(rng, size, longest, density, mix);
    std::vector<int> inactive;
    for (const TimelineRef& t : mv.timelines()) {
      if (rng.chance(25)) inactive.push_back(t->position());
    }
    for (const Colour c : {Colour::White, Colour::Black}) {
      std::vector<RoyalCapture> all;
      for_each_royal_capture(mv, rules, c, inactive, [&](const RoyalCapture& rc) {
        all.push_back(rc);
        return false;
      });
      ++multiverses;
      CHECK_EQ(royal_capture_exists(mv, rules, c, inactive), !all.empty());
      CHECK_EQ(find_royal_capture(mv, rules, c, inactive).has_value(), !all.empty());
      if (all.empty()) {
        ++without;
        continue;
      }
      ++with_capture;
      // Up to twelve captures, spread over the list, and every capture of the rarer
      // sorts - by a pawn, brawn, knight, unicorn or dragon, or from far along a
      // board axis - each with its piece alone.
      const std::size_t stride = std::max<std::size_t>(1, all.size() / 12);
      for (std::size_t i = 0; i < all.size(); ++i) {
        const RoyalCapture& rc = all[i];
        const PieceKind kind = rc.attacker.kind();
        const bool rare = kind == PieceKind::Pawn || kind == PieceKind::Brawn ||
                          kind == PieceKind::Knight || kind == PieceKind::Unicorn ||
                          kind == PieceKind::Dragon || (!rc.step.physical() && rc.steps >= 5);
        if (i % stride != 0 && !rare) continue;
        const Multiverse alone = only_attacker(mv, c, rc.from_timeline, rc.from_square);
        CHECK(find_royal_capture(alone, rules, c, inactive).has_value());
        CHECK(royal_capture_exists(alone, rules, c, inactive));
        // And with that piece gone too, nothing of the attacker's captures.
        Multiverse none = alone;
        BoardBuilder b(*alone.head(rc.from_timeline));
        b.remove(rc.from_square);
        none.replace_head(rc.from_timeline, b.publish());
        CHECK(!find_royal_capture(none, rules, c, inactive).has_value());
        CHECK(!royal_capture_exists(none, rules, c, inactive));
        ++isolated;
        ++by_kind[static_cast<std::size_t>(index_of(rc.attacker.kind()))];
        if (rc.step.physical()) ++physical;
        if (!rc.step.physical() && (kind == PieceKind::Pawn || kind == PieceKind::Brawn)) ++pawn_jumps;
        if (!rc.step.physical() && rc.step.x == 0 && rc.step.y == 0 && rc.steps >= 8) ++far_straight;
        if (!rc.step.physical() && (rc.step.x != 0 || rc.step.y != 0) && rc.steps >= 4) ++far_compound;
        if (rc.branching) ++branching;
      }
    }
  }
  // What the comparison covered: every kind as the capturing piece, captures within a
  // board and between boards, near and far, and multiverses with no capture at all.
  // (As recorded: 5,994 with a capture and 10,206 without; 27,770 captures isolated,
  // the fewest by a dragon, 287, and a pawn, 295; 48 from eight boards away or more.)
  CHECK_EQ(multiverses, 16200LL);
  CHECK(with_capture > 4000);
  CHECK(without > 7000);
  CHECK(isolated > 20000);
  for (std::size_t k = 0; k < by_kind.size(); ++k) CHECK(by_kind[k] > 200);
  CHECK(physical > 6000);
  CHECK(pawn_jumps > 250);
  CHECK(far_straight > 30);
  CHECK(far_compound > 300);
  CHECK(branching > 10000);
}

TEST(legality_check_status_read_in_place_equals_the_passed_multiverse) {
  // in_check reads the passed boards in place; passed_multiverse builds them. Along
  // seeded pseudo-legal games from every sample the two must agree at each turn
  // start, and with the line scan's witness.
  std::size_t compared = 0;
  std::size_t checks = 0;
  t5d_test::Rng rng(77);
  for (const Sample& sample : samples()) {
    GameState s = sample.state;
    for (int turn = 0; turn < 6; ++turn) {
      const Multiverse passed = passed_multiverse(s);
      const bool expected =
          find_royal_capture(passed, s.rules(), opposite(s.to_move()), s.inactive_timelines()).has_value();
      CHECK_EQ(in_check(s), expected);
      CHECK_EQ(royal_capture_exists(passed, s.rules(), opposite(s.to_move()), s.inactive_timelines()),
               expected);
      ++compared;
      if (expected) ++checks;
      std::optional<GameState> after = random_action(s, rng);
      if (!after) break;
      after->submit(CheckRuling::none());
      s = std::move(*after);
    }
  }
  CHECK(compared > 1200);
  CHECK(checks > 150);
}

TEST(legality_the_set_scan_reads_a_multiverse_wider_than_its_pass_table) {
  // The set scan marks passed timelines in a table of 256. A multiverse of more
  // timelines than that is still scanned, and check status there goes by the passed
  // multiverse: 300 timelines, every head in the present, both scans and both routes
  // to check status compared.
  t5d_test::Rng rng(300);
  int in_check_count = 0;
  int captures = 0;
  for (int round = 0; round < 12; ++round) {
    const BoardSize size{8, 8};
    Multiverse mv(size, false);
    const int length = 1 + round % 3;
    // Odd rounds leave all but six timelines empty, so that some have no capture.
    const bool sparse = round % 2 == 1;
    for (int p = -150; p < 150; ++p) {
      NewTimeline nt;
      nt.position = p;
      nt.first_half_turn = 2;
      for (int k = 0; k < length; ++k) {
        BoardBuilder b(size);
        for (int sq = 0; sq < kSquareCount; ++sq) {
          if ((sparse && p % 50 != 0) || !rng.chance(sparse ? 3 : 2)) continue;
          Piece piece = t5d_test::random_piece(rng);
          if (rng.chance(25)) piece = Piece(PieceKind::King, piece.colour());
          b.put(sq, piece);
        }
        nt.boards.push_back(b.publish());
      }
      if (p != 0) {
        nt.creator = p > 0 ? Colour::White : Colour::Black;
        nt.creation_order = p > 0 ? p : -p;
      }
      mv.add_timeline(std::move(nt));
    }
    REQUIRE_EQ(mv.timeline_count(), 300);
    for (const Colour c : {Colour::White, Colour::Black}) {
      const bool expected = find_royal_capture(mv, RuleOptions{}, c).has_value();
      CHECK_EQ(royal_capture_exists(mv, RuleOptions{}, c), expected);
      if (expected) ++captures;
    }
    // Every head stands at the same half-turn, so that is the present, and its
    // colour is to move.
    mv.set_to_move(to_move_at(mv.latest_half_turn()));
    const GameState s(mv);
    const bool expected =
        find_royal_capture(passed_multiverse(s), s.rules(), opposite(s.to_move()), s.inactive_timelines())
            .has_value();
    CHECK_EQ(in_check(s), expected);
    if (expected) ++in_check_count;
  }
  // (As recorded: a capture in 7 of the 12 rounds, and 7 in check.)
  CHECK(captures > 5);
  CHECK(in_check_count > 5);
  CHECK(in_check_count < 12);
}

// ---- check status read in place: the two tests of spec 3.3 item 8 (G1a ruling 1) ------------
//
// in_check reads the passed boards in place. The copied line scan on the passed
// multiverse, built board by board, is the reference. Two things the tests above do
// not reach: a pass table of more than one word, and the rules under which the scan
// must know which boards a pass turns into history.

namespace {

// One timeline of a hand-built multiverse: its first half-turn and its boards.
struct Line {
  int first_half_turn = 2;
  std::vector<BoardRef> boards;
};

BoardRef empty_board(BoardSize size) { return BoardBuilder(size).publish(); }

BoardRef board_with(BoardSize size, int square, Piece piece) {
  BoardBuilder b(size);
  b.put(square, piece);
  return b.publish();
}

// The timelines `low`, `low + 1`, ... holding `lines` in order, with `to_move` to move.
// White created the positive ones and Black the negative ones, each in order outward.
// Timeline `low + i` has index i in the set scan's pass table.
Multiverse multiverse_from(BoardSize size, int low, std::vector<Line> lines, Colour to_move) {
  Multiverse mv(size, false);
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const int p = low + static_cast<int>(i);
    NewTimeline nt;
    nt.position = p;
    nt.first_half_turn = lines[i].first_half_turn;
    nt.boards = std::move(lines[i].boards);
    if (p != 0) {
      nt.creator = p > 0 ? Colour::White : Colour::Black;
      nt.creation_order = p > 0 ? p : -p;
    }
    mv.add_timeline(std::move(nt));
  }
  mv.set_to_move(to_move);
  return mv;
}

// As many timelines below 0 as above it, or one more above: every one is active under
// the count rule (T10), since neither player is ever more than one creation ahead.
int balanced_low(int count) { return -((count - 1) / 2); }

// Check status at a turn start by both routes; the line scan's verdict is returned.
bool check_status_by_both_routes(const GameState& s) {
  const Multiverse passed = passed_multiverse(s);
  const Colour attacker = opposite(s.to_move());
  const bool expected = find_royal_capture(passed, s.rules(), attacker, s.inactive_timelines()).has_value();
  CHECK_EQ(in_check(s), expected);
  CHECK_EQ(royal_capture_exists(passed, s.rules(), attacker, s.inactive_timelines()), expected);
  return expected;
}

bool holds(const std::vector<int>& sorted, int value) {
  return std::binary_search(sorted.begin(), sorted.end(), value);
}

}  // namespace

TEST(legality_check_status_read_in_place_at_65_to_256_timelines) {
  // The set scan marks the passed timelines in a table of four 64-bit words, one bit
  // for each timeline by its index. With 65 to 256 timelines, some passed timelines
  // have an index of 64 or more, in the second to fourth word.
  //
  // Half-turns: T1 White 2, T1 Black 3, T2 White 4, T2 Black 5. White is to move and
  // the present is T1, so exactly the timelines whose last board is at half-turn 2 are
  // passed (R1).
  const BoardSize size{8, 8};
  const Piece king(PieceKind::King, Colour::White);
  const Piece rook(PieceKind::Rook, Colour::Black);
  t5d_test::Rng rng(65256);

  // By construction. A white king stands on timeline B and a black rook on the same
  // square of the timeline beside it, one step away along the timeline axis. B's
  // index is 64 or more. `alias` is the timeline of index (B's index mod 64): the one
  // a table that read its first word only would answer for.
  //
  //  - With B passed: B's last board is at half-turn 2, so its passed copy is at 3,
  //    beside the rook's board. The rook takes the king there: check. The alias is not
  //    passed.
  //  - With B not passed: B's last board is at half-turn 4, ahead of the present, and
  //    the rook's is at 5. Nothing stands beside the rook: no check. The alias is passed.
  int with_pass = 0;
  int without_pass = 0;
  for (const int count : {65, 66, 96, 127, 128, 129, 160, 191, 192, 193, 224, 255, 256}) {
    for (const int index : {64, 65, 100, 126, 127, 128, 129, 190, 191, 192, 193, 254}) {
      if (index >= count) continue;
      const int beside = index + 1 < count ? index + 1 : index - 1;
      const int alias = index & 63;
      const int square = rng.below(kSquareCount);
      for (const bool passed : {true, false}) {
        std::vector<Line> lines(static_cast<std::size_t>(count));
        for (Line& line : lines) {
          // Empty boards elsewhere: the last at half-turn 2 (passed) or 3 (not).
          line.boards.push_back(empty_board(size));
          if (rng.chance(50)) line.boards.push_back(empty_board(size));
        }
        const auto at = [&](int i) -> Line& { return lines[static_cast<std::size_t>(i)]; };
        if (passed) {
          at(index).boards = {board_with(size, square, king)};
          at(beside).boards = {empty_board(size), board_with(size, square, rook)};
          at(alias).boards = {empty_board(size), empty_board(size)};
        } else {
          at(index).boards = {empty_board(size), empty_board(size), board_with(size, square, king)};
          at(beside).boards = {empty_board(size), empty_board(size), empty_board(size),
                               board_with(size, square, rook)};
          at(alias).boards = {empty_board(size)};
        }
        const int low = balanced_low(count);
        const GameState s(multiverse_from(size, low, std::move(lines), Colour::White));
        REQUIRE_EQ(s.multiverse().timeline_count(), count);
        REQUIRE(s.inactive_timelines().empty());
        REQUIRE_EQ(s.present(), 2);
        const std::vector<int> passed_now = passed_timelines(s);
        REQUIRE_EQ(holds(passed_now, low + index), passed);
        REQUIRE_EQ(holds(passed_now, low + alias), !passed);
        CHECK_EQ(check_status_by_both_routes(s), passed);
        ++(passed ? with_pass : without_pass);
      }
    }
  }
  CHECK_EQ(with_pass, 89);  // every count with every index it holds
  CHECK_EQ(with_pass, without_pass);

  // And random boards. Pieces stand only on timelines of index 64 and above, a few to
  // a board, so that the verdict rests on those timelines and goes both ways.
  int rounds = 0;
  int checks = 0;
  int high_passes = 0;
  for (int round = 0; round < 80; ++round) {
    const int count = rng.between(65, 256);
    std::vector<Line> lines(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      const int length = 1 + rng.below(2);
      for (int k = 0; k < length; ++k) {
        BoardBuilder b(size);
        if (i >= 64 && rng.chance(12)) {
          for (int n = rng.between(1, 3); n > 0; --n) {
            Piece piece = t5d_test::random_piece(rng);
            if (rng.chance(40)) piece = Piece(PieceKind::King, piece.colour());
            b.put(rng.below(kSquareCount), piece);
          }
        }
        lines[static_cast<std::size_t>(i)].boards.push_back(b.publish());
      }
    }
    lines[0].boards.resize(1);  // a last board at half-turn 2: the present is T1, White's
    const int low = balanced_low(count);
    const GameState s(multiverse_from(size, low, std::move(lines), Colour::White));
    REQUIRE_EQ(s.present(), 2);
    for (const int p : passed_timelines(s)) {
      if (p - low >= 64) ++high_passes;
    }
    if (check_status_by_both_routes(s)) ++checks;
    ++rounds;
  }
  CHECK_EQ(rounds, 80);
  CHECK(high_passes > 1000);
  CHECK(checks > 10);
  CHECK(checks < 70);
}

TEST(legality_check_status_read_in_place_with_q8_or_q20_off) {
  // By default every board is a destination. Under Q8's other reading
  // (turn_zero_reachable = false) a history board at turn zero is none, and under Q20's
  // (inactive_history_is_destination = false) nor is a history board of an inactive
  // timeline. So under either the scan must know which boards are history, and a pass
  // changes that: the passed board becomes history, and its copy is the playable one.
  RuleOptions q8;
  q8.turn_zero_reachable = false;
  RuleOptions q20;
  q20.inactive_history_is_destination = false;
  RuleOptions both = q8;
  both.inactive_history_is_destination = false;
  const std::vector<RuleOptions> readings = {RuleOptions{}, q8, q20, both};
  const BoardSize size{8, 8};
  const int square = make_square(3, 4);
  const int other = make_square(6, 1);

  // Half-turns: T0 White 0, T0 Black 1, T1 White 2, T1 Black 3, T2 White 4.
  for (const RuleOptions& rules : readings) {
    const bool q8_off = !rules.turn_zero_reachable;
    const bool q20_off = !rules.inactive_history_is_destination;
    {
      // A passed copy at turn zero. Timelines -1, 0 and +1 start at T0 with White to
      // move, and the present is T0. Timeline 0 holds a white king, and is passed: its
      // copy stands at T0 with Black to move, beside timeline +1's last board, which
      // holds a black rook on the same square. The copy is the playable board, not
      // history, so the rook takes the king on it under every reading: check.
      std::vector<Line> lines(3);
      lines[0] = Line{0, {empty_board(size)}};
      lines[1] = Line{0, {board_with(size, square, Piece(PieceKind::King, Colour::White))}};
      lines[2] = Line{0, {empty_board(size), board_with(size, square, Piece(PieceKind::Rook, Colour::Black))}};
      const GameState s(multiverse_from(size, -1, std::move(lines), Colour::White), rules);
      REQUIRE_EQ(s.present(), 0);
      REQUIRE(passed_timelines(s) == std::vector<int>({-1, 0}));
      CHECK(check_status_by_both_routes(s));
    }
    {
      // A history board at turn zero. Black is to move: timeline -1's last board is at
      // T0 with Black to move, the present. Timeline 0 runs to T1 with White to move.
      // Its T0 board, long since history, holds a black king, and its last board a
      // white rook on the same square, one turn later. The rook goes back along the
      // time axis and takes the king, unless a history board at turn zero is no
      // destination (Q8).
      std::vector<Line> lines(2);
      lines[0] = Line{0, {empty_board(size), empty_board(size)}};
      lines[1] = Line{0,
                      {board_with(size, square, Piece(PieceKind::King, Colour::Black)), empty_board(size),
                       board_with(size, square, Piece(PieceKind::Rook, Colour::White))}};
      const GameState s(multiverse_from(size, -1, std::move(lines), Colour::Black), rules);
      REQUIRE_EQ(s.present(), 1);
      REQUIRE(passed_timelines(s) == std::vector<int>({-1}));
      CHECK_EQ(check_status_by_both_routes(s), !q8_off);
    }
    {
      // A history board at turn zero again, with a pass at turn zero beside it. White
      // is to move at T0, and timeline 0 is passed; its copy, at T0 with Black to move,
      // is empty. Timeline +1 has run to T1 with Black to move. Its last board holds a
      // black rook, and its T0 board with Black to move, which is history, a white
      // king on the same square. The rook goes back one turn and takes it, unless a
      // history board at turn zero is no destination (Q8).
      std::vector<Line> lines(2);
      lines[0] = Line{0, {empty_board(size)}};
      lines[1] = Line{0,
                      {empty_board(size), board_with(size, other, Piece(PieceKind::King, Colour::White)),
                       empty_board(size), board_with(size, other, Piece(PieceKind::Rook, Colour::Black))}};
      const GameState s(multiverse_from(size, 0, std::move(lines), Colour::White), rules);
      REQUIRE_EQ(s.present(), 0);
      REQUIRE(passed_timelines(s) == std::vector<int>({0}));
      CHECK_EQ(check_status_by_both_routes(s), !q8_off);
    }
    {
      // A history board of an inactive timeline. White created +1 and +2 and Black
      // nothing, so +2 is inactive (R2). Black is to move, at T1. Timeline +2 runs to
      // T2 with White to move: its last board holds a white rook, and its T1 board,
      // history, a black king on the same square. The rook goes back one turn and takes
      // it, unless a history board of an inactive timeline is no destination (Q20).
      std::vector<Line> lines(3);
      lines[0] = Line{2, {empty_board(size), empty_board(size)}};
      lines[1] = Line{2, {empty_board(size), empty_board(size)}};
      lines[2] = Line{2,
                      {board_with(size, square, Piece(PieceKind::King, Colour::Black)), empty_board(size),
                       board_with(size, square, Piece(PieceKind::Rook, Colour::White))}};
      const GameState s(multiverse_from(size, 0, std::move(lines), Colour::Black), rules);
      REQUIRE_EQ(s.present(), 3);
      REQUIRE(s.inactive_timelines() == std::vector<int>({2}));
      REQUIRE(passed_timelines(s) == std::vector<int>({0, 1}));
      CHECK_EQ(check_status_by_both_routes(s), !q20_off);
    }
  }

  // And random multiverses, some with boards at turn zero and some with inactive
  // timelines, under each of the three readings. Timeline 0 holds the earliest last
  // board, so the present is there whatever is inactive.
  t5d_test::Rng rng(820);
  int compared = 0;
  int checks = 0;
  int differ_from_default = 0;
  int passes_at_turn_zero = 0;
  int with_inactive = 0;
  for (int round = 0; round < 3000; ++round) {
    const int count = rng.between(2, 7);
    const int first = rng.below(3);
    const int present = first + rng.below(3);
    // Balanced, or all created by one player, which leaves all but one of them inactive.
    const int shape = rng.below(3);
    const int low = shape == 0 ? balanced_low(count) : shape == 1 ? 0 : -(count - 1);
    std::vector<Line> lines(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
      const bool central = low + i == 0;
      const int last = central ? present : present + rng.below(3);
      Line& line = lines[static_cast<std::size_t>(i)];
      line.first_half_turn = first;
      for (int h = first; h <= last; ++h) {
        BoardBuilder b(size);
        for (int n = rng.below(3); n > 0; --n) {
          Piece piece = t5d_test::random_piece(rng);
          if (rng.chance(50)) piece = Piece(PieceKind::King, piece.colour());
          b.put(rng.below(kSquareCount), piece);
        }
        line.boards.push_back(b.publish());
      }
    }
    const RuleOptions& rules = readings[static_cast<std::size_t>(1 + round % 3)];
    const GameState s(multiverse_from(size, low, std::move(lines), to_move_at(present)), rules);
    REQUIRE_EQ(s.present(), present);
    const bool expected = check_status_by_both_routes(s);
    ++compared;
    if (expected) ++checks;
    // The same boards under the default readings: where the answer differs, Q8 or Q20
    // decided it.
    const bool by_default = find_royal_capture(passed_multiverse(s), RuleOptions{}, opposite(s.to_move()),
                                               s.inactive_timelines())
                                .has_value();
    if (by_default != expected) ++differ_from_default;
    if (present <= 1 && !passed_timelines(s).empty()) ++passes_at_turn_zero;
    if (!s.inactive_timelines().empty()) ++with_inactive;
  }
  CHECK_EQ(compared, 3000);
  CHECK(checks > 300);
  CHECK(checks < 2700);
  CHECK(differ_from_default > 20);
  CHECK(passes_at_turn_zero > 500);
  CHECK(with_inactive > 500);
}
