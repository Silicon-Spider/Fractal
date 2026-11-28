// tools/bench/fractal_legality.cpp - fractal-legality: where the time of legality goes
// (docs/MEASUREMENTS.md; spec 3.3 items 6 and 7, 3.6). New code: nothing of
// Temporal-5D's tools is copied here. It reads only the copied public headers
// (engine/include/t5d) and holds no rule of chess or of 5D movement and no evaluation
// of any kind: every move, turn and verdict comes from the engine.
//
//   fractal-legality --records DIR [--passes P] [--stride N] [--scorer bands|hash]
//                    [--turns K] [--json FILE] [--no-pin] [--pool] [--verify]
//
// POSITIONS: every distinct turn start of the *.5dpgn records in DIR that replay to
// the end in parse mode, bucketed by board count as fractal-bench buckets them (under
// 50, 50-99, 100-199, 200+). --stride N keeps every N-th position of a bucket.
//
// WHAT IS TIMED, per bucket, one thread, each as P passes over the bucket's positions
// (the median pass is reported, in nanoseconds per position):
//
//   movegen      generate_moves: the pseudo-legal moves of the turn start
//   copy         a GameState copied and destroyed
//   copy+apply   a GameState copied, its first non-branching move applied
//   in_check     in_check: R1's check status (the passed multiverse, then the scan)
//   passed       passed_multiverse alone
//   scan passed  royal_capture_exists on the passed multiverse, built beforehand
//   build        a TurnGenerator constructed and destroyed, no turn asked for
//   first        a TurnGenerator constructed and its first turn taken
//   K turns      a TurnGenerator constructed and up to K turns taken
//   scan succ    royal_capture_exists on the first turn's successor: the submission
//                test's scan (C4) on a position the search would reach
//
// and, counted rather than timed: heap allocations per build, per first turn and per
// further turn (this program's operator new counts them), and the generator's own
// figures after K turns (nodes, points judged, finished actions tested).
//
// --verify times nothing. Over the same positions it holds the engine's answers equal
// by second routes, as tests/test_legality.cpp does on its samples: the set scan and
// the line scan on each position, its passed multiverse and its first turns'
// successors; check status read in place and on the passed multiverse; each generated
// turn replayed through the state machine to the successor it carries; a generator
// that runs out with no turn against the depth-first detector. Exit status 1 on any
// difference.
//
// SCORER. bands (default): what the search's order looks like to the generator - a
// move on a mandatory board scores in [1, 2), leaving an optional board 0, a move on
// an optional board in [-2, -1), leaving a mandatory board -3, a jump's arrival half
// 0; within a band a hash of the move, as fractal-bench's. hash: fractal-bench's own
// scorer (every move above leaving), for comparison with MEASUREMENTS entry 1.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <new>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include "t5d/check.hpp"
#include "t5d/move.hpp"
#include "t5d/movegen.hpp"
#include "t5d/multiverse.hpp"
#include "t5d/pgn.hpp"
#include "t5d/rules.hpp"
#include "t5d/search.hpp"
#include "t5d/state.hpp"
#include "t5d/turngen.hpp"
#include "t5d/types.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <sched.h>
#endif

#ifndef FRACTAL_COMPILER
#define FRACTAL_COMPILER "unknown"
#endif
#ifndef FRACTAL_BUILD_TYPE
#define FRACTAL_BUILD_TYPE "unknown"
#endif
#ifndef FRACTAL_CXX_FLAGS
#define FRACTAL_CXX_FLAGS "unknown"
#endif

// ---- allocation counting, and the pool experiment ---------------------------------------------
//
// Every operator new of the program is counted (one thread measures; the counter is
// atomic only so that a stray thread cannot corrupt it).
//
// --pool is an experiment, not the engine's allocator: it measures what recycling
// small blocks would save (spec 3.3 item 1, "allocation moves to per-thread pools")
// without changing a line of the engine. With it, a freed block of up to 512 bytes
// goes onto a per-thread free list of its size class (16 bytes apart) and the next
// request of that class takes it back; without it every request goes to malloc. Every
// block carries a 16-byte header saying which, so the two can be mixed.

namespace {
std::atomic<std::uint64_t> g_allocations{0};
bool g_pool = false;

constexpr std::size_t kBlockHeader = 16;
constexpr std::size_t kBlockClasses = 33;  // 16, 32, ... 512 bytes
constexpr std::uint64_t kDirectBlock = ~std::uint64_t{0};
struct FreeBlock {
  FreeBlock* next;
};
thread_local FreeBlock* t_free_blocks[kBlockClasses] = {};
}  // namespace

