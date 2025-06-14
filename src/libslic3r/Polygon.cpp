///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Filip Sykala @Jony01, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2014 Petr Ledvina @ledvinap
///|/
///|/ ported from lib/Slic3r/Polygon.pm:
///|/ Copyright (c) Prusa Research 2017 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 Mark Hindess
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Exception.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include <ankerl/unordered_dense.h>

namespace Slic3r {

double Polygon::length() const
{
    double l = 0;
    if (this->points.size() > 1) {
        l = (this->points.back() - this->points.front()).cast<double>().norm();
        for (size_t i = 1; i < this->points.size(); ++ i)
            l += (this->points[i] - this->points[i - 1]).cast<double>().norm();
    }
    return l;
}

Lines Polygon::lines() const
{
    return to_lines(*this);
}

Polyline Polygon::split_at_vertex(const Point &point) const
{
    // find index of point
    for (const Point &pt : this->points)
        if (pt == point)
            return this->split_at_index(int(&pt - &this->points.front()));
    throw Slic3r::InvalidArgument("Point not found");
    return Polyline();
}

// Split a closed polygon into an open polyline, with the split point duplicated at both ends.
Polyline
Polygon::split_at_index(size_t index) const
{
    assert(index < this->points.size());
    Polyline polyline;
    polyline.points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        polyline.points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        polyline.points.push_back(*it);
    return polyline;
}

double Polygon::area(const Points &points)
{
    // Better than ClipperLib::Area(this->points); ?
    double a = 0.;
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point &p : points) {
            Vec2d p2 = p.cast<double>();
            a += cross2(p1, p2);
            p1 = p2;
        }
    }
    assert(is_approx(ClipperLib::Area(points), 0.5 * a, SCALED_EPSILON * SCALED_EPSILON * 1.));
    return 0.5 * a;
}

double Polygon::area() const
{
    return Polygon::area(points);
}

bool Polygon::is_counter_clockwise() const
{
    return ClipperLib::Orientation(this->points);
}

bool Polygon::is_clockwise() const
{
    return !this->is_counter_clockwise();
}

bool Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

void Polygon::douglas_peucker(coord_t tolerance)
{
    if (this->size() < 3)
        return;
    this->points.push_back(this->points.front());
    MultiPoint::douglas_peucker(tolerance);
    assert(this->points.size() > 1);
    if (points.size() < 3) {
        // not a good polygon : too small. clear it
        points.clear();
    } else {
        assert(this->points.front().coincides_with(this->points.back()));
        this->points.pop_back();
        assert(!this->points.front().coincides_with_epsilon(this->points.back()));
        assert(this->points.size() > 1);
    }
}

Polygons Polygon::simplify(double tolerance) const
{
    // Works on CCW polygons only, CW contour will be reoriented to CCW by Clipper's simplify_polygons()!
    assert(this->is_counter_clockwise());

    // repeat first point at the end in order to apply Douglas-Peucker
    // on the whole polygon
    Points points = this->points;
    points.push_back(points.front());
    Polygon p(MultiPoint::douglas_peucker(points, tolerance));
    // last point remove in polygon contructor
    assert(!p.front().coincides_with_epsilon(p.back()));
    
    Polygons pp;
    pp.push_back(p);
    return simplify_polygons(pp);
}

// Only call this on convex polygons or it will return invalid results
void Polygon::triangulate_convex(Polygons* polygons) const
{
    for (Points::const_iterator it = this->points.begin() + 2; it != this->points.end(); ++it) {
        Polygon p;
        p.points.reserve(3);
        p.points.push_back(this->points.front());
        p.points.push_back(*(it-1));
        p.points.push_back(*it);
        
        // this should be replaced with a more efficient call to a merge_collinear_segments() method
        if (p.area() > 0) polygons->push_back(p);
    }
}

