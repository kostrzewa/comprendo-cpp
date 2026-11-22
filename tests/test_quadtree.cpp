/// \file
/// \brief Tests for the Barnes–Hut quadtree.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

#include "Comprendo/QuadTree.hpp"
#include "Comprendo/Random.hpp"

namespace
{
  using Comprendo::Body;
  using Comprendo::QuadTree;
  using Comprendo::Vec2;

  /// Create bodies at random positions with random masses and radii.
  ///
  /// \param count Number of bodies.
  /// \param seed Generator seed.
  /// \return Bodies in a 1000 x 1000 square.
  std::vector<Body>
  random_bodies(std::size_t count, std::uint64_t seed)
  {
    Comprendo::SplitMix64 random {seed};
    std::vector<Body> bodies(count);
    for (Body &body : bodies)
    {
      body.position = {
        .x = static_cast<float>(random.next_unit() * 1000.0),
        .y = static_cast<float>(random.next_unit() * 1000.0),
      };
      body.mass = static_cast<float>(0.5 + random.next_unit() * 4.0);
      body.radius = static_cast<float>(1.0 + random.next_unit() * 10.0);
    }
    return bodies;
  }

  /// Exact pairwise repulsion with the same law as QuadTree::repulsion(),
  /// accumulated in double precision as the reference for the float kernel.
  ///
  /// \param bodies All bodies.
  /// \param i Body receiving the force.
  /// \param strength Force constant.
  /// \return Force on body \p i.
  Vec2
  brute_force(const std::vector<Body> &bodies, std::size_t i, double strength)
  {
    double fx = 0.0;
    double fy = 0.0;
    for (std::size_t j = 0; j < bodies.size(); ++j)
    {
      if (j == i)
      {
        continue;
      }
      const double dx = static_cast<double>(bodies[i].position.x) - bodies[j].position.x;
      const double dy = static_cast<double>(bodies[i].position.y) - bodies[j].position.y;
      const double distance = std::sqrt(dx * dx + dy * dy);
      const double magnitude
        = strength * static_cast<double>(bodies[i].mass) * bodies[j].mass / distance;
      fx += magnitude * dx / distance;
      fy += magnitude * dy / distance;
    }
    return {.x = static_cast<float>(fx), .y = static_cast<float>(fy)};
  }

  /// Length of the difference of two vectors.
  ///
  /// \param a First vector.
  /// \param b Second vector.
  /// \return |a - b|.
  double
  difference(const Vec2 &a, const Vec2 &b)
  {
    return std::hypot(a.x - b.x, a.y - b.y);
  }
} // namespace

/// The layout trusts Barnes–Hut forces, so the blocked single-precision kernel
/// must reproduce the exact sum when never approximating and stay close when it
/// does. Builds a tree over 300 random bodies (not a multiple of the 16-lane
/// block, so padding lanes are exercised), compares theta = 0 against a
/// double-precision brute-force sum per body (relative error below 1e-4, float
/// rounding), and theta = 0.5 against the same sums (total error below 2 percent).
TEST_CASE("barnes-hut matches exact forces", "[quadtree]")
{
  const std::vector<Body> bodies = random_bodies(300, 7);
  const QuadTree tree {bodies};
  std::vector<Vec2> exact(bodies.size());
  std::vector<Vec2> approximate(bodies.size());
  tree.repulsion(0.0F, 2.0F, exact);
  tree.repulsion(0.5F, 2.0F, approximate);
  double exact_total = 0.0;
  double approx_error = 0.0;
  bool exact_match = true;
  for (std::size_t i = 0; i < bodies.size(); ++i)
  {
    const Vec2 expected = brute_force(bodies, i, 2.0);
    const double magnitude = std::hypot(expected.x, expected.y);
    exact_match = exact_match && difference(exact[i], expected) <= 1e-4 * magnitude;
    approx_error += difference(approximate[i], expected);
    exact_total += magnitude;
  }
  CHECK(exact_match);
  CHECK(approx_error / exact_total < 0.02);
}