// Not inlined: a compiler that sees malloc and free through the replaced operators
// reports a mismatched allocation.
#if defined(__GNUC__) || defined(__clang__)
#define FRACTAL_NOINLINE __attribute__((noinline))
#else
#define FRACTAL_NOINLINE
#endif

FRACTAL_NOINLINE void* operator new(std::size_t size) {
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  const std::size_t rounded = size == 0 ? 1 : (size + 15) / 16;
  std::uint64_t tag = kDirectBlock;
  void* raw = nullptr;
  if (g_pool && rounded < kBlockClasses) {
    tag = rounded;
    if (FreeBlock* block = t_free_blocks[rounded]) {
      t_free_blocks[rounded] = block->next;
      raw = block;
    } else {
      raw = std::malloc(kBlockHeader + rounded * 16);
    }
  } else {
    raw = std::malloc(kBlockHeader + (size == 0 ? 1 : size));
  }
  if (raw == nullptr) throw std::bad_alloc();
  std::memcpy(raw, &tag, sizeof tag);
  return static_cast<char*>(raw) + kBlockHeader;
}
FRACTAL_NOINLINE void operator delete(void* p) noexcept {
  if (p == nullptr) return;
  char* raw = static_cast<char*>(p) - kBlockHeader;
  std::uint64_t tag = 0;
  std::memcpy(&tag, raw, sizeof tag);
  if (tag == kDirectBlock) {
    std::free(raw);
    return;
  }
  t_free_blocks[tag] = ::new (static_cast<void*>(raw)) FreeBlock{t_free_blocks[tag]};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }

namespace {

using Clock = std::chrono::steady_clock;

std::atomic<std::uint64_t> g_sink{0};

std::uint64_t mix64(std::uint64_t z) noexcept {
  z += 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

double median(std::vector<double> v) {
  if (v.empty()) return std::nan("");
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  return n % 2 == 1 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// ---- options ----------------------------------------------------------------------------

struct Options {
  std::string records;
  std::string json;
  int passes = 5;
  int stride = 1;
  int turns = 8;
  bool bands = true;
  bool pin = true;
  bool pool = false;
  bool verify = false;
  bool ceilings = false;
  std::uint64_t turn_nodes = 2'000'000;
};

// ---- the allocation ceilings (spec 3.3 items 6 and 7) -----------------------------------------
//
// The heap allocations a turn generator may make on t0database turn starts under 50
// boards, per position: constructed and destroyed (`build`), and constructed with up
// to eight turns taken (`K turns`). --ceilings fails the run above either. They are a
// ratchet: each is lowered, on purpose and with the reason in the gate report, when a
// change to the generator lands, towards the spec's target of 18 and 51. The counts
// are the program's own (its operator new), so they do not depend on the machine.
//
//   as copied           181    519
//   G1a, item 6a         37    318   (docs/MEASUREMENTS.md entry 3)
//   G1a, item 7       18.24 286.36   (entry 4): the ceilings below, with room for a
//                                     standard library that grows a vector otherwise
constexpr double kBuildCeiling = 19.0;
constexpr double kTurnsCeiling = 290.0;

void usage(std::FILE* f) {
  std::fputs(
      "fractal-legality: where the time of legality goes (one thread).\n"
      "  --records DIR     the *.5dpgn records whose turn starts are measured (required)\n"
      "  --passes P        passes over each bucket per measurement (default 5; median kept)\n"
      "  --stride N        keep every N-th position of a bucket (default 1)\n"
      "  --turns K         turns taken by the 'K turns' measurement (default 8)\n"
      "  --scorer NAME     bands (default) or hash (fractal-bench's)\n"
      "  --json FILE       write every figure as JSON\n"
      "  --no-pin          do not pin the thread to a logical processor\n"
      "  --pool            experiment: recycle freed blocks of up to 512 bytes (spec 3.3 item 1)\n"
      "  --verify          time nothing: hold the engine's answers equal by second routes\n"
      "  --ceilings        fail if the generator allocates more than its ceilings on turn\n"
      "                    starts under 50 boards (needs the default --turns, --scorer and --stride)\n",
      f);
}

int positive_int(std::string_view name, const char* text) {
  char* end = nullptr;
  const long v = std::strtol(text, &end, 10);
  if (end == text || *end != '\0' || v < 1 || v > 1'000'000'000L) {
    throw std::invalid_argument(std::string(name) + ": expected a positive integer, got '" + text + "'");
  }
  return static_cast<int>(v);
}

Options parse_options(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    const auto value = [&]() -> const char* {
      if (i + 1 >= argc) throw std::invalid_argument(std::string(a) + ": missing value");
      return argv[++i];
    };
    if (a == "--help" || a == "-h") {
      usage(stdout);
      std::exit(0);
    } else if (a == "--records") {
      o.records = value();
    } else if (a == "--json") {
      o.json = value();
    } else if (a == "--passes") {
      o.passes = positive_int(a, value());
    } else if (a == "--stride") {
      o.stride = positive_int(a, value());
    } else if (a == "--turns") {
      o.turns = positive_int(a, value());
    } else if (a == "--scorer") {
      const std::string_view s = value();
      if (s != "bands" && s != "hash") throw std::invalid_argument("--scorer: bands or hash");
      o.bands = s == "bands";
    } else if (a == "--no-pin") {
      o.pin = false;
    } else if (a == "--pool") {
      o.pool = true;
    } else if (a == "--verify") {
      o.verify = true;
    } else if (a == "--ceilings") {
      o.ceilings = true;
    } else {
      throw std::invalid_argument("unknown option '" + std::string(a) + "' (--help lists them)");
    }
  }
  if (o.records.empty()) throw std::invalid_argument("--records DIR is required");
  if (o.ceilings && (o.verify || o.pool || !o.bands || o.turns != 8 || o.stride != 1)) {
    throw std::invalid_argument("--ceilings: the ceilings are for the default --turns, --scorer and --stride, "
                                "without --pool or --verify");
  }
  return o;
}

bool pin_this_thread() {
  const unsigned n = std::thread::hardware_concurrency();
  const int cpu = (n > 2 || n == 0) ? 2 : 0;
#if defined(_WIN32)
  const bool ok = SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR{1} << cpu) != 0;
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
  return ok;
#elif defined(__linux__) && defined(CPU_SET)
  cpu_set_t set;
  CPU_ZERO(&set);
  (void)CPU_SET(static_cast<std::size_t>(cpu), &set);
  return sched_setaffinity(0, sizeof set, &set) == 0;
#else
  (void)cpu;
  return false;
#endif
}

// ---- positions ----------------------------------------------------------------------------

struct Bucket {
  const char* name;
  int lo;
  int hi;  // 0: open
  std::vector<t5d::GameState> positions;
};

std::string read_whole_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

// Every distinct turn start of the records that replay to the end in parse mode, by
// board count: fractal-bench's records buckets.
std::vector<Bucket> load_buckets(const std::string& dir, int stride) {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".5dpgn") files.push_back(entry.path());
  }
  std::sort(files.begin(), files.end(),
            [](const auto& a, const auto& b) { return a.filename().string() < b.filename().string(); });
  std::vector<Bucket> buckets = {{"<50", 0, 49, {}}, {"50-99", 50, 99, {}}, {"100-199", 100, 199, {}}, {"200+", 200, 0, {}}};
  struct KeyHash {
    std::size_t operator()(const std::pair<std::uint64_t, int>& k) const noexcept {
      return static_cast<std::size_t>(mix64(k.first ^ static_cast<std::uint64_t>(k.second)));
    }
  };
  std::unordered_set<std::pair<std::uint64_t, int>, KeyHash> seen;
  for (const std::filesystem::path& path : files) {
    t5d::PgnRecord record = t5d::read_pgn(read_whole_file(path));
    if (record.unreadable) continue;
    std::vector<t5d::GameState> starts;
    try {
      t5d::PgnReplay replay(std::move(record));
      replay.set_check_ruling(t5d::CheckRuling::none());
      for (;;) {
        starts.push_back(replay.state());
        if (replay.finished()) break;
        replay.play_next();
      }
    } catch (const std::exception&) {
      continue;
    }
    for (t5d::GameState& s : starts) {
      const std::pair<std::uint64_t, int> key{s.zobrist(), t5d::index_of(s.to_move())};
      if (!seen.insert(key).second) continue;
      const std::size_t boards = s.multiverse().board_count();
      for (Bucket& b : buckets) {
        if (b.hi == 0 || boards <= static_cast<std::size_t>(b.hi)) {
          b.positions.push_back(std::move(s));
          break;
        }
      }
    }
  }
  for (Bucket& b : buckets) {
    if (stride <= 1) continue;
    std::vector<t5d::GameState> kept;
    for (std::size_t i = 0; i < b.positions.size(); i += static_cast<std::size_t>(stride)) {
      kept.push_back(std::move(b.positions[i]));
    }
    b.positions = std::move(kept);
  }
  return buckets;
}

