///|/ Copyright (c) Prusa Research 2016 - 2023 Tomáš Mészáros @tamasmeszaros, Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Matěna @lukasmatena, Lukáš Hejl @hejllukas, Filip Sykala @Jony01
///|/ Copyright (c) Slic3r 2013 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2015 Maksim Derbasov @ntfshard
///|/
///|/ ported from lib/Slic3r/Geometry/Clipper.pm:
///|/ Copyright (c) Prusa Research 2016 - 2022 Vojtěch Bubník @bubnikv
///|/ Copyright (c) Slic3r 2011 - 2014 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2012 - 2013 Mike Sheldrake @mesheldrake
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "ShortestPath.hpp"
#include "Utils.hpp"

// #define CLIPPER_UTILS_TIMING

#ifdef CLIPPER_UTILS_TIMING
    // time limit for one ClipperLib operation (union / diff / offset), in ms
    #define CLIPPER_UTILS_TIME_LIMIT_DEFAULT 50
    #include <boost/current_function.hpp>
    #include "Timer.hpp"
    #define CLIPPER_UTILS_TIME_LIMIT_SECONDS(limit) Timing::TimeLimitAlarm time_limit_alarm(uint64_t(limit) * 1000000000l, BOOST_CURRENT_FUNCTION)
    #define CLIPPER_UTILS_TIME_LIMIT_MILLIS(limit) Timing::TimeLimitAlarm time_limit_alarm(uint64_t(limit) * 1000000l, BOOST_CURRENT_FUNCTION)
#else
    #define CLIPPER_UTILS_TIME_LIMIT_SECONDS(limit) do {} while(false)
    #define CLIPPER_UTILS_TIME_LIMIT_MILLIS(limit) do {} while(false)
#endif // CLIPPER_UTILS_TIMING

// #define CLIPPER_UTILS_DEBUG

#ifdef CLIPPER_UTILS_DEBUG
#include "SVG.hpp"
#endif /* CLIPPER_UTILS_DEBUG */

namespace Slic3r {

#ifdef CLIPPER_UTILS_DEBUG
// For debugging the Clipper library, for providing bug reports to the Clipper author.
bool export_clipper_input_polygons_bin(const char *path, const ClipperLib::Paths &input_subject, const ClipperLib::Paths &input_clip)
{
    FILE *pfile = fopen(path, "wb");
    if (pfile == NULL)
        return false;

    uint32_t sz = uint32_t(input_subject.size());
    fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_subject.size(); ++i) {
        const ClipperLib::Path &path = input_subject[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    sz = uint32_t(input_clip.size());
    ::fwrite(&sz, 1, sizeof(sz), pfile);
    for (size_t i = 0; i < input_clip.size(); ++i) {
        const ClipperLib::Path &path = input_clip[i];
        sz = uint32_t(path.size());
        ::fwrite(&sz, 1, sizeof(sz), pfile);
        ::fwrite(path.data(), sizeof(ClipperLib::IntPoint), sz, pfile);
    }
    ::fclose(pfile);
    return true;

err:
    ::fclose(pfile);
    return false;
}
#endif /* CLIPPER_UTILS_DEBUG */

namespace ClipperUtils {
    Points EmptyPathsProvider::s_empty_points;
    Points SinglePathProvider::s_end;

    // Clip source polygon to be used as a clipping polygon with a bouding box around the source (to be clipped) polygon.
    // Useful as an optimization for expensive ClipperLib operations, for example when clipping source polygons one by one
    // with a set of polygons covering the whole layer below.
    template<typename PointsType>
    inline void clip_clipper_polyline_with_subject_bbox_templ(const PointsType &src, const BoundingBox &bbox, PointsType &out)
    {
        using PointType = typename PointsType::value_type;

        out.clear();
        const size_t cnt = src.size();
        if (cnt < 3)
            return;

        enum class Side {
            Left   = 1,
            Right  = 2,
            Top    = 4,
            Bottom = 8
        };

        auto sides = [bbox](const PointType &p) {
            return  int(p.x() < bbox.min.x()) * int(Side::Left) +
                    int(p.x() > bbox.max.x()) * int(Side::Right) +
                    int(p.y() < bbox.min.y()) * int(Side::Bottom) +
                    int(p.y() > bbox.max.y()) * int(Side::Top);
        };

        int sides_prev = sides(src.back());
        int sides_this = sides(src.front());
        const size_t last = cnt - 1;
        for (size_t i = 0; i < last; ++ i) {
            int sides_next = sides(src[i + 1]);
            if (// This point is inside. Take it.
                sides_this == 0 ||
                // Either this point is outside and previous or next is inside, or
                // the edge possibly cuts corner of the bounding box.
                (sides_prev & sides_this & sides_next) == 0) {
                out.emplace_back(src[i]);
                sides_prev = sides_this;
            } else {
                // All the three points (this, prev, next) are outside at the same side.
                // Ignore this point.
            }
            sides_this = sides_next;
        }

        // Never produce just a single point output polygon.
        if (!out.empty()) {
            if (int sides_next = sides(out.front());
                // The last point is inside. Take it.
                sides_this == 0 ||
                // Either this point is outside and previous or next is inside, or
                // the edge possibly cuts corner of the bounding box.
                (sides_prev & sides_this & sides_next) == 0) {
                out.emplace_back(src.back());
            }
        }
        // hack bugfix https://github.com/prusa3d/PrusaSlicer/issues/13356
        if(out.size() < 3)
            out.clear();
        assert(out.size() > 2 || out.empty());
    }

    void clip_clipper_polyline_with_subject_bbox(const Points &src, const BoundingBox &bbox, Points &out)
        { clip_clipper_polyline_with_subject_bbox_templ(src, bbox, out); }
    void clip_clipper_polyline_with_subject_bbox(const ZPoints &src, const BoundingBox &bbox, ZPoints &out)
        { clip_clipper_polyline_with_subject_bbox_templ(src, bbox, out); }

    template<typename PointsType>
    [[nodiscard]] PointsType clip_clipper_polyline_with_subject_bbox_templ(const PointsType &src, const BoundingBox &bbox)
    {
        PointsType out;
        clip_clipper_polyline_with_subject_bbox(src, bbox, out);
        return out;
    }

    [[nodiscard]] Points clip_clipper_polyline_with_subject_bbox(const Points &src, const BoundingBox &bbox)
        { return clip_clipper_polyline_with_subject_bbox_templ(src, bbox); }
    [[nodiscard]] ZPoints clip_clipper_polyline_with_subject_bbox(const ZPoints &src, const BoundingBox &bbox)
        { return clip_clipper_polyline_with_subject_bbox_templ(src, bbox); }

    void clip_clipper_polygon_with_subject_bbox(const Polygon &src, const BoundingBox &bbox, Polygon &out)
    {
        clip_clipper_polyline_with_subject_bbox_templ(src.points, bbox, out.points);
    }

    [[nodiscard]] Polygon clip_clipper_polygon_with_subject_bbox(const Polygon &src, const BoundingBox &bbox)
    {
        Polygon out;
        clip_clipper_polyline_with_subject_bbox_templ(src.points, bbox, out.points);
        return out;
    }

    [[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const Polygons &src, const BoundingBox &bbox)
    {
        Polygons out;
        out.reserve(src.size());
        for (const Polygon &p : src)
            out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox));
        out.erase(
            std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
            out.end());
        return out;
    }
    [[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygon &src, const BoundingBox &bbox)
    {
        Polygons out;
        out.reserve(src.num_contours());
        out.emplace_back(clip_clipper_polygon_with_subject_bbox(src.contour, bbox));
        for (const Polygon &p : src.holes)
            out.emplace_back(clip_clipper_polygon_with_subject_bbox(p, bbox));
        out.erase(
            std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
            out.end());
        return out;
    }

    [[nodiscard]] Polygons clip_clipper_polygons_with_subject_bbox(const ExPolygons &src, const BoundingBox &bbox)
    {
        Polygons out;
        out.reserve(number_polygons(src));
        for (const ExPolygon &p : src) {
            Polygons temp = clip_clipper_polygons_with_subject_bbox(p, bbox);
            out.insert(out.end(), temp.begin(), temp.end());
        }
        out.erase(std::remove_if(out.begin(), out.end(), [](const Polygon &polygon) { return polygon.empty(); }),
                  out.end());
        return out;
    }

    [[nodiscard]] ExPolygons clip_clipper_expolygons_with_subject_bbox(const ExPolygons &src, const BoundingBox &bbox)
    {
        ExPolygons out;
        Polygons temp;
        out.reserve(number_polygons(src));
        for (const ExPolygon &exp : src) {
            temp.clear();
            temp.emplace_back();
            clip_clipper_polygon_with_subject_bbox(exp.contour, bbox, temp.back());
            if (temp.back().empty()) {
                // contour out of bounds, nothing is left.
                continue;
            } else if (temp.back().size() == exp.contour.size()) {
                //no modification
                out.push_back(exp);
                continue;
            }
            bool need_union = false;
            for (const Polygon &hole : exp.holes) {
                temp.emplace_back();
                clip_clipper_polygon_with_subject_bbox(hole, bbox, temp.back());
                if (temp.back().empty() ) {
                    // hole out of bounds, nothing is left.
                    temp.pop_back();
                } else if (temp.back().size() == exp.contour.size()) {
                    //no modification
                } else {
                    need_union = true;
                }
            }
            if (need_union) {
                append(out, union_ex(temp));
            } else {
                out.emplace_back();
                out.back().contour = temp.front();
                if (temp.size() > 1) {
                    out.back().holes.assign(temp.begin() + 1, temp.end());
                }
            }
        }
        return out;
    }

}


static ExPolygons PolyTreeToExPolygons(ClipperLib::PolyTree &&polytree)
{
    struct Inner {
        static void PolyTreeToExPolygonsRecursive(ClipperLib::PolyNode &&polynode, ExPolygons *expolygons)
        {  
            size_t cnt = expolygons->size();
            expolygons->resize(cnt + 1);
            (*expolygons)[cnt].contour.points = std::move(polynode.Contour);
            double area = std::abs((*expolygons)[cnt].contour.area());
            // I saw a clockwise artifact with 4 points
            if ((*expolygons)[cnt].contour.size() < 5 && !(*expolygons)[cnt].contour.is_counter_clockwise()) {
                assert( std::abs((*expolygons)[cnt].contour.area()) < SCALED_EPSILON * SCALED_EPSILON * SCALED_EPSILON);
                // error, delete.
                (*expolygons).pop_back();
                return;
            }
            assert((*expolygons)[cnt].contour.is_counter_clockwise());
            (*expolygons)[cnt].holes.resize(polynode.ChildCount());
            for (int i = 0; i < polynode.ChildCount(); ++ i) {
                (*expolygons)[cnt].holes[i].points = std::move(polynode.Childs[i]->Contour);
                assert((*expolygons)[cnt].holes[i].is_clockwise());
                // Add outer polygons contained by (nested within) holes.
                for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++ j)
                    PolyTreeToExPolygonsRecursive(std::move(*polynode.Childs[i]->Childs[j]), expolygons);
            }
        }

        static size_t PolyTreeCountExPolygons(const ClipperLib::PolyNode &polynode)
        {
            size_t cnt = 1;
            for (int i = 0; i < polynode.ChildCount(); ++ i) {
                for (int j = 0; j < polynode.Childs[i]->ChildCount(); ++ j)
                cnt += PolyTreeCountExPolygons(*polynode.Childs[i]->Childs[j]);
            }
            return cnt;
        }
    };

    ExPolygons retval;
    size_t cnt = 0;
    for (int i = 0; i < polytree.ChildCount(); ++ i)
        cnt += Inner::PolyTreeCountExPolygons(*polytree.Childs[i]);
    retval.reserve(cnt);
    for (int i = 0; i < polytree.ChildCount(); ++ i)
        Inner::PolyTreeToExPolygonsRecursive(std::move(*polytree.Childs[i]), &retval);
    return retval;
}

Polylines PolyTreeToPolylines(ClipperLib::PolyTree &&polytree)
{
    struct Inner {
        static void AddPolyNodeToPaths(ClipperLib::PolyNode &polynode, Polylines &out)
        {
            if (! polynode.Contour.empty())
                out.emplace_back(std::move(polynode.Contour));
            for (ClipperLib::PolyNode *child : polynode.Childs)
                AddPolyNodeToPaths(*child, out);
        }
    };

    Polylines out;
    out.reserve(polytree.Total());
    Inner::AddPolyNodeToPaths(polytree, out);
    return out;
}

#if 0
// Global test.
bool has_duplicate_points(const ClipperLib::PolyTree &polytree)
{
    struct Helper {
        static void collect_points_recursive(const ClipperLib::PolyNode &polynode, ClipperLib::Path &out) {
            // For each hole of the current expolygon:
            out.insert(out.end(), polynode.Contour.begin(), polynode.Contour.end());
            for (int i = 0; i < polynode.ChildCount(); ++ i)
                collect_points_recursive(*polynode.Childs[i], out);
        }
    };
    ClipperLib::Path pts;
    for (int i = 0; i < polytree.ChildCount(); ++ i)
        Helper::collect_points_recursive(*polytree.Childs[i], pts);
    return has_duplicate_points(std::move(pts));
}
#else
// Local test inside each of the contours.
bool has_duplicate_points(const ClipperLib::PolyTree &polytree)
{
    struct Helper {
        static bool has_duplicate_points_recursive(const ClipperLib::PolyNode &polynode) {
            if (has_duplicate_points(polynode.Contour))
                return true;
            for (int i = 0; i < polynode.ChildCount(); ++ i)
                if (has_duplicate_points_recursive(*polynode.Childs[i]))
                    return true;
            return false;
        }
    };
    ClipperLib::Path pts;
    for (int i = 0; i < polytree.ChildCount(); ++ i)
        if (Helper::has_duplicate_points_recursive(*polytree.Childs[i]))
            return true;
    return false;
}
#endif

// Offset CCW contours outside, CW contours (holes) inside.
// Don't calculate union of the output paths.
template<typename PathsProvider>
static ClipperLib::Paths raw_offset(PathsProvider &&paths, double offset, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType endType = ClipperLib::etClosedPolygon)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::ClipperOffset co;
    ClipperLib::Paths out;
    out.reserve(paths.size());
    ClipperLib::Paths out_this;
    if (joinType == jtRound)
        co.ArcTolerance = miterLimit;
    else
        co.MiterLimit = miterLimit;
    co.ShortestEdgeLength = std::abs(offset * ClipperOffsetShortestEdgeFactor);
    for (const ClipperLib::Path &path : paths) {
        co.Clear();
        // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
        // contours will be CCW oriented even though the input paths are CW oriented.
        // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
        co.AddPath(path, joinType, endType);
        bool ccw = endType == ClipperLib::etClosedPolygon ? ClipperLib::Orientation(path) : true;
        co.Execute(out_this, ccw ? offset : - offset);
        if (! ccw) {
            // Reverse the resulting contours.
            for (ClipperLib::Path &path : out_this)
                std::reverse(path.begin(), path.end());
        }
        append(out, std::move(out_this));
    }
    return out;
}

