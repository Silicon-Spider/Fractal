// Fractal provenance: copied from Temporal-5D (https://github.com/Silicon-Spider/temporal-5d)
//   source: engine/src/check.cpp
//   commit: c8a9c490bfde650466609531f8083d69513c6036
//   copied: 2026-11-08 (Fractal spec 3.1 and 3.4; decisions NR1, NR5)
//   state:  changed: a set scan answers royal_capture_exists and in_check under the default Q18/Q19 (spec 3.3); see PROVENANCE.md
// Check status and submission legality (see t5d/check.hpp). Rule IDs refer to
// the rulebook, docs/rules/README.md; the definitions are its ruling R1
// (register H36). Written from the rulebook and the design spec; no other
// implementation was consulted.

#include "t5d/check.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "t5d/board.hpp"
#include "t5d/search.hpp"
#include "t5d/timeline.hpp"

namespace t5d {

namespace {

// ---- the scan: the move generator's capture semantics, one capture at a time ------
//
// The generator's rules (movegen.cpp) restated for captures only: what a piece
// of the attacker's can capture from its board. Each function returns whether
// the visitor asked to stop.

struct Scan {
  const Multiverse& mv;
  const RuleOptions& rules;
  std::span<const int> inactive;
  Colour attacker;
  BoardSize size;
  // The bounds of every board (timelines are contiguous; half-turns >= 0): a
  // slide that leaves them can never meet a board again.
  int min_timeline = 0;
  int max_timeline = -1;
  int max_half_turn = -1;
  // The source: timeline p's playable board, at half-turn h.
  int p = 0;
  int h = 0;
  const Board* board = nullptr;
};

// A board at a place, or void (board == nullptr), and whether it is a
// history board (L5: not the latest of its timeline).
struct Spot {
  const Board* board = nullptr;
  bool history = false;
  int half_turn = 0;
};

inline Spot spot_at(const Scan& s, int timeline, int half_turn) noexcept {
  if (timeline < s.min_timeline || timeline > s.max_timeline) return Spot{};
  const Timeline* t = s.mv.timeline(timeline);
  if (!t->contains(half_turn)) return Spot{};
  return Spot{t->board_at(half_turn), half_turn != t->last_half_turn(), half_turn};
}

// Whether a piece may land on `spot` (J2): any board but, under the ruleset's
// alternatives, a turn-zero history board (Q8) or a history board of an
// inactive timeline (Q20).
inline bool landable(const Scan& s, const Spot& spot, int timeline) noexcept {
  if (!spot.history) return true;
  if (!s.rules.turn_zero_reachable && turn_of(spot.half_turn) == 0) return false;
  if (!s.rules.inactive_history_is_destination &&
      std::binary_search(s.inactive.begin(), s.inactive.end(), timeline)) {
    return false;
  }
  return true;
}

// J3: a move between boards branches iff it lands on a history board - or,
// under the Quick Rules' literal reading, at an earlier turn than it started.
inline bool branches(const Scan& s, const Spot& spot, bool physical) noexcept {
  if (physical) return false;
  return spot.history || (s.rules.earlier_playable_jump_branches && spot.half_turn < s.h);
}

inline RoyalCapture capture_of(const Scan& s, int from, Piece attacker, Vec4 step, int steps,
                               int to_timeline, const Spot& spot, int to, Piece royal) noexcept {
  RoyalCapture c;
  c.attacker = attacker;
  c.from_timeline = s.p;
  c.from_half_turn = s.h;
  c.from_square = from;
  c.royal = royal;
  c.to_timeline = to_timeline;
  c.to_half_turn = spot.half_turn;
  c.to_square = to;
  c.step = step;
  c.steps = steps;
  c.branching = branches(s, spot, step.physical());
  return c;
}

// A leap, a king's step or a pawn's capture: only the destination matters
// (4.4, 4.5, 4.6, 4.7; J8: knights leap over void and pieces alike).
template <class Visit>
bool leap(const Scan& s, int from, Piece piece, Vec4 d, Visit& visit) {
  const int x = file_of(from) + d.x;
  const int y = rank_of(from) + d.y;
  if (!s.size.contains(x, y)) return false;
  const Spot spot =
      d.physical() ? Spot{s.board, false, s.h} : spot_at(s, s.p + d.l, s.h + 2 * d.t);
  if (spot.board == nullptr || !landable(s, spot, s.p + d.l)) return false;  // J2, L6
  const int to = make_square(x, y);
  const Piece occupant = spot.board->at(to);
  if (occupant.empty() || occupant.colour() == s.attacker || !occupant.royal()) return false;
  return visit(capture_of(s, from, piece, d, 1, s.p + d.l, spot, to, occupant));
}

// A slide (J7, J8; Q18, Q19): one unit per moved axis per step, over boards of
// the attacker's colour; every square crossed empty; void blocks unless
// sliders_cross_void; the slide ends on the first piece.
template <class Visit>
bool slide(const Scan& s, int from, Piece piece, Vec4 d, Visit& visit) {
  const bool physical = d.physical();
  // Q19: only a slide straight along T on its own square and timeline can meet
  // the piece's own earlier self.
  const bool past_self_passes = !s.rules.past_self_blocks && d.x == 0 && d.y == 0 && d.l == 0;
  int x = file_of(from);
  int y = rank_of(from);
  int l = s.p;
  int ht = s.h;
  for (int n = 1;; ++n) {
    x += d.x;
    y += d.y;
    l += d.l;
    ht += 2 * d.t;
    if (!s.size.contains(x, y)) return false;
    Spot spot{s.board, false, s.h};
    if (!physical) {
      if (l < s.min_timeline || l > s.max_timeline || ht < 0 || ht > s.max_half_turn) {
        return false;  // beyond every board
      }
      spot = spot_at(s, l, ht);
      if (spot.board == nullptr) {
        if (s.rules.sliders_cross_void) continue;  // Q18's dissent
        return false;                              // J8: void blocks
      }
    }
    const int to = make_square(x, y);
    const Piece occupant = spot.board->at(to);
    if (occupant.empty()) continue;
    if (occupant.colour() == s.attacker) {
      if (past_self_passes && occupant.kind() == piece.kind()) continue;
      return false;
    }
    if (!occupant.royal() || !landable(s, spot, l)) return false;
    return visit(capture_of(s, from, piece, d, n, l, spot, to, occupant));
  }
}

constexpr Vec4 v4(int x, int y, int l, int t) noexcept {
  return Vec4{static_cast<std::int8_t>(x), static_cast<std::int8_t>(y),
              static_cast<std::int8_t>(l), static_cast<std::int8_t>(t)};
}

// Every capture a piece can make (only royal ones reach the visitor).
template <class Visit>
bool piece_captures(const Scan& s, int square, Piece piece, Visit& visit) {
  switch (piece.kind()) {
    case PieceKind::Pawn:
    case PieceKind::Brawn: {
      // 4.6: forward rank and +-1 file; forward timeline and +-1 turn. 4.7: a
      // brawn also forward rank and +-1 turn, forward timeline and +-1 file,
      // and - Q14, the Guide's literal reading - the two forward axes
      // together. Forward moves never capture; en passant takes only a pawn
      // or a brawn, never a royal (S7).
      const int fy = geometry::forward_rank(s.attacker);
      const int fl = geometry::forward_timeline(s.attacker);
      const Vec4 pawn[] = {v4(-1, fy, 0, 0), v4(1, fy, 0, 0), v4(0, 0, fl, -1), v4(0, 0, fl, 1)};
      for (const Vec4 v : pawn) {
        if (leap(s, square, piece, v, visit)) return true;
      }
      if (piece.kind() == PieceKind::Brawn) {
        const Vec4 brawn[] = {v4(0, fy, 0, -1), v4(0, fy, 0, 1), v4(-1, 0, fl, 0),
                              v4(1, 0, fl, 0)};
        for (const Vec4 v : brawn) {
          if (leap(s, square, piece, v, visit)) return true;
        }
        if (s.rules.brawn_rank_timeline_capture && leap(s, square, piece, v4(0, fy, fl, 0), visit)) {
          return true;
        }
      }
      return false;
    }
    case PieceKind::Knight:
      for (const Vec4 v : geometry::knight_leaps()) {
        if (leap(s, square, piece, v, visit)) return true;
      }
      return false;
    case PieceKind::King:
    case PieceKind::CommonKing:
      // 4.4, 4.12: one step along any set of axes. Castling never captures.
      for (const Vec4 v : geometry::all_directions()) {
        if (leap(s, square, piece, v, visit)) return true;
      }
      return false;
    default:
      for (const Vec4 v : geometry::slider_directions(piece.kind())) {
        if (slide(s, square, piece, v, visit)) return true;
      }
      return false;
  }
}

// Whether `colour` has a royal piece anywhere (C13: without one it can be
// neither captured nor checked).
bool has_royal(const Multiverse& mv, Colour colour) {
  for (const TimelineRef& t : mv.timelines()) {
    for (const BoardRef& b : t->boards()) {
      if (b->royals(colour) != 0) return true;
    }
  }
  return false;
}

template <class Visit>
bool scan(const Multiverse& mv, const RuleOptions& rules, Colour attacker,
          std::span<const int> inactive, Visit& visit) {
  if (mv.empty() || !has_royal(mv, opposite(attacker))) return false;
  Scan s{mv, rules, inactive, attacker, mv.board_size(), mv.min_timeline(), mv.max_timeline(),
         mv.latest_half_turn(), 0, 0, nullptr};
  for (const TimelineRef& t : mv.timelines()) {
    // L5: the attacker's playable boards - the latest of each timeline, with
    // the attacker to move on it.
    if (to_move_at(t->last_half_turn()) != attacker) continue;
    s.p = t->position();
    s.h = t->last_half_turn();
    s.board = t->head().get();
    for (std::uint64_t bits = s.board->pieces(attacker); bits != 0; bits &= bits - 1) {
      const int square = std::countr_zero(bits);
      if (piece_captures(s, square, s.board->at(square), visit)) return true;
    }
  }
  return false;
}

// ---- the set scan: whether a royal capture exists ---------------------------------------
//
// Fractal's addition (docs/gates/G1a-rules-review.md): the search asks only WHETHER
// a royal can be captured, at every turn it tests and every node it visits, and
// the scan above answers by walking every attacking piece's every line. This one
// asks the same question from the other end, a board at a time: for each of the
// attacker's playable boards, and each board a move from it can reach, which of
// the attacker's pieces stand where a capture of that board's royals starts.
//
//  * Within the board: from each royal's square, the leapers' patterns and the
//    first piece along each of the eight lines.
//  * Between boards: a move's board displacement is one of eight directions
//    (dL, dT) repeated n times, and after n steps the piece is n squares along
//    each spatial axis it moves on. So for the n-th board along a direction, a
//    royal on square r is captured by a slider on r itself (no spatial axis),
//    on r's file or rank at distance n (one), or on a diagonal at distance n
//    (two) - the slider's kind fixed by the number of axes moved - if every
//    square between is empty. The sliders that keep their square walk together
//    as one set, losing the squares the boards they cross occupy.
//  * Leapers between boards: the king's step and the pawn's and brawn's captures
//    are the first step of those walks; the knight's sixteen board displacements
//    are read apart.
//
// It reads the rules it serves from the scan above and must give its answer
// exactly: the tests hold the two equal (tests/test_legality.cpp, and the copied
// test_check.cpp through the move generator). It is written for the default
// reading of Q18 and Q19 (void blocks; a piece's earlier self blocks); under
// either alternative the scan above answers instead.

constexpr std::uint64_t bit_at(int x, int y) noexcept {
  return x >= 0 && x < 8 && y >= 0 && y < 8 ? square_bit(make_square(x, y)) : 0;
}

struct ScanTables {
  // [square]: the squares a knight's leap, a king's step away within a board.
  std::array<std::uint64_t, 64> knight{};
  std::array<std::uint64_t, 64> king{};
  // [n][square]: the squares exactly n away along the file or rank; along a diagonal.
  std::array<std::array<std::uint64_t, 64>, 8> straight{};
  std::array<std::array<std::uint64_t, 64>, 8> diagonal{};
  // [direction][square]: every square along a line within a board. Directions
  // 0-3 run towards higher squares, 4-7 towards lower; 0, 1, 4, 5 are the
  // rook's and 2, 3, 6, 7 the bishop's.
  std::array<std::array<std::uint64_t, 64>, 8> ray{};
  // [attacker][square]: where a pawn or brawn of the attacker's stands to capture
  // on the square within its board (4.6); the square one rank behind it.
  std::array<std::array<std::uint64_t, 64>, 2> pawn_from{};
  std::array<std::array<std::uint64_t, 64>, 2> behind{};
  // [square]: the squares beside it on its rank.
  std::array<std::uint64_t, 64> beside{};
};

constexpr ScanTables make_scan_tables() noexcept {
  ScanTables t{};
  constexpr int kRayX[8] = {1, 0, 1, -1, -1, 0, -1, 1};
  constexpr int kRayY[8] = {0, 1, 1, 1, 0, -1, -1, -1};
  for (int sq = 0; sq < 64; ++sq) {
    const int x = file_of(sq);
    const int y = rank_of(sq);
    const auto s = static_cast<std::size_t>(sq);
    for (const int a : {-2, 2}) {
      for (const int b : {-1, 1}) t.knight[s] |= bit_at(x + a, y + b) | bit_at(x + b, y + a);
    }
    for (int dx = -1; dx <= 1; ++dx) {
      for (int dy = -1; dy <= 1; ++dy) {
        if (dx != 0 || dy != 0) t.king[s] |= bit_at(x + dx, y + dy);
      }
    }
    for (int n = 1; n < 8; ++n) {
      const auto k = static_cast<std::size_t>(n);
      t.straight[k][s] = bit_at(x - n, y) | bit_at(x + n, y) | bit_at(x, y - n) | bit_at(x, y + n);
      t.diagonal[k][s] = bit_at(x - n, y - n) | bit_at(x + n, y - n) | bit_at(x - n, y + n) |
                         bit_at(x + n, y + n);
    }
    for (std::size_t d = 0; d < 8; ++d) {
      for (int n = 1; n < 8; ++n) t.ray[d][s] |= bit_at(x + n * kRayX[d], y + n * kRayY[d]);
    }
    for (const Colour c : {Colour::White, Colour::Black}) {
      const auto ci = static_cast<std::size_t>(index_of(c));
      const int fy = geometry::forward_rank(c);
      t.pawn_from[ci][s] = bit_at(x - 1, y - fy) | bit_at(x + 1, y - fy);
      t.behind[ci][s] = bit_at(x, y - fy);
    }
    t.beside[s] = bit_at(x - 1, y) | bit_at(x + 1, y);
  }
  return t;
}

constexpr ScanTables kScan = make_scan_tables();

// The boards a set scan reads: a multiverse and, for check status, one more
// board at the end of each passed timeline - its head again, as a pass moves
// nothing (R1; what passed_multiverse builds, read in place).
struct View {
  const Multiverse& mv;
  int min_timeline = 0;
  int max_timeline = -1;
  int max_half_turn = -1;
  // Bit (timeline - min_timeline): the timeline is advanced by a pass. Read only
  // once a timeline has been passed, and none is passed in a multiverse of more
  // than kMostTimelines (in_check builds the passed multiverse there instead), so
  // a view with no pass reads a multiverse of any size.
  std::array<std::uint64_t, 4> passed{};
  bool any_passed = false;