// ---- scorers ------------------------------------------------------------------------------

double move_unit(const t5d::Move& m, int timeline) {
  std::uint64_t h = 0x43687230626E6368ull;
  const auto feed = [&h](std::int64_t v) { h = mix64(h ^ static_cast<std::uint64_t>(v)); };
  feed(m.from_timeline);
  feed(m.from_half_turn);
  feed(m.to_timeline);
  feed(m.to_half_turn);
  feed(m.from_square);
  feed(m.to_square);
  feed(m.promotes() ? static_cast<std::int64_t>(t5d::index_of(m.promotion)) + 1 : 0);
  feed(m.branching() ? 1 : 0);
  feed(timeline);
  return static_cast<double>(mix64(h) >> 11) * 0x1.0p-53;  // [0, 1)
}

// fractal-bench's scorer: every move above leaving its board.
class HashScorer final : public t5d::TurnScorer {
 public:
  double leave(int /*timeline*/) const override { return 0.0; }
  double move(const t5d::Move& m, int timeline) const override {
    return 0.001 + 0.999 * move_unit(m, timeline);
  }
};

// The bands of spec 5.3, without any chess knowledge inside a band.
class BandScorer final : public t5d::TurnScorer {
 public:
  explicit BandScorer(const t5d::GameState& s) : s_(s) {}
  double leave(int timeline) const override { return s_.mandatory(timeline) ? -3.0 : 0.0; }
  double move(const t5d::Move& m, int timeline) const override {
    // The arrival half of a jump: scored once, on the board it leaves.
    if (timeline != m.from_timeline) return 0.0;
    const double u = move_unit(m, timeline);
    return s_.mandatory(timeline) ? 1.0 + u : -2.0 + u;
  }

 private:
  const t5d::GameState& s_;
};

// ---- measurements -------------------------------------------------------------------------

struct Row {
  std::string name;
  double ns = std::nan("");       // median over passes, per position
  double allocations = std::nan("");  // per position (the last pass)
  std::size_t positions = 0;
};

// Times `body(i)` over positions [0, n), P passes; the median pass per position.
template <class Body>
Row timed(const char* name, std::size_t n, int passes, Body body) {
  Row row;
  row.name = name;
  row.positions = n;
  if (n == 0) return row;
  std::vector<double> ns;
  for (int p = 0; p < passes; ++p) {
    const std::uint64_t a0 = g_allocations.load(std::memory_order_relaxed);
    const Clock::time_point t0 = Clock::now();
    std::uint64_t sink = 0;
    for (std::size_t i = 0; i < n; ++i) sink += body(i);
    const Clock::time_point t1 = Clock::now();
    const std::uint64_t a1 = g_allocations.load(std::memory_order_relaxed);
    g_sink.fetch_add(sink, std::memory_order_relaxed);
    ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / static_cast<double>(n));
    row.allocations = static_cast<double>(a1 - a0) / static_cast<double>(n);
  }
  row.ns = median(ns);
  return row;
}

struct BucketResult {
  std::string name;
  std::size_t positions = 0;
  std::size_t with_turn = 0;
  double mean_boards = 0;
  double mean_moves = 0;
  double mean_playable = 0;
  std::vector<Row> rows;
  // The generator's own figures, summed over the positions, after K turns.
  std::uint64_t turns = 0;
  std::uint64_t nodes = 0;
  std::uint64_t points = 0;
  std::uint64_t actions_tested = 0;
  std::uint64_t in_check = 0;
  std::uint64_t first_unsafe = 0;  // sanity: successors that fail the scan (must be 0)
};

