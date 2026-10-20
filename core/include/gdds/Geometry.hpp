#pragma once

// Basic geometry in Geometry Dash level units.
// One grid block = 30 units. Object positions are object centers; Y points up.
// Coordinates follow the level-string convention (ground surface at y = 0).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace gdds {

constexpr double kBlock = 30.0;      // size of one editor grid block
constexpr double kHalfBlock = 15.0;  // analysis grid resolution
constexpr double kPi = 3.14159265358979323846;

struct Vec2 {
    double x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(double x, double y) : x(x), y(y) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const { return {x / s, y / s}; }
    constexpr bool operator==(Vec2 const&) const = default;

    double length() const { return std::sqrt(x * x + y * y); }
    double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double cross(Vec2 o) const { return x * o.y - y * o.x; }

    // Rotates clockwise by `deg` degrees (GD rotation convention).
    Vec2 rotatedCW(double deg) const {
        double r = -deg * kPi / 180.0;
        double c = std::cos(r), s = std::sin(r);
        return {x * c - y * s, x * s + y * c};
    }
};

struct Rect {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // min / max corners

    static Rect fromCenter(Vec2 c, double w, double h) {
        return {c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2};
    }
    static Rect empty() { return {1e300, 1e300, -1e300, -1e300}; }

    double width() const { return x1 - x0; }
    double height() const { return y1 - y0; }
    Vec2 center() const { return {(x0 + x1) / 2, (y0 + y1) / 2}; }
    bool isEmpty() const { return x1 <= x0 || y1 <= y0; }
    double area() const { return isEmpty() ? 0.0 : width() * height(); }

    bool contains(Vec2 p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
    bool containsRect(Rect const& o) const { return o.x0 >= x0 && o.x1 <= x1 && o.y0 >= y0 && o.y1 <= y1; }
    bool intersects(Rect const& o) const { return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1; }

    Rect intersection(Rect const& o) const {
        return {std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
    }
    Rect united(Rect const& o) const {
        return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
    }
    void expandToInclude(Vec2 p) {
        x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
    Rect expanded(double m) const { return {x0 - m, y0 - m, x1 + m, y1 + m}; }
    Rect expanded(double mx, double my) const { return {x0 - mx, y0 - my, x1 + mx, y1 + my}; }

    bool operator==(Rect const&) const = default;
};

// Integer cell coordinate on an analysis grid.
struct Cell {
    int32_t x = 0, y = 0;
    constexpr bool operator==(Cell const&) const = default;
    constexpr auto operator<=>(Cell const&) const = default;
};

struct CellHash {
    size_t operator()(Cell const& c) const noexcept {
        uint64_t k = (uint64_t(uint32_t(c.x)) << 32) | uint32_t(c.y);
        k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33;
        return size_t(k);
    }
};

// Oriented rectangle (object hitboxes and sprites can be rotated).
struct OrientedRect {
    Vec2 center;
    double halfW = 0, halfH = 0;
    double rotation = 0;  // degrees, clockwise

    Rect bounds() const {
        if (std::fmod(std::abs(rotation), 90.0) < 1e-6) {
            bool swap = std::fmod(std::abs(rotation), 180.0) > 45.0;
            double hw = swap ? halfH : halfW, hh = swap ? halfW : halfH;
            return {center.x - hw, center.y - hh, center.x + hw, center.y + hh};
        }
        Rect r = Rect::empty();
        for (Vec2 c : corners()) r.expandToInclude(c);
        return r;
    }

    std::vector<Vec2> corners() const {
        std::vector<Vec2> out;
        Vec2 local[4] = {{-halfW, -halfH}, {halfW, -halfH}, {halfW, halfH}, {-halfW, halfH}};
        for (auto& l : local) out.push_back(center + l.rotatedCW(rotation));
        return out;
    }

    bool contains(Vec2 p) const {
        Vec2 local = (p - center).rotatedCW(-rotation);
        return std::abs(local.x) <= halfW + 1e-9 && std::abs(local.y) <= halfH + 1e-9;
    }
};

// Simple polygon (no self intersections assumed); even-odd containment.
struct Polygon {
    std::vector<Vec2> points;

    bool contains(Vec2 p) const {
        bool inside = false;
        size_t n = points.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) {
            Vec2 a = points[i], b = points[j];
            if ((a.y > p.y) != (b.y > p.y)) {
                double xCross = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
                if (p.x < xCross) inside = !inside;
            }
        }
        return inside;
    }

    Rect bounds() const {
        Rect r = Rect::empty();
        for (auto& p : points) r.expandToInclude(p);
        return r;
    }

    double signedArea() const {
        double a = 0;
        size_t n = points.size();
        for (size_t i = 0, j = n - 1; i < n; j = i++) a += points[j].cross(points[i]);
        return a / 2;
    }
};

// Triangle used for slope hitboxes.
struct Triangle {
    Vec2 a, b, c;
    bool contains(Vec2 p) const {
        auto sign = [](Vec2 p1, Vec2 p2, Vec2 p3) {
            return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
        };
        double d1 = sign(p, a, b), d2 = sign(p, b, c), d3 = sign(p, c, a);
        bool neg = d1 < -1e-9 || d2 < -1e-9 || d3 < -1e-9;
        bool pos = d1 > 1e-9 || d2 > 1e-9 || d3 > 1e-9;
        return !(neg && pos);
    }
    Rect bounds() const {
        Rect r = Rect::empty();
        r.expandToInclude(a); r.expandToInclude(b); r.expandToInclude(c);
        return r;
    }
};

inline double snap(double v, double step) { return std::round(v / step) * step; }

}  // namespace gdds