  static constexpr int kMostTimelines = 256;

  bool is_passed(int timeline) const noexcept {
    if (!any_passed) return false;
    const auto i = static_cast<unsigned>(timeline - min_timeline);
    return ((passed[i >> 6] >> (i & 63u)) & 1u) != 0;
  }
  void pass(int timeline) noexcept {
    const auto i = static_cast<unsigned>(timeline - min_timeline);
    passed[i >> 6] |= std::uint64_t{1} << (i & 63u);
    any_passed = true;
  }
  // The timeline's last half-turn, as scanned.
  int last_half_turn(const Timeline& t) const noexcept {
    return t.last_half_turn() + (is_passed(t.position()) ? 1 : 0);
  }
  // The board at half-turn h of `t`, or nullptr; `history`: it is not the last.
  const Board* board_at(const Timeline& t, int h, bool& history) const noexcept {
    const int last = t.last_half_turn();
    if (h <= last) {
      if (h < t.first_half_turn()) return nullptr;
      history = h != last || is_passed(t.position());
      return t.boards()[static_cast<std::size_t>(h - t.first_half_turn())].get();
    }
    if (h == last + 1 && is_passed(t.position())) {
      history = false;
      return t.head().get();
    }
    return nullptr;
  }
};

struct SetScan {
  const View& v;
  const RuleOptions& rules;
  std::span<const int> inactive;
  Colour attacker;
  // Every board is a destination (the defaults of Q8 and Q20).
  bool lands_anywhere = true;

