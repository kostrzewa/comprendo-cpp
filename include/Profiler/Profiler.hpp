#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>

#include "Logger/Logger.hpp"

namespace Comprendo
{

  /// Lightweight hierarchical profiler with task-graph output.
  ///
  /// \c push(name) / \c pop() bracket scopes.  Every unique (parent_scope,
  /// name) pair is an aggregated *scope* — its wall-clock duration (inclusive of
  /// children) and call count are accumulated across all invocations.
  ///
  /// At the end, \c print_profile(max_depth) walks the scope DAG and prints
  /// a nested tree with per-scope call counts, inclusive times, and
  /// missing (self) time at every level.
  class Profiler
  {
  public:
    Profiler() = default;
    ~Profiler() = default;
    Profiler(const Profiler &) = delete;
    Profiler &
    operator=(const Profiler &) = delete;
    Profiler(Profiler &&) = delete;
    Profiler &
    operator=(Profiler &&) = delete;

    // ---- constants ------------------------------------------------------------

    /// Maximum number of trace events stored in the circular buffer.
    static constexpr size_t kMaxTraceEvents = 2000000;

    // ---- public types --------------------------------------------------------

    /// An aggregated profiling scope, uniquely identified by its parent scope
    /// and its name.  A single scope can be entered many times; call_count and
    /// total_ns accumulate across all invocations.
    struct Scope
    {
      std::string name;
      uint64_t id;        // unique scope id (1-based, 0 = sentinel root)
      uint64_t parent_id; // 0 for root-level scopes
      int depth;          // nesting depth (0 = outermost)
      size_t call_count;  // how many times this scope was entered
      double total_ns;    // inclusive wall time across all invocations
    };

    /// A single invocation recorded for Perfetto / Chrome trace output.
    struct TraceEvent
    {
      double ts_ns = 0.0;  // timestamp in ns relative to m_start_time
      double dur_ns = 0.0; // duration in ns
      int tid = 0;         // thread id
      std::string name;
    };

    // ---- public API ----------------------------------------------------------

    static Profiler &
    instance()
    {
      static Profiler instance;
      return instance;
    }

    static void
    init()
    {
#if !COMPRENDO_PROFILER_ENABLED
      return;
#endif
      m_timers.clear();
      m_scopes.clear();
      m_scopes.push_back({"<root>", 0, 0, -1, 0, 0.0}); // sentinel at index 0
      m_scope_map.clear();
      m_next_scope_id = 1;
      m_trace_events.clear();
      m_trace_events.resize(kMaxTraceEvents);
      m_trace_head = 0;
      m_trace_wrapped = false;
      m_tracing = true;
      [[maybe_unused]]
      auto &profiler = instance();
      m_start_time = std::chrono::high_resolution_clock::now();
    }

    /// Enter a named scope.  The scope identity is (parent_scope, name), so
    /// the same name under different callers produces distinct scope records.
    ///
    /// \param name Scope name.
    static void
    push(const std::string &name)
    {
#if !COMPRENDO_PROFILER_ENABLED
      return;
#endif
      if (m_scopes.empty())
      {
        return;
      }
      const uint64_t parent_id = m_timers.empty() ? 0 : m_timers.back().scope_id;
      const uint64_t scope_id = get_or_create_scope(name, parent_id);
      m_timers.push_back({scope_id, std::chrono::high_resolution_clock::now()});
    }

    /// Exit the innermost scope and accumulate its wall-clock time (inclusive
    /// of any children) into the corresponding aggregated Scope record.
    static void
    pop()
    {
#if !COMPRENDO_PROFILER_ENABLED
      return;
#endif
      if (m_scopes.empty())
      {
        return;
      }
      if (m_timers.empty())
      {
        Comprendo::Logger::error("[Profiler] Mismatched push/pop calls");
        return;
      }

      const auto end = std::chrono::high_resolution_clock::now();
      const TimerEntry &entry = m_timers.back();
      const double duration_ns = static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - entry.start_time).count()
      );