// center of mass
// source: https://en.wikipedia.org/wiki/Centroid
Point Polygon::centroid() const
{
    double area_sum = 0.;
    Vec2d  c(0., 0.);
    if (points.size() >= 3) {
        Vec2d p1 = points.back().cast<double>();
        for (const Point &p : points) {
            Vec2d p2 = p.cast<double>();
            double a = cross2(p1, p2);
            area_sum += a;
            c += (p1 + p2) * a;
            p1 = p2;
        }
    }
    return Point(Vec2d(c / (3. * area_sum)));
}

bool Polygon::intersection(const Line &line, Point *intersection) const
{
    if (this->points.size() < 2)
        return false;
    if (Line(this->points.front(), this->points.back()).intersection(line, intersection))
        return true;
    for (size_t i = 1; i < this->points.size(); ++ i)
        if (Line(this->points[i - 1], this->points[i]).intersection(line, intersection))
            return true;
    return false;
}

bool Polygon::first_intersection(const Line& line, Point* intersection) const
{
    if (this->points.size() < 2)
        return false;

    bool   found = false;
    double dmin  = 0.;
    Line l(this->points.back(), this->points.front());
    for (size_t i = 0; i < this->points.size(); ++ i) {
        l.b = this->points[i];
        Point ip;
        if (l.intersection(line, &ip)) {
            if (! found) {
                found = true;
                dmin = (line.a - ip).cast<double>().squaredNorm();
                *intersection = ip;
            } else {
                double d = (line.a - ip).cast<double>().squaredNorm();
                if (d < dmin) {
                    dmin = d;
                    *intersection = ip;
                }
            }
        }
        l.a = l.b;
    }
    return found;
}

bool Polygon::intersections(const Line &line, Points *intersections) const
{
    if (this->points.size() < 2)
        return false;

    size_t intersections_size = intersections->size();
    Line l(this->points.back(), this->points.front());
    for (size_t i = 0; i < this->points.size(); ++ i) {
        l.b = this->points[i];
        Point intersection;
        if (l.intersection(line, &intersection))
            intersections->emplace_back(std::move(intersection));
        l.a = l.b;
    }
    return intersections->size() > intersections_size;
}

// Filter points from poly to the output with the help of FilterFn.
// filter function receives two vectors:
// v1: this_point - previous_point
// v2: next_point - this_point
// and returns true if the point is to be copied to the output.
template<typename FilterFn>
Points filter_points_by_vectors(const Points &poly, FilterFn filter) {
    // Last point is the first point visited.
    Point p1 = poly.back();
    // Previous vector to p1.
    Vec2d v1 = (p1 - *(poly.end() - 2)).cast<double>();

    Points out;
    for (Point p2 : poly) {
        // p2 is next point to the currently visited point p1.
        Vec2d v2 = (p2 - p1).cast<double>();
        if (filter(v1, v2))
            out.emplace_back(p1);
        v1 = v2;
        p1 = p2;
    }

    return out;
}

template<typename ConvexConcaveFilterFn>
Points filter_convex_concave_points_by_angle_threshold(const Points &poly,
                                                       double min_angle,
                                                       double max_angle,
                                                       ConvexConcaveFilterFn convex_concave_filter) {
    assert(min_angle >= 0.);
    assert(max_angle >= 0.);
    assert(max_angle <= PI);
    if (min_angle > EPSILON || max_angle < PI - EPSILON) {
        double min_dot = -cos(min_angle);
        double max_dot = -cos(max_angle);
        return filter_points_by_vectors(poly,
                                        [convex_concave_filter, min_dot, max_dot](const Vec2d &v1, const Vec2d &v2) {
                                            //first, check if it's the right kind of angle.
                                            bool is_convex = convex_concave_filter(v1, v2);
                                            if (!is_convex)
                                                return false;
                                            double dot = v1.normalized().dot(v2.normalized());
                                            return (min_dot <= dot) && (dot <= max_dot);
                                        });
    } else {
        return filter_points_by_vectors(poly, [convex_concave_filter](const Vec2d &v1, const Vec2d &v2) {
            return convex_concave_filter(v1, v2);
        });
    }
}

