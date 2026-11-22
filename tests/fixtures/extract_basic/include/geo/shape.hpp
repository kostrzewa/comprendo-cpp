#pragma once

namespace geo
{
  struct Point
  {
    double x;
    double y;
  };

  class Shape
  {
  public:
    virtual ~Shape() = default;
    virtual double area() const = 0;
    int id = 0;
  };

  double distance(const Point &a, const Point &b);
  double perimeter(const Point &centre, double r = 1.0);
} // namespace geo