BucketResult measure(const Bucket& bucket, const Options& opt) {
  BucketResult out;
  out.name = bucket.name;
  const std::vector<t5d::GameState>& ps = bucket.positions;
  const std::size_t n = ps.size();
  out.positions = n;
  if (n == 0) return out;
  const HashScorer hash;
  const auto with_scorer = [&](const t5d::GameState& s, auto&& use) {
    if (opt.bands) {
      const BandScorer bands(s);
      return use(static_cast<const t5d::TurnScorer&>(bands));
    }
    return use(static_cast<const t5d::TurnScorer&>(hash));
  };
  const t5d::SearchBudget budget = t5d::SearchBudget::nodes(opt.turn_nodes);

  // What is prepared beforehand: each position's first non-branching move, its passed
  // multiverse and its first generated successor.
  t5d::MoveList moves;
  std::vector<std::optional<t5d::Move>> first_move(n);
  std::vector<t5d::Multiverse> passed;
  std::vector<std::optional<t5d::GameState>> successor(n);
  passed.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const t5d::GameState& s = ps[i];
    t5d::generate_moves(s, moves);
    out.mean_moves += static_cast<double>(moves.size());
    out.mean_boards += static_cast<double>(s.multiverse().board_count());
    out.mean_playable += static_cast<double>(s.playable_timelines().size());
    for (const t5d::Move& m : moves) {
      if (!m.branching()) {
        first_move[i] = m;
        break;
      }
    }
    passed.push_back(t5d::passed_multiverse(s));
    if (t5d::in_check(s)) ++out.in_check;
    with_scorer(s, [&](const t5d::TurnScorer& scorer) {
      t5d::TurnGenerator gen(s, scorer, t5d::TurnUnit::Successors, budget);
      std::uint64_t taken = 0;
      while (taken < static_cast<std::uint64_t>(opt.turns)) {
        std::optional<t5d::GeneratedTurn> turn = gen.next();
        if (!turn) break;
        if (taken == 0) successor[i] = std::move(turn->result);
        ++taken;
      }
      out.turns += taken;
      out.nodes += gen.stats().nodes;
      out.points += gen.stats().points;
      out.actions_tested += gen.stats().actions_tested;
      return 0;
    });
    if (successor[i]) {
      ++out.with_turn;
      const t5d::GameState& r = *successor[i];
      // The mover's royals, seen from the opponent's side of the submitted position.
      if (t5d::royal_capture_exists(r.multiverse(), r.rules(), r.to_move(), r.inactive_timelines())) {
        ++out.first_unsafe;
      }
    }
  }
  out.mean_moves /= static_cast<double>(n);
  out.mean_boards /= static_cast<double>(n);
  out.mean_playable /= static_cast<double>(n);

  const int P = opt.passes;
  out.rows.push_back(timed("movegen", n, P, [&](std::size_t i) {
    t5d::generate_moves(ps[i], moves);
    return static_cast<std::uint64_t>(moves.size());
  }));
  out.rows.push_back(timed("copy", n, P, [&](std::size_t i) {
    const t5d::GameState c = ps[i];
    return static_cast<std::uint64_t>(c.present() & 1);
  }));
  out.rows.push_back(timed("copy+apply", n, P, [&](std::size_t i) {
    if (!first_move[i]) return std::uint64_t{0};
    t5d::GameState c = ps[i];
    c.apply(*first_move[i]);
    return static_cast<std::uint64_t>(c.present() & 1);
  }));
  out.rows.push_back(timed("in_check", n, P, [&](std::size_t i) {
    return static_cast<std::uint64_t>(t5d::in_check(ps[i]) ? 1 : 0);
  }));
  out.rows.push_back(timed("passed", n, P, [&](std::size_t i) {
    const t5d::Multiverse mv = t5d::passed_multiverse(ps[i]);
    return static_cast<std::uint64_t>(mv.board_count() & 1);
  }));
  out.rows.push_back(timed("scan passed", n, P, [&](std::size_t i) {
    const t5d::GameState& s = ps[i];
    return static_cast<std::uint64_t>(
        t5d::royal_capture_exists(passed[i], s.rules(), t5d::opposite(s.to_move()), s.inactive_timelines()) ? 1 : 0);
  }));
  out.rows.push_back(timed("build", n, P, [&](std::size_t i) {
    return with_scorer(ps[i], [&](const t5d::TurnScorer& scorer) {
      const t5d::TurnGenerator gen(ps[i], scorer, t5d::TurnUnit::Successors, budget);
      return static_cast<std::uint64_t>(gen.generated());
    });
  }));
  out.rows.push_back(timed("first", n, P, [&](std::size_t i) {
    return with_scorer(ps[i], [&](const t5d::TurnScorer& scorer) {
      t5d::TurnGenerator gen(ps[i], scorer, t5d::TurnUnit::Successors, budget);
      const std::optional<t5d::GeneratedTurn> turn = gen.next();
      return static_cast<std::uint64_t>(turn ? turn->moves.size() : 0);
    });
  }));
  out.rows.push_back(timed("K turns", n, P, [&](std::size_t i) {
    return with_scorer(ps[i], [&](const t5d::TurnScorer& scorer) {
      t5d::TurnGenerator gen(ps[i], scorer, t5d::TurnUnit::Successors, budget);
      std::uint64_t taken = 0;
      while (taken < static_cast<std::uint64_t>(opt.turns) && gen.next()) ++taken;
      return taken;
    });
  }));
  out.rows.push_back(timed("scan succ", n, P, [&](std::size_t i) {
    if (!successor[i]) return std::uint64_t{0};
    const t5d::GameState& r = *successor[i];
    return static_cast<std::uint64_t>(
        t5d::royal_capture_exists(r.multiverse(), r.rules(), r.to_move(), r.inactive_timelines()) ? 1 : 0);
  }));
  return out;
}

