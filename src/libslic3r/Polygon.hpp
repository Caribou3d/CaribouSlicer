///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ ported from lib/Slic3r/Polygon.pm:
///|/ Copyright (c) Prusa Research 2017 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

#include "libslic3r.h"
#include <vector>
#include <string>
#include "Line.hpp"
#include "Point.hpp"
#include "MultiPoint.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class Polygon;
using Polygons          = std::vector<Polygon, PointsAllocator<Polygon>>;
using PolygonPtrs       = std::vector<Polygon*, PointsAllocator<Polygon*>>;
using ConstPolygonPtrs  = std::vector<const Polygon*, PointsAllocator<const Polygon*>>;

// Returns true if inside. Returns border_result if on boundary.
bool contains(const Polygon& polygon, const Point& p, bool border_result = true);
bool contains(const Polygons& polygons, const Point& p, bool border_result = true);

class Polygon : public MultiPoint
{
public:
    Polygon() = default;
    explicit Polygon(const Points &points) : MultiPoint(points) {
        assert(points.size() != 1);
        if (points.size() > 1 && this->front().coincides_with(this->back()))
            this->points.pop_back();
    }
    explicit Polygon(Points &&points) : MultiPoint(points) {
        assert(points.size() != 1);
        if (points.size() > 1 && this->front().coincides_with_epsilon(this->back()))
            this->points.pop_back();
    }
    Polygon(std::initializer_list<Point> points) : MultiPoint(points) {
        assert(this->size() != 1);
        assert(this->empty() || !this->front().coincides_with(this->back()));
        if (this->size() > 1 && this->front().coincides_with_epsilon(this->back()))
            this->points.pop_back();
    }
    Polygon(const Polygon &other) : MultiPoint(other.points) {
        assert(this->empty() || !this->front().coincides_with(this->back()));
    }
    Polygon(Polygon &&other) : MultiPoint(std::move(other.points)) {
        assert(this->size() != 1);
        assert(this->empty() || !this->front().coincides_with(this->back()));
        if (this->size() > 1 && this->front().coincides_with_epsilon(this->back()))
            this->points.pop_back();
    }
    static Polygon new_scale(const std::vector<Vec2d> &points) {
        Polygon pgn;
        pgn.points.reserve(points.size());
        for (const Vec2d &pt : points)
            pgn.points.emplace_back(Point::new_scale(pt(0), pt(1)));
		return pgn;
	}
    Polygon& operator=(const Polygon &other) { points = other.points; return *this; }
    Polygon& operator=(Polygon &&other) { points = std::move(other.points); return *this; }

    Point& operator[](Points::size_type idx) { return this->points[idx]; }
    const Point& operator[](Points::size_type idx) const { return this->points[idx]; }

    // last point == first point for polygons
    //please don't use that, prefer 'is_loop', front() and back().
    const Point& last_point() const { return this->points.front(); }
    virtual bool is_loop() const { return true; }

    double length() const;
    Lines lines() const;
    Polyline split_at_vertex(const Point &point) const;
    // Split a closed polygon into an open polyline, with the split point duplicated at both ends.
    Polyline split_at_index(size_t index) const;
    // Split a closed polygon into an open polyline, with the split point duplicated at both ends.
    Polyline split_at_first_point() const { return this->split_at_index(0); }
    Points   equally_spaced_points(double distance) const { return this->split_at_first_point().equally_spaced_points(distance); }

    static double area(const Points &pts);
    double area() const;
    bool is_counter_clockwise() const;
    bool is_clockwise() const;
    bool make_counter_clockwise();
    bool make_clockwise();
    bool is_valid() const { assert_valid(); return this->points.size() >= 3; }
    void douglas_peucker(coord_t tolerance) override;

    // Does an unoriented polygon contain a point?
    bool contains(const Point &point) const { return Slic3r::contains(*this, point, true); }
    // Approximate on boundary test.
    bool on_boundary(const Point &point, double eps) const
        { return (this->point_projection(point).first - point).cast<double>().squaredNorm() < eps * eps; }