  // J2 with Q8 and Q20, as landable() above.
  bool landable(int timeline, int half_turn, bool history) const noexcept {
    if (lands_anywhere || !history) return true;
    if (!rules.turn_zero_reachable && turn_of(half_turn) == 0) return false;
    if (!rules.inactive_history_is_destination &&
        std::binary_search(inactive.begin(), inactive.end(), timeline)) {
      return false;
    }
    return true;
  }

  // Whether a piece of the attacker's on `board` - timeline p's last, at
  // half-turn h - can capture a royal.
  bool from_board(const Timeline& home, int p, int h, const Board& board) const noexcept {
    const Colour defender = opposite(attacker);
    const std::uint64_t mine = board.pieces(attacker);
    if (mine == 0) return false;
    const auto side = static_cast<std::size_t>(index_of(attacker));
    const std::uint64_t queens = board.pieces(PieceKind::Queen) | board.pieces(PieceKind::RoyalQueen);
    // The sliders by the number of axes a step moves along (4.1-4.3, 4.8-4.11).
    const std::array<std::uint64_t, 5> lines = {
        0,
        mine & (board.pieces(PieceKind::Rook) | board.pieces(PieceKind::Princess) | queens),
        mine & (board.pieces(PieceKind::Bishop) | board.pieces(PieceKind::Princess) | queens),
        mine & (board.pieces(PieceKind::Unicorn) | queens),
        mine & (board.pieces(PieceKind::Dragon) | queens),
    };
    const std::uint64_t knights = mine & board.pieces(PieceKind::Knight);
    const std::uint64_t kings =
        mine & (board.pieces(PieceKind::King) | board.pieces(PieceKind::CommonKing));
    const std::uint64_t brawns = mine & board.pieces(PieceKind::Brawn);
    const std::uint64_t pawns = (mine & board.pieces(PieceKind::Pawn)) | brawns;

    // -- within the board
    if (const std::uint64_t royals = board.royals(defender); royals != 0) {
      const std::uint64_t occupied = board.occupied();
      for (std::uint64_t bits = royals; bits != 0; bits &= bits - 1) {
        const auto r = static_cast<std::size_t>(std::countr_zero(bits));
        if ((kScan.knight[r] & knights) != 0 || (kScan.king[r] & kings) != 0 ||
            (kScan.pawn_from[side][r] & pawns) != 0) {
          return true;
        }
        if ((lines[1] | lines[2]) == 0) continue;
        for (std::size_t d = 0; d < 8; ++d) {
          const std::uint64_t blockers = kScan.ray[d][r] & occupied;
          if (blockers == 0) continue;
          const int first = d < 4 ? std::countr_zero(blockers) : 63 - std::countl_zero(blockers);
          const bool rook_line = d == 0 || d == 1 || d == 4 || d == 5;
          if ((square_bit(first) & lines[rook_line ? 1 : 2]) != 0) return true;
        }
      }
    }

    // -- between boards: the eight board directions
    const int fl = geometry::forward_timeline(attacker);
    const bool any_slider = (lines[1] | lines[2] | lines[3] | lines[4]) != 0;
    if (any_slider || kings != 0 || pawns != 0) {
      for (int dl = -1; dl <= 1; ++dl) {
        for (int dt = -1; dt <= 1; ++dt) {
          if (dl == 0 && dt == 0) continue;
          const std::size_t board_axes = dl != 0 && dt != 0 ? 2 : 1;
          const std::uint64_t straight = lines[board_axes + 1];
          const std::uint64_t diagonal = lines[board_axes + 2];
          // The sliders that keep their squares, their lines still open.
          std::uint64_t alive = lines[board_axes];
          // The first step's leapers: the king along any axes (4.4, 4.12); the
          // pawn and the brawn along the forward timeline and a turn (4.6); the
          // brawn's own captures (4.7; Q14).
          const bool pawn_step = dl == fl && dt != 0;
          const bool brawn_turn = dl == 0;              // forward rank, a turn either way
          const bool brawn_line = dl == fl && dt == 0;  // forward timeline: a file, or the rank
          const bool leapers = kings != 0 || (pawn_step && pawns != 0) ||
                               ((brawn_turn || brawn_line) && brawns != 0);
          if (alive == 0 && straight == 0 && diagonal == 0 && !leapers) continue;
          std::array<std::uint64_t, 8> crossed{};  // [i]: what board i of the walk occupies
          const Timeline* t = &home;
          for (int n = 1;; ++n) {
            const int l = p + n * dl;
            const int ht = h + 2 * n * dt;
            if (l < v.min_timeline || l > v.max_timeline || ht < 0 || ht > v.max_half_turn) break;
            if (dl != 0) t = v.mv.timeline(l);
            bool history = false;
            const Board* target = v.board_at(*t, ht, history);
            if (target == nullptr) break;  // J8: void blocks
            const std::uint64_t royals = target->royals(defender);
            if (royals != 0 && landable(l, ht, history)) {
              if ((alive & royals) != 0) return true;
              const bool compound = n < 8 && (straight | diagonal) != 0;
              if (n == 1 ? (leapers || compound) : compound) {
                const auto k = static_cast<std::size_t>(n);
                for (std::uint64_t bits = royals; bits != 0; bits &= bits - 1) {
                  const int square = std::countr_zero(bits);
                  const auto r = static_cast<std::size_t>(square);
                  if (n == 1) {
                    if (((kScan.king[r] | square_bit(square)) & kings) != 0) return true;
                    if (pawn_step && (square_bit(square) & pawns) != 0) return true;
                    if (brawn_turn && (kScan.behind[side][r] & brawns) != 0) return true;
                    if (brawn_line && (kScan.beside[r] & brawns) != 0) return true;
                    if (brawn_line && rules.brawn_rank_timeline_capture &&
                        (kScan.behind[side][r] & brawns) != 0) {
                      return true;
                    }
                    if (!compound) continue;
                  }
                  // A slider n squares away along a file, rank or diagonal: every
                  // square between, one on each board crossed, empty (J7).
                  for (std::uint64_t from = (kScan.straight[k][r] & straight) |
                                            (kScan.diagonal[k][r] & diagonal);
                       from != 0; from &= from - 1) {
                    const int a = std::countr_zero(from);
                    const int sx = (file_of(square) > file_of(a)) - (file_of(square) < file_of(a));
                    const int sy = (rank_of(square) > rank_of(a)) - (rank_of(square) < rank_of(a));
                    bool open = true;
                    for (int i = 1; i < n && open; ++i) {
                      open = (crossed[static_cast<std::size_t>(i)] &
                              square_bit(make_square(file_of(a) + i * sx, rank_of(a) + i * sy))) == 0;
                    }
                    if (open) return true;
                  }
                }
              }
            }
            const std::uint64_t occupied = target->occupied();
            alive &= ~occupied;
            if (n < 8) crossed[static_cast<std::size_t>(n)] = occupied;
            // Past seven boards only a slider on its own square is still moving.
            if (alive == 0 && (n >= 7 || (straight | diagonal) == 0)) break;
          }
        }
      }
    }

    // -- the knight between boards (4.5; J8: over void and pieces alike): two
    //    along one axis and one along another, one of them at least a board axis
    if (knights != 0) {
      for (int bl = -2; bl <= 2; ++bl) {
        for (int bt = -2; bt <= 2; ++bt) {
          // The board steps of the leap: 1 or 2 along one board axis, or 3 as two
          // along one and one along the other.
          const int reach = (bl < 0 ? -bl : bl) + (bt < 0 ? -bt : bt);
          if (reach == 0 || reach > 3 || (reach == 2 && bl != 0 && bt != 0)) continue;
          const int l = p + bl;
          const int ht = h + 2 * bt;
          if (l < v.min_timeline || l > v.max_timeline || ht < 0) continue;
          bool history = false;
          const Board* target = v.board_at(*v.mv.timeline(l), ht, history);
          if (target == nullptr) continue;
          const std::uint64_t royals = target->royals(defender);
          if (royals == 0 || !landable(l, ht, history)) continue;
          for (std::uint64_t bits = royals; bits != 0; bits &= bits - 1) {
            const int square = std::countr_zero(bits);
            const auto r = static_cast<std::size_t>(square);
            // One board step: two squares along a file or rank. Two board steps
            // on one axis: one square. A step on each board axis: none.
            const std::uint64_t from = reach == 1   ? kScan.straight[2][r]
                                       : reach == 2 ? kScan.straight[1][r]
                                                    : square_bit(square);
            if ((from & knights) != 0) return true;
          }
        }
      }
    }
    return false;
  }