const Row* find_row(const BucketResult& r, std::string_view name) {
  for (const Row& row : r.rows) {
    if (row.name == name) return &row;
  }
  return nullptr;
}

void print(const std::vector<BucketResult>& results, const Options& opt) {
  std::printf("fractal-legality: %s %s, %s; scorer %s; %d passes; K = %d turns; small blocks %s\n",
              FRACTAL_COMPILER, FRACTAL_BUILD_TYPE, FRACTAL_CXX_FLAGS, opt.bands ? "bands" : "hash",
              opt.passes, opt.turns, opt.pool ? "recycled (--pool)" : "from malloc");
  for (const BucketResult& r : results) {
    if (r.positions == 0) continue;
    std::printf("\n== %s boards: %zu turn starts (%zu with a legal turn, %llu in check); means: "
                "%.1f boards, %.1f moves, %.2f playable boards ==\n",
                r.name.c_str(), r.positions, r.with_turn, static_cast<unsigned long long>(r.in_check),
                r.mean_boards, r.mean_moves, r.mean_playable);
    std::printf("%-12s %12s %14s\n", "measure", "ns/position", "allocations");
    for (const Row& row : r.rows) {
      std::printf("%-12s %12.0f %14.2f\n", row.name.c_str(), row.ns, row.allocations);
    }
    const Row* build = find_row(r, "build");
    const Row* first = find_row(r, "first");
    const Row* k = find_row(r, "K turns");
    const double turns = static_cast<double>(r.turns) / static_cast<double>(r.positions);
    if (build != nullptr && first != nullptr && k != nullptr && turns > 1.0) {
      std::printf("first turn after the build: %.0f ns, %.1f allocations; each further turn: %.0f ns, "
                  "%.1f allocations (%.2f turns taken per position)\n",
                  first->ns - build->ns, first->allocations - build->allocations,
                  (k->ns - first->ns) / (turns - 1.0), (k->allocations - first->allocations) / (turns - 1.0),
                  turns);
    }
    std::printf("generator, per position after K turns: %.1f nodes, %.2f points judged, %.2f actions "
                "tested; successors failing the scan: %llu\n",
                static_cast<double>(r.nodes) / static_cast<double>(r.positions),
                static_cast<double>(r.points) / static_cast<double>(r.positions),
                static_cast<double>(r.actions_tested) / static_cast<double>(r.positions),
                static_cast<unsigned long long>(r.first_unsafe));
  }
  std::printf("\nsink %llu\n", static_cast<unsigned long long>(g_sink.load()));
}

void write_json(const std::vector<BucketResult>& results, const Options& opt) {
  std::ofstream out(opt.json, std::ios::binary);
  if (!out) throw std::runtime_error("cannot write " + opt.json);
  out << "{\n  \"tool\": \"fractal-legality\",\n  \"compiler\": \"" << FRACTAL_COMPILER << "\",\n  \"build\": \""
      << FRACTAL_BUILD_TYPE << "\",\n  \"flags\": \"" << FRACTAL_CXX_FLAGS << "\",\n  \"scorer\": \""
      << (opt.bands ? "bands" : "hash") << "\",\n  \"pool\": " << (opt.pool ? "true" : "false")
      << ",\n  \"passes\": " << opt.passes << ",\n  \"turns\": " << opt.turns
      << ",\n  \"stride\": " << opt.stride << ",\n  \"buckets\": [";
  bool first_bucket = true;
  for (const BucketResult& r : results) {
    if (r.positions == 0) continue;
    out << (first_bucket ? "\n" : ",\n") << "    {\"name\": \"" << r.name << "\", \"positions\": " << r.positions
        << ", \"with_turn\": " << r.with_turn << ", \"in_check\": " << r.in_check << ", \"mean_boards\": "
        << r.mean_boards << ", \"mean_moves\": " << r.mean_moves << ", \"mean_playable\": " << r.mean_playable
        << ", \"turns\": " << r.turns << ", \"nodes\": " << r.nodes << ", \"points\": " << r.points
        << ", \"actions_tested\": " << r.actions_tested << ", \"successors_failing_scan\": " << r.first_unsafe
        << ", \"rows\": [";
    for (std::size_t i = 0; i < r.rows.size(); ++i) {
      out << (i == 0 ? "" : ", ") << "{\"name\": \"" << r.rows[i].name << "\", \"ns\": " << r.rows[i].ns
          << ", \"allocations\": " << r.rows[i].allocations << "}";
    }
    out << "]}";
    first_bucket = false;
  }
  out << "\n  ]\n}\n";
}