    // Works on CCW polygons only, CW contour will be reoriented to CCW by Clipper's simplify_polygons()!
    Polygons simplify(double tolerance) const;
    void densify(float min_length, std::vector<float>* lengths = nullptr);
    void triangulate_convex(Polygons* polygons) const;
    Point centroid() const;

    bool intersection(const Line& line, Point* intersection) const;
    bool first_intersection(const Line& line, Point* intersection) const;
    bool intersections(const Line &line, Points *intersections) const;

    // Considering CCW orientation of this polygon
    // (it means that a ccw (contour) is mostly convex, while a cw (hole) is mostly concave),
    // find all convex resp. concave points
    // with the angle at the vertex between two threshold.
    Points convex_points(double min_angle /*=0*/, double max_angle /*=PI*/) const;
    Points concave_points(double min_angle, double max_angle) const;
    std::vector<size_t> concave_points_idx(double min_angle, double max_angle) const;
    std::vector<size_t> convex_points_idx(double min_angle, double max_angle) const;
    // Projection of a point onto the polygon.
    std::pair<Point, size_t> point_projection(const Point &point) const override;
    std::vector<float> parameter_by_length() const;
    /// remove points that are (almost) on an existing line from previous & next point.
    /// return number of point removed
    size_t remove_collinear(coord_t max_offset);
    size_t remove_collinear_angle(double angle);

#ifdef _DEBUGINFO
    void assert_valid() const override {
        assert(size() > 2);
        for (size_t i_pt = 1; i_pt < size(); ++i_pt)
            release_assert(!points[i_pt - 1].coincides_with_epsilon(points[i_pt]));
        release_assert(!points.front().coincides_with_epsilon(points.back()));
    }
#else
    void assert_valid() const {}
#endif

    using iterator = Points::iterator;
    using const_iterator = Points::const_iterator;
};

inline bool operator==(const Polygon &lhs, const Polygon &rhs) { return lhs.points == rhs.points; }
inline bool operator!=(const Polygon &lhs, const Polygon &rhs) { return lhs.points != rhs.points; }

BoundingBox get_extents(const Polygon &poly);
BoundingBox get_extents(const Polygons &polygons);
BoundingBox get_extents_rotated(const Polygon &poly, double angle);
BoundingBox get_extents_rotated(const Polygons &polygons, double angle);
std::vector<BoundingBox> get_extents_vector(const Polygons &polygons);

// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool        polygon_is_convex(const Points &poly);
inline bool polygon_is_convex(const Polygon &poly) { return polygon_is_convex(poly.points); }

// Test for duplicate points. The points are copied, sorted and checked for duplicates globally.
inline bool has_duplicate_points(Polygon &&poly)      { return has_duplicate_points(std::move(poly.points)); }
inline bool has_duplicate_points(const Polygon &poly) { return has_duplicate_points(poly.points); }
bool        has_duplicate_points(const Polygons &polys);

// Return True when erase some otherwise False.
bool remove_same_neighbor(Polygon &polygon);
bool remove_same_neighbor(Polygons &polygons);
// remove any point that are at epsilon  (or resolution) 'distance' (douglas_peuckere algo for now) and all polygons that are too small to be valid

void ensure_valid(Polygons &polygons, coord_t resolution = SCALED_EPSILON);
Polygons ensure_valid(Polygons &&polygons, coord_t resolution = SCALED_EPSILON);
Polygons ensure_valid(coord_t resolution, Polygons &&polygons);
// return false if the polygon isn't valid and need to be removed.
bool ensure_valid(Polygon &polygon, coord_t resolution = SCALED_EPSILON);
#ifdef _DEBUGINFO
void assert_valid(const Polygons &polygons);
#else
inline void assert_valid(const Polygons &polygons) {}
#endif

inline double total_length(const Polygons &polylines) {
    double total = 0;
    for (Polygons::const_iterator it = polylines.begin(); it != polylines.end(); ++it)
        total += it->length();
    return total;
}