      // Build call chain: root -> child1 -> ... -> this scope
      std::string chain;
      for (size_t i = 0; i < m_timers.size(); ++i)
      {
        if (i > 0)
        {
          chain += " -> ";
        }
        chain += m_scopes[m_timers[i].scope_id].name;
      }

      Comprendo::Logger::debug("[Profiler] {}: {:.6e} us", chain, duration_ns * 1e-3);

      m_scopes[entry.scope_id].total_ns += duration_ns;
      m_scopes[entry.scope_id].call_count++;

      // Record raw trace event for Perfetto dump
      if (m_tracing)
      {
        const double ts_ns = static_cast<double>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(entry.start_time - m_start_time)
            .count()
        );
        TraceEvent &ev = m_trace_events[m_trace_head];
        ev.ts_ns = ts_ns;
        ev.dur_ns = duration_ns;
        ev.tid = 1;
        ev.name = m_scopes[entry.scope_id].name;
        m_trace_head = (m_trace_head + 1) % kMaxTraceEvents;
        if (m_trace_head == 0)
        {
          m_trace_wrapped = true;
        }
      }

      m_timers.pop_back();
    }

    /// Access the aggregated scope records (scope_id → Scope).
    /// Scope 0 is the implicit root.
    ///
    /// \return Scope records indexed by scope id.
    static const std::vector<Scope> &
    get_scopes()
    {
      return m_scopes;
    }

    /// Enable / disable per-invocation trace recording (on by default).
    ///
    /// \param on True to record trace events.
    static void
    set_tracing(bool on)
    {
      m_tracing = on;
    }

    /// Write the trace buffer to \p path in Chrome Trace Event JSON format.
    /// The file can be opened directly in Perfetto (ui.perfetto.dev) or
    /// chrome://tracing.
    ///
    /// \param path Output file.
    static void
    dump_perfetto(const std::string &path)
    {
#if !COMPRENDO_PROFILER_ENABLED
      return;
#endif
      std::ofstream f(path);
      if (!f)
      {
        Comprendo::Logger::error("[Profiler] Cannot open trace file: {}", path);
        return;
      }

      const size_t count = m_trace_wrapped ? kMaxTraceEvents : m_trace_head;
      const size_t start = m_trace_wrapped ? m_trace_head : 0;

      // Build a sorted list of (ts_ns, ph, trace_index) triples so that
      // B and E events are interleaved in strict chronological order.
      // Perfetto matches B/E pairs by processing them sequentially on a
      // thread — emitting out of order breaks the hierarchy.
      struct Point
      {
        double ts_ns;
        char ph;       // 'B' or 'E'
        size_t ev_idx; // index into m_trace_events
      };
      std::vector<Point> points;
      points.reserve(count * 2);

      for (size_t i = 0; i < count; ++i)
      {
        const size_t idx = (start + i) % kMaxTraceEvents;
        const TraceEvent &ev = m_trace_events[idx];
        points.push_back({ev.ts_ns, 'B', idx});
        points.push_back({ev.ts_ns + ev.dur_ns, 'E', idx});
      }

      std::sort(
        points.begin(),
        points.end(),
        [](const Point &a, const Point &b)
        {
          if (a.ts_ns != b.ts_ns)
          {
            return a.ts_ns < b.ts_ns;
          }
          return a.ph > b.ph; // 'E' before 'B' at the same timestamp
        }
      );

      f << std::fixed << std::setprecision(3); // µs with ns resolution
      f << "{\"traceEvents\":[\n";
      for (size_t i = 0; i < points.size(); ++i)
      {
        const Point &p = points[i];
        const TraceEvent &ev = m_trace_events[p.ev_idx];
        f << "{\"name\":\"" << ev.name << "\",\"ph\":\"" << p.ph << "\""
          << ",\"ts\":" << (p.ts_ns * 1e-3) << ",\"pid\":1" << ",\"tid\":" << ev.tid << "}";
        if (i + 1 < points.size())
        {
          f << ",\n";
        }
        else
        {
          f << "\n";
        }
      }
      f << "]}\n";

      Comprendo::Logger::info(
        "[Profiler] Trace written to {} ({} events, {} B/E pairs{})",
        path,
        count,
        points.size(),
        m_trace_wrapped ? ", buffer wrapped" : ""
      );
    }

    /// Print a nested profile report.
    ///
    /// Root-level scopes are printed as section headers, each followed by a
    /// tree of children.  Percentages at each level are relative to the
    /// parent scope's inclusive time.  \p max_depth controls how many levels
    /// of descendants are shown (0 = only root scopes).
    ///
    /// A grand-total footer reports program wall time, total accounted time,
    /// and total missing (unprofiled) time.
    ///
    /// \param max_depth Levels of descendants shown below each root scope.
    static void
    print_profile(int max_depth = 1)
    {
#if !COMPRENDO_PROFILER_ENABLED
      return;
#endif
      if (m_scopes.size() <= 1)
      {
        Comprendo::Logger::info("[Profiler] No scopes recorded.");
        return;
      }

      // Build child-index, sorted by total_ns descending.
      std::unordered_map<uint64_t, std::vector<uint64_t>> children;
      for (size_t i = 1; i < m_scopes.size(); ++i)
      {
        children[m_scopes[i].parent_id].push_back(m_scopes[i].id);
      }

      for (auto &[_, ids] : children)
      {
        std::sort(
          ids.begin(),
          ids.end(),
          [&](uint64_t a, uint64_t b) { return m_scopes[a].total_ns > m_scopes[b].total_ns; }
        );
      }

      const double wall_ns
        = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::high_resolution_clock::now() - m_start_time
        )
                                .count());

      Comprendo::Logger::info("[Profiler] ═══════════════════════════════════════════");
      Comprendo::Logger::info(
        "[Profiler] Profile  (wall: {:.4e} s, depth: {})", wall_ns * 1e-9, max_depth
      );
      Comprendo::Logger::info("[Profiler] ═══════════════════════════════════════════");

      const auto &root_ids = children[0];
      for (uint64_t root_id : root_ids)
      {
        const Scope &s = m_scopes[root_id];

        const double root_avg_us
          = (s.call_count > 0) ? (s.total_ns / static_cast<double>(s.call_count)) * 1e-3 : 0.0;
        const double root_pct = (wall_ns > 0.0) ? (s.total_ns / wall_ns) * 100.0 : 0.0;

        Comprendo::Logger::info("[Profiler]");
        Comprendo::Logger::info(
          "[Profiler] {:<40s} {:>10.4e} s    x{:<5} {:>10.0f} us/call  ({:>3.0f}% of wall)",
          s.name,
          s.total_ns * 1e-9,
          s.call_count,
          root_avg_us,
          root_pct
        );

        auto cit = children.find(root_id);
        const bool has_children = (cit != children.end() && !cit->second.empty());

        if (has_children && max_depth >= 1)
        {
          print_scope_children(
            root_id,
            children,
            s.total_ns,
            " ", // base indent for children of a root scope
            1,   // depth of the children being printed (always ≥ 1)
            max_depth
          );
        }

        if (has_children)
        {
          const double missing_ns = scope_missing(root_id, children);
          const double accounted_ns = s.total_ns - missing_ns;
          Comprendo::Logger::info("[Profiler]    ──────────────────────────────────────────");
          Comprendo::Logger::info(
            "[Profiler]    {:<40s} {:>10.4e} s   ({:>3.0f}%)",
            "accounted",
            accounted_ns * 1e-9,
            (s.total_ns > 0.0 ? (accounted_ns / s.total_ns) * 100.0 : 0.0)
          );
          Comprendo::Logger::info(
            "[Profiler]    {:<40s} {:>10.4e} s   ({:>3.0f}%)",
            "missing",
            missing_ns * 1e-9,
            (s.total_ns > 0.0 ? (missing_ns / s.total_ns) * 100.0 : 0.0)
          );
        }
        Comprendo::Logger::info("[Profiler]    ==========================================");
      }

      // ---- grand total --------------------------------------------------
      const double root_total_ns = all_scopes_ns();
      Comprendo::Logger::info("[Profiler]");
      Comprendo::Logger::info("[Profiler] ═══════════════════════════════════════════");
      Comprendo::Logger::info(
        "[Profiler] {:<40s} {:>10.4e} s   ({:>3.0f}% of wall)",
        "accounted (profiled)",
        root_total_ns * 1e-9,
        (wall_ns > 0.0 ? (root_total_ns / wall_ns) * 100.0 : 0.0)
      );
      Comprendo::Logger::info(
        "[Profiler] {:<40s} {:>10.4e} s   ({:>3.0f}% of wall)",
        "missing (unprofiled)",
        (wall_ns - root_total_ns) * 1e-9,
        (wall_ns > 0.0 ? ((wall_ns - root_total_ns) / wall_ns) * 100.0 : 0.0)
      );
      Comprendo::Logger::info("[Profiler] ═══════════════════════════════════════════");
    }

  private:
    // ---- internal types ------------------------------------------------------

    /// Lightweight entry on the active timer stack — just enough to restore
    /// context on pop.
    struct TimerEntry
    {
      uint64_t scope_id;
      std::chrono::high_resolution_clock::time_point start_time;
    };

    // ---- helpers -------------------------------------------------------------

    /// Return an existing scope matching (parent_id, name), or create one.
    static uint64_t
    get_or_create_scope(const std::string &name, uint64_t parent_id)
    {
      auto &name_map = m_scope_map[parent_id];
      auto it = name_map.find(name);
      if (it != name_map.end())
      {
        return it->second;
      }

      const uint64_t id = m_next_scope_id++;
      const int depth = static_cast<int>(m_timers.size()); // before push
      name_map[name] = id;
      m_scopes.push_back({name, id, parent_id, depth, 0, 0.0});
      return id;
    }

    /// Return the missing (self) time for a scope — its inclusive total minus
    /// the sum of its direct children's inclusive times.
    /// \p children must be a map from parent_scope_id → [child_scope_id …].
    static double
    scope_missing(
      uint64_t scope_id, const std::unordered_map<uint64_t, std::vector<uint64_t>> &children
    )
    {
      const double parent_total = m_scopes[scope_id].total_ns;
      double child_sum = 0.0;
      auto it = children.find(scope_id);
      if (it != children.end())
      {
        for (uint64_t child_id : it->second)
        {
          child_sum += m_scopes[child_id].total_ns;
        }
      }
      return parent_total - child_sum;
    }

    /// Sum of inclusive times of all root scopes (not the sentinel).
    static double
    all_scopes_ns()
    {
      double sum = 0.0;
      for (size_t i = 1; i < m_scopes.size(); ++i) // skip sentinel
      {
        if (m_scopes[i].parent_id == 0)
        {
          sum += m_scopes[i].total_ns;
        }
      }
      return sum;
    }

    /// Recursively print the children of \p parent_id as an indented tree.
    ///
    /// Always called with depth ≥ 1 — tree characters are always drawn.
    /// \p parent_total_ns is used for percentage calculations.
    /// \p ancestor_indent carries the visual prefix from ancestor levels
    /// (a string of "│ " and " " segments).
    static void
    print_scope_children(
      uint64_t parent_id,
      const std::unordered_map<uint64_t, std::vector<uint64_t>> &children,
      double parent_total_ns,
      const std::string &ancestor_indent,
      int depth,
      int max_depth
    )
    {
      auto it = children.find(parent_id);
      if (it == children.end() || it->second.empty())
      {
        return;
      }

      const auto &child_ids = it->second;
      for (size_t i = 0; i < child_ids.size(); ++i)
      {
        const uint64_t child_id = child_ids[i];
        const Scope &s = m_scopes[child_id];
        const bool is_last = (i == child_ids.size() - 1);

        // ---- tree-drawing prefix ------------------------------------------
        const std::string prefix = ancestor_indent + (is_last ? "└ " : "├ ");
        const std::string child_indent = ancestor_indent + (is_last ? " " : "│ ");

        // ---- percentage and average ---------------------------------------
        const double pct = (parent_total_ns > 0.0) ? (s.total_ns / parent_total_ns) * 100.0 : 0.0;
        const double avg_us
          = (s.call_count > 0) ? (s.total_ns / static_cast<double>(s.call_count)) * 1e-3 : 0.0;

        // ---- print the scope line -----------------------------------------
        Comprendo::Logger::info(
          "[Profiler] {}{:<40s} {:>10.4e} s    x{:<5} {:>10.0f} us/call  ({:>3.0f}%)",
          prefix,
          s.name,
          s.total_ns * 1e-9,
          s.call_count,
          avg_us,
          pct
        );

        // ---- recurse into grandchildren -----------------------------------
        auto grandchild_it = children.find(child_id);
        const bool has_children
          = (grandchild_it != children.end() && !grandchild_it->second.empty());

        if (has_children && depth < max_depth)
        {
          print_scope_children(child_id, children, s.total_ns, child_indent, depth + 1, max_depth);
        }

        // ---- missing line -------------------------------------------------
        if (has_children)
        {
          const double missing_ns = scope_missing(child_id, children);
          const double missing_pct = (s.total_ns > 0.0) ? (missing_ns / s.total_ns) * 100.0 : 0.0;
          Comprendo::Logger::info(
            "[Profiler] {}└ {:<40s} {:>10.4e} s                 ({:>3.0f}%)",
            child_indent,
            "missing",
            missing_ns * 1e-9,
            missing_pct
          );
        }
      }
    }

    // ---- data members --------------------------------------------------------

    /// Stack of currently active (unclosed) timer scopes.
    inline static std::vector<TimerEntry> m_timers;

    /// All aggregated scopes, indexed by scope_id (0 = sentinel root, unused).
    inline static std::vector<Scope> m_scopes;

    /// Two-level map for fast scope lookup:  parent_scope_id → (name → scope_id).
    /// Avoids string concatenation on the hot path.
    inline static std::unordered_map<uint64_t, std::unordered_map<std::string, uint64_t>>
      m_scope_map;

    /// Monotonically increasing counter for unique scope IDs.  0 is the
    /// sentinel root.
    inline static uint64_t m_next_scope_id = 1;

    /// Program-start timestamp for time-zero reference.
    // time_point's default constructor cannot throw.
    // NOLINTNEXTLINE(bugprone-throwing-static-initialization)
    inline static std::chrono::high_resolution_clock::time_point m_start_time;

    // ---- trace buffer (Perfetto / Chrome Trace Event output) -----------------

    /// Pre-allocated circular buffer of raw invocation events.
    inline static std::vector<TraceEvent> m_trace_events;

    /// Next write position in the circular buffer.
    inline static size_t m_trace_head = 0;

    /// True once the circular buffer has wrapped around at least once.
    inline static bool m_trace_wrapped = false;

    /// Toggle for trace recording (on by default, set_tracing(false) to mute).
    inline static bool m_tracing = true;

  }; // class Profiler

  /// RAII guard that brackets a profiler scope with Profiler::push and Profiler::pop.
  ///
  /// Both calls are no-ops until Profiler::init has run, so library code may use
  /// the guard unconditionally.
  class ProfileScope
  {
  public:
    /// Enter the scope \p name.
    ///
    /// \param name Scope name shown in the profile and trace.
    explicit ProfileScope(const std::string &name) { Profiler::push(name); }

    /// Leave the scope.
    // pop() throws only std::bad_alloc, and terminating then is acceptable.
    // NOLINTNEXTLINE(bugprone-exception-escape)
    ~ProfileScope() { Profiler::pop(); }

    ProfileScope(const ProfileScope &) = delete;
    ProfileScope &
    operator=(const ProfileScope &) = delete;
    ProfileScope(ProfileScope &&) = delete;
    ProfileScope &
    operator=(ProfileScope &&) = delete;
  };

} // namespace Comprendo