// Offset outside by 10um, one by one.
template<typename PathsProvider>
static ClipperLib::Paths safety_offset(PathsProvider &&paths)
{
    return raw_offset(std::forward<PathsProvider>(paths), ClipperSafetyOffset, DefaultJoinType, DefaultMiterLimit);
}

template<class TResult, class TSubj, class TClip>
TResult clipper_do(
    const ClipperLib::ClipType     clipType,
    TSubj &&                       subject,
    TClip &&                       clip,
    const ClipperLib::PolyFillType fillType)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Clipper clipper;
    clipper.AddPaths(std::forward<TSubj>(subject), ClipperLib::ptSubject, true);
    clipper.AddPaths(std::forward<TClip>(clip),    ClipperLib::ptClip,    true);
    TResult retval;
    clipper.Execute(clipType, retval, fillType, fillType);
    return retval;
}

template<class TResult, class TSubj, class TClip>
TResult clipper_do(
    const ClipperLib::ClipType     clipType,
    TSubj &&                       subject,
    TClip &&                       clip,
    const ClipperLib::PolyFillType fillType,
    const ApplySafetyOffset        do_safety_offset)
{
    // Safety offset only allowed on intersection and difference.
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipperLib::ctUnion);
    return do_safety_offset == ApplySafetyOffset::Yes ? 
        clipper_do<TResult>(clipType, std::forward<TSubj>(subject), safety_offset(std::forward<TClip>(clip)), fillType) :
        clipper_do<TResult>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip), fillType);
}

template<class TResult, class TSubj>
TResult clipper_union(
    TSubj &&                       subject,
    // fillType pftNonZero and pftPositive "should" produce the same result for "normalized with implicit union" set of polygons
    const ClipperLib::PolyFillType fillType = ClipperLib::pftNonZero)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Clipper clipper;
    clipper.AddPaths(std::forward<TSubj>(subject), ClipperLib::ptSubject, true);
    TResult retval;
    clipper.Execute(ClipperLib::ctUnion, retval, fillType, fillType);
    return retval;
}

// Perform union of input polygons using the positive rule, convert to ExPolygons.
//FIXME is there any benefit of not doing the boolean / using pftEvenOdd?
inline ExPolygons ClipperPaths_to_Slic3rExPolygons(const ClipperLib::Paths &input, bool do_union)
{
    return PolyTreeToExPolygons(clipper_union<ClipperLib::PolyTree>(input, do_union ? ClipperLib::pftNonZero : ClipperLib::pftEvenOdd));
}

template<typename PathsProvider>
static ClipperLib::Paths raw_offset_polyline(PathsProvider &&paths, double offset, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType end_type = ClipperLib::etOpenButt)
{
    assert(offset > 0);
    return raw_offset<PathsProvider>(std::forward<PathsProvider>(paths), offset, joinType, miterLimit, end_type);
}

template<class TResult, typename PathsProvider>
static TResult expand_paths(PathsProvider &&paths, double offset, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(offset > 0);
    return clipper_union<TResult>(raw_offset(std::forward<PathsProvider>(paths), offset, joinType, miterLimit));
}

// used by shrink_paths()
template<class Container> static void remove_outermost_polygon(Container & solution);
template<> void remove_outermost_polygon<ClipperLib::Paths>(ClipperLib::Paths &solution)
    { if (! solution.empty()) solution.erase(solution.begin()); }
template<> void remove_outermost_polygon<ClipperLib::PolyTree>(ClipperLib::PolyTree &solution)
    { solution.RemoveOutermostPolygon(); }

template<class TResult, typename PathsProvider>
static TResult shrink_paths(PathsProvider &&paths, double offset, ClipperLib::JoinType joinType, double miterLimit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    assert(offset > 0);
    TResult out;
    if (auto raw = raw_offset(std::forward<PathsProvider>(paths), - offset, joinType, miterLimit); ! raw.empty()) {
        ClipperLib::Clipper clipper;
        clipper.AddPaths(raw, ClipperLib::ptSubject, true);
        ClipperLib::IntRect r = clipper.GetBounds();
        clipper.AddPath({ { r.left - 10, r.bottom + 10 }, { r.right + 10, r.bottom + 10 }, { r.right + 10, r.top - 10 }, { r.left - 10, r.top - 10 } }, ClipperLib::ptSubject, true);
        clipper.ReverseSolution(true);
        clipper.Execute(ClipperLib::ctUnion, out, ClipperLib::pftNegative, ClipperLib::pftNegative);
        remove_outermost_polygon(out);
    }
    return out;
}

template<class TResult, typename PathsProvider>
static TResult offset_paths(PathsProvider &&paths, double offset, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(offset != 0);
    return offset > 0 ?
        expand_paths<TResult>(std::forward<PathsProvider>(paths),   offset, joinType, miterLimit) :
        shrink_paths<TResult>(std::forward<PathsProvider>(paths), - offset, joinType, miterLimit);
}

Slic3r::Polygons offset(const Slic3r::Polygon &polygon, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(raw_offset(ClipperUtils::SinglePathProvider(polygon.points), delta, joinType, miterLimit)); }

Slic3r::Polygons offset(const Slic3r::Polygons &polygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(offset_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit)); }
Slic3r::ExPolygons offset_ex(const Slic3r::Polygons &polygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return PolyTreeToExPolygons(offset_paths<ClipperLib::PolyTree>(ClipperUtils::PolygonsProvider(polygons), delta, joinType, miterLimit)); }

