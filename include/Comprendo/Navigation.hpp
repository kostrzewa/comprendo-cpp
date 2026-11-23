#pragma once

/// \file
/// \brief Pure helpers for the viewer: list cursors, panel heights, camera animations, history, text size.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

#include "Comprendo/QuadTree.hpp"

namespace Comprendo
{
  /// Move a keyboard cursor through a list.
  ///
  /// Moving from no cursor lands on the first row; the cursor stays within the
  /// list and disappears when the list is empty.
  ///
  /// \param cursor Current row, if any.
  /// \param rows Number of rows.
  /// \param delta Rows to move (negative moves up).
  /// \return The new row, or nothing for an empty list.
  [[nodiscard]]
  std::optional<std::size_t>
  move_list_cursor(std::optional<std::size_t> cursor, std::size_t rows, int delta);

  /// Factor between neighbouring text sizes (Ctrl++ / Ctrl+-).
  inline constexpr float kTextScaleStep = 1.125F;

  /// Smallest text size level: about 0.62 times the default.
  inline constexpr int kMinTextLevel = -4;

  /// Largest text size level: about 2.9 times the default.
  inline constexpr int kMaxTextLevel = 9;

  /// Change a text size level, staying within kMinTextLevel..kMaxTextLevel.
  ///
  /// Sizes are kept as integer levels rather than scales so repeated steps up
  /// and down return exactly to where they started.
  ///
  /// \param level Current level; 0 is the default size.
  /// \param delta Steps to move (negative makes text smaller).
  /// \return The new level.
  [[nodiscard]]
  int
  step_text_level(int level, int delta);

  /// Scale of a text size level relative to the default size.
  ///
  /// \param level Text size level.
  /// \return kTextScaleStep to the power of \p level.
  [[nodiscard]]
  float
  text_scale(int level);

  /// What a Ctrl shortcut does to the text size.
  enum class TextSizeKey : std::uint8_t
  {
    /// Not a text size shortcut.
    None,
    /// Ctrl++ (or Ctrl+=, the unshifted + on US layouts): larger text.
    Larger,
    /// Ctrl+-: smaller text.
    Smaller,
    /// Ctrl+0: default size.
    Reset,
  };

  /// Classify the character a key types on the user's keyboard layout.
  ///
  /// Working from the typed character rather than the key's position makes
  /// Ctrl++ work on layouts with a dedicated + key (e.g. German) as well as on
  /// US layouts, where + shares the = key.
  ///
  /// \param character Character the key types without modifiers.
  /// \return The shortcut's action.
  [[nodiscard]]
  TextSizeKey
  text_size_key(char character);

  /// Share of the available space a panel unfolds to when its size before
  /// folding is unknown (e.g. folded in an earlier session).
  inline constexpr float kUnfoldShare = 0.35F;

  /// Size of a docked panel after its fold button is clicked.
  ///
  /// A panel larger than its folded size folds to it; a folded panel unfolds
  /// to the size it had before folding, or to kUnfoldShare of the available
  /// space when that is unknown, leaving room for its neighbour's tab bar.
  ///
  /// \param size Current size along the panel's split axis.
  /// \param folded Folded size: the height of its tab bar.
  /// \param remembered Size before folding, or 0 when unknown.
  /// \param available Space the panel shares with its neighbour.
  /// \return New size along the split axis.
  [[nodiscard]]
  float
  fold_toggle_size(float size, float folded, float remembered, float available);

  /// Whether a panel of a given size counts as folded.
  ///
  /// \param size Size along the panel's split axis.
  /// \param folded Folded size.
  /// \return True when the size is at most a pixel or two above \p folded.
  [[nodiscard]]
  bool
  is_folded(float size, float folded);

  /// Share a height between stacked panels so they fill it without wasting space.
  ///
  /// Panels that need less than an equal share get exactly what they need;
  /// the rest is split evenly among the larger ones, so one long list can take
  /// what short lists leave. No panel gets less than min(need, minimum), even
  /// when that exceeds the available height.
  ///
  /// \param needs Height each panel needs to show everything.
  /// \param available Height to share.
  /// \param minimum Smallest height of a panel that needs more.
  /// \return Height per panel, in input order.
  [[nodiscard]]
  std::vector<float>
  share_heights(std::span<const float> needs, float available, float minimum);

  /// Where a camera looks: the chart point at the screen centre and the zoom.
  struct CameraPose
  {
    /// Chart point shown at the centre of the screen.
    Vec2 target;
    /// Screen pixels per chart unit.
    float zoom = 1.0F;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const CameraPose &) const = default;
  };

  /// A short camera move between two poses.
  ///
  /// Eases out (cubic), so the move starts fast and settles gently. The target
  /// moves linearly in chart space and the zoom geometrically, so a pure pan
  /// keeps the zoom exactly and zooming feels uniform.
  class CameraAnimation
  {
  public:
    /// Plan a move.
    ///
    /// \param from Pose at the start.
    /// \param to Pose at the end.
    /// \param start Start time in seconds.
    /// \param duration Length in seconds; zero or less jumps immediately.
    CameraAnimation(CameraPose from, CameraPose to, double start, double duration);

    /// Pose at a time; clamped to the start and end poses outside the move.
    ///
    /// \param now Time in seconds.
    /// \return The pose.
    [[nodiscard]]
    CameraPose
    at(double now) const;

    /// Check whether the move is over.
    ///
    /// \param now Time in seconds.
    /// \return True from the end time on.
    [[nodiscard]]
    bool
    finished(double now) const;

  private:
    /// Pose at the start.
    CameraPose m_from;
    /// Pose at the end.
    CameraPose m_to;
    /// Start time in seconds.
    double m_start;
    /// Length in seconds.
    double m_duration;
  };

  /// A view to return to: camera pose and the selected group's stable key.
  struct NavigationEntry
  {
    /// Camera pose.
    CameraPose pose;
    /// HierarchyGroup::key of the selection, or nothing when nothing was selected.
    std::optional<std::uint64_t> selected;

    /// Memberwise equality.
    ///
    /// \return True when all members are equal.
    bool
    operator==(const NavigationEntry &) const = default;
  };

  /// Views visited before jumps, newest last, for going back.
  class NavigationHistory
  {
  public:
    /// Create an empty history keeping kDefaultCapacity entries.
    NavigationHistory() = default;

    /// Create an empty history.
    ///
    /// \param capacity Most entries kept; the oldest are dropped beyond it.
    explicit NavigationHistory(std::size_t capacity) : m_capacity(capacity) { }

    /// Remember a view before jumping away from it.
    ///
    /// \param entry View to return to; ignored when equal to the newest entry.
    void
    push(const NavigationEntry &entry);

    /// Take the newest view.
    ///
    /// \return The view, or nothing when the history is empty.
    [[nodiscard]]
    std::optional<NavigationEntry>
    pop();

    /// Number of views remembered.
    ///
    /// \return Entry count.
    [[nodiscard]]
    std::size_t
    size() const
    {
      return m_entries.size();
    }

  private:
    /// Default number of entries kept.
    static constexpr std::size_t kDefaultCapacity = 100;

    /// Most entries kept.
    std::size_t m_capacity = kDefaultCapacity;
    /// Entries, oldest first.
    std::deque<NavigationEntry> m_entries;
  };
} // namespace Comprendo
