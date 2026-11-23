// engine/src/move_edits.hpp - what a move leaves on the boards it plays, as edits.
//
// Fractal's own file, not copied from Temporal-5D (PROVENANCE.md, "Fractal's own
// files"). Internal to the rules engine: GameState::apply builds its new boards
// from these edits, and the turn generator reads the same edits to learn which
// squares each candidate changes - without applying the candidate to a copy of the
// position, which is what its construction used to spend most of its time on
// (spec 3.3 item 6; docs/gates/G1a-rules-review.md). One function, so the two can
// never disagree about what a move does to a board.

#ifndef T5D_MOVE_EDITS_HPP
#define T5D_MOVE_EDITS_HPP

#include <array>
#include <cstddef>
#include <cstdint>

#include "t5d/board.hpp"
#include "t5d/move.hpp"
#include "t5d/rules.hpp"
#include "t5d/types.hpp"

namespace t5d::detail {

/// One square of a board and the piece a move leaves there (empty: none).
struct SquareEdit {
  std::int8_t square = kNoSquare;
  Piece piece;
};

/// A move's edits (T2, T3, J4, J5; S1, S7, S8; Q15).
struct MoveEdits {
  /// The source board's successor: the squares the move changes on it, each
  /// once, ascending - the source square emptied; within a board the
  /// destination filled, en passant's victim removed, castling's rook moved.
  std::array<SquareEdit, 4> source{};
  std::size_t source_count = 0;
  /// The piece that arrives on the destination square: promoted, or the
  /// moving piece without its unmoved flag (Q15: unless a pawn or brawn left
  /// its board under jumps_keep_unmoved). A move within a board has it among
  /// `source` too; a jump puts it on the destination board's successor (J4) or
  /// on the new timeline's first board (J5).
  Piece placed;
};

/// The edits of `move`, a move GameState::apply accepts on a state whose
/// playable board of the move's source timeline is `source` (apply has checked
/// it by the time it asks; the generator's moves are such moves).
MoveEdits move_edits(const Move& move, const Board& source, const RuleOptions& rules) noexcept;

}  // namespace t5d::detail

#endif  // T5D_MOVE_EDITS_HPP
