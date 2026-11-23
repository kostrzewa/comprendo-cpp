/// \file
/// \brief Area formulas for simple shapes.

#pragma once

namespace shapes
{
  /** A circle given by its radius. */
  struct Circle
  {
    double radius; ///< Distance from the centre to the edge.
    double unused;
  };

  /// \brief Area of a circle.
  ///
  /// Uses the exact value of pi, so results match the C library.
  /// \param circle Circle to measure.
  /// \return Area in square units.
  [[nodiscard]]
  double
  area(const Circle &circle);

  // Plain comments are not documentation.
  double perimeter(const Circle &circle);

  /// Square of a value.

  double square(double value);
} // namespace shapes
