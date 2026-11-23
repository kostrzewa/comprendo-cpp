#include "area.hpp"

namespace shapes
{
  double
  area(const Circle &circle)
  {
    return 3.141592653589793 * square(circle.radius);
  }

  double
  perimeter(const Circle &circle)
  {
    return 2.0 * 3.141592653589793 * circle.radius;
  }

  double
  square(double value)
  {
    return value * value;
  }
} // namespace shapes