Slic3r::Polygons offset(const Slic3r::Polyline &polyline, const double delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType end_type)
    { assert(delta > 0); return to_polygons(clipper_union<ClipperLib::Paths>(raw_offset_polyline(ClipperUtils::SinglePathProvider(polyline.points), delta, joinType, miterLimit, end_type))); }
Slic3r::Polygons offset(const Slic3r::Polylines &polylines, const double delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::EndType end_type)
    { assert(delta > 0); return to_polygons(clipper_union<ClipperLib::Paths>(raw_offset_polyline(ClipperUtils::PolylinesProvider(polylines), delta, joinType, miterLimit, end_type))); }

Polygons contour_to_polygons(const Polygon &polygon, const float line_width, ClipperLib::JoinType join_type, double miter_limit){
    assert(line_width > 1.f); return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset(ClipperUtils::SinglePathProvider(polygon.points), line_width/2, join_type, miter_limit, ClipperLib::etClosedLine)));}
Polygons contour_to_polygons(const Polygons &polygons, const float line_width, ClipperLib::JoinType join_type, double miter_limit){
    assert(line_width > 1.f); return to_polygons(clipper_union<ClipperLib::Paths>(
        raw_offset(ClipperUtils::PolygonsProvider(polygons), line_width/2, join_type, miter_limit, ClipperLib::etClosedLine)));}

// returns number of expolygons collected (0 or 1).
static int offset_expolygon_inner(const Slic3r::ExPolygon &expoly, const double delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths &out)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // 1) Offset the outer contour.
    ClipperLib::Paths contours;
    {
        ClipperLib::ClipperOffset co;
        if (joinType == jtRound)
            co.ArcTolerance = miterLimit;
        else
            co.MiterLimit = miterLimit;
        co.ShortestEdgeLength = std::abs(delta * ClipperOffsetShortestEdgeFactor);
        co.AddPath(expoly.contour.points, joinType, ClipperLib::etClosedPolygon);
        co.Execute(contours, delta);
    }
    if (contours.empty())
        // No need to try to offset the holes.
        return 0;

    if (expoly.holes.empty()) {
        // No need to subtract holes from the offsetted expolygon, we are done.
        append(out, std::move(contours));
    } else {
        // 2) Offset the holes one by one, collect the offsetted holes.
        ClipperLib::Paths holes;
        {
            for (const Polygon &hole : expoly.holes) {
                ClipperLib::ClipperOffset co;
                if (joinType == jtRound)
                    co.ArcTolerance = miterLimit;
                else
                    co.MiterLimit = miterLimit;
                co.ShortestEdgeLength = std::abs(delta * ClipperOffsetShortestEdgeFactor);
                co.AddPath(hole.points, joinType, ClipperLib::etClosedPolygon);
                ClipperLib::Paths out2;
                // Execute reorients the contours so that the outer most contour has a positive area. Thus the output
                // contours will be CCW oriented even though the input paths are CW oriented.
                // Offset is applied after contour reorientation, thus the signum of the offset value is reversed.
                co.Execute(out2, - delta);
                append(holes, std::move(out2));
            }
        }

        // 3) Subtract holes from the contours.
        if (holes.empty()) {
            // No hole remaining after an offset. Just copy the outer contour.
            append(out, std::move(contours));
        } else if (delta < 0) {
            // Negative offset. There is a chance, that the offsetted hole intersects the outer contour. 
            // Subtract the offsetted holes from the offsetted contours.            
            if (auto output = clipper_do<ClipperLib::Paths>(ClipperLib::ctDifference, contours, holes, ClipperLib::pftNonZero); ! output.empty()) {
                append(out, std::move(output));
            } else {
                // The offsetted holes have eaten up the offsetted outer contour.
                return 0;
            }
        } else {
            // Positive offset. As long as the Clipper offset does what one expects it to do, the offsetted hole will have a smaller
            // area than the original hole or even disappear, therefore there will be no new intersections.
            // Just collect the reversed holes.
            out.reserve(contours.size() + holes.size());
            append(out, std::move(contours));
            // Reverse the holes in place.
            for (size_t i = 0; i < holes.size(); ++ i)
                std::reverse(holes[i].begin(), holes[i].end());
            append(out, std::move(holes));
        }
    }

    return 1;
}

static int offset_expolygon_inner(const Slic3r::Surface &surface, const double delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths &out)
    { return offset_expolygon_inner(surface.expolygon, delta, joinType, miterLimit, out); }
static int offset_expolygon_inner(const Slic3r::Surface *surface, const double delta, ClipperLib::JoinType joinType, double miterLimit, ClipperLib::Paths &out)
    { return offset_expolygon_inner(surface->expolygon, delta, joinType, miterLimit, out); }

ClipperLib::Paths expolygon_offset(const Slic3r::ExPolygon &expolygon, const double delta, ClipperLib::JoinType joinType, double miterLimit)
{
    ClipperLib::Paths out;
    offset_expolygon_inner(expolygon, delta, joinType, miterLimit, out);
    return out;
}

