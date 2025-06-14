///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Enrico Turri @enricoturri1966
///|/ Copyright (c) Slic3r 2013 - 2016 Alessandro Ranellucci @alranel
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "MultiPoint.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

void MultiPoint::scale(double factor)
{
    for (Point &pt : points)
        pt *= factor;
}

void MultiPoint::scale(double factor_x, double factor_y)
{
    for (Point &pt : points)
    {
		pt(0) = coord_t(pt(0) * factor_x);
		pt(1) = coord_t(pt(1) * factor_y);
    }
}

void MultiPoint::translate(const Vector &v)
{
    for (Point &pt : points)
        pt += v;
}

void MultiPoint::rotate(double cos_angle, double sin_angle)
{
    for (Point &pt : this->points) {
        double cur_x = double(pt(0));
        double cur_y = double(pt(1));
        pt(0) = coord_t(round(cos_angle * cur_x - sin_angle * cur_y));
        pt(1) = coord_t(round(cos_angle * cur_y + sin_angle * cur_x));
    }
}

void MultiPoint::rotate(double angle, const Point &center)
{
    double s = sin(angle);
    double c = cos(angle);
    for (Point &pt : points) {
        Vec2crd v(pt - center);
        pt(0) = (coord_t)round(double(center(0)) + c * v[0] - s * v[1]);
        pt(1) = (coord_t)round(double(center(1)) + c * v[1] + s * v[0]);
    }
}

int MultiPoint::find_point(const Point &point) const
{
    for (const Point &pt : this->points)
        if (pt == point)
            return int(&pt - &this->points.front());
    return -1;  // not found
}

int MultiPoint::find_point(const Point &point, coordf_t scaled_epsilon) const
{
    if (scaled_epsilon == 0)
        return this->find_point(point);

    coordf_t dist2_min = std::numeric_limits<coordf_t>::max();
    coordf_t eps2      = scaled_epsilon * scaled_epsilon;
    int      idx_min   = -1;
    for (const Point &pt : this->points) {
        coordf_t d2 = pt.distance_to_square(point); //(pt - point).cast<coordf_t>().squaredNorm();
        if (d2 < dist2_min) {
            idx_min = int(&pt - &this->points.front());
            dist2_min = d2;
        }
    }
    return dist2_min < eps2 ? idx_min : -1;
}

BoundingBox MultiPoint::bounding_box() const
{
    return BoundingBox(this->points);
}

bool MultiPoint::has_duplicate_points() const
{
    for (size_t i = 1; i < points.size(); ++i)
        if (points[i-1] == points[i])
            return true;
    return false;
}

bool MultiPoint::remove_duplicate_points()
{
    size_t j = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[j] == points[i]) {
            // Just increase index i.
        } else {
            ++ j;
            if (j < i)
                points[j] = points[i];
        }
    }
    if (++ j < points.size()) {
        points.erase(points.begin() + j, points.end());
        return true;
    }
    return false;
}

