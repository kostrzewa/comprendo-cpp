#include "circle.hpp"
#include "missing.hpp"
/* #include "geo/shape.hpp" */
#include <vector>

#define SCALE_FACTOR 2

using Number = double;

void log_value(int value);

int
scale(int value)
{
  return value * SCALE_FACTOR;
}

double
scale(double value)
{
  return value * SCALE_FACTOR;
}

double
total(const geo::Shape &shape)
{
  return shape.area();
}

int
main()
{
  const geo::Circle circle{1.0};
  return static_cast<int>(scale(circle.area()) + total(circle));
}
