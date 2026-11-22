#include "circle.hpp"

#include <cmath>

namespace geo
{
  Circle::Circle(double radius) : radius_(radius) {}

  double
  Circle::area() const
  {
    return M_PI * radius_ * radius_;
  }

  double
  distance(const Point &a, const Point &b)
  {
    return std::hypot(a.x - b.x, a.y - b.y);
  }

  double
  perimeter(const Point &, double radius)
  {
    return 2.0 * M_PI * radius;
  }
} // namespace geo
