#pragma once

/// \file
/// \brief Deterministic hierarchical force-directed layout with Barnes–Hut repulsion.

#include <cstdint>
#include <vector>

#include "Comprendo/Chart.hpp"
#include "Comprendo/Hierarchy.hpp"

namespace Comprendo
{
  /// Tuning parameters of compute_layout(). Defaults suit charts of 10^2–10^5 nodes.
  struct LayoutOptions
  {
    /// Seed mixed with each group key for the initial jitter.
    std::uint64_t seed = 0x00c0ffee5eedULL;
    /// Force-simulation iterations for groups of up to reference_siblings children.
    std::uint32_t iterations = 100;
    /// Iterations never drop below this for large groups.
    std::uint32_t min_iterations = 20;
    /// Sibling count above which iterations shrink with 1/sqrt(children).
    std::uint32_t reference_siblings = 1024;
    /// Barnes–Hut opening angle; 0 is exact, larger is faster and coarser.
    float theta = 0.8F;
    /// Radius of a group without children.
    float leaf_radius = 6.0F;
    /// Gap kept between sibling disks and between children and their container.
    float padding = 4.0F;
    /// Repulsion strength.
    float repulsion = 1.0F;
    /// Spring strength for edges between sibling subtrees.
    float attraction = 1.0F;
    /// Pull towards the group centre, keeping disconnected siblings together.
    /// Against the 1/d repulsion, siblings settle at a radius of about
    /// spacing * sqrt(n / gravity). Too little leaves every nesting level sparse
    /// (the slack compounds); too much compresses large groups so that removing
    /// overlaps dominates the run time. 3.0 balances both on real codebases.
    float gravity = 3.0F;
    /// Maximum overlap relaxation sweeps after the simulation. Sweeps stop once
    /// the worst overlap is shallow; what remains is removed by uniformly
    /// expanding the group, which always succeeds.
    std::uint32_t overlap_passes = 256;
  };

  /// Placement of one hierarchy group: a disk enclosing its children.
  struct GroupPlacement
  {
    /// Centre in chart coordinates.
    Position centre;
    /// Radius of the enclosing disk.
    float radius = 0.0F;

    /// Memberwise equality.
    bool
    operator==(const GroupPlacement &) const = default;
  };

  /// Result of compute_layout().
  struct Layout
  {
    /// Position of each chart node, parallel to Chart::nodes; nodes of one group
    /// (a merged namespace) share the group's centre.
    std::vector<Position> nodes;
    /// Placement of each hierarchy group, parallel to Hierarchy::groups.
    std::vector<GroupPlacement> groups;

    /// Memberwise equality.
    bool
    operator==(const Layout &) const = default;
  };

  /// Lay out a chart along a hierarchy.
  ///
  /// Groups are laid out bottom-up: the children of each group are disks placed
  /// by a fixed number of force iterations (Barnes–Hut repulsion, springs for
  /// non-`Contains` edges between sibling subtrees, gravity), then separated so
  /// they do not overlap; the group's radius encloses them. Groups with more than
  /// LayoutOptions::reference_siblings children run proportionally fewer
  /// iterations, and their forces are computed on several threads without
  /// affecting the result. Positions are then
  /// made absolute top-down, with the root centred at the origin. The result is
  /// a pure function of the chart, the hierarchy, and the options.
  ///
  /// \param chart Chart whose nodes and edges are laid out.
  /// \param hierarchy Grouping of \p chart, from build_hierarchy().
  /// \param options Tuning parameters.
  /// \return Node positions and group disks.
  [[nodiscard]]
  Layout
  compute_layout(const Chart &chart, const Hierarchy &hierarchy, const LayoutOptions &options = {});
} // namespace Comprendo