// This is a safe variant of the polygons offset, tailored for multiple ExPolygons.
// It is required, that the input expolygons do not overlap and that the holes of each ExPolygon don't intersect with their respective outer contours.
// Each ExPolygon is offsetted separately. For outer offset, the the offsetted ExPolygons shall be united outside of this function.
template<typename ExPolygonVector>
static std::pair<ClipperLib::Paths, size_t> expolygons_offset_raw(const ExPolygonVector &expolygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
{
    // Offsetted ExPolygons before they are united.
    ClipperLib::Paths output;
    output.reserve(expolygons.size());
    // How many non-empty offsetted expolygons were actually collected into output?
    // If only one, then there is no need to do a final union.
    size_t expolygons_collected = 0;
    for (const auto &expoly : expolygons)
        expolygons_collected += offset_expolygon_inner(expoly, delta, joinType, miterLimit, output);
    return std::make_pair(std::move(output), expolygons_collected);
}

// See comment on expolygon_offsets_raw. In addition, for positive offset the contours are united.
template<typename ExPolygonVector>
static ClipperLib::Paths expolygons_offset(const ExPolygonVector &expolygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // Unite the offsetted expolygons.
    return expolygons_collected > 1 && delta > 0 ?
        // There is a chance that the outwards offsetted expolygons may intersect. Perform a union.
        clipper_union<ClipperLib::Paths>(output) :
        // Negative offset. The shrunk expolygons shall not mutually intersect. Just copy the output.
        output;
}

// See comment on expolygons_offset_raw. In addition, the polygons are always united to conver to polytree.
template<typename ExPolygonVector>
static ClipperLib::PolyTree expolygons_offset_pt(const ExPolygonVector &expolygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
{
    auto [output, expolygons_collected] = expolygons_offset_raw(expolygons, delta, joinType, miterLimit);
    // Unite the offsetted expolygons for both the 
    return clipper_union<ClipperLib::PolyTree>(output);
}

Slic3r::Polygons offset(const Slic3r::ExPolygon &expolygon, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(expolygon_offset(expolygon, delta, joinType, miterLimit)); }
Slic3r::Polygons offset(const Slic3r::ExPolygons &expolygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(expolygons_offset(expolygons, delta, joinType, miterLimit)); }
Slic3r::Polygons offset(const Slic3r::Surfaces &surfaces, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(expolygons_offset(surfaces, delta, joinType, miterLimit)); }
Slic3r::Polygons offset(const Slic3r::SurfacesPtr &surfaces, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return to_polygons(expolygons_offset(surfaces, delta, joinType, miterLimit)); }
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygon &expolygon, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    //FIXME one may spare one Clipper Union call.
    { return ClipperPaths_to_Slic3rExPolygons(expolygon_offset(expolygon, delta, joinType, miterLimit), /* do union */ false); }
Slic3r::ExPolygons offset_ex(const Slic3r::ExPolygons &expolygons, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return PolyTreeToExPolygons(expolygons_offset_pt(expolygons, delta, joinType, miterLimit)); }
Slic3r::ExPolygons offset_ex(const Slic3r::Surfaces &surfaces, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return PolyTreeToExPolygons(expolygons_offset_pt(surfaces, delta, joinType, miterLimit)); }
Slic3r::ExPolygons offset_ex(const Slic3r::SurfacesPtr &surfaces, const double delta, ClipperLib::JoinType joinType, double miterLimit)
    { return PolyTreeToExPolygons(expolygons_offset_pt(surfaces, delta, joinType, miterLimit)); }

Polygons offset2(const ExPolygons &expolygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    return to_polygons(offset_paths<ClipperLib::Paths>(expolygons_offset(expolygons, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
ExPolygons offset2_ex(const ExPolygons &expolygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    return PolyTreeToExPolygons(offset_paths<ClipperLib::PolyTree>(expolygons_offset(expolygons, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
ExPolygons offset2_ex(const Surfaces &surfaces, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    //FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return PolyTreeToExPolygons(offset_paths<ClipperLib::PolyTree>(expolygons_offset(surfaces, delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}

// Offset outside, then inside produces morphological closing. All deltas should be positive.
Slic3r::Polygons closing(const Slic3r::Polygons &polygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(shrink_paths<ClipperLib::Paths>(expand_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
Slic3r::ExPolygons closing_ex(const Slic3r::Polygons &polygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return PolyTreeToExPolygons(shrink_paths<ClipperLib::PolyTree>(expand_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
Slic3r::ExPolygons closing_ex(const Slic3r::Surfaces &surfaces, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    //FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return PolyTreeToExPolygons(shrink_paths<ClipperLib::PolyTree>(expand_paths<ClipperLib::Paths>(ClipperUtils::SurfacesProvider(surfaces), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}

// Offset inside, then outside produces morphological opening. All deltas should be positive.
Slic3r::Polygons opening(const Slic3r::Polygons &polygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
Slic3r::Polygons opening(const Slic3r::ExPolygons &expolygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::ExPolygonsProvider(expolygons), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
Slic3r::Polygons opening(const Slic3r::Surfaces &surfaces, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    //FIXME it may be more efficient to offset to_expolygons(surfaces) instead of to_polygons(surfaces).
    return to_polygons(expand_paths<ClipperLib::Paths>(shrink_paths<ClipperLib::Paths>(ClipperUtils::SurfacesProvider(surfaces), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}
Slic3r::ExPolygons opening_ex(const Slic3r::Polygons& polygons, const double delta1, const double delta2, ClipperLib::JoinType joinType, double miterLimit)
{
    assert(delta1 > 0);
    assert(delta2 > 0);
    return PolyTreeToExPolygons(expand_paths<ClipperLib::PolyTree>(shrink_paths<ClipperLib::Paths>(ClipperUtils::PolygonsProvider(polygons), delta1, joinType, miterLimit), delta2, joinType, miterLimit));
}

bool test_path(const ClipperLib::Path &path) {

    double area = ClipperLib::Area(path);
    // get highest dist, but as it's n² in complexity, i use 2*dist to center wich is 2n in complexity
    ClipperLib::cInt max_dist_sqr= 0;
    // Centroid need the signed area.
    ClipperLib::IntPoint centroid = ClipperLib::Centroid(path, area);
    area = std::abs(area);
    for (const ClipperLib::IntPoint& pt : path) {
        // &0x3FFFFFFF to let  (dx * dx + dy * dy) be storable into a int64
        ClipperLib::cInt dx = std::abs(pt.x() - centroid.x()) & 0x3FFFFFFF;
        ClipperLib::cInt dy = std::abs(pt.y() - centroid.y()) & 0x3FFFFFFF;
        ClipperLib::cInt dist_sqr = (dx * dx + dy * dy);
        // store a bit more than the max (up to x2), but good enough as it avoid a branch.
        max_dist_sqr = max_dist_sqr | dist_sqr; //std::max(max_dist_sqrd, dist_sqrd);
    }
    // max_dist is the "biggest radius"
    // area is < 2*max_dist * 2*max_dist
    // here we create the min area posible: a cube with the radius x 2*Epsilon
    // if it's lower than that, it means that the polygon is too small or too narrow.
    return (area < (2 * SCALED_EPSILON) * std::sqrt(max_dist_sqr));
}

void remove_small_areas(ClipperLib::Paths& paths) {
    for (int idx_path = 0; idx_path < paths.size(); ++idx_path) {
        if (test_path(paths[idx_path])) {
            paths.erase(paths.begin() + idx_path);
            --idx_path;
        }
    }
}

void remove_small_areas(ClipperLib::PolyTree& tree) {
    for (int idx_poly = 0; idx_poly < tree.ChildCount(); ++idx_poly) {
        ClipperLib::PolyNode* ex_polygon = tree.Childs[idx_poly];
        if (test_path(ex_polygon->Contour)) {
            tree.Childs.erase(tree.Childs.begin() + idx_poly);
            --idx_poly;
        } else {
            for (int i = 0; i < ex_polygon->ChildCount(); ++i)
            {
                if (test_path(ex_polygon->Childs[i]->Contour)) {
                    ex_polygon->Childs.erase(ex_polygon->Childs.begin() + i);
                    --i;
                }
            }
        }
    }
}

// Fix of #117: A large fractal pyramid takes ages to slice
// The Clipper library has difficulties processing overlapping polygons.
// Namely, the function ClipperLib::JoinCommonEdges() has potentially a terrible time complexity if the output
// of the operation is of the PolyTree type.
// This function implemenets a following workaround:
// 1) Peform the Clipper operation with the output to Paths. This method handles overlaps in a reasonable time.
// 2) Run Clipper Union once again to extract the PolyTree from the result of 1).
template<typename PathProvider1, typename PathProvider2>
inline ClipperLib::PolyTree clipper_do_polytree(
    const ClipperLib::ClipType       clipType,
    PathProvider1                  &&subject,
    PathProvider2                  &&clip,
    const ClipperLib::PolyFillType   fillType)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // Perform the operation with the output to input_subject.
    // This pass does not generate a PolyTree, which is a very expensive operation with the current Clipper library
    // if there are overapping edges.
    if (auto output = clipper_do<ClipperLib::Paths>(clipType, subject, clip, fillType); ! output.empty()) {
        // remove too small polygons & holes
        remove_small_areas(output);

        // Perform an additional Union operation to generate the PolyTree ordering.
        return clipper_union<ClipperLib::PolyTree>(output, fillType);
    }
    return ClipperLib::PolyTree();
}

template<typename PathProvider1, typename PathProvider2>
inline ClipperLib::PolyTree clipper_do_polytree(
    const ClipperLib::ClipType       clipType,
    PathProvider1                  &&subject,
    PathProvider2                  &&clip,
    const ClipperLib::PolyFillType   fillType,
    const ApplySafetyOffset          do_safety_offset)
{
    assert(do_safety_offset == ApplySafetyOffset::No || clipType != ClipperLib::ctUnion);

    if (do_safety_offset == ApplySafetyOffset::Yes) {
        ClipperLib::PolyTree retval = clipper_do_polytree(clipType, std::forward<PathProvider1>(subject), safety_offset(std::forward<PathProvider2>(clip)), fillType);
        // if safety_offset_, remove too small polygons & holes
        for (int idx_poly = 0; idx_poly < retval.ChildCount(); ++idx_poly) {
            ClipperLib::PolyNode* ex_polygon = retval.Childs[idx_poly];
            if (test_path(ex_polygon->Contour)) {
                retval.Childs.erase(retval.Childs.begin() + idx_poly);
                --idx_poly;
            } else {
                for (int i = 0; i < ex_polygon->ChildCount(); ++i)
                {
                    if (test_path(ex_polygon->Childs[i]->Contour)) {
                        ex_polygon->Childs.erase(ex_polygon->Childs.begin() + i);
                        --i;
                    }
                }
            }
        }
        return retval;
    } else {
        return clipper_do_polytree(clipType, std::forward<PathProvider1>(subject), std::forward<PathProvider2>(clip), fillType);
    }
}

template<class TSubj, class TClip>
static inline Polygons _clipper(ClipperLib::ClipType clipType, TSubj &&subject, TClip &&clip, ApplySafetyOffset do_safety_offset)
{
    return to_polygons(clipper_do<ClipperLib::Paths>(clipType, std::forward<TSubj>(subject), std::forward<TClip>(clip), ClipperLib::pftNonZero, do_safety_offset));
}

Slic3r::Polygons diff(const Slic3r::Polygon &subject, const Slic3r::Polygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::SinglePathProvider(clip.points), do_safety_offset); }
Slic3r::Polygons diff(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons diff_clipped(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset) 
    { return diff(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)), do_safety_offset); }
Slic3r::Polygons diff(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons diff(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons diff(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons diff(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::Polygon &subject, const Slic3r::Polygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::SinglePathProvider(clip.points), do_safety_offset); }
Slic3r::Polygons intersection_clipped(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset) 
    { return intersection(subject, ClipperUtils::clip_clipper_polygons_with_subject_bbox(clip, get_extents(subject).inflated(SCALED_EPSILON)), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::Polygons &subject, const Slic3r::ExPolygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::ExPolygon &subject, const Slic3r::ExPolygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons intersection(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::Polygons union_(const Slic3r::Polygons &subject)
    { return _clipper(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ApplySafetyOffset::No); }
Slic3r::Polygons union_(const Slic3r::Polygons &subject, const ClipperLib::PolyFillType fillType)
    { return to_polygons(clipper_do<ClipperLib::Paths>(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), fillType, ApplySafetyOffset::No)); }
Slic3r::Polygons union_(const Slic3r::ExPolygons &subject)
    { return _clipper(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ApplySafetyOffset::No); }
Slic3r::Polygons union_(const Slic3r::Polygons &subject, const Slic3r::Polygons &subject2)
    { return _clipper(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(subject2), ApplySafetyOffset::No); }
Slic3r::Polygons union_(Slic3r::Polygons &&subject, const Slic3r::Polygons &subject2) { 
    if (subject.empty())
        return subject2;
    if (subject2.empty())
        return std::move(subject);
    return union_(subject, subject2);
}
Slic3r::Polygons union_(const Slic3r::Polygons &subject, const Slic3r::ExPolygon &subject2)
    { return _clipper(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonProvider(subject2), ApplySafetyOffset::No); }

template <typename TSubject, typename TClip>
static ExPolygons _clipper_ex(ClipperLib::ClipType clipType, TSubject &&subject,  TClip &&clip, ApplySafetyOffset do_safety_offset, ClipperLib::PolyFillType fill_type = ClipperLib::pftNonZero)
    { return PolyTreeToExPolygons(clipper_do_polytree(clipType, std::forward<TSubject>(subject), std::forward<TClip>(clip), fill_type, do_safety_offset)); }

Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::SurfacesProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Polygon &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon &subject, const Slic3r::Polygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::SinglePathProvider(clip.points), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygon &subject, const Slic3r::ExPolygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::ExPolygons &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::SurfacesProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesProvider(subject), ClipperUtils::SurfacesProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons diff_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctDifference, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }

Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygon &subject, const Slic3r::ExPolygon &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonProvider(subject), ClipperUtils::ExPolygonProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Polygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::PolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::Surfaces &subject, const Slic3r::Surfaces &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesProvider(subject), ClipperUtils::SurfacesProvider(clip), do_safety_offset); }
Slic3r::ExPolygons intersection_ex(const Slic3r::SurfacesPtr &subject, const Slic3r::ExPolygons &clip, ApplySafetyOffset do_safety_offset)
    { return _clipper_ex(ClipperLib::ctIntersection, ClipperUtils::SurfacesPtrProvider(subject), ClipperUtils::ExPolygonsProvider(clip), do_safety_offset); }
// May be used to "heal" unusual models (3DLabPrints etc.) by providing fill_type (pftEvenOdd, pftNonZero, pftPositive, pftNegative).
Slic3r::ExPolygons union_ex(const Slic3r::Polygons &subject, ClipperLib::PolyFillType fill_type)
    { return _clipper_ex(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ApplySafetyOffset::No, fill_type); }
Slic3r::ExPolygons union_ex(const Slic3r::Polygons &subject, const Slic3r::Polygons &subject2, ClipperLib::PolyFillType fill_type)
    { return _clipper_ex(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(subject2), ApplySafetyOffset::No, fill_type); }
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons &subject)
    { return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ClipperLib::pftNonZero)); }
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons &subject, const Slic3r::ExPolygons &subject2)
    { return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(subject2), ClipperLib::pftNonZero)); }
Slic3r::ExPolygons union_ex(const Slic3r::Polygons &subject, const Slic3r::ExPolygons &subject2)
    { return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::ExPolygonsProvider(subject2), ClipperLib::pftNonZero)); }
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons &subject, const Slic3r::Polygons &subject2)
    { return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::PolygonsProvider(subject2), ClipperLib::pftNonZero)); }
Slic3r::ExPolygons union_ex(const Slic3r::Surfaces &subject)
    { return PolyTreeToExPolygons(clipper_do_polytree(ClipperLib::ctUnion, ClipperUtils::SurfacesProvider(subject), ClipperUtils::EmptyPathsProvider(), ClipperLib::pftNonZero)); }
Slic3r::ExPolygons union_ex(const Slic3r::ExPolygons & expolygons1, const Slic3r::ExPolygons & expolygons2, ApplySafetyOffset do_safety_offset)
{
    ExPolygons poly_union = expolygons1;
    poly_union.insert(poly_union.end(), expolygons2.begin(), expolygons2.end());
    if(ApplySafetyOffset::Yes == do_safety_offset)
        return union_safety_offset_ex(poly_union);
    else
        return union_ex(poly_union);
    //return _clipper_ex(ClipperLib::ctUnion, poly_union, Slic3r::Polygons(), safety_offset_);
    //OR that, i don't know what is the best
    //return _clipper_ex(ClipperLib::ctUnion, to_polygons(subject1), to_polygons(subject2), safety_offset_);
}

#define CLIPPER_OFFSET_POWER_OF_2 17
#define CLIPPER_OFFSET_SCALE (1 << CLIPPER_OFFSET_POWER_OF_2)
#define CLIPPER_OFFSET_SCALE_ROUNDING_DELTA ((1 << (CLIPPER_OFFSET_POWER_OF_2 - 1)) - 1)
void scaleClipperPolygons(ClipperLib::Paths& polygons)
{
    for (ClipperLib::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it)
        for (ClipperLib::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            pit->x() <<= CLIPPER_OFFSET_POWER_OF_2;
            pit->y() <<= CLIPPER_OFFSET_POWER_OF_2;
        }
}
template<typename PathsProvider1, typename PathsProvider2>
Polylines _clipper_pl_open(ClipperLib::ClipType clipType, PathsProvider1 &&subject, PathsProvider2 &&clip)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);
    //sadly, it still has the y-bug, so need to mitigate it.
    //ClipperLib::Clipper clipper;
    //clipper.AddPaths(std::forward<PathsProvider1>(subject), ClipperLib::ptSubject, false);
    //clipper.AddPaths(std::forward<PathsProvider2>(clip), ClipperLib::ptClip, true);
    //ClipperLib::PolyTree retval;
    //clipper.Execute(clipType, retval, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    //return PolyTreeToPolylines(std::move(retval));

    // read input
    ClipperLib::Paths input_subject;
    for (const ClipperLib::Path& pg : subject)
        input_subject.push_back(pg);
    ClipperLib::Paths input_clip;
    for (const ClipperLib::Path& pg : clip)
        input_clip.push_back(pg);

    //scale to have some more precision to do some Y-bugfix
    scaleClipperPolygons(input_subject);
    scaleClipperPolygons(input_clip);

    //perform xy safing : if a line is on the same Y, clipper may not pick the good point.
    for (ClipperLib::Paths* input : { &input_subject, &input_clip }) {
        for (ClipperLib::Path& path : *input) {
            coord_t lastx = 0;
            coord_t lasty = 0;
            for (ClipperLib::IntPoint& pt : path) {
                {
                    //add something from the x() to allow points to be equal even if in different collection
                    ClipperLib::cInt dy = pt.x() & 0xFFFF;
                    dy ^= ((pt.x()>>16) & 0xFFFF);
#ifndef CLIPPERLIB_INT32
                    dy ^= ((pt.x()>>32) & 0xFFFF);
                    dy ^= ((pt.x()>>48) & 0xFFFF);
#endif
                    assert(dy >= 0 && dy <= 0xFFFF);
                    ClipperLib::cInt dx = pt.y() & 0xFFFF;
                    dx ^= ((pt.y()>>16) & 0xFFFF);
#ifndef CLIPPERLIB_INT32
                    dx ^= ((pt.y()>>32) & 0xFFFF);
                    dx ^= ((pt.y()>>48) & 0xFFFF);
#endif
                    assert(dx >= 0 && dx <= 0xFFFF);
                    pt.x() += dx;
                    pt.y() += dy;
                }
                //just to be sure
                if (lastx == pt.x()) {
                    // this can create artifacts, as two identical point aren't identical anymore.
                    // But it's better to have a little point returned instead of a wierd result.
                    // note: it also trigger when x==y, but it's okay
                    pt.x() += 2048;// well below CLIPPER_OFFSET_POWER_OF_2, need also to be high enough that it won't be reduce to 0 if cut near an end
                }
                if (lasty == pt.y()) {
                    pt.y() += 2048;
                }
                lastx = pt.x();
                lasty = pt.y();
            }
        }
    }

    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();

    // add polygons
    clipper.AddPaths(input_subject, ClipperLib::ptSubject, false);
    clipper.AddPaths(input_clip, ClipperLib::ptClip, true);

    // perform operation
    ClipperLib::PolyTree retval;
    clipper.Execute(clipType, retval, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

    //restore good y
    std::vector<ClipperLib::PolyNode*> to_check;
    to_check.push_back(&retval);
    while (!to_check.empty()) {
        ClipperLib::PolyNode* node = to_check.back();
        to_check.pop_back();
        for (ClipperLib::IntPoint& pit : node->Contour) {
            pit.x() += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
            pit.y() += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
            pit.x() >>= CLIPPER_OFFSET_POWER_OF_2;
            pit.y() >>= CLIPPER_OFFSET_POWER_OF_2;
        }
        //note: moving in Y may create 0-length segment, so it needs an extra post-processing step to remove these duplicate points.
        for (size_t idx = 1; idx < node->Contour.size(); ++idx) {
            ClipperLib::IntPoint& pit = node->Contour[idx];
            ClipperLib::IntPoint& previous = node->Contour[idx - 1];
            // unscaling remove too small differences. The equality is enough.
            if (pit.x() == previous.x() && pit.y() == previous.y()) {
                node->Contour.erase(node->Contour.begin() + idx);
                --idx;
            }
        }
        //be sure you don't save 1-point paths
        if (node->Contour.size() == 1)
            node->Contour.clear();
        to_check.insert(to_check.end(), node->Childs.begin(), node->Childs.end());
    }

    return PolyTreeToPolylines(std::move(retval));
}

void scaleClipperPolygons(ClipperLib_Z::Paths& polygons)
{
    for (ClipperLib_Z::Paths::iterator it = polygons.begin(); it != polygons.end(); ++it)
        for (ClipperLib_Z::Path::iterator pit = (*it).begin(); pit != (*it).end(); ++pit) {
            pit->x() <<= CLIPPER_OFFSET_POWER_OF_2;
            pit->y() <<= CLIPPER_OFFSET_POWER_OF_2;
            pit->z() <<= CLIPPER_OFFSET_POWER_OF_2;
        }
}
ClipperLib_Z::Paths clip_extrusion(const ClipperLib_Z::Paths& subjects, const ClipperLib_Z::Paths& clip, ClipperLib_Z::ClipType clipType)
{
    ClipperLib_Z::Clipper clipper;
    clipper.ZFillFunction([](const ClipperLib_Z::IntPoint& e1bot, const ClipperLib_Z::IntPoint& e1top, const ClipperLib_Z::IntPoint& e2bot,
        const ClipperLib_Z::IntPoint& e2top, ClipperLib_Z::IntPoint& pt) {
            // The clipping contour may be simplified by clipping it with a bounding box of "subject" path.
            // The clipping function used may produce self intersections outside of the "subject" bounding box. Such self intersections are 
            // harmless to the result of the clipping operation,
            // Both ends of each edge belong to the same source: Either they are from subject or from clipping path.
            assert(e1bot.z() >= 0 && e1top.z() >= 0);
            assert(e2bot.z() >= 0 && e2top.z() >= 0);
            assert((e1bot.z() == 0) == (e1top.z() == 0));
            assert((e2bot.z() == 0) == (e2top.z() == 0));

            // Start & end points of the clipped polyline (extrusion path with a non-zero width).
            ClipperLib_Z::IntPoint start = e1bot;
            ClipperLib_Z::IntPoint end = e1top;
            if (start.z() <= 0 && end.z() <= 0) {
                start = e2bot;
                end = e2top;
            }

            if (start.z() <= 0 && end.z() <= 0) {
                // Self intersection on the source contour.
                assert(start.z() == 0 && end.z() == 0);
                pt.z() = 0;
            } else {
                // Interpolate extrusion line width.
                assert(start.z() > 0 && end.z() > 0);

                double length_sqr = (end - start).cast<double>().squaredNorm();
                double dist_sqr   = (pt - start).cast<double>().squaredNorm();
                double t          = std::sqrt(dist_sqr / length_sqr);

                pt.z() = start.z() + coord_t((end.z() - start.z()) * t);
            }
        });

    //scale to have some more precision to do some Y-bugfix like in _clipper_pl_open

    // read input
    ClipperLib_Z::Paths input_subject;
    for (const ClipperLib_Z::Path& pg : subjects)
        input_subject.push_back(pg);
    ClipperLib_Z::Paths input_clip;
    for (const ClipperLib_Z::Path& pg : clip)
        input_clip.push_back(pg);

    //scale to have some more precision to do some Y-bugfix
    scaleClipperPolygons(input_subject);
    scaleClipperPolygons(input_clip);
    
    //perform xy safing : if a line is on the same Y, clipper may not pick the good point.
    for (ClipperLib_Z::Paths* input : { &input_subject, &input_clip }) {
        for (ClipperLib_Z::Path& path : *input) {
            coord_t lastx = 0;
            coord_t lasty = 0;
            for (ClipperLib_Z::IntPoint& pt : path) {
                {
                    //add something from the x() to allow points to be equal even if in different collection
                    ClipperLib::cInt dy = pt.x() & 0xFFFF;
                    dy ^= ((pt.x()>>16) & 0xFFFF);
#ifndef CLIPPERLIB_INT32
                    dy ^= ((pt.x()>>32) & 0xFFFF);
                    dy ^= ((pt.x()>>48) & 0xFFFF);
#endif
                    assert(dy >= 0 && dy <= 0xFFFF);
                    ClipperLib::cInt dx = pt.y() & 0xFFFF;
                    dx ^= ((pt.y()>>16) & 0xFFFF);
#ifndef CLIPPERLIB_INT32
                    dx ^= ((pt.y()>>32) & 0xFFFF);
                    dx ^= ((pt.y()>>48) & 0xFFFF);
#endif
                    assert(dx >= 0 && dx <= 0xFFFF);
                    pt.x() += dx;
                    pt.y() += dy;
                }
                //just to be sure
                if (lastx == pt.x()) {
                    // this can create artifacts, as two identical point aren't identical anymore.
                    // But it's better to have a little point returned instead of a wierd result.
                    // note: it also trigger when x==y, but it's okay
                    pt.x() += 2048;// well below CLIPPER_OFFSET_POWER_OF_2, need also to be high enough that it won't be reduce to 0 if cut near an end
                }
                if (lasty == pt.y()) {
                    pt.y() += 2048;
                }
                lastx = pt.x();
                lasty = pt.y();
            }
        }
    }
    ////perform y safing : if a line is on the same Y, clipper may not pick the good point.
    ////note: if not enough, next time, add some of the X coordinate (modulo it so it's contained in the scaling part)
    //for (ClipperLib_Z::Paths* input : { &input_subject, &input_clip }) {
    //    for (ClipperLib_Z::Path& path : *input) {
    //        coord_t lasty = 0;
    //        for (ClipperLib_Z::IntPoint& pt : path) {
    //            if (lasty == pt.y()) {
    //                pt.y() += 2048;// well below CLIPPER_OFFSET_POWER_OF_2, need also to be high enough that it won't be reduce to 0 if cut near an end
    //            }
    //            lasty = pt.y();
    //        }
    //    }
    //}

    // now it's scaled, do the clip
    clipper.AddPaths(input_subject, ClipperLib_Z::ptSubject, false);
    clipper.AddPaths(input_clip, ClipperLib_Z::ptClip, true);

    ClipperLib_Z::Paths    clipped_paths;
    {
        ClipperLib_Z::PolyTree clipped_polytree;
        clipper.Execute(clipType, clipped_polytree, ClipperLib_Z::pftNonZero, ClipperLib_Z::pftNonZero);


        // unscale, while restoring good y
        std::vector<ClipperLib_Z::PolyNode*> to_check;
        to_check.push_back(&clipped_polytree);
        while (!to_check.empty()) {
            ClipperLib_Z::PolyNode* node = to_check.back();
            to_check.pop_back();
            for (ClipperLib_Z::IntPoint& pit : node->Contour) {
                pit.x() += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
                pit.y() += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
                pit.z() += CLIPPER_OFFSET_SCALE_ROUNDING_DELTA;
                pit.x() >>= CLIPPER_OFFSET_POWER_OF_2;
                pit.y() >>= CLIPPER_OFFSET_POWER_OF_2;
                pit.z() >>= CLIPPER_OFFSET_POWER_OF_2;
            }
            //note: moving in Y may create 0-length segment, so it needs an extra post-processing step to remove these duplicate points.
            for (size_t idx = 1; idx < node->Contour.size(); ++idx) {
                ClipperLib_Z::IntPoint& pit = node->Contour[idx];
                ClipperLib_Z::IntPoint& previous = node->Contour[idx - 1];
                // unscaling remove too small differences. The equality is enough.
                if (pit.x() == previous.x() && pit.y() == previous.y()) {
                    node->Contour.erase(node->Contour.begin() + idx);
                    --idx;
                }
            }
            //be sure you don't save 1-point paths
            if (node->Contour.size() == 1)
                node->Contour.clear();
            to_check.insert(to_check.end(), node->Childs.begin(), node->Childs.end());
        }

        //copy back to clipped_paths, to continue
        ClipperLib_Z::PolyTreeToPaths(clipped_polytree, clipped_paths);
    }

    // cleaning
    for (size_t i_path = 0; i_path < clipped_paths.size(); ++i_path) {
        ClipperLib_Z::Path &path = clipped_paths[i_path];
        for (size_t i_pt = 1; i_pt < path.size() - 1; i_pt++) {
            if ((path[i_pt - 1] - path[i_pt]).squaredNorm() < SCALED_EPSILON) {
                path.erase(path.begin() + i_pt);
                i_pt--;
            }
        }
    }

    // Clipped path could contain vertices from the clip with a Z coordinate equal to zero.
    // For those vertices, we must assign value based on the subject.
    // This happens only in sporadic cases.
    for (ClipperLib_Z::Path& path : clipped_paths)
        for (ClipperLib_Z::IntPoint& c_pt : path)
            if (c_pt.z() == 0) {
                const Point pt(c_pt.x(), c_pt.y());
                Point       projected_pt_min;
                const ClipperLib_Z::IntPoint* it_a = nullptr;
                const ClipperLib_Z::IntPoint* it_b = nullptr;
                auto        dist_sqr_min = std::numeric_limits<double>::max();
                Point      prev(0, 0);
                for (const ClipperLib_Z::Path& subject : subjects) {
                    // Now we must find the corresponding line on with this point is located and compute line width (Z coordinate).
                    if (subject.size() <= 2)
                        continue;

                    for (auto it = std::next(subject.begin()); it != subject.end(); ++it) {
                        Point curr(it->x(), it->y());
                        if (it_a == nullptr) {
                            assert(std::prev(it) == subject.begin());
                            prev = Point(subject.front().x(), subject.front().y());
                        }
                        Point projected_pt;
                        if (double dist_sqr = line_alg::distance_to_squared(Line(prev, curr), pt, &projected_pt); dist_sqr < dist_sqr_min) {
                            dist_sqr_min = dist_sqr;
                            projected_pt_min = projected_pt;
                            it_a = &*std::prev(it);
                            it_b = &*it;
                        }
                        prev = curr;
                    }

                    assert(dist_sqr_min <= SCALED_EPSILON);
                    assert(*it_a != subject.back());
                }

                const Point  pt_a(it_a->x(), it_a->y());
                const Point  pt_b(it_b->x(), it_b->y());
                const double line_len = (pt_b - pt_a).cast<double>().norm();
                const double dist = (projected_pt_min - pt_a).cast<double>().norm();
                c_pt.z() = coord_t(double(it_a->z()) + (dist / line_len) * double(it_b->z() - it_a->z()));
            }
    assert([&clipped_paths = std::as_const(clipped_paths)]() -> bool {
        for (const ClipperLib_Z::Path& path : clipped_paths)
            for (const ClipperLib_Z::IntPoint& pt : path)
                if (pt.z() <= 0)
                    return false;
        return true;
    }());

    return clipped_paths;
}


// If the split_at_first_point() call above happens to split the polygon inside the clipping area
// we would get two consecutive polylines instead of a single one, so we go through them in order
// to recombine continuous polylines.
static void _clipper_pl_recombine(Polylines &polylines)
{
    for (size_t i = 0; i < polylines.size(); ++i) {
        for (size_t j = i+1; j < polylines.size(); ++j) {
            if (polylines[i].points.back() == polylines[j].points.front()) {
                /* If last point of i coincides with first point of j,
                   append points of j to i and delete j */
                polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin()+1, polylines[j].points.end());
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.front() == polylines[j].points.back()) {
                /* If first point of i coincides with last point of j,
                   prepend points of j to i and delete j */
                polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(), polylines[j].points.end()-1);
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.front() == polylines[j].points.front()) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when first point of i coincides with first point of j. */
                polylines[j].reverse();
                polylines[i].points.insert(polylines[i].points.begin(), polylines[j].points.begin(), polylines[j].points.end()-1);
                polylines.erase(polylines.begin() + j);
                --j;
            } else if (polylines[i].points.back() == polylines[j].points.back()) {
                /* Since Clipper does not preserve orientation of polylines, 
                   also check the case when last point of i coincides with last point of j. */
                polylines[j].reverse();
                polylines[i].points.insert(polylines[i].points.end(), polylines[j].points.begin()+1, polylines[j].points.end());
                polylines.erase(polylines.begin() + j);
                --j;
            }
        }
    }
}

template<typename PathProvider1, typename PathProvider2>
Polylines _clipper_pl_closed(ClipperLib::ClipType clipType, PathProvider1 &&subject, PathProvider2 &&clip)
{
    // Transform input polygons into open paths.
    ClipperLib::Paths paths;
    paths.reserve(subject.size());
    for (const Points &poly : subject) {
        // Emplace polygon, duplicate the 1st point.
        paths.push_back({});
        ClipperLib::Path &path = paths.back();
        path.reserve(poly.size() + 1);
        path = poly;
        path.emplace_back(poly.front());
    }
    // perform clipping
    Polylines retval = _clipper_pl_open(clipType, paths, std::forward<PathProvider2>(clip));
    _clipper_pl_recombine(retval);
    return retval;
}

Slic3r::Polylines diff_pl(const Slic3r::Polyline &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::PolygonsProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::PolygonsProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polyline &subject, const Slic3r::ExPolygon &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polyline &subject, const Slic3r::Polygon &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::SinglePathProvider(clip.points)); }
Slic3r::Polylines diff_pl(const Slic3r::Polyline &subject, const Slic3r::ExPolygons &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonsProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygon &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygons &clip)
    { return _clipper_pl_open(ClipperLib::ctDifference, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonsProvider(clip)); }
Slic3r::Polylines diff_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_closed(ClipperLib::ctDifference, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polylines &subject, const Slic3r::Polygon &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::SinglePathProvider(clip.points)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polyline &subject, const Slic3r::ExPolygon &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygon &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polyline &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::PolygonsProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polyline &subject, const Slic3r::ExPolygons &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::SinglePathProvider(subject.points), ClipperUtils::ExPolygonsProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polylines &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::PolygonsProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polylines &subject, const Slic3r::ExPolygons &clip)
    { return _clipper_pl_open(ClipperLib::ctIntersection, ClipperUtils::PolylinesProvider(subject), ClipperUtils::ExPolygonsProvider(clip)); }
Slic3r::Polylines intersection_pl(const Slic3r::Polygons &subject, const Slic3r::Polygons &clip)
    { return _clipper_pl_closed(ClipperLib::ctIntersection, ClipperUtils::PolygonsProvider(subject), ClipperUtils::PolygonsProvider(clip)); }

Lines _clipper_ln(ClipperLib::ClipType clipType, const Lines &subject, const Polygons &clip)
{
    // convert Lines to Polylines
    Polylines polylines;
    polylines.reserve(subject.size());
    for (const Line &line : subject)
        polylines.emplace_back(Polyline(line.a, line.b));
    
    // perform operation
    polylines = _clipper_pl_open(clipType, ClipperUtils::PolylinesProvider(polylines), ClipperUtils::PolygonsProvider(clip));
    
    // convert Polylines to Lines
    Lines retval;
    for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline)
        if (polyline->size() >= 2)
            //FIXME It may happen, that Clipper produced a polyline with more than 2 collinear points by clipping a single line with polygons. It is a very rare issue, but it happens, see GH #6933.
            retval.push_back({ polyline->front(), polyline->back() });
    return retval;
}

// Convert polygons / expolygons into ClipperLib::PolyTree using ClipperLib::pftEvenOdd, thus union will NOT be performed.
// If the contours are not intersecting, their orientation shall not be modified by union_pt().
ClipperLib::PolyTree union_pt(const Polygons &subject)
{
    return clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, ClipperUtils::PolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ClipperLib::pftEvenOdd);
}
ClipperLib::PolyTree union_pt(const ExPolygons &subject)
{
    return clipper_do<ClipperLib::PolyTree>(ClipperLib::ctUnion, ClipperUtils::ExPolygonsProvider(subject), ClipperUtils::EmptyPathsProvider(), ClipperLib::pftEvenOdd);
}

// Simple spatial ordering of Polynodes
ClipperLib::PolyNodes order_nodes(const ClipperLib::PolyNodes &nodes)
{
    // collect ordering points
    Points ordering_points;
    ordering_points.reserve(nodes.size());
    
    for (const ClipperLib::PolyNode *node : nodes)
        ordering_points.emplace_back(
            Point(node->Contour.front().x(), node->Contour.front().y()));

    // perform the ordering
    ClipperLib::PolyNodes ordered_nodes =
        chain_clipper_polynodes(ordering_points, nodes);

    return ordered_nodes;
}

static void traverse_pt_noholes(const ClipperLib::PolyNodes &nodes, Polygons *out)
{
    foreach_node<e_ordering::ON>(nodes, [&out](const ClipperLib::PolyNode *node) 
    {
        traverse_pt_noholes(node->Childs, out);
        out->emplace_back(node->Contour);
        if (node->IsHole()) out->back().reverse(); // ccw
    });
}

static void traverse_pt_outside_in(ClipperLib::PolyNodes &&nodes, Polygons *retval)
{
    // collect ordering points
    Points ordering_points;
    ordering_points.reserve(nodes.size());
    for (const ClipperLib::PolyNode *node : nodes)
        ordering_points.emplace_back(node->Contour.front().x(), node->Contour.front().y());

    // Perform the ordering, push results recursively.
    //FIXME pass the last point to chain_clipper_polynodes?
    for (ClipperLib::PolyNode *node : chain_clipper_polynodes(ordering_points, nodes)) {
        retval->emplace_back(std::move(node->Contour));
        if (node->IsHole()) 
            // Orient a hole, which is clockwise oriented, to CCW.
            retval->back().reverse();
        // traverse the next depth
        traverse_pt_outside_in(std::move(node->Childs), retval);
    }
}

Polygons union_pt_chained_outside_in(const Polygons &subject)
{
    Polygons retval;
    traverse_pt_outside_in(union_pt(subject).Childs, &retval);
    return retval;
}

Polygons simplify_polygons(const Polygons &subject)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Paths output;
        ClipperLib::Clipper c;
//    c.PreserveCollinear(true);
    //FIXME StrictlySimple is very expensive! Is it needed?
        c.StrictlySimple(true);
        c.AddPaths(ClipperUtils::PolygonsProvider(subject), ClipperLib::ptSubject, true);
        c.Execute(ClipperLib::ctUnion, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);

    // convert into Slic3r polygons
    return to_polygons(std::move(output));
}

ExPolygons simplify_polygons_ex(const Polygons &subject, bool preserve_collinear)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::PolyTree polytree;
    ClipperLib::Clipper c;
    if (preserve_collinear) c.PreserveCollinear(preserve_collinear);
    //FIXME StrictlySimple is very expensive! Is it needed?
    c.StrictlySimple(true);
    c.AddPaths(ClipperUtils::PolygonsProvider(subject), ClipperLib::ptSubject, true);
    c.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    
    // convert into ExPolygons
    return PolyTreeToExPolygons(std::move(polytree));
}

Polygons top_level_islands(const Slic3r::Polygons &polygons)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    // init Clipper
    ClipperLib::Clipper clipper;
    clipper.Clear();
    // perform union
    clipper.AddPaths(ClipperUtils::PolygonsProvider(polygons), ClipperLib::ptSubject, true);
    ClipperLib::PolyTree polytree;
    clipper.Execute(ClipperLib::ctUnion, polytree, ClipperLib::pftEvenOdd, ClipperLib::pftEvenOdd); 
    // Convert only the top level islands to the output.
    Polygons out;
    out.reserve(polytree.ChildCount());
    for (int i = 0; i < polytree.ChildCount(); ++i)
        out.emplace_back(std::move(polytree.Childs[i]->Contour));
    return out;
}