Points Polygon::convex_points(double min_angle, double max_angle) const
{
    assert(size() > 2);
    return filter_convex_concave_points_by_angle_threshold(this->points, min_angle, max_angle, [](const Vec2d &v1, const Vec2d &v2){ return (cross2(v1, v2) >= 0.); });
}

Points Polygon::concave_points(double min_angle, double max_angle) const
{
    assert(size() > 2);
    return filter_convex_concave_points_by_angle_threshold(this->points, min_angle, max_angle, [](const Vec2d &v1, const Vec2d &v2){ return (cross2(v1, v2) <= 0.); });
}

template<typename FilterFn>
std::vector<size_t> filter_points_idx_by_vectors(const Points &poly, FilterFn filter) {
    assert(poly.size() > 2);
    if (poly.size() < 3)
        return {};

    // first point is the first point visited.
    Point p1 = poly.front();
    // Previous vector to p1.
    Vec2d v1 = (p1 - poly.back()).cast<double>();

    std::vector<size_t> out;
    for (size_t idx = 1; idx < poly.size(); ++idx) {
        const Point &p2 = poly[idx];
        // p2 is next point to the currently visited point p1.
        Vec2d v2 = (p2 - p1).cast<double>();
        if (filter(v1, v2))
            out.push_back(idx - 1);
        v1 = v2;
        p1 = p2;
    }

    // also check last point.
    {
        const Point &p2 = poly.front();
        // p2 is next point to the currently visited point p1.
        Vec2d v2 = (p2 - p1).cast<double>();
        if (filter(v1, v2))
            out.push_back(poly.size() - 1);
    }
    return out;
}

template<typename ConvexConcaveFilterFn>
std::vector<size_t> filter_convex_concave_points_idx_by_angle_threshold(const Points &poly,
                                                                        double min_angle,
                                                                        double max_angle,
                                                                        ConvexConcaveFilterFn convex_concave_filter) {
    assert(min_angle >= 0.);
    assert(max_angle >= 0.);
    assert(max_angle <= PI);
    if (min_angle > EPSILON || max_angle < PI - EPSILON) {
        double min_dot = -cos(min_angle);
        double max_dot = -cos(max_angle);
        return filter_points_idx_by_vectors(poly,
                                            [convex_concave_filter, min_dot, max_dot](const Vec2d &v1, const Vec2d &v2) {
                                                //first, check if it's the right kind of angle.
                                                bool is_convex = convex_concave_filter(v1, v2);
                                                if (!is_convex)
                                                    return false;
                                                double dot = v1.normalized().dot(v2.normalized());
                                                // if v1 and v2 has same direction = flat angle.
                                                // if v1.dot(v2) is negative -> sharp angle
                                                return (min_dot <= dot) && (dot <= max_dot);
                                            });
    } else {
        return filter_points_idx_by_vectors(poly, [convex_concave_filter](const Vec2d &v1, const Vec2d &v2) {
            return convex_concave_filter(v1, v2);
        });
    }
}

std::vector<size_t> Polygon::convex_points_idx(double min_angle, double max_angle) const
{
    return filter_convex_concave_points_idx_by_angle_threshold(this->points, min_angle, max_angle, [](const Vec2d &v1, const Vec2d &v2){ return (cross2(v1, v2) > 0.); });
}

std::vector<size_t> Polygon::concave_points_idx(double min_angle, double max_angle) const
{
    return filter_convex_concave_points_idx_by_angle_threshold(this->points, min_angle, max_angle, [](const Vec2d &v1, const Vec2d &v2){ return (cross2(v1, v2) < 0.); });
}