// Projection of a point onto the polygon.
//FIXME: delete this, it's moved somewhere.
std::pair<Point, size_t> MultiPoint::point_projection(const Point &point) const {
    size_t pt_idx = size_t(-1);
    Point proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (!this->points.empty()) {
        for (size_t i = 0; i < this->points.size()-1; ++i) {
            const Point &pt0 = this->points[i];
            const Point &pt1 = this->points[i + 1];
            double d = pt0.distance_to(point);
            if (d < dmin) {
                dmin = d;
                proj = pt0;
                pt_idx = i;
            }
            d = pt1.distance_to(point);
            if (d < dmin) {
                dmin = d;
                proj = pt1;
                pt_idx = i + 1;
            }
            Vec2d v1(coordf_t(pt1(0) - pt0(0)), coordf_t(pt1(1) - pt0(1)));
            coordf_t div = dot(v1);
            if (div > 0.) {
                Vec2d v2(coordf_t(point(0) - pt0(0)), coordf_t(point(1) - pt0(1)));
                coordf_t t = dot(v1, v2) / div;
                if (t > 0. && t < 1.) {
                    Point foot(coord_t(floor(coordf_t(pt0(0)) + t * v1(0) + 0.5)), coord_t(floor(coordf_t(pt0(1)) + t * v1(1) + 0.5)));
                    d = foot.distance_to(point);
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

// Visivalingam simplification algorithm https://github.com/slic3r/Slic3r/pull/3825
// thanks to @fuchstraumer
/*
     struct - vis_node
     Used with the visivalignam simplification algorithm, which needs to be able to find a points
    successors and predecessors to operate succesfully. Since this struct is only used in one
    location, it could probably be dropped into a namespace to avoid polluting the slic3r namespace.
     Source: https://github.com/shortsleeves/visvalingam_simplify
     ^ Provided original algorithm implementation. I've only changed things a bit to "clean" them up
    (i.e be more like my personal style), and managed to do this without requiring a binheap implementation
 */
struct vis_node{
    vis_node(const size_t& idx, const size_t& _prev_idx, const size_t& _next_idx, const double& _area) : pt_idx(idx), prev_idx(_prev_idx), next_idx(_next_idx), area(_area) {}
    // Indices into a Points container, from which this object was constructed
    size_t pt_idx, prev_idx, next_idx;
    // Effective area of this "node"
    double area;
    // Overloaded operator used to sort the binheap
    // Greater area = "more important" node. So, this node is less than the 
    // other node if it's area is less than the other node's area
    bool operator<(const vis_node& other) { return (this->area < other.area); }
};
Points MultiPoint::visivalingam(const Points &pts, const double tolerance)
{
    // Make sure there's enough points in "pts" to bother with simplification.
    assert(pts.size() >= 2);
     // Result object
    Points results;
     // Lambda to calculate effective area spanned by a point and its immediate 
    // successor + predecessor.
    auto effective_area = [pts](const size_t& curr_pt_idx, const size_t& prev_pt_idx, const size_t& next_pt_idx)->coordf_t {
        const Point& curr = pts[curr_pt_idx];
        const Point& prev = pts[prev_pt_idx];
        const Point& next = pts[next_pt_idx];
        // Use point objects as vector-distances
		const Vec2d curr_to_next = (next - curr).cast<double>();
		const Vec2d prev_to_next = (prev - curr).cast<double>();
        // Take cross product of these two vector distances
		return 0.50 * abs(cross2(curr_to_next, prev_to_next));
    };
     // We store the effective areas for each node
    std::vector<coordf_t> areas;
    areas.reserve(pts.size());
     // Construct the initial set of nodes. We will make a heap out of the "heap" vector using 
    // std::make_heap. node_list is used later.
    std::vector<vis_node*> node_list;
    node_list.resize(pts.size());
    std::vector<vis_node*> heap;
    heap.reserve(pts.size());
    for (size_t i = 1; i < pts.size() - 1; ++ i) {
        // Get effective area of current node.
        coordf_t area = effective_area(i, i - 1, i + 1);
        // If area is greater than some arbitrarily small value, use it.
        node_list[i] = new vis_node(i, i - 1, i + 1, area);
        heap.push_back(node_list[i]);
    }
     // Call std::make_heap, which uses the < operator by default to make "heap" into 
    // a binheap, sorted by the < operator we defind in the vis_node struct
    std::make_heap(heap.begin(), heap.end());
     // Start comparing areas. Set min_area to an outrageous value initially.
    double min_area = -std::numeric_limits<double>::max();
    while (!heap.empty()) {
         // Get current node.
        vis_node* curr = heap.front();
         // Pop node we just retrieved off the heap. pop_heap moves front element in vector
        // to the back, so we can call pop_back()
        std::pop_heap(heap.begin(), heap.end());
        heap.pop_back();
         // Sanity assert check
        assert(curr == node_list[curr->pt_idx]);
         // If the current pt'ss area is less than that of the previous pt's area
        // use the last pt's area instead. This ensures we don't elimate the current
        // point without eliminating the previous 
        min_area = std::max(min_area, curr->area);
         // Update prev
        vis_node* prev = node_list[curr->prev_idx];
        if(prev != nullptr){
            prev->next_idx = curr->next_idx;
            prev->area = effective_area(prev->pt_idx, prev->prev_idx, prev->next_idx);
            // For some reason, std::make_heap() is the fastest way to resort the heap. Probably needs testing.
            std::make_heap(heap.begin(), heap.end());
        }
         // Update next
        vis_node* next = node_list[curr->next_idx];
        if(next != nullptr){
            next->prev_idx = curr->prev_idx;
            next->area = effective_area(next->pt_idx, next->prev_idx, next->next_idx);
            std::make_heap(heap.begin(), heap.end());
        }
         areas[curr->pt_idx] = min_area;
        node_list[curr->pt_idx] = nullptr;
        delete curr;
    }
    // Clear node list and shrink_to_fit() (to free actual memory). Not necessary. Could be removed.
    node_list.clear();
    node_list.shrink_to_fit();
    // This lambda is how we test whether or not to keep a point.
    auto use_point = [areas, tolerance](const size_t& idx)->bool {
        assert(idx < areas.size());
        // Return true at front/back of path/areas
        if(idx == 0 || idx == areas.size() - 1){
            return true;
        }
        // Return true if area at idx is greater than minimum area to consider "valid"
        else{
            return areas[idx] > tolerance;
        }
    };
    // Use previously defined lambda to build results.
    for (size_t i = 0; i < pts.size(); ++i) {
        if (use_point(i)){
            results.push_back(pts[i]);
        }
    }
     // Check that results has at least two points
    assert(results.size() >= 2);
     // Return simplified vector of points
    return results;
}

/// <summary>
/// douglas_peucker will keep only points that are more than 'tolerance' out of the current polygon.
/// But when we want to ensure we don't have a segment less than min_length, it's not very usable.
/// This one is more effective: it will keep all points like the douglas_peucker, and also all points 
/// in-between that satisfies the min_length, ordered by their tolerance.
/// Note: to have a all 360 points of a circle, then you need 'tolerance  <= min_length * (1-cos(1°)) ~= min_length * 0.000155'
/// Note: douglas_peucker is bad for simplifying circles, as it will create uneven segments.
/// </summary>
/// <param name="pts"></param>
/// <param name="tolerance"></param>
/// <param name="min_length"></param>
/// <returns></returns>
Points MultiPoint::_douglas_peucker_plus(const Points& pts, const double tolerance, const double min_length)
{
    Points result_pts;
    std::vector<size_t> result_idx;
    const double tolerance_sq = tolerance * tolerance;
    if (!pts.empty()) {
        const Point* anchor = &pts.front();
        size_t        anchor_idx = 0;
        const Point* floater = &pts.back();
        size_t        floater_idx = pts.size() - 1;
        result_pts.reserve(pts.size());
        result_pts.emplace_back(*anchor);
        result_idx.reserve(pts.size());
        result_idx.emplace_back(anchor_idx);
        if (anchor_idx != floater_idx) {
            assert(pts.size() > 1);
            std::vector<size_t> dpStack;
            dpStack.reserve(pts.size());
            dpStack.emplace_back(floater_idx);
            for (;;) {
                double max_dist_sq = 0.0;
                size_t furthest_idx = anchor_idx;
                // find point furthest from line seg created by (anchor, floater) and note it
                for (size_t i = anchor_idx + 1; i < floater_idx; ++i) {
                    double dist_sq = Line::distance_to_squared(pts[i], *anchor, *floater);
                    if (dist_sq > max_dist_sq) {
                        max_dist_sq = dist_sq;
                        furthest_idx = i;
                    }
                }
                // remove point if less than tolerance
                if (max_dist_sq <= tolerance_sq) {
                    if (!floater->coincides_with_epsilon(result_pts.back())) {
                        result_pts.emplace_back(*floater);
                        result_idx.emplace_back(floater_idx);
                    }
                    anchor_idx = floater_idx;
                    anchor = floater;
                    assert(dpStack.back() == floater_idx);
                    dpStack.pop_back();
                    if (dpStack.empty())
                        break;
                    floater_idx = dpStack.back();
                } else {
                    floater_idx = furthest_idx;
                    dpStack.emplace_back(floater_idx);
                }
                floater = &pts[floater_idx];
            }
        }
        assert(result_pts.front() == pts.front());
        assert(result_pts.back() == pts.back());

        // add other points that are at not less than min_length dist of the other points.
        //std::vector<double> distances;
        for (size_t segment_idx = 0; segment_idx < result_idx.size()-1; segment_idx++) {
            //distances.clear();
            size_t start_idx = result_idx[segment_idx];
            size_t end_idx = result_idx[segment_idx + 1];
            if (end_idx - start_idx == 1) continue;
            //create the list of distances
            double sum = 0;
            for (size_t i = start_idx; i < end_idx; i++) {
                double dist = pts[i].distance_to(pts[i + 1]);
                //distances.push_back(dist);
                sum += dist;
            }
            if (sum < min_length * 2) continue;

            Point* start_point = &result_pts[segment_idx];
            Point* end_point = &result_pts[segment_idx + 1];

            //use at least a point, even if it's not in the middle and sum ~= min_length * 2
            double max_dist_sq = 0.0;
            size_t furthest_idx = start_idx;
            const double half_min_length_sq = min_length * min_length / 4;
            // find point furthest from line seg created by (anchor, floater) and note it
            for (size_t i = start_idx + 1; i < end_idx; ++i) {
                if (start_point->distance_to_square(pts[i]) > half_min_length_sq && end_point->distance_to_square(pts[i]) > half_min_length_sq) {
                    double dist_sq = Line::distance_to_squared(pts[i], *start_point, *end_point);
                    if (dist_sq > max_dist_sq) {
                        max_dist_sq = dist_sq;
                        furthest_idx = i;
                    }
                }
            }

            if (furthest_idx > start_idx) {
                //add this point
                if (!floater->coincides_with_epsilon(result_pts[segment_idx + 1]) &&
                    (segment_idx + 2 >= result_pts.size() ||
                     !floater->coincides_with_epsilon(result_pts[segment_idx + 2]))) {
                    result_idx.insert(result_idx.begin() + segment_idx + 1, furthest_idx);
                    result_pts.insert(result_pts.begin() + segment_idx + 1, pts[furthest_idx]);
                    //and retry to simplify it
                    segment_idx--;
                }
            }
        }
    }
    for(int i=1;i<result_pts.size();++i)
        assert(!result_pts[i - 1].coincides_with_epsilon(result_pts[i]));
    return result_pts;
}
#ifdef _DEBUGINFO
// to create a cpp multipoint to create test units.
std::string MultiPoint::to_debug_string()
{
    if (points.empty()) {
        return "{}";
    }
    std::string ret;
    for (Point pt : points) {
        ret += std::string(",Point{") + std::to_string(pt.x()) + std::string(",") + std::to_string(pt.y()) +
            std::string("}");
    }
    assert(!ret.empty());
    ret[0] = '{';
    ret += std::string("}");
    return ret;
}
#endif

void MultiPoint3::translate(double x, double y)
{
    for (Vec3crd &p : points) {
        p(0) += coord_t(x);
        p(1) += coord_t(y);
    }
}

void MultiPoint3::translate(const Point& vector)
{
    this->translate(vector(0), vector(1));
}

BoundingBox3 MultiPoint3::bounding_box() const
{
    return BoundingBox3(points);
}

bool MultiPoint3::remove_duplicate_points()
{
    size_t j = 0;
    for (size_t i = 1; i < points.size(); ++i) {
        if (points[j] == points[i]) {
            // Just increase index i.
        } else {
            ++ j;
            if (j < i)
                points[j] = points[i];
        }
    }

    if (++j < points.size())
    {
        points.erase(points.begin() + j, points.end());
        return true;
    }

    return false;
}

BoundingBox get_extents(const MultiPoint &mp)
{ 
    return BoundingBox(mp.points);
}

BoundingBox get_extents_rotated(const Points &points, double angle)
{ 
    BoundingBox bbox;
    if (! points.empty()) {
        double s = sin(angle);
        double c = cos(angle);
        Points::const_iterator it = points.begin();
        double cur_x = (double)(*it)(0);
        double cur_y = (double)(*it)(1);
        bbox.min(0) = bbox.max(0) = (coord_t)round(c * cur_x - s * cur_y);
        bbox.min(1) = bbox.max(1) = (coord_t)round(c * cur_y + s * cur_x);
        for (++it; it != points.end(); ++it) {
            double cur_x = (double)(*it)(0);
            double cur_y = (double)(*it)(1);
            coord_t x = (coord_t)round(c * cur_x - s * cur_y);
            coord_t y = (coord_t)round(c * cur_y + s * cur_x);
            bbox.min(0) = std::min(x, bbox.min(0));
            bbox.min(1) = std::min(y, bbox.min(1));
            bbox.max(0) = std::max(x, bbox.max(0));
            bbox.max(1) = std::max(y, bbox.max(1));
        }
        bbox.defined = true;
    }
    return bbox;
}

BoundingBox get_extents_rotated(const MultiPoint &mp, double angle)
{
    return get_extents_rotated(mp.points, angle);
}

}