// Outer offset shall not split the input contour into multiples. It is expected, that the solution will be non empty and it will contain just a single polygon.
ClipperLib::Paths fix_after_outer_offset(
	const ClipperLib::Path 		&input, 
													// combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution, double delta)
													// to produce a CCW output contour from CCW input contour for a positive offset.
	ClipperLib::PolyFillType 	 filltype, 			// = ClipperLib::pftPositive
	bool 						 reverse_result)	// = false
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

  	ClipperLib::Paths solution;
  	if (! input.empty()) {
		ClipperLib::Clipper clipper;
	  	clipper.AddPath(input, ClipperLib::ptSubject, true);
		clipper.ReverseSolution(reverse_result);
		clipper.Execute(ClipperLib::ctUnion, solution, filltype, filltype);
	}
    return solution;
}

// Inner offset may split the source contour into multiple contours, but one resulting contour shall not lie inside the other.
ClipperLib::Paths fix_after_inner_offset(
	const ClipperLib::Path 		&input, 
													// combination of default prameters to correspond to void ClipperOffset::Execute(Paths& solution, double delta)
													// to produce a CCW output contour from CCW input contour for a negative offset.
	ClipperLib::PolyFillType 	 filltype, 			// = ClipperLib::pftNegative
	bool 						 reverse_result) 	// = true
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

  	ClipperLib::Paths solution;
  	if (! input.empty()) {
		ClipperLib::Clipper clipper;
		clipper.AddPath(input, ClipperLib::ptSubject, true);
		ClipperLib::IntRect r = clipper.GetBounds();
		r.left -= 10; r.top -= 10; r.right += 10; r.bottom += 10;
		if (filltype == ClipperLib::pftPositive)
			clipper.AddPath({ ClipperLib::IntPoint(r.left, r.bottom), ClipperLib::IntPoint(r.left, r.top), ClipperLib::IntPoint(r.right, r.top), ClipperLib::IntPoint(r.right, r.bottom) }, ClipperLib::ptSubject, true);
		else
			clipper.AddPath({ ClipperLib::IntPoint(r.left, r.bottom), ClipperLib::IntPoint(r.right, r.bottom), ClipperLib::IntPoint(r.right, r.top), ClipperLib::IntPoint(r.left, r.top) }, ClipperLib::ptSubject, true);
		clipper.ReverseSolution(reverse_result);
		clipper.Execute(ClipperLib::ctUnion, solution, filltype, filltype);
		if (! solution.empty())
			solution.erase(solution.begin());
	}
	return solution;
}

