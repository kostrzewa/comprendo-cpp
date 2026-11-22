#pragma once

#include "geo/shape.hpp"

namespace geo
{
  class Circle : public Shape
  {
  public:
    explicit Circle(double radius);
    Circle(const Circle &) = delete;
    double area() const override;

  private:
    double radius_;
  };
} // namespace geo
