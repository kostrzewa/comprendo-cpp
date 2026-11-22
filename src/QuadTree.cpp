/// \file
/// \brief Blocked Barnes–Hut quadtree: vectorizable repulsion and disk-overlap queries.

#include "Comprendo/QuadTree.hpp"

#include "Comprendo/RadixSort.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <ranges>
#include <span>
#include <vector>

namespace
{
  using Comprendo::Vec2;

  /// Floor for squared distances, so coincident pairs give finite zero forces.
  constexpr float kTinySquared = 1e-18F;

  /// Bits per coordinate in a Morton code.
  constexpr int kCoordinateBits = Comprendo::kMortonCoordinateBits;

  /// Low bits of a packed sort key holding the body index; the 42-bit Morton
  /// code fills the 42 bits above, so key order is (code, index) order.
  constexpr unsigned kIndexBits = 64 - (2 * kCoordinateBits);

  /// Mask selecting the body index of a packed sort key.
  constexpr std::uint64_t kIndexMask = (std::uint64_t {1} << kIndexBits) - 1;

  /// Spread the low 32 bits of a value so that bit k moves to bit 2k.
  ///
  /// \param value Value whose low 32 bits are spread.
  /// \return Value with a zero bit after every original bit.
  std::uint64_t
  spread_bits(std::uint64_t value)
  {
    value &= 0x00000000ffffffffULL;
    value = (value | (value << 16U)) & 0x0000ffff0000ffffULL;
    value = (value | (value << 8U)) & 0x00ff00ff00ff00ffULL;
    value = (value | (value << 4U)) & 0x0f0f0f0f0f0f0f0fULL;
    value = (value | (value << 2U)) & 0x3333333333333333ULL;
    value = (value | (value << 1U)) & 0x5555555555555555ULL;
    return value;
  }

  /// Accumulate the contribution of one source onto all lanes of a target block.
  ///
  /// Adds `m * (t - s) / max(|t - s|^2, tiny)` per lane. A lane at the source's
  /// position (itself, or a padding lane) gets zero because its offset is zero.
  /// The loop has a fixed trip count and no branches, so it vectorizes.
  ///
  /// \param x Target x coordinates.
  /// \param y Target y coordinates.
  /// \param sx Source x.
  /// \param sy Source y.
  /// \param mass Source mass.
  /// \param ax Accumulated x components.
  /// \param ay Accumulated y components.
  template <std::size_t N>
  void
  accumulate(
    const std::array<float, N> &x,
    const std::array<float, N> &y,
    float sx,
    float sy,
    float mass,
    std::array<float, N> &ax,
    std::array<float, N> &ay
  )
  {
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-constant-array-index): at() would block
    // vectorization
    for (std::size_t lane = 0; lane < N; ++lane)
    {
      const float dx = x[lane] - sx;
      const float dy = y[lane] - sy;
      const float weight = mass / std::max((dx * dx) + (dy * dy), kTinySquared);
      ax[lane] += weight * dx;
      ay[lane] += weight * dy;
    }
    // NOLINTEND(cppcoreguidelines-pro-bounds-constant-array-index)
  }

  /// A far cell summarized as one body.
  struct FarSource
  {
    /// Centre of mass.
    Vec2 centre;
    /// Total mass.
    float mass = 0.0F;
  };
} // namespace

namespace Comprendo
{
  std::uint64_t
  morton_code(std::uint32_t x, std::uint32_t y)
  {
    constexpr std::uint32_t kCoordinateMask = (1U << kMortonCoordinateBits) - 1U;
    return spread_bits(x & kCoordinateMask) | (spread_bits(y & kCoordinateMask) << 1U);
  }