// Projection of a point onto the polygon. Return {Point, pt_idx}
std::pair<Point, size_t> Polygon::point_projection(const Point &point) const
{
    size_t pt_idx = size_t(-1);
    Point proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (! this->points.empty()) {
        for (size_t i = 0; i < this->points.size(); ++ i) {
            const Point &pt0 = this->points[i];
            const Point &pt1 = this->points[(i + 1 == this->points.size()) ? 0 : i + 1];
            double d = (point - pt0).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt0;
                pt_idx = i;
            }
            d = (point - pt1).cast<double>().norm();
            if (d < dmin) {
                dmin = d;
                proj = pt1;
                pt_idx = (i + 1 == this->points.size()) ? 0 : i + 1;
            }
            Vec2d v1(coordf_t(pt1(0) - pt0(0)), coordf_t(pt1(1) - pt0(1)));
            coordf_t div = v1.squaredNorm();
            if (div > 0.) {
                Vec2d v2(coordf_t(point(0) - pt0(0)), coordf_t(point(1) - pt0(1)));
                coordf_t t = v1.dot(v2) / div;
                if (t > 0. && t < 1.) {
                    Point foot(coord_t(floor(coordf_t(pt0(0)) + t * v1(0) + 0.5)), coord_t(floor(coordf_t(pt0(1)) + t * v1(1) + 0.5)));
                    d = (point - foot).cast<double>().norm();
                    if (d < dmin) {
                        dmin = d;
                        proj = foot;
                        pt_idx = i;
                    }
                }
            }
        }
    }
    return {proj, pt_idx};
}

std::vector<float> Polygon::parameter_by_length() const
{
    // Parametrize the polygon by its length.
    std::vector<float> lengths(points.size()+1, 0.);
    for (size_t i = 1; i < points.size(); ++ i)
        lengths[i] = lengths[i-1] + (points[i] - points[i-1]).cast<float>().norm();
    lengths.back() = lengths[lengths.size()-2] + (points.front() - points.back()).cast<float>().norm();
    return lengths;
}

void Polygon::densify(float min_length, std::vector<float>* lengths_ptr)
{
    std::vector<float> lengths_local;
    std::vector<float>& lengths = lengths_ptr ? *lengths_ptr : lengths_local;

    if (! lengths_ptr) {
        // Length parametrization has not been provided. Calculate our own.
        lengths = this->parameter_by_length();
    }

    assert(points.size() == lengths.size() - 1);

    for (size_t j=1; j<=points.size(); ++j) {
        bool last = j == points.size();
        int i = last ? 0 : j;

        if (lengths[j] - lengths[j-1] > min_length) {
            Point diff = points[i] - points[j-1];
            float diff_len = lengths[j] - lengths[j-1];
            float r = (min_length/diff_len);
            Point new_pt = points[j-1] + Point(r*diff[0], r*diff[1]);
            points.insert(points.begin() + j, new_pt);
            lengths.insert(lengths.begin() + j, lengths[j-1] + min_length);
        }
    }
    assert(points.size() == lengths.size() - 1);
}

size_t Polygon::remove_collinear(coord_t max_offset){
    size_t nb_del = 0;
    if (points.size() < 3) return 0;

    double min_dist_sq = coordf_t(max_offset) * max_offset;
    while (points.size() > 2 && Line::distance_to_squared(points[0], points.back(), points[1]) < min_dist_sq){
        //colinear! delete!
        points.erase(points.begin());
        nb_del++;
    }
    for (size_t idx = 1; idx < points.size()-1; ) {
        //if (Line(previous, points[idx + 1]).distance_to(points[idx]) < SCALED_EPSILON){
        if (Line::distance_to_squared(points[idx], points[idx-1], points[idx + 1]) < min_dist_sq){
            //colinear! delete!
            points.erase(points.begin() + idx);
            nb_del++;
        } else {
            idx++;
        }
    }
    while (points.size() > 2 && Line::distance_to_squared(points.back(), points[points.size()-2], points.front()) < min_dist_sq) {
        //colinear! delete!
        points.erase(points.end()-1);
        nb_del++;
    }

    return nb_del;
}

