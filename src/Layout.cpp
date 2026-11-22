/// \file
/// \brief Deterministic hierarchical force-directed layout with Barnes–Hut repulsion.

#include "Comprendo/Layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <ranges>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Comprendo/QuadTree.hpp"
#include "Comprendo/Random.hpp"
#include "Profiler/Profiler.hpp"

namespace
{
  using Comprendo::Body;
  using Comprendo::Hierarchy;
  using Comprendo::LayoutOptions;
  using Comprendo::Vec2;

  /// Cosine of the golden angle π(3 − √5), hard-coded so no libm call is needed.
  constexpr float kGoldenCos = -0.73736888F;
  /// Sine of the golden angle.
  constexpr float kGoldenSin = 0.67549029F;
  /// Spread factor of the initial spiral relative to dense packing.
  constexpr float kSpiralSpread = 1.25F;
  /// Initial jitter as a fraction of the ideal spacing.
  constexpr float kJitter = 0.1F;
  /// Distance below which two bodies are treated as coincident.
  constexpr float kEpsilon = 1e-9F;

  /// An aggregated edge between two sibling groups, as local body indices.
  struct Spring
  {
    /// First body.
    std::size_t a = 0;
    /// Second body.
    std::size_t b = 0;
    /// Summed edge weight.
    float weight = 0.0F;
  };

  /// Length of a vector.
  ///
  /// \param v Vector.
  /// \return Euclidean length.
  float
  length(const Vec2 &v)
  {
    return std::sqrt(v.x * v.x + v.y * v.y);
  }

  /// Order groups so that every child comes before its parent.
  ///
  /// \param hierarchy Hierarchy to traverse.
  /// \return Group indices in post-order.
  std::vector<std::size_t>
  post_order(const Hierarchy &hierarchy)
  {
    std::vector<std::size_t> order;
    order.reserve(hierarchy.groups.size());
    std::vector<std::pair<std::size_t, std::size_t>> stack {{0, 0}};
    while (!stack.empty())
    {
      auto &[group, next_child] = stack.back();
      const auto &children = hierarchy.groups[group].children;
      if (next_child < children.size())
      {
        const std::size_t child = children[next_child++];
        stack.emplace_back(child, 0);
        continue;
      }
      order.push_back(group);
      stack.pop_back();
    }
    return order;
  }

  /// Aggregate non-`Contains` edges into springs between sibling groups.
  ///
  /// An edge between nodes in groups g and h becomes a spring between the two
  /// children of their deepest common ancestor that contain g and h. Edges
  /// inside one group or between a group and its own descendant are skipped.
  ///
  /// \param chart Chart providing the edges.
  /// \param hierarchy Grouping of \p chart.
  /// \param local_index Index of each group within its parent's children.
  /// \return Springs per group, keyed by the common ancestor, in deterministic order.
  std::vector<std::vector<Spring>>
  collect_springs(
    const Comprendo::Chart &chart,
    const Hierarchy &hierarchy,
    const std::vector<std::size_t> &local_index
  )
  {
    std::vector<std::size_t> depth(hierarchy.groups.size(), 0);
    for (const std::size_t group : post_order(hierarchy) | std::views::reverse)
    {
      for (const std::size_t child : hierarchy.groups[group].children)
      {
        depth[child] = depth[group] + 1;
      }
    }

    std::unordered_map<std::uint64_t, std::size_t> index_of_id;
    for (std::size_t i = 0; i < chart.nodes.size(); ++i)
    {
      index_of_id.emplace(chart.nodes[i].id, i);
    }

    std::vector<std::vector<std::tuple<std::size_t, std::size_t, float>>> raw(
      hierarchy.groups.size()
    );
    for (const Comprendo::Edge &edge : chart.edges)
    {
      const auto src = index_of_id.find(edge.src);
      const auto dst = index_of_id.find(edge.dst);
      if (
        edge.kind == Comprendo::EdgeKind::Contains || src == index_of_id.end()
        || dst == index_of_id.end()
      )
      {
        continue;
      }
      std::size_t a = hierarchy.group_of_node[src->second];
      std::size_t b = hierarchy.group_of_node[dst->second];
      while (depth[a] > depth[b])
      {
        a = hierarchy.groups[a].parent;
      }
      while (depth[b] > depth[a])
      {
        b = hierarchy.groups[b].parent;
      }
      if (a == b)
      {
        continue;
      }
      while (hierarchy.groups[a].parent != hierarchy.groups[b].parent)
      {
        a = hierarchy.groups[a].parent;
        b = hierarchy.groups[b].parent;
      }
      const auto [first, second] = std::minmax(local_index[a], local_index[b]);
      raw[hierarchy.groups[a].parent].emplace_back(first, second, edge.weight);
    }

    std::vector<std::vector<Spring>> springs(hierarchy.groups.size());
    for (std::size_t group = 0; group < raw.size(); ++group)
    {
      std::ranges::sort(raw[group]);
      for (const auto &[a, b, weight] : raw[group])
      {
        if (!springs[group].empty() && springs[group].back().a == a && springs[group].back().b == b)
        {
          springs[group].back().weight += weight;
        }
        else
        {
          springs[group].push_back({.a = a, .b = b, .weight = weight});
        }
      }
    }
    return springs;
  }