  QuadTree::QuadTree(std::span<const Body> bodies) : m_bodies(bodies.begin(), bodies.end())
  {
    const std::size_t count = m_bodies.size();
    Vec2 low {.x = 0.0, .y = 0.0};
    float size = 1.0F;
    if (count > 0)
    {
      Vec2 high = m_bodies.front().position;
      low = high;
      for (const Body &body : m_bodies)
      {
        low.x = std::min(low.x, body.position.x);
        low.y = std::min(low.y, body.position.y);
        high.x = std::max(high.x, body.position.x);
        high.y = std::max(high.y, body.position.y);
      }
      size = std::max({high.x - low.x, high.y - low.y, std::numeric_limits<float>::min()});
    }

    constexpr auto kCells = static_cast<double>((1ULL << kCoordinateBits) - 1);
    const auto quantize = [&](float value, float origin)
    {
      const double scaled
        = std::floor(static_cast<double>(value - origin) / static_cast<double>(size) * kCells);
      return static_cast<std::uint64_t>(std::clamp(scaled, 0.0, kCells));
    };
    const auto code_at = [&](std::size_t i)
    {
      const Vec2 &p = m_bodies[i].position;
      return morton_code(
        static_cast<std::uint32_t>(quantize(p.x, low.x)),
        static_cast<std::uint32_t>(quantize(p.y, low.y))
      );
    };
    m_order.resize(count);
    m_codes.resize(count);
    if (count <= kIndexMask + 1)
    {
      std::vector<std::uint64_t> keys(count);
      for (std::size_t i = 0; i < count; ++i)
      {
        keys[i] = (code_at(i) << kIndexBits) | i;
      }
      radix_sort(keys);
      for (std::size_t i = 0; i < count; ++i)
      {
        m_order[i] = static_cast<std::uint32_t>(keys[i] & kIndexMask);
        m_codes[i] = keys[i] >> kIndexBits;
      }
    }
    else
    {
      std::vector<std::pair<std::uint64_t, std::uint32_t>> pairs(count);
      for (std::size_t i = 0; i < count; ++i)
      {
        pairs[i] = {code_at(i), static_cast<std::uint32_t>(i)};
      }
      std::ranges::sort(pairs);
      for (std::size_t i = 0; i < count; ++i)
      {
        m_codes[i] = pairs[i].first;
        m_order[i] = pairs[i].second;
      }
    }

    m_cells.reserve((2 * count / kLanes) + 8);
    m_cells.emplace_back();
    build(0, 0, count, 0);
  }

  void
  QuadTree::build(std::uint32_t cell, std::size_t begin, std::size_t end, int depth)
  {
    {
      Cell &current = m_cells[cell];
      constexpr float kInfinity = std::numeric_limits<float>::infinity();
      current.low = {.x = kInfinity, .y = kInfinity};
      current.high = {.x = -kInfinity, .y = -kInfinity};
      Vec2 weighted;
      for (std::size_t i = begin; i < end; ++i)
      {
        const Body &body = m_bodies[m_order[i]];
        current.low.x = std::min(current.low.x, body.position.x);
        current.low.y = std::min(current.low.y, body.position.y);
        current.high.x = std::max(current.high.x, body.position.x);
        current.high.y = std::max(current.high.y, body.position.y);
        current.mass += body.mass;
        weighted.x += body.mass * body.position.x;
        weighted.y += body.mass * body.position.y;
        current.max_radius = std::max(current.max_radius, body.radius);
      }
      if (current.mass > 0.0F)
      {
        current.centre = {.x = weighted.x / current.mass, .y = weighted.y / current.mass};
      }
      else if (begin < end)
      {
        current.centre = m_bodies[m_order[begin]].position;
      }
    }

    if (end - begin <= kLanes || depth >= kMaxDepth)
    {
      Cell &leaf = m_cells[cell];
      leaf.leaf = true;
      leaf.first_block = static_cast<std::uint32_t>(m_blocks.size());
      leaf.block_count = static_cast<std::uint32_t>((end - begin + kLanes - 1) / kLanes);
      const Vec2 filler = leaf.centre;
      for (std::size_t first = begin; first < end; first += kLanes)
      {
        Block block;
        block.x.fill(filler.x);
        block.y.fill(filler.y);
        block.index.fill(kPadding);
        for (std::size_t lane = 0; lane < kLanes && first + lane < end; ++lane)
        {
          const std::uint32_t index = m_order[first + lane];
          block.x.at(lane) = m_bodies[index].position.x;
          block.y.at(lane) = m_bodies[index].position.y;
          block.mass.at(lane) = m_bodies[index].mass;
          block.index.at(lane) = index;
        }
        m_blocks.push_back(block);
      }
      m_leaves.push_back(cell);
      return;
    }

    const unsigned shift = 2U * static_cast<unsigned>(kCoordinateBits - 1 - depth);
    std::array<std::size_t, 5> bounds {begin, begin, begin, begin, end};
    for (std::uint64_t q = 1; q < 4; ++q)
    {
      bounds.at(q) = *std::ranges::partition_point(
        std::views::iota(bounds.at(q - 1), end),
        [this, shift, q](std::size_t i) { return ((m_codes[i] >> shift) & 3U) < q; }
      );
    }

    const auto first_child = static_cast<std::uint32_t>(m_cells.size());
    std::uint32_t children = 0;
    for (std::size_t q = 0; q < 4; ++q)
    {
      children += bounds.at(q) < bounds.at(q + 1) ? 1U : 0U;
    }
    m_cells[cell].first_child = first_child;
    m_cells[cell].child_count = children;
    m_cells.resize(m_cells.size() + children);
    std::uint32_t child = first_child;
    for (std::size_t q = 0; q < 4; ++q)
    {
      if (bounds.at(q) < bounds.at(q + 1))
      {
        build(child++, bounds.at(q), bounds.at(q + 1), depth + 1);
      }
    }
  }