// ---- --verify -------------------------------------------------------------------------------

int verify(const std::vector<Bucket>& buckets, const Options& opt) {
  std::uint64_t positions = 0, scans = 0, captures = 0, checks = 0, turns = 0, no_turn = 0;
  std::uint64_t mates = 0, stalemates = 0, undecided = 0, differences = 0;
  const t5d::SearchBudget budget = t5d::SearchBudget::nodes(opt.turn_nodes);
  const HashScorer hash;
  t5d::MoveList scratch;
  for (const Bucket& bucket : buckets) {
    for (std::size_t i = 0; i < bucket.positions.size(); ++i) {
      const t5d::GameState& s = bucket.positions[i];
      ++positions;
      const auto differ = [&](const char* what) {
        ++differences;
        std::fprintf(stderr, "fractal-legality: DIFFERENCE at %s boards, position %zu (key %016llx): %s\n",
                     bucket.name, i, static_cast<unsigned long long>(s.zobrist()), what);
      };
      // The set scan against the line scan's witness.
      const auto scan = [&](const t5d::Multiverse& mv, const t5d::RuleOptions& rules, t5d::Colour attacker,
                            std::span<const int> inactive, const char* what) {
        const bool exists = t5d::royal_capture_exists(mv, rules, attacker, inactive);
        ++scans;
        if (exists) ++captures;
        if (exists != t5d::find_royal_capture(mv, rules, attacker, inactive).has_value()) differ(what);
        return exists;
      };
      const t5d::Multiverse passed = t5d::passed_multiverse(s);
      const t5d::Colour opponent = t5d::opposite(s.to_move());
      for (const t5d::Colour c : {t5d::Colour::White, t5d::Colour::Black}) {
        scan(s.multiverse(), s.rules(), c, s.inactive_timelines(), "the scans differ on the position");
      }
      scan(passed, s.rules(), s.to_move(), s.inactive_timelines(), "the scans differ on the passed multiverse");
      const bool check =
          scan(passed, s.rules(), opponent, s.inactive_timelines(), "the scans differ on the passed multiverse");
      if (check) ++checks;
      if (t5d::in_check(s) != check) differ("check status read in place differs from the passed multiverse's");
      // The generator's turns, each replayed through the state machine.
      std::uint64_t taken = 0;
      bool exhausted = false;
      bool stopped = false;
      {
        const BandScorer bands(s);
        const t5d::TurnScorer& scorer = opt.bands ? static_cast<const t5d::TurnScorer&>(bands)
                                                  : static_cast<const t5d::TurnScorer&>(hash);
        t5d::TurnGenerator gen(s, scorer, t5d::TurnUnit::Successors, budget);
        double last = 0;
        while (taken < static_cast<std::uint64_t>(opt.turns)) {
          const std::optional<t5d::GeneratedTurn> turn = gen.next();
          if (!turn) break;
          if (taken > 0 && turn->score > last) differ("a turn scores above the one before it");
          last = turn->score;
          ++taken;
          t5d::GameState replayed = s;
          bool playable = true;
          for (const t5d::Move& m : turn->moves) {
            if (!t5d::is_pseudo_legal(replayed, m, scratch)) {
              playable = false;
              break;
            }
            replayed.apply(m);
          }
          if (!playable || replayed.submission_status() != t5d::SubmitStatus::Safe) {
            differ("a generated turn does not replay to a safe submission");
            continue;
          }
          replayed.submit();
          if (replayed.zobrist() != turn->result.zobrist()) differ("a generated turn's successor is not its replay's");
          const t5d::GameState& r = turn->result;
          if (scan(r.multiverse(), r.rules(), r.to_move(), r.inactive_timelines(),
                   "the scans differ on a successor")) {
            differ("a generated turn leaves a royal capturable");
          }
          scan(r.multiverse(), r.rules(), t5d::opposite(r.to_move()), r.inactive_timelines(),
               "the scans differ on a successor");
        }
        exhausted = gen.exhausted();
        stopped = gen.stopped();
      }
      turns += taken;
      // A generator that runs out with no turn, against the depth-first detector.
      const t5d::MateResult mate = t5d::detect_mate(s, t5d::Detector::Hypercuboid, budget);
      if (stopped || mate.verdict == t5d::Outcome::Unknown) {
        ++undecided;
      } else if (taken == 0) {
        ++no_turn;
        if (!exhausted) differ("no turn, yet neither exhausted nor stopped");
        if (mate.verdict == t5d::Outcome::Ongoing) differ("the generator has no turn where the detector has one");
        if ((mate.verdict == t5d::Outcome::Checkmate) != check) differ("the detector's verdict disagrees with check status");
        if (mate.verdict == t5d::Outcome::Checkmate) ++mates;
        if (mate.verdict == t5d::Outcome::Stalemate) ++stalemates;
      } else if (mate.verdict != t5d::Outcome::Ongoing) {
        differ("the generator has a turn where the detector proves none");
      }
    }
    std::fprintf(stderr, "fractal-legality: verified %s: %zu positions\n", bucket.name, bucket.positions.size());
  }
  std::printf("fractal-legality --verify: %llu positions; %llu scans compared (%llu with a capture); %llu in "
              "check; %llu turns replayed; %llu positions with no turn (%llu checkmate, %llu stalemate); %llu "
              "undecided within the budget; %llu difference(s)\n",
              static_cast<unsigned long long>(positions), static_cast<unsigned long long>(scans),
              static_cast<unsigned long long>(captures), static_cast<unsigned long long>(checks),
              static_cast<unsigned long long>(turns), static_cast<unsigned long long>(no_turn),
              static_cast<unsigned long long>(mates), static_cast<unsigned long long>(stalemates),
              static_cast<unsigned long long>(undecided), static_cast<unsigned long long>(differences));
  return differences == 0 ? 0 : 1;
}