  bool run() const noexcept {
    for (const TimelineRef& t : v.mv.timelines()) {
      // L5: the attacker's playable boards - the last of each timeline, with
      // the attacker to move on it.
      const int h = v.last_half_turn(*t);
      if (to_move_at(h) != attacker) continue;
      if (from_board(*t, t->position(), h, *t->head())) return true;
    }
    return false;
  }
};

// Whether the set scan reads these rules (see its comment).
inline bool set_scan_reads(const RuleOptions& rules) noexcept {
  return rules.past_self_blocks && !rules.sliders_cross_void;
}

inline View view_of(const Multiverse& mv) noexcept {
  return View{mv, mv.min_timeline(), mv.max_timeline(), mv.latest_half_turn(), {}};
}

inline bool set_scan(const View& view, const RuleOptions& rules, Colour attacker,
                     std::span<const int> inactive) noexcept {
  const SetScan s{view, rules, inactive, attacker,
                  rules.turn_zero_reachable && rules.inactive_history_is_destination};
  return s.run();
}

std::string piece_word(PieceKind k) {
  switch (k) {
    case PieceKind::Pawn: return "pawn";
    case PieceKind::Knight: return "knight";
    case PieceKind::Bishop: return "bishop";
    case PieceKind::Rook: return "rook";
    case PieceKind::Queen: return "queen";
    case PieceKind::King: return "king";
    case PieceKind::Unicorn: return "unicorn";
    case PieceKind::Dragon: return "dragon";
    case PieceKind::Princess: return "princess";
    case PieceKind::Brawn: return "brawn";
    case PieceKind::CommonKing: return "common king";
    case PieceKind::RoyalQueen: return "royal queen";
  }
  return "piece";
}

std::string board_text(int timeline, int half_turn, bool even) {
  return "(" + timeline_label_string(timeline, even) + "T" + std::to_string(turn_of(half_turn)) +
         ")";
}

// The hooks of the engine's check ruling (t5d/state.hpp).
Verdict royals_safe_hook(const GameState& s) { return royals_safe(s) ? Verdict::Yes : Verdict::No; }
Verdict in_check_hook(const GameState& s) { return in_check(s) ? Verdict::Yes : Verdict::No; }
// C5, exhaustively (no budget: never Unknown - H37), by the hypercuboid
// detector (the faster near mate).
Verdict has_legal_action_hook(const GameState& s) {
  switch (detect_mate(s, Detector::Hypercuboid).verdict) {
    case Outcome::Ongoing: return Verdict::Yes;
    case Outcome::Checkmate:
    case Outcome::Stalemate: return Verdict::No;
    case Outcome::Unknown: break;
  }
  return Verdict::Unknown;
}

}  // namespace

// ---- captures -----------------------------------------------------------------------

std::vector<CapturePlace> capture_places(const RoyalCapture& c) {
  std::vector<CapturePlace> out;
  out.reserve(static_cast<std::size_t>(c.steps) + 1);
  const int x0 = file_of(c.from_square);
  const int y0 = rank_of(c.from_square);
  out.push_back(CapturePlace{c.from_timeline, c.from_half_turn, c.from_square});
  for (int i = 1; i < c.steps; ++i) {
    out.push_back(CapturePlace{c.from_timeline + i * c.step.l, c.from_half_turn + 2 * i * c.step.t,
                               make_square(x0 + i * c.step.x, y0 + i * c.step.y)});
  }
  out.push_back(CapturePlace{c.to_timeline, c.to_half_turn, c.to_square});
  return out;
}

bool royal_capture_exists(const Multiverse& multiverse, const RuleOptions& rules, Colour attacker,
                          std::span<const int> inactive_timelines) {
  if (multiverse.empty()) return false;
  if (set_scan_reads(rules)) {
    return set_scan(view_of(multiverse), rules, attacker, inactive_timelines);
  }
  const auto stop = [](const RoyalCapture&) { return true; };
  return scan(multiverse, rules, attacker, inactive_timelines, stop);
}

bool for_each_royal_capture(const Multiverse& multiverse, const RuleOptions& rules,
                            Colour attacker, std::span<const int> inactive_timelines,
                            const std::function<bool(const RoyalCapture&)>& visit) {
  const auto call = [&](const RoyalCapture& c) { return visit(c); };
  return scan(multiverse, rules, attacker, inactive_timelines, call);
}

std::optional<RoyalCapture> find_royal_capture(const Multiverse& multiverse,
                                               const RuleOptions& rules, Colour attacker,
                                               std::span<const int> inactive_timelines) {
  std::optional<RoyalCapture> found;
  const auto keep = [&](const RoyalCapture& c) {
    found = c;
    return true;
  };
  scan(multiverse, rules, attacker, inactive_timelines, keep);
  return found;
}

bool royal_capture_from(const Multiverse& multiverse, const RuleOptions& rules, Colour attacker,
                        std::span<const int> inactive_timelines,
                        std::span<const ScanSource> sources) {
  const Multiverse& mv = multiverse;
  if (mv.empty()) return false;
  // C13: a royal of the other colour on a source (a branch's copy) or anywhere.
  bool royal = false;
  for (const ScanSource& src : sources) {
    if (src.board != nullptr && src.board->royals(opposite(attacker)) != 0) royal = true;
  }
  if (!royal && !has_royal(mv, opposite(attacker))) return false;
  const auto stop = [](const RoyalCapture&) { return true; };
  // The bounds stay the multiverse's: a slide or leap from a source outside
  // them reads void there (spot_at), which is what lies beyond every board.
  Scan s{mv, rules, inactive_timelines, attacker, mv.board_size(), mv.min_timeline(),
         mv.max_timeline(), mv.latest_half_turn(), 0, 0, nullptr};
  for (const ScanSource& src : sources) {
    if (src.board == nullptr) continue;
    s.p = src.timeline;
    s.h = src.half_turn;
    s.board = src.board;
    for (std::uint64_t bits = s.board->pieces(attacker); bits != 0; bits &= bits - 1) {
      const int square = std::countr_zero(bits);
      if (piece_captures(s, square, s.board->at(square), stop)) return true;
    }
  }
  return false;
}

// ---- check status --------------------------------------------------------------------

std::vector<int> passed_timelines(const GameState& s) {
  std::vector<int> out;
  for (const TimelineRef& t : s.multiverse().timelines()) {
    const int p = t->position();
    // R1: ACTIVE boards IN THE PRESENT (T4) - the mover's, as the present is
    // on the mover's colour at a turn's start.
    if (!s.active(p) || t->last_half_turn() != s.present() || !s.playable_by_mover(p)) continue;
    // Q6's alternative defers a timeline only while its board is its owner's,
    // and every deferral of the mover's ends as the mover's turn begins: at a
    // turn's start no deferred timeline is the mover's to play.
    const auto& deferred = s.deferred_timelines();
    if (std::any_of(deferred.begin(), deferred.end(),
                    [p](const GameState::Deferral& d) { return d.timeline == p; })) {
      throw std::logic_error("passed_timelines: a deferred timeline of the mover's (Q6) - not a "
                             "turn's start");
    }
    out.push_back(p);
  }
  return out;
}

Multiverse passed_multiverse(const GameState& s) {
  Multiverse mv = s.multiverse();
  for (const int p : passed_timelines(s)) {
    // A null move (C6, "a passed board is read as advanced unchanged"): the same
    // pieces, flags and all; no en-passant square, as no double step was made.
    BoardBuilder b(*mv.head(p));
    b.set_en_passant(kNoSquare);
    mv.append_board(p, b.publish());
  }
  return mv;
}

bool in_check(const GameState& s) {
  const Colour opponent = opposite(s.to_move());
  const Multiverse& mv = s.multiverse();
  // The passed boards read in place (View), where the set scan reads the rules:
  // no multiverse is built. A side with no royal has none to capture (C13).
  // (Under Q6's alternative a deferred timeline can make this no turn's start:
  // passed_timelines says so, below.)
  if (set_scan_reads(s.rules()) && s.deferred_timelines().empty() &&
      mv.timeline_count() <= View::kMostTimelines) {
    View view = view_of(mv);
    for (const TimelineRef& t : mv.timelines()) {
      const int p = t->position();
      // R1: ACTIVE boards IN THE PRESENT, the mover's (passed_timelines).
      if (!s.active(p) || t->last_half_turn() != s.present() || !s.playable_by_mover(p)) continue;
      view.pass(p);
    }
    if (view.any_passed) view.max_half_turn = std::max(view.max_half_turn, s.present() + 1);
    return set_scan(view, s.rules(), opponent, s.inactive_timelines());
  }
  if (!has_royal(mv, s.to_move())) return false;  // C13
  return royal_capture_exists(passed_multiverse(s), s.rules(), opponent, s.inactive_timelines());
}

std::optional<RoyalCapture> check_witness(const GameState& s) {
  return find_royal_capture(passed_multiverse(s), s.rules(), opposite(s.to_move()),
                            s.inactive_timelines());
}

// ---- submission legality --------------------------------------------------------------

bool royals_safe(const GameState& s) {
  return !royal_capture_exists(s.multiverse(), s.rules(), opposite(s.to_move()),
                               s.inactive_timelines());
}

std::optional<RoyalCapture> submission_witness(const GameState& s) {
  return find_royal_capture(s.multiverse(), s.rules(), opposite(s.to_move()),
                            s.inactive_timelines());
}

std::string describe(const RoyalCapture& c, bool even) {
  return "the " + piece_word(c.attacker.kind()) + " on " +
         board_text(c.from_timeline, c.from_half_turn, even) + square_name(c.from_square) +
         " can capture the " + piece_word(c.royal.kind()) + " on " +
         board_text(c.to_timeline, c.to_half_turn, even) + square_name(c.to_square) +
         (c.branching ? " (a capture that branches)" : "");
}

// ---- the engine's check ruling ----------------------------------------------------------

CheckRuling standard_check_ruling() noexcept {
  CheckRuling r;
  r.royals_safe = &royals_safe_hook;
  r.in_check = &in_check_hook;
  r.has_legal_action = &has_legal_action_hook;
  return r;
}

}  // namespace t5d