inline double area(const Polygon &poly) { return poly.area(); }

inline double area(const Polygons &polys)
{
    double s = 0.;
    for (auto &p : polys) s += p.area();

    return s;
}

// Remove sticks (tentacles with zero area) from the polygon.
bool remove_sticks(Polygon &poly);
bool remove_sticks(Polygons &polys);

// Remove polygons with less than 3 edges.
bool remove_degenerate(Polygons &polys);
bool remove_small(Polygons &polys, double min_area);
void remove_collinear(Polygon &poly, coord_t max_offset = SCALED_EPSILON);
void remove_collinear(Polygons &polys, coord_t max_offset = SCALED_EPSILON);

// Append a vector of polygons at the end of another vector of polygons.
inline void polygons_append(Polygons &dst, const Polygons &src) { dst.insert(dst.end(), src.begin(), src.end()); }

inline void polygons_append(Polygons &dst, Polygons &&src) 
{
    if (dst.empty()) {
        dst = std::move(src);
    } else {
        std::move(std::begin(src), std::end(src), std::back_inserter(dst));
        src.clear();
    }
}

Polygons polygons_simplify(Polygons &&polys, double tolerance, bool strictly_simple = true);
Polygons polygons_simplify(const Polygons &polys, double tolerance, bool strictly_simple = true);

inline void polygons_rotate(Polygons &polys, double angle)
{
    const double cos_angle = cos(angle);
    const double sin_angle = sin(angle);
    for (Polygon &p : polys)
        p.rotate(cos_angle, sin_angle);
}

inline void polygons_reverse(Polygons &polys)
{
    for (Polygon &p : polys)
        p.reverse();
}

inline Points to_points(const Polygon &poly)
{
    return poly.points;
}

inline size_t count_points(const Polygons &polys) {
    size_t n_points = 0;
    for (const auto &poly: polys) n_points += poly.points.size();
    return n_points;
}

inline Points to_points(const Polygons &polys) 
{
    Points points;
    points.reserve(count_points(polys));
    for (const Polygon &poly : polys)
        append(points, poly.points);
    return points;
}