  /// Place bodies on a golden-angle spiral, largest first, with seeded jitter.
  ///
  /// \param bodies Bodies with radii set; positions are written.
  /// \param padding Gap between disks.
  /// \param spacing Ideal spacing, scaling the jitter.
  /// \param random Jitter source.
  void
  seed_positions(
    std::vector<Body> &bodies, float padding, float spacing, Comprendo::SplitMix64 &random
  )
  {
    std::vector<std::size_t> order(bodies.size());
    std::ranges::iota(order, std::size_t {0});
    std::ranges::stable_sort(
      order, [&bodies](std::size_t l, std::size_t r) { return bodies[l].radius > bodies[r].radius; }
    );
    Vec2 direction {.x = 1.0F, .y = 0.0F};
    float area = 0.0F;
    for (const std::size_t index : order)
    {
      const float extent = bodies[index].radius + padding;
      const float distance = kSpiralSpread * std::sqrt(area + extent * extent / 2.0F);
      area += extent * extent;
      const float jitter_x = (static_cast<float>(random.next_unit()) - 0.5F) * kJitter * spacing;
      const float jitter_y = (static_cast<float>(random.next_unit()) - 0.5F) * kJitter * spacing;
      bodies[index].position
        = {.x = direction.x * distance + jitter_x, .y = direction.y * distance + jitter_y};
      direction = {
        .x = direction.x * kGoldenCos - direction.y * kGoldenSin,
        .y = direction.x * kGoldenSin + direction.y * kGoldenCos,
      };
    }
  }

  /// Number of bodies below which work stays on the calling thread.
  constexpr std::size_t kParallelThreshold = 2048;

  /// Threads to use for a piece of work.
  ///
  /// \param bodies Number of bodies involved.
  /// \param allow False when the caller already runs on a worker thread.
  /// \return 1 for small or nested work, otherwise the hardware concurrency.
  std::size_t
  threads_for(std::size_t bodies, bool allow)
  {
    if (!allow || bodies < kParallelThreshold)
    {
      return 1;
    }
    return std::max(1U, std::thread::hardware_concurrency());
  }

  /// Run a function for every index in [0, count) on up to \p threads threads.
  /// Each index is handled exactly once, so results written per index do not
  /// depend on the number of threads.
  ///
  /// \param count Number of indices.
  /// \param threads Number of threads; 1 runs on the calling thread.
  /// \param body Function called with each index; must only write per-index data.
  template <typename Function>
  void
  parallel_for(std::size_t count, std::size_t threads, const Function &body)
  {
    if (threads <= 1 || count <= 1)
    {
      for (std::size_t i = 0; i < count; ++i)
      {
        body(i);
      }
      return;
    }
    const std::size_t chunk = (count + threads - 1) / threads;
    std::vector<std::jthread> workers;
    workers.reserve(threads);
    for (std::size_t first = 0; first < count; first += chunk)
    {
      const std::size_t last = std::min(count, first + chunk);
      workers.emplace_back(
        [&body, first, last]
        {
          for (std::size_t i = first; i < last; ++i)
          {
            body(i);
          }
        }
      );
    }
  }