size_t Polygon::remove_collinear_angle(double angle_radian) {
    size_t nb_del = 0;
    if (points.size() < 3) return 0;
    //std::cout << "== remove_collinear_angle \n";
    double min_dist_sq = std::sin(angle_radian);
    min_dist_sq = min_dist_sq * min_dist_sq;
    while (points.size() > 2 && Line::distance_to_squared(points.front(), points.back(), points[1]) < min_dist_sq * std::min(points.back().distance_to_square(points.front()), points.front().distance_to_square(points[1]))) {
       /* if (Line::distance_to_squared(points.front(), points.back(), points[1]) > SCALED_EPSILON) {
            std::cout << "Fcolinear angle " << Line::distance_to_squared(points[0], points.back(), points[1]) << " < " << (min_dist_sq * std::min(points.back().distance_to_square(points.front()), points.front().distance_to_square(points[1]))) << " (" << min_dist_sq << " * " << std::min(points.back().distance_to_square(points.front()), points.front().distance_to_square(points[1])) << ")\n";
            std::cout << "      unscaled= " << unscaled(Line::distance_to(points[0], points.back(), points[1])) << " < " << unscaled(std::sin(angle_radian) * std::min(points.back().distance_to(points.front()), points.front().distance_to(points[1]))) << " (" << std::sin(angle_radian) << " * " << unscaled(std::min(points.back().distance_to(points.front()), points.front().distance_to(points[1]))) << ")\n";
            std::cout << "      dists: " << unscaled(points.back().distance_to(points.front())) << " => " << unscaled(points.front().distance_to(points[1])) << "\n";
        }*/
        //colinear! delete!
        points.erase(points.begin());
        nb_del++;
    }
    for (size_t idx = 1; idx < points.size() - 1 && points.size() > 2; ) {
        if (Line::distance_to_squared(points[idx], points[idx - 1], points[idx + 1]) < min_dist_sq * std::min(points[idx - 1].distance_to_square(points[idx]), points[idx].distance_to_square(points[idx + 1]))) {
            /*if (Line::distance_to_squared(points[idx], points[idx - 1], points[idx + 1]) > SCALED_EPSILON) {
                std::cout << " colinear angle " << Line::distance_to_squared(points[idx], points[idx - 1], points[idx + 1]) << " < " << (min_dist_sq * std::min(points[idx - 1].distance_to_square(points[idx]), points[idx].distance_to_square(points[idx + 1]))) << " (" << min_dist_sq << " * " << std::min(points[idx - 1].distance_to_square(points[idx]), points[idx].distance_to_square(points[idx + 1])) << ")\n";
                std::cout << "      unscaled= " << unscaled(Line::distance_to(points[idx], points[idx - 1], points[idx + 1])) << " < " << unscaled(std::sin(angle_radian) * std::min(points[idx - 1].distance_to(points[idx]), points[idx].distance_to(points[idx + 1]))) << " (" << std::sin(angle_radian) << " * " << unscaled(std::min(points[idx - 1].distance_to(points[idx]), points[idx].distance_to(points[idx + 1]))) << ")\n";
                std::cout << "      dists: " << unscaled(points[idx - 1].distance_to(points[idx])) << " => " << unscaled(points[idx].distance_to(points[idx + 1])) << "\n";
            }*/
            //colinear! delete!
            points.erase(points.begin() + idx);
            nb_del++;
        } else {
            idx++;
        }
    }
    while (points.size() > 2 && Line::distance_to_squared(points.back(), points[points.size() - 2], points.front()) < min_dist_sq * std::min(points.back().distance_to_square(points[points.size() - 2]), points.front().distance_to_square(points.back()))) {
        //colinear! delete!
        points.erase(points.end() - 1);
        nb_del++;
    }

    return nb_del;
}