ClipperLib::Path mittered_offset_path_scaled(const Points &contour, const std::vector<float> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

	assert(contour.size() == deltas.size());

#ifndef NDEBUG
	// Verify that the deltas are either all positive, or all negative.
	bool positive = false;
	bool negative = false;
	for (float delta : deltas)
		if (delta < 0.f)
			negative = true;
		else if (delta > 0.f)
			positive = true;
	assert(! (negative && positive));
#endif /* NDEBUG */

	ClipperLib::Path out;

	if (deltas.size() > 2)
	{
		out.reserve(contour.size() * 2);

		// Clamp miter limit to 2.
		miter_limit = (miter_limit > 2.) ? 2. / (miter_limit * miter_limit) : 0.5;
		
		// perpenduclar vector
		auto   perp = [](const Vec2d &v) -> Vec2d { return Vec2d(v.y(), - v.x()); };

		// Add a new point to the output, scale by CLIPPER_OFFSET_SCALE and round to ClipperLib::cInt.
		auto   add_offset_point = [&out](Vec2d pt) {
            pt += Vec2d(0.5 - (pt.x() < 0), 0.5 - (pt.y() < 0));
			out.emplace_back(ClipperLib::cInt(pt.x()), ClipperLib::cInt(pt.y()));
		};

		// Minimum edge length, squared.
		double lmin  = static_cast<double>(*std::max_element(deltas.begin(), deltas.end()) * ClipperOffsetShortestEdgeFactor);
		double l2min = lmin * lmin;
		// Minimum angle to consider two edges to be parallel.
		// Vojtech's estimate.
//		const double sin_min_parallel = EPSILON + 1. / double(CLIPPER_OFFSET_SCALE);
		// Implementation equal to Clipper.
		const double sin_min_parallel = 1.;

		// Find the last point further from pt by l2min.
		Vec2d  pt     = contour.front().cast<double>();
		size_t iprev  = contour.size() - 1;
		Vec2d  ptprev;
		for (; iprev > 0; -- iprev) {
			ptprev = contour[iprev].cast<double>();
			if ((ptprev - pt).squaredNorm() > l2min)
				break;
		}

		if (iprev != 0) {
			size_t ilast = iprev;
			// Normal to the (pt - ptprev) segment.
			Vec2d nprev = perp(pt - ptprev).normalized();
			for (size_t i = 0; ; ) {
				// Find the next point further from pt by l2min.
				size_t j = i + 1;
				Vec2d ptnext;
				for (; j <= ilast; ++ j) {
					ptnext = contour[j].cast<double>();
					double l2 = (ptnext - pt).squaredNorm();
					if (l2 > l2min)
						break;
				}
				if (j > ilast) {
					assert(i <= ilast);
					// If the last edge is too short, merge it with the previous edge.
					i = ilast;
					ptnext = contour.front().cast<double>();
				}

				// Normal to the (ptnext - pt) segment.
				Vec2d nnext  = perp(ptnext - pt).normalized();

				double delta  = deltas[i];
				double sin_a  = std::clamp(cross2(nprev, nnext), -1., 1.);
				double convex = sin_a * delta;
				if (convex <= - sin_min_parallel) {
					// Concave corner.
					add_offset_point(pt + nprev * delta);
					add_offset_point(pt);
					add_offset_point(pt + nnext * delta);
				} else {
					double dot = nprev.dot(nnext);
					if (convex < sin_min_parallel && dot > 0.) {
						// Nearly parallel.
						add_offset_point((nprev.dot(nnext) > 0.) ? (pt + nprev * delta) : pt);
					} else {
						// Convex corner, possibly extremely sharp if convex < sin_min_parallel.
						double r = 1. + dot;
					  	if (r >= miter_limit)
							add_offset_point(pt + (nprev + nnext) * (delta / r));
					  	else {
							double dx = std::tan(std::atan2(sin_a, dot) / 4.);
							Vec2d  newpt1 = pt + (nprev - perp(nprev) * dx) * delta;
							Vec2d  newpt2 = pt + (nnext + perp(nnext) * dx) * delta;
#ifndef NDEBUG
							Vec2d vedge = 0.5 * (newpt1 + newpt2) - pt;
							double dist_norm = vedge.norm();
							assert(std::abs(dist_norm - std::abs(delta)) < SCALED_EPSILON);
#endif /* NDEBUG */
							add_offset_point(newpt1);
							add_offset_point(newpt2);
					  	}
					}
				}

				if (i == ilast)
					break;

				ptprev = pt;
				nprev  = nnext;
				pt     = ptnext;
				i = j;
			}
		}
	}

#if 0
	{
		ClipperLib::Path polytmp(out);
		unscaleClipperPolygon(polytmp);
		Slic3r::Polygon offsetted(std::move(polytmp));
		BoundingBox bbox = get_extents(contour);
		bbox.merge(get_extents(offsetted));
		static int iRun = 0;
		SVG svg(debug_out_path("mittered_offset_path_scaled-%d.svg", iRun ++).c_str(), bbox);
		svg.draw_outline(Polygon(contour), "blue", scale_(0.01));
		svg.draw_outline(offsetted, "red", scale_(0.01));
		svg.draw(contour, "blue", scale_(0.03));
		svg.draw((Points)offsetted, "blue", scale_(0.03));
	}
#endif

	return out;
}