/// Leaves are evaluated on different threads in arbitrary order, so each leaf's
/// result must not depend on which leaves ran before it. Evaluates all leaves of
/// a 2000-body tree forwards and backwards and checks the forces are bit-identical,
/// which is what makes the layout independent of the thread count.
TEST_CASE("leaf evaluation order does not change forces", "[quadtree]")
{
  const std::vector<Body> bodies = random_bodies(2000, 3);
  const QuadTree tree {bodies};
  std::vector<Vec2> forward(bodies.size());
  std::vector<Vec2> backward(bodies.size());
  for (std::size_t leaf = 0; leaf < tree.leaf_count(); ++leaf)
  {
    tree.repulsion_for_leaf(leaf, 0.8F, 1.0F, forward);
  }
  for (std::size_t leaf = tree.leaf_count(); leaf-- > 0;)
  {
    tree.repulsion_for_leaf(leaf, 0.8F, 1.0F, backward);
  }
  CHECK(tree.leaf_count() >= bodies.size() / QuadTree::kLanes);
  CHECK(forward == backward);
}

/// Coincident bodies occur in degenerate input; the kernel must stay finite
/// (no division by zero) and leave their separation to the collision sweep.
/// Places three bodies on one point and a fourth elsewhere, computes forces with
/// and without approximation, and checks every component is finite, the
/// coincident trio only feels the fourth body (identical forces), and the fourth
/// body is pushed away from them.
TEST_CASE("coincident bodies get finite forces", "[quadtree]")
{
  std::vector<Body> bodies(3, Body {.position = {.x = 5.0, .y = 5.0}, .mass = 1.0});
  bodies.push_back({.position = {.x = 15.0, .y = 5.0}, .mass = 1.0});
  const QuadTree tree {bodies};
  for (const float theta : {0.0F, 0.8F})
  {
    std::vector<Vec2> forces(bodies.size());
    tree.repulsion(theta, 1.0F, forces);
    for (const Vec2 &force : forces)
    {
      CHECK(std::isfinite(force.x));
      CHECK(std::isfinite(force.y));
    }
    CHECK(forces[0] == forces[1]);
    CHECK(forces[1] == forces[2]);
    CHECK(forces[0].x < 0.0);
    CHECK(forces[3].x > 0.0);
  }
}

/// Overlap removal only looks at candidates from for_each_near(), so it must
/// never miss a truly overlapping pair. For 400 random disks, collects the
/// candidates of each disk (with a reach of 2 added to every radius) and checks
/// every pair found to overlap by brute force with that reach is among them, and
/// that a disk is never reported as its own neighbour.
TEST_CASE("neighbour query finds every overlapping disk", "[quadtree]")
{
  constexpr float kReach = 2.0F;
  const std::vector<Body> bodies = random_bodies(400, 11);
  const QuadTree tree {bodies};
  bool complete = true;
  bool no_self = true;
  for (std::size_t i = 0; i < bodies.size(); ++i)
  {
    std::set<std::size_t> candidates;
    tree.for_each_near(i, kReach, [&](std::size_t j) { candidates.insert(j); });
    no_self = no_self && !candidates.contains(i);
    for (std::size_t j = 0; j < bodies.size(); ++j)
    {
      const float reach = bodies[i].radius + bodies[j].radius + (2.0F * kReach);
      if (j != i && difference(bodies[i].position, bodies[j].position) < reach)
      {
        complete = complete && candidates.contains(j);
      }
    }
  }
  CHECK(complete);
  CHECK(no_self);
}

/// The tree splits cells by reading two code bits per level, which is only
/// correct if the code interleaves x and y bit by bit; a wrong spreading mask
/// (for example the 3D one) still sorts plausibly but builds wrong quadrants.
/// Checks codes of small coordinates against the Z-order by hand, the largest
/// 21-bit coordinates against the 42-bit maximum, and that coordinates are
/// truncated to 21 bits.
TEST_CASE("morton codes interleave x and y bits", "[quadtree]")
{
  using Comprendo::morton_code;
  CHECK(morton_code(0, 0) == 0);
  CHECK(morton_code(1, 0) == 1);
  CHECK(morton_code(0, 1) == 2);
  CHECK(morton_code(1, 1) == 3);
  CHECK(morton_code(2, 0) == 4);
  CHECK(morton_code(0, 2) == 8);
  CHECK(morton_code(3, 5) == 0b100111);
  constexpr std::uint32_t kMax = (1U << Comprendo::kMortonCoordinateBits) - 1U;
  CHECK(morton_code(kMax, kMax) == (std::uint64_t {1} << 42U) - 1U);
  CHECK(morton_code(kMax, 0) == 0x15555555555ULL);
  CHECK(morton_code(kMax + 1U, 0) == 0);
}