  /// Run one Jacobi sweep pushing overlapping disks apart.
  ///
  /// Every body sums the pushes from its overlapping neighbours at the old
  /// positions, then all bodies move at once. Each pair's overlap is split by
  /// mass, the heavier body moving less (half each for equal masses), along the
  /// line between centres (along x, by index order, for coincident pairs).
  /// Without the mass split, a large disk overlapping many small ones would sum
  /// all their pushes and overshoot. Bodies are independent, so the sweep runs on
  /// several threads with a thread-count-independent result.
  ///
  /// \param bodies Bodies to separate; positions updated.
  /// \param padding Required gap between disks.
  /// \param threads Threads to use.
  /// \return The largest required/actual distance ratio among overlapping
  ///         pairs before the sweep; 1 when none overlapped.
  float
  relax_overlaps(std::vector<Body> &bodies, float padding, std::size_t threads)
  {
    const Comprendo::QuadTree tree {bodies};
    std::vector<Vec2> shift(bodies.size());
    std::vector<float> worst(bodies.size(), 1.0F);
    parallel_for(
      bodies.size(),
      threads,
      [&](std::size_t i)
      {
        tree.for_each_near(
          i,
          padding / 2.0F,
          [&](std::size_t j)
          {
            const Vec2 delta {
              .x = bodies[i].position.x - bodies[j].position.x,
              .y = bodies[i].position.y - bodies[j].position.y,
            };
            const float distance = length(delta);
            const float needed = bodies[i].radius + bodies[j].radius + padding;
            if (distance >= needed)
            {
              return;
            }
            const Vec2 unit = distance >= kEpsilon
              ? Vec2 {.x = delta.x / distance, .y = delta.y / distance}
              : Vec2 {.x = i < j ? -1.0F : 1.0F, .y = 0.0F};
            const float share = bodies[j].mass / (bodies[i].mass + bodies[j].mass);
            const float push = (needed - distance) * share;
            shift[i].x += unit.x * push;
            shift[i].y += unit.y * push;
            worst[i] = std::max(worst[i], needed / std::max(distance, kEpsilon));
          }
        );
      }
    );
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
      bodies[i].position.x += shift[i].x;
      bodies[i].position.y += shift[i].y;
    }
    return std::ranges::max(worst, {}, [](float w) { return w; });
  }

  /// Scale positions away from the centroid just enough that no pair overlaps.
  ///
  /// Scaling multiplies every distance by the same factor, so choosing the
  /// largest required/actual distance ratio over overlapping pairs separates all
  /// of them while keeping already separated pairs apart. Coincident pairs are
  /// first nudged apart deterministically so the ratio stays finite.
  ///
  /// \param bodies Bodies to separate; positions updated.
  /// \param padding Required gap between disks.
  void
  expand_to_separate(std::vector<Body> &bodies, float padding)
  {
    const Comprendo::QuadTree tree {bodies};
    float scale = 1.0F;
    for (std::size_t i = 0; i < bodies.size(); ++i)
    {
      tree.for_each_near(
        i,
        padding / 2.0F,
        [&](std::size_t j)
        {
          if (j <= i)
          {
            return;
          }
          const float needed = bodies[i].radius + bodies[j].radius + padding;
          float distance = length({
            .x = bodies[j].position.x - bodies[i].position.x,
            .y = bodies[j].position.y - bodies[i].position.y,
          });
          if (distance < kEpsilon)
          {
            bodies[j].position.x += needed;
            distance = needed;
          }
          scale = std::max(scale, needed / distance);
        }
      );
    }
    if (scale <= 1.0F)
    {
      return;
    }
    float total_mass = 0.0F;
    Vec2 centroid;
    for (const Body &body : bodies)
    {
      total_mass += body.mass;
      centroid.x += body.mass * body.position.x;
      centroid.y += body.mass * body.position.y;
    }
    centroid = {.x = centroid.x / total_mass, .y = centroid.y / total_mass};
    constexpr float kSafety = 1.0F + 1e-5F;
    for (Body &body : bodies)
    {
      body.position.x = centroid.x + (body.position.x - centroid.x) * scale * kSafety;
      body.position.y = centroid.y + (body.position.y - centroid.y) * scale * kSafety;
    }
  }

  /// Overlap ratio below which the remaining overlaps are removed by expansion.
  constexpr float kShallowOverlap = 1.05F;

  /// Remove all overlaps: relaxation sweeps until the remaining overlaps are
  /// shallow (or the pass limit is hit), then an exact uniform expansion.
  ///
  /// Expanding resolves any overlap but scales the whole group by the worst
  /// ratio, so deep overlaps are left to the local sweeps first.
  ///
  /// \param bodies Bodies to separate; positions updated.
  /// \param padding Required gap between disks.
  /// \param passes Maximum number of relaxation sweeps before expanding.
  /// \param threads Threads to use for the sweeps.
  void
  remove_overlaps(
    std::vector<Body> &bodies, float padding, std::uint32_t passes, std::size_t threads
  )
  {
    for (std::uint32_t pass = 0; pass < passes; ++pass)
    {
      const float worst = relax_overlaps(bodies, padding, threads);
      if (worst <= 1.0F)
      {
        return;
      }
      if (worst <= kShallowOverlap)
      {
        break;
      }
    }
    expand_to_separate(bodies, padding);
  }

  /// Iterations for a group: the configured count up to reference_siblings
  /// children, then shrinking with 1/sqrt(children) down to min_iterations.
  ///
  /// \param children Number of bodies in the group.
  /// \param options Tuning parameters.
  /// \return Iteration count.
  std::uint32_t
  iterations_for(std::size_t children, const LayoutOptions &options)
  {
    if (children <= options.reference_siblings)
    {
      return options.iterations;
    }
    const float scale
      = std::sqrt(static_cast<float>(options.reference_siblings) / static_cast<float>(children));
    const auto scaled = static_cast<std::uint32_t>(static_cast<float>(options.iterations) * scale);
    return std::max(options.min_iterations, scaled);
  }

  /// Final step limit of the simulation, as a fraction of the leaf spacing.
  constexpr float kFinalTemperature = 0.1F;

  /// Run the force simulation for one group's children.
  ///
  /// Steps are capped by a temperature that cools geometrically from the group's
  /// extent to a tenth of the leaf spacing: early steps arrange the group, late
  /// steps are small enough for the per-iteration collision sweep to settle it.
  ///
  /// \param bodies Bodies with radii, masses, and initial positions; updated.
  /// \param springs Springs between bodies.
  /// \param options Tuning parameters.
  /// \param spacing Ideal distance between neighbouring leaves.
  /// \param threads Threads to use for forces and collision sweeps.
  void
  simulate(
    std::vector<Body> &bodies,
    const std::vector<Spring> &springs,
    const LayoutOptions &options,
    float spacing,
    std::size_t threads
  )
  {
    const std::uint32_t iterations = iterations_for(bodies.size(), options);
    float total_area = 0.0F;
    for (const Body &body : bodies)
    {
      total_area += body.radius * body.radius;
    }
    const float start_temperature = std::max(spacing, std::sqrt(total_area));
    const float end_temperature = kFinalTemperature * spacing;
    const float cooling = iterations > 1
      ? std::pow(end_temperature / start_temperature, 1.0F / static_cast<float>(iterations - 1))
      : 1.0F;
    float temperature = start_temperature;
    const float repulsion = options.repulsion * spacing * spacing;
    std::vector<Vec2> forces(bodies.size());
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration, temperature *= cooling)
    {
      const Comprendo::QuadTree tree {bodies};
      parallel_for(
        tree.leaf_count(),
        threads,
        [&](std::size_t leaf) { tree.repulsion_for_leaf(leaf, options.theta, repulsion, forces); }
      );
      for (std::size_t i = 0; i < bodies.size(); ++i)
      {
        forces[i].x -= options.gravity * bodies[i].mass * bodies[i].position.x;
        forces[i].y -= options.gravity * bodies[i].mass * bodies[i].position.y;
      }
      for (const Spring &spring : springs)
      {
        const Vec2 delta {
          .x = bodies[spring.b].position.x - bodies[spring.a].position.x,
          .y = bodies[spring.b].position.y - bodies[spring.a].position.y,
        };
        const float distance = length(delta);
        const float rest = bodies[spring.a].radius + bodies[spring.b].radius + options.padding;
        if (distance <= rest)
        {
          continue;
        }
        const float stretch = distance - rest;
        const float magnitude = options.attraction * spring.weight * stretch * stretch / spacing;
        const Vec2 pull {.x = magnitude * delta.x / distance, .y = magnitude * delta.y / distance};
        forces[spring.a].x += pull.x;
        forces[spring.a].y += pull.y;
        forces[spring.b].x -= pull.x;
        forces[spring.b].y -= pull.y;
      }
      for (std::size_t i = 0; i < bodies.size(); ++i)
      {
        const float magnitude = length(forces[i]);
        if (magnitude > kEpsilon)
        {
          const float acceleration = magnitude / bodies[i].mass;
          const float step = std::min(acceleration, temperature) / magnitude;
          bodies[i].position.x += forces[i].x * step;
          bodies[i].position.y += forces[i].y * step;
        }
      }
      relax_overlaps(bodies, options.padding, threads);
    }
  }

  /// Inputs shared by all group layouts of one compute_layout() call.
  struct GroupContext
  {
    /// Hierarchy being laid out; never null.
    const Hierarchy *hierarchy = nullptr;
    /// Springs per group; never null.
    const std::vector<std::vector<Spring>> *springs = nullptr;
    /// Tuning parameters; never null.
    const LayoutOptions *options = nullptr;
    /// Ideal distance between neighbouring leaves.
    float spacing = 0.0F;
    /// Radius of every group laid out so far; children are read, the group is
    /// written. Never null.
    std::vector<float> *radius = nullptr;
    /// Offset of every group from its parent's centre; the group's children are
    /// written. Never null.
    std::vector<Vec2> *local = nullptr;
  };

  /// Lay out the children of one group, whose own children are already laid out.
  ///
  /// Writes only the group's radius and its children's offsets, so groups of the
  /// same depth can be laid out concurrently.
  ///
  /// \param context Shared inputs and outputs.
  /// \param group Group index.
  /// \param threads Threads the group may use internally.
  void
  layout_group(const GroupContext &context, std::size_t group, std::size_t threads)
  {
    const LayoutOptions &options = *context.options;
    const std::vector<std::size_t> &children = context.hierarchy->groups[group].children;
    std::vector<Body> bodies(children.size());
    for (std::size_t i = 0; i < children.size(); ++i)
    {
      const float r = (*context.radius)[children[i]];
      bodies[i].radius = r;
      bodies[i].mass = (r * r) / (options.leaf_radius * options.leaf_radius);
    }
    if (children.size() > 1)
    {
      Comprendo::SplitMix64 random {options.seed ^ context.hierarchy->groups[group].key};
      seed_positions(bodies, options.padding, context.spacing, random);
      simulate(bodies, (*context.springs)[group], options, context.spacing, threads);
      remove_overlaps(bodies, options.padding, options.overlap_passes, threads);
    }

    constexpr float kInfinity = std::numeric_limits<float>::infinity();
    Vec2 low {.x = kInfinity, .y = kInfinity};
    Vec2 high {.x = -kInfinity, .y = -kInfinity};
    for (const Body &body : bodies)
    {
      low.x = std::min(low.x, body.position.x - body.radius);
      low.y = std::min(low.y, body.position.y - body.radius);
      high.x = std::max(high.x, body.position.x + body.radius);
      high.y = std::max(high.y, body.position.y + body.radius);
    }
    const Vec2 centre {.x = (low.x + high.x) / 2.0F, .y = (low.y + high.y) / 2.0F};
    float enclosing = 0.0F;
    for (std::size_t i = 0; i < children.size(); ++i)
    {
      const Vec2 offset {
        .x = bodies[i].position.x - centre.x, .y = bodies[i].position.y - centre.y
      };
      (*context.local)[children[i]] = offset;
      enclosing = std::max(enclosing, length(offset) + bodies[i].radius);
    }
    (*context.radius)[group] = std::max(options.leaf_radius, enclosing + options.padding);
  }
} // namespace