static void variable_offset_inner_raw(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit, ClipperLib::Paths &contours, ClipperLib::Paths &holes)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

#ifndef NDEBUG
	// Verify that the deltas are all non positive.
	for (const std::vector<float> &ds : deltas)
		for (float delta : ds)
			assert(delta <= 0.);
	assert(expoly.holes.size() + 1 == deltas.size());
    assert(ClipperLib::Area(expoly.contour.points) > 0.);
    for (auto &h : expoly.holes)
        assert(ClipperLib::Area(h.points) < 0.);
#endif /* NDEBUG */

	// 1) Offset the outer contour.
    contours = fix_after_inner_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit), ClipperLib::pftNegative, true);
#ifndef NDEBUG
    // Shrinking a contour may split it into pieces, but never create a new hole inside the contour.
	for (auto &c : contours)
		assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

	// 2) Offset the holes one by one, collect the results.
	holes.reserve(expoly.holes.size());
    for (const Polygon &hole : expoly.holes)
		append(holes, fix_after_outer_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit), ClipperLib::pftNegative, false));
#ifndef NDEBUG
    // Offsetting a hole curve of a C shape may close the C into a ring with a new hole inside, thus creating a hole inside a hole shape, thus a hole will be created with negative area
    // and the following test will fail.
//    for (auto &c : holes)
//        assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */
}

