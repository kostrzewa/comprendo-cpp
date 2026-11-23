/// \file
/// \brief Pure navigation helpers for the viewer: list cursors, camera animations, and history.

#include "Comprendo/Navigation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace Comprendo
{
  std::optional<std::size_t>
  move_list_cursor(std::optional<std::size_t> cursor, std::size_t rows, int delta)
  {
    if (rows == 0)
    {
      return std::nullopt;
    }
    if (!cursor)
    {
      return 0;
    }
    const auto last = static_cast<std::ptrdiff_t>(rows - 1);
    const std::ptrdiff_t moved = static_cast<std::ptrdiff_t>(*cursor) + delta;
    return static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(moved, 0, last));
  }

  std::vector<float>
  share_heights(std::span<const float> needs, float available, float minimum)
  {
    std::vector<std::size_t> order(needs.size());
    for (std::size_t i = 0; i < order.size(); ++i)
    {
      order[i] = i;
    }
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return needs[i]; });
    std::vector<float> heights(needs.size(), 0.0F);
    float remaining = std::max(available, 0.0F);
    for (std::size_t k = 0; k < order.size(); ++k)
    {
      const float need = needs[order[k]];
      const float share = remaining / static_cast<float>(order.size() - k);
      const float height = std::min(need, std::max(share, std::min(need, minimum)));
      heights[order[k]] = height;
      remaining = std::max(remaining - height, 0.0F);
    }
    return heights;
  }

  CameraAnimation::CameraAnimation(CameraPose from, CameraPose to, double start, double duration) :
    m_from(from), m_to(to), m_start(start), m_duration(duration)
  {
  }

  CameraPose
  CameraAnimation::at(double now) const
  {
    if (m_duration <= 0.0 || now >= m_start + m_duration)
    {
      return m_to;
    }
    const double t = std::clamp((now - m_start) / m_duration, 0.0, 1.0);
    const auto eased = static_cast<float>(1.0 - std::pow(1.0 - t, 3.0));
    const float zoom = m_from.zoom * std::pow(m_to.zoom / m_from.zoom, eased);
    return {
      .target = {
        .x = m_from.target.x + ((m_to.target.x - m_from.target.x) * eased),
        .y = m_from.target.y + ((m_to.target.y - m_from.target.y) * eased),
      },
      .zoom = m_from.zoom == m_to.zoom ? m_to.zoom : zoom,
    };
  }

  bool
  CameraAnimation::finished(double now) const
  {
    return m_duration <= 0.0 || now >= m_start + m_duration;
  }

  void
  NavigationHistory::push(const NavigationEntry &entry)
  {
    if (!m_entries.empty() && m_entries.back() == entry)
    {
      return;
    }
    m_entries.push_back(entry);
    while (m_entries.size() > m_capacity)
    {
      m_entries.pop_front();
    }
  }

  std::optional<NavigationEntry>
  NavigationHistory::pop()
  {
    if (m_entries.empty())
    {
      return std::nullopt;
    }
    NavigationEntry entry = m_entries.back();
    m_entries.pop_back();
    return entry;
  }
} // namespace Comprendo