namespace Comprendo
{
  Layout
  compute_layout(const Chart &chart, const Hierarchy &hierarchy, const LayoutOptions &options)
  {
    const ProfileScope profile {"compute_layout"};
    const std::size_t group_count = hierarchy.groups.size();

    std::vector<std::size_t> local_index(group_count, 0);
    for (const HierarchyGroup &group : hierarchy.groups)
    {
      for (std::size_t i = 0; i < group.children.size(); ++i)
      {
        local_index[group.children[i]] = i;
      }
    }
    const std::vector<std::vector<Spring>> springs = collect_springs(chart, hierarchy, local_index);

    std::vector<std::size_t> depth(group_count, 0);
    std::size_t max_depth = 0;
    for (const std::size_t group : post_order(hierarchy) | std::views::reverse)
    {
      for (const std::size_t child : hierarchy.groups[group].children)
      {
        depth[child] = depth[group] + 1;
        max_depth = std::max(max_depth, depth[child]);
      }
    }
    std::vector<std::vector<std::size_t>> small_by_depth(max_depth + 1);
    std::vector<std::vector<std::size_t>> large_by_depth(max_depth + 1);
    for (std::size_t group = 0; group < group_count; ++group)
    {
      const std::size_t children = hierarchy.groups[group].children.size();
      if (children > 0)
      {
        (children < kParallelThreshold ? small_by_depth : large_by_depth)[depth[group]].push_back(
          group
        );
      }
    }

    std::vector<float> radius(group_count, options.leaf_radius);
    std::vector<Vec2> local(group_count);
    const GroupContext context {
      .hierarchy = &hierarchy,
      .springs = &springs,
      .options = &options,
      .spacing = 2.0F * options.leaf_radius + options.padding,
      .radius = &radius,
      .local = &local,
    };
    const std::size_t all_threads = threads_for(kParallelThreshold, true);
    for (std::size_t level = max_depth + 1; level-- > 0;)
    {
      {
        const ProfileScope small_profile {"layout_small_groups"};
        const std::vector<std::size_t> &small = small_by_depth[level];
        parallel_for(
          small.size(),
          small.size() > 1 ? all_threads : 1,
          [&](std::size_t i) { layout_group(context, small[i], 1); }
        );
      }
      const ProfileScope large_profile {"layout_large_groups"};
      for (const std::size_t group : large_by_depth[level])
      {
        layout_group(context, group, all_threads);
      }
    }

    Layout layout;
    layout.groups.resize(group_count);
    std::vector<Vec2> absolute(group_count);
    for (const std::size_t group : post_order(hierarchy) | std::views::reverse)
    {
      if (group != 0)
      {
        const Vec2 &parent = absolute[hierarchy.groups[group].parent];
        absolute[group] = {.x = parent.x + local[group].x, .y = parent.y + local[group].y};
      }
      layout.groups[group] = {
        .centre = {.x = absolute[group].x, .y = absolute[group].y},
        .radius = static_cast<float>(radius[group]),
      };
    }
    layout.nodes.resize(chart.nodes.size());
    for (std::size_t node = 0; node < chart.nodes.size(); ++node)
    {
      layout.nodes[node] = layout.groups[hierarchy.group_of_node[node]].centre;
    }
    return layout;
  }
} // namespace Comprendo