inline Lines to_lines(const Polygon &poly) 
{
    Lines lines;
    lines.reserve(poly.points.size());
    if (poly.points.size() > 2) {
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end()-1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Lines to_lines(const Polygons &polys) 
{
    Lines lines;
    lines.reserve(count_points(polys));
    for (size_t i = 0; i < polys.size(); ++ i) {
        const Polygon &poly = polys[i];
        for (Points::const_iterator it = poly.points.begin(); it != poly.points.end()-1; ++it)
            lines.push_back(Line(*it, *(it + 1)));
        lines.push_back(Line(poly.points.back(), poly.points.front()));
    }
    return lines;
}

inline Polyline to_polyline(const Polygon &polygon)
{
    Polyline out;
    out.points.reserve(polygon.size() + 1);
    out.points.assign(polygon.points.begin(), polygon.points.end());
    out.points.push_back(polygon.points.front());
    return out;
}

// to have easier time with svg output.
inline Polylines to_polylines(const Polygon &polygon)
{
    Polylines out;
    assert(!polygon.empty());
    if (!polygon.empty()) {
        out.push_back(to_polyline(polygon));
    }
    return out;
}

inline Polylines to_polylines(const Polygons &polygons)
{
    Polylines out;
    out.reserve(polygons.size());
    for (const Polygon &polygon : polygons) {
        assert(!polygon.empty());
        out.push_back(to_polyline(polygon));
    }
    return out;
}

inline Polylines to_polylines(Polygons &&polys)
{
    Polylines polylines;
    polylines.assign(polys.size(), Polyline());
    size_t idx = 0;
    for (auto it = polys.begin(); it != polys.end(); ++ it) {
        assert(!it->empty());
        Polyline &pl = polylines[idx ++];
        pl.points = std::move(it->points);
        pl.points.push_back(pl.points.front());
    }
    assert(idx == polylines.size());
    return polylines;
}

// close polyline to polygon (connect first and last point in polyline)
inline Polygons to_polygons(const Polylines &polylines)
{
    Polygons out;
    out.reserve(polylines.size());
    for (const Polyline &polyline : polylines) {
        if (polyline.size())
        out.emplace_back(polyline.points);
    }
    return out;
}

inline Polygons to_polygons(const VecOfPoints &paths)
{
    Polygons out;
    out.reserve(paths.size());
    for (const Points &path : paths)
        out.emplace_back(path);
    return out;
}

inline Polygons to_polygons(VecOfPoints &&paths)
{
    Polygons out;
    out.reserve(paths.size());
    for (Points &path : paths)
        out.emplace_back(std::move(path));
    return out;
}

// Do polygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool polygons_match(const Polygon &l, const Polygon &r);

Polygon make_circle(double radius, double error);
Polygon make_circle_num_segments(double radius, size_t num_segments);

/// <summary>
/// Define point laying on polygon
/// keep index of polygon line and point coordinate
/// </summary>
struct PolygonPoint
{
    // index of line inside of polygon
    // 0 .. from point polygon[0] to polygon[1]
    size_t index;

    // Point, which lay on line defined by index
    Point point;
};
using PolygonPoints = std::vector<PolygonPoint>;

// To replace reserve_vector where it's used for Polygons
template<class I> IntegerOnly<I, Polygons> reserve_polygons(I cap)
{
    return reserve_vector<Polygon, I, typename Polygons::allocator_type>(cap);
}

} // Slic3r

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Polygon>{ typedef polygon_concept type; };

    template <>
    struct polygon_traits<Slic3r::Polygon> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Points::const_iterator iterator_type;
        typedef Slic3r::Point point_type;

        // Get the begin iterator
        static inline iterator_type begin_points(const Slic3r::Polygon& t) {
            return t.points.begin();
        }

        // Get the end iterator
        static inline iterator_type end_points(const Slic3r::Polygon& t) {
            return t.points.end();
        }

        // Get the number of sides of the polygon
        static inline std::size_t size(const Slic3r::Polygon& t) {
            return t.points.size();
        }

        // Get the winding direction of the polygon
        static inline winding_direction winding(const Slic3r::Polygon& /* t */) {
            return unknown_winding;
        }
    };

    template <>
    struct polygon_mutable_traits<Slic3r::Polygon> {
        // expects stl style iterators
        template <typename iT>
        static inline Slic3r::Polygon& set_points(Slic3r::Polygon& polygon, iT input_begin, iT input_end) {
            polygon.points.clear();
            while (input_begin != input_end) {
                polygon.points.push_back(Slic3r::Point());
                boost::polygon::assign(polygon.points.back(), *input_begin);
                ++input_begin;
            }
            // skip last point since Boost will set last point = first point
            assert(polygon.points.front() == polygon.points.back());
            polygon.points.pop_back();
            return polygon;
        }
    };
    
    template <>
    struct geometry_concept<Slic3r::Polygons> { typedef polygon_set_concept type; };

    //next we map to the concept through traits
    template <>
    struct polygon_set_traits<Slic3r::Polygons> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Polygons::const_iterator iterator_type;
        typedef Slic3r::Polygons operator_arg_type;

        static inline iterator_type begin(const Slic3r::Polygons& polygon_set) {
            return polygon_set.begin();
        }

        static inline iterator_type end(const Slic3r::Polygons& polygon_set) {
            return polygon_set.end();
        }

        //don't worry about these, just return false from them
        static inline bool clean(const Slic3r::Polygons& /* polygon_set */) { return false; }
        static inline bool sorted(const Slic3r::Polygons& /* polygon_set */) { return false; }
    };

    template <>
    struct polygon_set_mutable_traits<Slic3r::Polygons> {
        template <typename input_iterator_type>
        static inline void set(Slic3r::Polygons& polygons, input_iterator_type input_begin, input_iterator_type input_end) {
          polygons.assign(input_begin, input_end);
        }
    };
} }
// end Boost

#endif