// --ceilings: the generator's allocations on the first bucket against the ratchet.
int check_ceilings(const std::vector<BucketResult>& results) {
  const BucketResult* under_50 = nullptr;
  for (const BucketResult& r : results) {
    if (r.name == "<50") under_50 = &r;
  }
  if (under_50 == nullptr || under_50->positions == 0) {
    std::printf("ceilings: FAILED - no turn start under 50 boards was measured\n");
    return 1;
  }
  int failures = 0;
  const auto hold = [&](const char* row_name, double ceiling) {
    double measured = std::nan("");
    for (const Row& row : under_50->rows) {
      if (row.name == row_name) measured = row.allocations;
    }
    // A count that was not measured fails too: NaN is not at or under any ceiling.
    const bool ok = measured <= ceiling;
    std::printf("ceilings: %-8s %8.2f allocations per position, ceiling %.2f: %s\n", row_name, measured, ceiling,
                ok ? "ok" : "FAILED");
    if (!ok) ++failures;
  };
  hold("build", kBuildCeiling);
  hold("K turns", kTurnsCeiling);
  return failures == 0 ? 0 : 1;
}

int run(int argc, char** argv) {
  const Options opt = parse_options(argc, argv);
  if (!std::filesystem::is_directory(opt.records)) {
    throw std::invalid_argument("--records: not a directory: " + opt.records);
  }
  const bool pinned = opt.pin && pin_this_thread();
  std::fprintf(stderr, "fractal-legality: thread %s\n", pinned ? "pinned" : "not pinned");
  g_pool = opt.pool;
  const std::vector<Bucket> buckets = load_buckets(opt.records, opt.stride);
  if (opt.verify) return verify(buckets, opt);
  std::vector<BucketResult> results;
  for (const Bucket& b : buckets) {
    std::fprintf(stderr, "fractal-legality: %s: %zu positions\n", b.name, b.positions.size());
    results.push_back(measure(b, opt));
  }
  print(results, opt);
  if (!opt.json.empty()) write_json(results, opt);
  return opt.ceilings ? check_ceilings(results) : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fractal-legality: %s\n", e.what());
    return 2;
  }
}