  void
  QuadTree::repulsion_for_leaf(
    std::size_t leaf, float theta, float strength, std::span<Vec2> forces
  ) const
  {
    const std::uint32_t target_index = m_leaves[leaf];
    const Cell &target = m_cells[target_index];

    std::vector<FarSource> far;
    std::vector<std::uint32_t> near;
    std::array<std::uint32_t, kStackCapacity> stack {};
    std::size_t size = 0;
    stack.at(size++) = 0;
    while (size > 0)
    {
      const std::uint32_t index = stack.at(--size);
      const Cell &cell = m_cells[index];
      if (cell.mass <= 0.0F)
      {
        continue;
      }
      if (index != target_index)
      {
        const float gap_x
          = std::max({target.low.x - cell.centre.x, 0.0F, cell.centre.x - target.high.x});
        const float gap_y
          = std::max({target.low.y - cell.centre.y, 0.0F, cell.centre.y - target.high.y});
        const float gap = std::sqrt((gap_x * gap_x) + (gap_y * gap_y));
        const float extent = std::max(cell.high.x - cell.low.x, cell.high.y - cell.low.y);
        if (gap > 0.0F && extent < theta * gap)
        {
          far.push_back({.centre = cell.centre, .mass = cell.mass});
          continue;
        }
      }
      if (cell.leaf)
      {
        near.push_back(index);
        continue;
      }
      for (std::uint32_t child = cell.child_count; child-- > 0;)
      {
        stack.at(size++) = cell.first_child + child;
      }
    }

    for (std::uint32_t b = target.first_block; b < target.first_block + target.block_count; ++b)
    {
      const Block &block = m_blocks[b];
      std::array<float, kLanes> ax {};
      std::array<float, kLanes> ay {};
      for (const FarSource &source : far)
      {
        accumulate(block.x, block.y, source.centre.x, source.centre.y, source.mass, ax, ay);
      }
      for (const std::uint32_t index : near)
      {
        const Cell &cell = m_cells[index];
        for (std::uint32_t s = cell.first_block; s < cell.first_block + cell.block_count; ++s)
        {
          const Block &source = m_blocks[s];
          for (std::size_t lane = 0; lane < kLanes; ++lane)
          {
            accumulate(
              block.x, block.y, source.x.at(lane), source.y.at(lane), source.mass.at(lane), ax, ay
            );
          }
        }
      }
      for (std::size_t lane = 0; lane < kLanes; ++lane)
      {
        const std::uint32_t body = block.index.at(lane);
        if (body != kPadding)
        {
          const float scale = strength * block.mass.at(lane);
          forces[body] = {.x = scale * ax.at(lane), .y = scale * ay.at(lane)};
        }
      }
    }
  }

  void
  QuadTree::repulsion(float theta, float strength, std::span<Vec2> forces) const
  {
    for (std::size_t leaf = 0; leaf < m_leaves.size(); ++leaf)
    {
      repulsion_for_leaf(leaf, theta, strength, forces);
    }
  }
} // namespace Comprendo