Polygons variable_offset_inner(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Paths contours, holes;
    variable_offset_inner_raw(expoly, deltas, miter_limit, contours, holes);

	// Subtract holes from the contours.
	ClipperLib::Paths output;
	if (holes.empty())
		output = std::move(contours);
	else {
		ClipperLib::Clipper clipper;
		clipper.Clear();
		clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        // Holes may contain holes in holes produced by expanding a C hole shape.
        // The situation is processed correctly by Clipper diff operation.
		clipper.AddPaths(holes, ClipperLib::ptClip, true);
		clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
	}

	return to_polygons(std::move(output));
}

ExPolygons variable_offset_inner_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Paths contours, holes;
    variable_offset_inner_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
	ExPolygons output;
	if (holes.empty()) {
		output.reserve(contours.size());
        // Shrinking a CCW contour may only produce more CCW contours, but never new holes.
		for (ClipperLib::Path &path : contours) 
			output.emplace_back(std::move(path));
	} else {
		ClipperLib::Clipper clipper;
		clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        // Holes may contain holes in holes produced by expanding a C hole shape.
        // The situation is processed correctly by Clipper diff operation, producing concentric expolygons.
		clipper.AddPaths(holes, ClipperLib::ptClip, true);
	    ClipperLib::PolyTree polytree;
		clipper.Execute(ClipperLib::ctDifference, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
	    output = PolyTreeToExPolygons(std::move(polytree));
	}

	return output;
}

static void variable_offset_outer_raw(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit, ClipperLib::Paths &contours, ClipperLib::Paths &holes)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

#ifndef NDEBUG
	// Verify that the deltas are all non positive.
    for (const std::vector<float> &ds : deltas)
		for (float delta : ds)
            assert(delta >= 0.);
	assert(expoly.holes.size() + 1 == deltas.size());
    assert(ClipperLib::Area(expoly.contour.points) > 0.);
    for (auto &h : expoly.holes)
        assert(ClipperLib::Area(h.points) < 0.);
#endif /* NDEBUG */

	// 1) Offset the outer contour.
    contours = fix_after_outer_offset(mittered_offset_path_scaled(expoly.contour.points, deltas.front(), miter_limit), ClipperLib::pftPositive, false);
    // Inflating a contour must not remove it.
    assert(contours.size() >= 1);
#ifndef NDEBUG
    // Offsetting a positive curve of a C shape may close the C into a ring with hole shape, thus a hole will be created with negative area
    // and the following test will fail.
//  for (auto &c : contours)
//      assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */

	// 2) Offset the holes one by one, collect the results.
	holes.reserve(expoly.holes.size());
	for (const Polygon& hole : expoly.holes)
        append(holes, fix_after_inner_offset(mittered_offset_path_scaled(hole.points, deltas[1 + &hole - expoly.holes.data()], miter_limit), ClipperLib::pftPositive, true));
    //tiny holes can be reduced to giberish, get rid of them.
    for (auto it = holes.begin(); it != holes.end();)
        //if (ClipperLib::Area(*it) < double(CLIPPER_OFFSET_SCALE) * double(CLIPPER_OFFSET_SCALE)) { // sice PS 2.4, there is no clipperscale
        if (ClipperLib::Area(*it) < double(SCALED_EPSILON) * double(SCALED_EPSILON)) {
            it = holes.erase(it);
        }
        else ++it;
#ifndef NDEBUG
    // Shrinking a hole may split it into pieces, but never create a new hole inside a hole.
	for (auto &c : holes)
		assert(ClipperLib::Area(c) > 0.);
#endif /* NDEBUG */
}

Polygons variable_offset_outer(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Paths contours, holes;
    variable_offset_outer_raw(expoly, deltas, miter_limit, contours, holes);

    // Subtract holes from the contours.
    ClipperLib::Paths output;
    if (holes.empty())
        output = std::move(contours);
    else {
        //FIXME the difference is not needed as the holes may never intersect with other holes.
        ClipperLib::Clipper clipper;
        clipper.Clear();
        clipper.AddPaths(contours, ClipperLib::ptSubject, true);
        clipper.AddPaths(holes, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, output, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
    }

    return to_polygons(std::move(output));
}

ExPolygons variable_offset_outer_ex(const ExPolygon &expoly, const std::vector<std::vector<float>> &deltas, double miter_limit)
{
    CLIPPER_UTILS_TIME_LIMIT_MILLIS(CLIPPER_UTILS_TIME_LIMIT_DEFAULT);

    ClipperLib::Paths contours, holes;
    variable_offset_outer_raw(expoly, deltas, miter_limit, contours, holes);

	// Subtract holes from the contours.
	ExPolygons output;
	if (holes.empty()) {
		output.reserve(1);
        if (contours.size() > 1) {
            // One expolygon with holes created by closing a C shape. Which is which?
            output.push_back({});
            ExPolygon &out = output.back();
            out.holes.reserve(contours.size() - 1);
    		for (ClipperLib::Path &path : contours) {
                if (ClipperLib::Area(path) > 0) {
                    // Only one contour with positive area is expected to be created by an outer offset of an ExPolygon.
                    assert(out.contour.empty());
                    out.contour.points = std::move(path);
                } else
                    out.holes.push_back(Polygon{ std::move(path) });
            }
	} else {
            // Single contour must be CCW.
            assert(contours.size() == 1);
            assert(ClipperLib::Area(contours.front()) > 0);
            output.push_back(ExPolygon{ std::move(contours.front()) });
        }
	} else {
        //FIXME the difference is not needed as the holes may never intersect with other holes.
		ClipperLib::Clipper clipper;
        // Contours may have holes if they were created by closing a C shape.
		clipper.AddPaths(contours, ClipperLib::ptSubject, true);
		clipper.AddPaths(holes, ClipperLib::ptClip, true);
	    ClipperLib::PolyTree polytree;
		clipper.Execute(ClipperLib::ctDifference, polytree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
	    output = PolyTreeToExPolygons(std::move(polytree));
	}

    assert(output.size() == 1);
	return output;
}

}