BoundingBox get_extents(const Polygon &poly) 
{ 
    return poly.bounding_box();
}

BoundingBox get_extents(const Polygons &polygons)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents(polygons.front());
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents(polygons[i]));
    }
    return bb;
}

BoundingBox get_extents_rotated(const Polygon &poly, double angle) 
{ 
    return get_extents_rotated(poly.points, angle);
}

BoundingBox get_extents_rotated(const Polygons &polygons, double angle)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents_rotated(polygons.front().points, angle);
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents_rotated(polygons[i].points, angle));
    }
    return bb;
}

extern std::vector<BoundingBox> get_extents_vector(const Polygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

// Polygon must be valid (at least three points), collinear points and duplicate points removed.
bool polygon_is_convex(const Points &poly)
{
    if (poly.size() < 3)
        return false;

    Point p0 = poly[poly.size() - 2];
    Point p1 = poly[poly.size() - 1];
    for (size_t i = 0; i < poly.size(); ++ i) {
        Point p2 = poly[i];
        auto det = cross2((p1 - p0).cast<int64_t>(), (p2 - p1).cast<int64_t>());
        if (det < 0)
            return false;
        p0 = p1;
        p1 = p2;
    }
    return true;
}

bool has_duplicate_points(const Polygons &polys)
{
#if 1
    // Check globally.
#if 0
    // Detect duplicates by sorting with quicksort. It is quite fast, but ankerl::unordered_dense is around 1/4 faster.
    Points allpts;
    allpts.reserve(count_points(polys));
    for (const Polygon &poly : polys)
        allpts.insert(allpts.end(), poly.points.begin(), poly.points.end());
    return has_duplicate_points(std::move(allpts));
#else
    // Detect duplicates by inserting into an ankerl::unordered_dense hash set, which is is around 1/4 faster than qsort.
    struct PointHash {
        uint64_t operator()(const Point &p) const noexcept {
#ifdef COORD_64B
            return ankerl::unordered_dense::detail::wyhash::hash(p.x()) 
                + ankerl::unordered_dense::detail::wyhash::hash(p.y());
#else
            uint64_t h;
            static_assert(sizeof(h) == sizeof(p));
            memcpy(&h, &p, sizeof(p));
            return ankerl::unordered_dense::detail::wyhash::hash(h);
#endif
        }
    };
    ankerl::unordered_dense::set<Point, PointHash> allpts;
    allpts.reserve(count_points(polys));
    for (const Polygon &poly : polys)
        for (const Point &pt : poly.points)
        if (! allpts.insert(pt).second)
            // Duplicate point was discovered.
            return true;
    return false;
#endif
#else
    // Check per contour.
    for (const Polygon &poly : polys)
        if (has_duplicate_points(poly))
            return true;
    return false;
#endif
}

bool remove_same_neighbor(Polygon &polygon)
{
    Points &points = polygon.points;
    if (points.empty())
        return false;
    auto last = std::unique(points.begin(), points.end());

    // remove first and last neighbor duplication
    if (const Point &last_point = *(last - 1); last_point == points.front()) {
        --last;
    }

    // no duplicits
    if (last == points.end())
        return false;

    points.erase(last, points.end());
    return true;
}

bool remove_same_neighbor(Polygons &polygons)
{
    if (polygons.empty())
        return false;
    bool exist = false;
    for (Polygon &polygon : polygons)
        exist |= remove_same_neighbor(polygon);
    // remove empty polygons
    polygons.erase(std::remove_if(polygons.begin(), polygons.end(), [](const Polygon &p) { return p.points.size() <= 2; }), polygons.end());
    return exist;
}

// note: prefer using ExPolygons.
void ensure_valid(Polygons &polygons, coord_t resolution /*= SCALED_EPSILON*/) {
    for (size_t i = 0; i < polygons.size(); ++i) {
        bool ccw = polygons[i].is_counter_clockwise();
        polygons[i].douglas_peucker(resolution);
        if (polygons[i].size() < 3) {
            // if erase contour, also erase its holes.
            if (ccw) {
                for (size_t hole_idx = i + 1; hole_idx < polygons.size(); ++hole_idx) {
                    if (polygons[i].is_clockwise()) {
                        polygons.erase(polygons.begin() + hole_idx);
                        --hole_idx;
                    } else {
                        break;
                    }
                }
            }
            polygons.erase(polygons.begin() + i);
            --i;
        }
    }
}

Polygons ensure_valid(Polygons &&polygons, coord_t resolution /*= SCALED_EPSILON*/)
{
    ensure_valid(polygons, resolution);
    return std::move(polygons);
}

Polygons ensure_valid(coord_t resolution, Polygons &&polygons) {
    return ensure_valid(std::move(polygons), resolution);
}

// unsafe, can delete a contour withotu its holes (ie, only call it if you work only on contour)
bool ensure_valid(Polygon &polygon, coord_t resolution) {
    polygon.douglas_peucker(resolution);
    if (polygon.size() < 3) {
        polygon.clear();
        return false;
    }
    return true;
}

#ifdef _DEBUGINFO
void assert_valid(const Polygons &polygons) {
    for (const Polygon &polygon : polygons) {
        polygon.assert_valid();
    }
}
#endif

static inline bool is_stick(const Point &p1, const Point &p2, const Point &p3)
{
    Point v1 = p2 - p1;
    Point v2 = p3 - p2;
    int64_t dir = int64_t(v1(0)) * int64_t(v2(0)) + int64_t(v1(1)) * int64_t(v2(1));
    if (dir > 0)
        // p3 does not turn back to p1. Do not remove p2.
        return false;
    double l2_1 = double(v1(0)) * double(v1(0)) + double(v1(1)) * double(v1(1));
    double l2_2 = double(v2(0)) * double(v2(0)) + double(v2(1)) * double(v2(1));
    if (dir == 0)
        // p1, p2, p3 may make a perpendicular corner, or there is a zero edge length.
        // Remove p2 if it is coincident with p1 or p2.
        return l2_1 == 0 || l2_2 == 0;
    // p3 turns back to p1 after p2. Are p1, p2, p3 collinear?
    // Calculate distance from p3 to a segment (p1, p2) or from p1 to a segment(p2, p3),
    // whichever segment is longer
    double cross = double(v1(0)) * double(v2(1)) - double(v2(0)) * double(v1(1));
    double dist2 = cross * cross / std::max(l2_1, l2_2);
    return dist2 < EPSILON * EPSILON;
}

bool remove_sticks(Polygon &poly)
{
    bool modified = false;
    size_t j = 1;
    for (size_t i = 1; i + 1 < poly.points.size(); ++ i) {
        if (! is_stick(poly[j-1], poly[i], poly[i+1])) {
            // Keep the point.
            if (j < i)
                poly.points[j] = poly.points[i];
            ++ j;
        }
    }
    if (++ j < poly.points.size()) {
        poly.points[j-1] = poly.points.back();
        poly.points.erase(poly.points.begin() + j, poly.points.end());
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points[poly.points.size()-2], poly.points.back(), poly.points.front())) {
        poly.points.pop_back();
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points.back(), poly.points.front(), poly.points[1]))
        poly.points.erase(poly.points.begin());
    return modified;
}

