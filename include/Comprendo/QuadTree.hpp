#pragma once

/// \file
/// \brief Blocked Barnes–Hut quadtree: vectorizable repulsion and disk-overlap queries.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Comprendo
{
  /// A 2D vector in single precision, used by the layout engine.
  struct Vec2
  {
    /// Horizontal component.
    float x = 0.0F;
    /// Vertical component.
    float y = 0.0F;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const Vec2 &) const = default;
  };

  /// A body of the simulation: a disk with a mass.
  struct Body
  {
    /// Centre.
    Vec2 position;
    /// Repulsion weight; larger bodies push harder.
    float mass = 1.0F;
    /// Disk radius, used by overlap queries.
    float radius = 0.0F;
    /// Kept in place by the layout (the group's centre when re-centring on a
    /// selection); others move around it.
    bool pinned = false;
  };

  /// Bits per coordinate of a Morton code; codes use 2 * this many bits.
  inline constexpr int kMortonCoordinateBits = 21;

  /// Interleave two grid coordinates into a 2D Morton (Z-order) code.
  ///
  /// Bit k of \p x becomes bit 2k and bit k of \p y becomes bit 2k + 1, so the
  /// two bits at positions 2k and 2k + 1 name the quadrant at tree depth
  /// kMortonCoordinateBits - 1 - k. Coordinates are truncated to
  /// kMortonCoordinateBits bits.
  ///
  /// \param x Horizontal grid coordinate.
  /// \param y Vertical grid coordinate.
  /// \return Code below 2^(2 * kMortonCoordinateBits).
  [[nodiscard]]
  std::uint64_t
  morton_code(std::uint32_t x, std::uint32_t y);

  /// Barnes–Hut quadtree over a fixed set of bodies, stored for SIMD evaluation.
  ///
  /// Everything is single precision: positions inside one group stay below
  /// about 1e4, where float resolves 1e-3, far below disk sizes and the
  /// Barnes–Hut error, and a SIMD register holds twice as many floats.
  ///
  /// Bodies are sorted along a Morton (Z-order) curve, by radix-sorting keys
  /// that pack the Morton code above the body index, and the tree is built
  /// top-down over that order, so every cell covers a contiguous range. Leaves
  /// hold up to kLanes bodies (more only for coincident bodies at the depth
  /// limit) in structure-of-arrays blocks of kLanes lanes; unused lanes are
  /// padding with zero mass. Repulsion is evaluated per leaf: one tree walk
  /// against the leaf's bounding box collects far cells and near leaves, and a
  /// lane-parallel kernel sums their contributions in a fixed order, so results
  /// do not depend on vector width or on how leaves are spread over threads.
  class QuadTree
  {
  public:
    /// Bodies per block, and the leaf capacity.
    static constexpr std::size_t kLanes = 16;

    /// Build the tree.
    ///
    /// \param bodies Bodies to index; copied, so they may change afterwards.
    explicit QuadTree(std::span<const Body> bodies);

    /// Number of leaves; repulsion_for_leaf() accepts indices below this.
    ///
    /// \return Leaf count.
    [[nodiscard]]
    std::size_t
    leaf_count() const
    {
      return m_leaves.size();
    }

    /// Repulsive forces on the bodies of one leaf: for body i,
    /// `strength * m_i * sum_j m_j * (p_i - p_j) / |p_i - p_j|^2`, i.e. magnitude
    /// `strength * m_i * m_j / d` directed away from each other body. Exactly
    /// coincident pairs contribute nothing.
    ///
    /// Different leaves write disjoint entries, so leaves may be evaluated on
    /// different threads.
    ///
    /// \param leaf Leaf index.
    /// \param theta Opening angle: a cell of extent s whose centre of mass is at
    ///        least d from the leaf's bounding box is treated as one body when
    ///        `s < theta * d`. 0 computes the exact pairwise sum.
    /// \param strength Force constant.
    /// \param forces Output indexed like the constructor's bodies; only the
    ///        entries of this leaf's bodies are written.
    void
    repulsion_for_leaf(std::size_t leaf, float theta, float strength, std::span<Vec2> forces) const;

    /// Repulsive forces on all bodies, evaluated leaf by leaf on this thread.
    ///
    /// \param theta Opening angle (see repulsion_for_leaf()).
    /// \param strength Force constant.
    /// \param forces Output indexed like the constructor's bodies.
    void
    repulsion(float theta, float strength, std::span<Vec2> forces) const;

    /// Visit every other body whose disk, grown by \p reach, may overlap the
    /// disk of \p body grown by \p reach.
    ///
    /// \param body Index of the body, as passed to the constructor.
    /// \param reach Extra distance added to every radius.
    /// \param visit Called with the original index of each candidate, in tree order.
    template <typename Visit>
    void
    for_each_near(std::size_t body, float reach, const Visit &visit) const
    {
      const Body &self = m_bodies[body];
      std::array<std::uint32_t, kStackCapacity> stack {};
      std::size_t size = 0;
      stack.at(size++) = 0;
      while (size > 0)
      {
        const Cell &cell = m_cells[stack.at(--size)];
        const float grow = self.radius + cell.max_radius + (2.0F * reach);
        if (
          self.position.x < cell.low.x - grow || self.position.x > cell.high.x + grow
          || self.position.y < cell.low.y - grow || self.position.y > cell.high.y + grow
        )
        {
          continue;
        }
        if (cell.leaf)
        {
          for (std::uint32_t b = cell.first_block; b < cell.first_block + cell.block_count; ++b)
          {
            for (const std::uint32_t other : m_blocks[b].index)
            {
              if (other != kPadding && other != body)
              {
                visit(static_cast<std::size_t>(other));
              }
            }
          }
          continue;
        }
        for (std::uint32_t child = cell.child_count; child-- > 0;)
        {
          stack.at(size++) = cell.first_child + child;
        }
      }
    }

  private:
    /// Marker for padding lanes in Block::index.
    static constexpr std::uint32_t kPadding = 0xffffffffU;
    /// Depth limit of the tree (bits per coordinate of the Morton code).
    static constexpr int kMaxDepth = 21;
    /// Traversal stack capacity: at most 3 pending siblings per level plus the root.
    static constexpr std::size_t kStackCapacity = (3 * kMaxDepth) + 4;

    /// kLanes bodies in structure-of-arrays form.
    struct Block
    {
      /// Horizontal positions.
      std::array<float, kLanes> x {};
      /// Vertical positions.
      std::array<float, kLanes> y {};
      /// Masses; 0 for padding lanes.
      std::array<float, kLanes> mass {};
      /// Original body index, kPadding for padding lanes.
      std::array<std::uint32_t, kLanes> index {};
    };

    /// One cell of the tree.
    struct Cell
    {
      /// Lower corner of the tight bounding box of the bodies below.
      Vec2 low;
      /// Upper corner of the tight bounding box of the bodies below.
      Vec2 high;
      /// Total mass of the bodies below.
      float mass = 0.0F;
      /// Mass-weighted centre of the bodies below.
      Vec2 centre;
      /// Largest body radius below.
      float max_radius = 0.0F;
      /// Index of the first child; children are consecutive.
      std::uint32_t first_child = 0;
      /// Number of children.
      std::uint32_t child_count = 0;
      /// First block of a leaf.
      std::uint32_t first_block = 0;
      /// Number of blocks of a leaf.
      std::uint32_t block_count = 0;
      /// True for leaves.
      bool leaf = false;
    };

    /// Build the cell for sorted bodies [begin, end) whose Morton codes share
    /// the top 2 * depth bits, then its children.
    ///
    /// \param cell Index of the cell to fill.
    /// \param begin First sorted body.
    /// \param end One past the last sorted body.
    /// \param depth Depth of the cell.
    void
    build(std::uint32_t cell, std::size_t begin, std::size_t end, int depth);

    /// Bodies, indexed by original index (copied from the constructor input).
    std::vector<Body> m_bodies;
    /// Original body indices in Morton order.
    std::vector<std::uint32_t> m_order;
    /// Morton codes in sorted order.
    std::vector<std::uint64_t> m_codes;
    /// Cells; index 0 is the root.
    std::vector<Cell> m_cells;
    /// Leaf cell indices in Morton order.
    std::vector<std::uint32_t> m_leaves;
    /// Body blocks of all leaves.
    std::vector<Block> m_blocks;
  };
} // namespace Comprendo