bool remove_sticks(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        modified |= remove_sticks(polys[i]);
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        }
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_degenerate(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_small(Polygons &polys, double min_area)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (std::abs(polys[i].area()) >= min_area) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

void remove_collinear(Polygon &poly, coord_t max_offset)
{
    poly.remove_collinear(max_offset);
}
void remove_collinear(Polygons &polys, coord_t max_offset)
{
    for (Polygon &poly : polys)
        poly.remove_collinear(max_offset);
}

static inline void simplify_polygon_impl(const Points &points, double tolerance, bool strictly_simple, Polygons &out)
{
    Points simplified = MultiPoint::douglas_peucker(points, tolerance);
    // then remove the last (repeated) point.
    assert(simplified.front().coincides_with_epsilon(simplified.back()));
    simplified.pop_back();
    // Simplify the decimated contour by ClipperLib.
    bool ccw = ClipperLib::Area(simplified) > 0.;
    for (Points& path : ClipperLib::SimplifyPolygons(ClipperUtils::SinglePathProvider(simplified), ClipperLib::pftNonZero, strictly_simple)) {
        if (!ccw)
            // ClipperLib likely reoriented negative area contours to become positive. Reverse holes back to CW.
            std::reverse(path.begin(), path.end());
        out.emplace_back(std::move(path));
    }
}

Polygons polygons_simplify(Polygons &&source_polygons, double tolerance, bool strictly_simple /* = true */)
{
    Polygons out;
    out.reserve(source_polygons.size());
    for (Polygon &source_polygon : source_polygons) {
        // Run Douglas / Peucker simplification algorithm on an open polyline (by repeating the first point at the end of the polyline),
        source_polygon.points.emplace_back(source_polygon.points.front());
        simplify_polygon_impl(source_polygon.points, tolerance, strictly_simple, out);
    }
    return out;
}

Polygons polygons_simplify(const Polygons &source_polygons, double tolerance, bool strictly_simple /* = true */)
{
    Polygons out;
    out.reserve(source_polygons.size());
    for (const Polygon &source_polygon : source_polygons) {
        // Run Douglas / Peucker simplification algorithm on an open polyline (by repeating the first point at the end of the polyline),
        simplify_polygon_impl(to_polyline(source_polygon).points, tolerance, strictly_simple, out);
    }
    return out;
}

// Do polygons match? If they match, they must have the same topology,
// however their contours may be rotated.
bool polygons_match(const Polygon &l, const Polygon &r)
{
    if (l.size() != r.size())
        return false;
    auto it_l = std::find(l.points.begin(), l.points.end(), r.points.front());
    if (it_l == l.points.end())
        return false;
    auto it_r = r.points.begin();
    for (; it_l != l.points.end(); ++ it_l, ++ it_r)
        if (*it_l != *it_r)
            return false;
    it_l = l.points.begin();
    for (; it_r != r.points.end(); ++ it_l, ++ it_r)
        if (*it_l != *it_r)
            return false;
    return true;
}

bool contains(const Polygon &polygon, const Point &p, bool border_result)
{
    if (const int poly_count_inside = ClipperLib::PointInPolygon(p, polygon.points); 
        poly_count_inside == -1)
        return border_result;
    else
        return (poly_count_inside % 2) == 1;
}

bool contains(const Polygons &polygons, const Point &p, bool border_result)
{
    int poly_count_inside = 0;
    for (const Polygon &poly : polygons) {
        const int is_inside_this_poly = ClipperLib::PointInPolygon(p, poly.points);
        if (is_inside_this_poly == -1)
            return border_result;
        poly_count_inside += is_inside_this_poly;
    }
    return (poly_count_inside % 2) == 1;
}

Polygon make_circle(double radius, double error)
{
    double angle = 2. * acos(1. - error / radius);
    size_t num_segments = size_t(ceil(2. * M_PI / angle));
    return make_circle_num_segments(radius, num_segments);
}

Polygon make_circle_num_segments(double radius, size_t num_segments)
{
    Polygon out;
    out.points.reserve(num_segments);
    double angle_inc = 2.0 * M_PI / num_segments;
    for (size_t i = 0; i < num_segments; ++ i) {
        const double angle = angle_inc * i;
        out.points.emplace_back(coord_t(cos(angle) * radius), coord_t(sin(angle) * radius));
    }
    return out;
}

}
