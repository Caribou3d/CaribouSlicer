#include "WipeTowerIntegration.hpp"

#include "../GCode.hpp"
#include "../libslic3r.h"

#include "boost/algorithm/string/replace.hpp"

namespace Slic3r::GCode {

static inline Point wipe_tower_point_to_object_point(GCodeGenerator &gcodegen, const Vec2f& wipe_tower_pt)
{
    return Point(scale_(wipe_tower_pt.x() - gcodegen.origin()(0)), scale_(wipe_tower_pt.y() - gcodegen.origin()(1)));
}

std::string WipeTowerIntegration::append_tcr(GCodeGenerator &gcodegen, const WipeTower::ToolChangeResult& tcr, int new_extruder_id, double z) const
{
    // has previous pos, or it's first layer.
    assert(gcodegen.last_pos_defined() || gcodegen.layer() == nullptr || gcodegen.layer()->lower_layer == nullptr);

    if (new_extruder_id != -1 && new_extruder_id != tcr.new_tool)
        throw Slic3r::InvalidArgument("Error: WipeTowerIntegration::append_tcr was asked to do a toolchange it didn't expect.");

    std::string gcode;
    
    assert(gcodegen.writer().tool_is_extruder());
    if (!gcodegen.writer().tool_is_extruder())
        return gcode;

    // write the gcode_label_objects_end before the priming tower.
    gcodegen.ensure_end_object_change_labels(gcode);

    // tag for fan speed (to not lost it)
    const bool finalize = &tcr == &m_final_purge;
    if (!finalize)
        gcode += ";_STORE_FAN_SPEED_WT\n";

    // Toolchangeresult.gcode assumes the wipe tower corner is at the origin (except for priming lines)
    // We want to rotate and shift all extrusions (gcode postprocessing) and starting and ending position
    float alpha = m_wipe_tower_rotation / 180.f * float(M_PI);

    auto transform_wt_pt = [&alpha, this](const Vec2f& pt) -> Vec2f {
        Vec2f out = Eigen::Rotation2Df(alpha) * pt;
        out += m_wipe_tower_pos;
        return out;
    };

    Vec2f start_pos = tcr.start_pos;
    Vec2f end_pos = tcr.end_pos;
    bool need_unretract = false;
    if (! tcr.priming) {
        start_pos = transform_wt_pt(start_pos);
        end_pos = transform_wt_pt(end_pos);
    }

    Vec2f wipe_tower_offset = tcr.priming ? Vec2f::Zero() : m_wipe_tower_pos;
    float wipe_tower_rotation = tcr.priming ? 0.f : alpha;

    std::string tcr_rotated_gcode = post_process_wipe_tower_moves(tcr, wipe_tower_offset, wipe_tower_rotation);

    double current_z = gcodegen.writer().get_unlifted_position().z();

    if (z == -1.) // in case no specific z was provided, print at current_z pos
        z = current_z;

    const bool needs_toolchange = gcodegen.writer().need_toolchange(new_extruder_id);
    const bool will_go_down = ! is_approx(z, current_z);
    const bool is_ramming = (gcodegen.config().single_extruder_multi_material)
                         || (! gcodegen.config().single_extruder_multi_material && gcodegen.config().filament_multitool_ramming.get_at(tcr.initial_tool));
    const bool should_travel_to_tower = ! tcr.priming
                                     && (tcr.force_travel        // wipe tower says so
                                         || ! needs_toolchange   // this is just finishing the tower with no toolchange
                                         || is_ramming
                                         || will_go_down);       // don't dig into the print
    if (should_travel_to_tower) {
        const Point xy_point = wipe_tower_point_to_object_point(gcodegen, start_pos);
        gcode += gcodegen.retract_and_wipe();
        gcodegen.m_avoid_crossing_perimeters.use_external_mp_once();
        const std::string comment{"Travel to a Wipe Tower"};
        Polyline travel_path = gcodegen.travel_to(gcode, xy_point, ExtrusionRole::Mixed);
        gcodegen.write_travel_to(gcode, travel_path, comment);
        need_unretract = true;
    } else {
        // When this is multiextruder printer without any ramming, we can just change
        // the tool without travelling to the tower.
    }

    if (will_go_down) {
        if (!need_unretract)
            gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(z, "Travel down to the last wipe tower layer.");
        need_unretract = true;
    }
    //now that we traveld and are ready to unretract, unlift.
    gcode += gcodegen.writer().unlift();
    // only unretract when travel is finished
    if(need_unretract)
        gcode += gcodegen.unretract();

    std::string toolchange_gcode_str;
    std::string deretraction_str;
    if (tcr.priming || (new_extruder_id >= 0 && needs_toolchange)) {
        if (is_ramming)
            gcodegen.m_wipe.reset_path(); // We don't want wiping on the ramming lines.
        toolchange_gcode_str = gcodegen.set_extruder(new_extruder_id, tcr.print_z); // TODO: toolchange_z vs print_z
        if (gcodegen.config().wipe_tower) {
            //const double retract_to_z = tcr.priming ? tcr.print_z : z;
            deretraction_str += gcodegen.writer().unlift();
            deretraction_str += gcodegen.unretract();
        }
    }
    assert(toolchange_gcode_str.empty() || toolchange_gcode_str.back() == '\n');
    assert(deretraction_str.empty() || deretraction_str.back() == '\n');

    // Insert the toolchange and deretraction gcode into the generated gcode.
    boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_from_wipe_tower_generator]", toolchange_gcode_str);
    boost::replace_first(tcr_rotated_gcode, "[deretraction_from_wipe_tower_generator]", deretraction_str);
    boost::replace_first(tcr_rotated_gcode, "{layer_z}", to_string_nozero(gcodegen.writer().get_position().z() + gcodegen.writer().config.z_offset.value, 4));
    if (gcodegen.config().filament_pressure_advance.is_enabled(tcr.initial_tool)) {
        boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_disable_linear_advance]",
                             gcodegen.writer().set_pressure_advance(0));
    } else {
        boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_disable_linear_advance]\n","");
    }
    if (gcodegen.config().filament_pressure_advance.is_enabled(new_extruder_id)) {
        boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_enable_linear_advance]",
                             gcodegen.writer().set_pressure_advance(gcodegen.config().filament_pressure_advance.get_at(new_extruder_id)));
    } else {
        boost::replace_first(tcr_rotated_gcode, "[toolchange_gcode_enable_linear_advance]\n","");
    }

    // the custom gcode is already processed by the parser...
    // supermerill: why? it breaks the '\' from custom gcode. disabling it
    //std::string tcr_gcode;
    //unescape_string_cstyle(tcr_rotated_gcode, tcr_gcode);
    //gcode += tcr_gcode;
    gcode += tcr_rotated_gcode;

    // tag for fan speed (to not lost it)
    if (!finalize)
        gcode += ";_RESTORE_FAN_SPEED_WT\n";
        
    // A phony move to the end position at the wipe tower.
    gcodegen.writer().travel_to_xy(end_pos.cast<double>());
    gcodegen.set_last_pos(wipe_tower_point_to_object_point(gcodegen, end_pos));
    if (!is_approx(z, current_z)) {
        gcode += gcodegen.writer().retract();
        gcode += gcodegen.writer().travel_to_z(current_z, "Travel back up to the topmost object layer.");
        // gcode += gcodegen.writer().unretract(); //why? it's done automatically later, where needed!
    } else if(should_travel_to_tower) {
        // Prepare a future wipe.
        // Convert to a smooth path.
        Geometry::ArcWelder::Path path;
        path.reserve(tcr.wipe_path.size());
        std::transform(tcr.wipe_path.begin(), tcr.wipe_path.end(), std::back_inserter(path),
            [&gcodegen, &transform_wt_pt](const Vec2f &wipe_pt) { 
                return Geometry::ArcWelder::Segment(wipe_tower_point_to_object_point(gcodegen, transform_wt_pt(wipe_pt)), 0, Geometry::ArcWelder::Orientation::Unknown);
            });
        // Pass to the wipe cache.
        assert(gcodegen.m_wipe.path().empty());
        gcodegen.m_wipe.set_path(std::move(path), false);
    }

    // Let the planner know we are traveling between objects.
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once();
    return gcode;
}

// This function postprocesses gcode_original, rotates and moves all G1 extrusions and returns resulting gcode
// Starting position has to be supplied explicitely (otherwise it would fail in case first G1 command only contained one coordinate)
std::string WipeTowerIntegration::post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const
{
    Vec2f extruder_offset = m_extruder_offsets[tcr.initial_tool].cast<float>();

    std::istringstream gcode_str(tcr.gcode);
    std::string gcode_out;
    std::string line;
    Vec2f pos = tcr.start_pos;
    Vec2f transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;
    Vec2f old_pos(-1000.1f, -1000.1f);

    while (gcode_str) {
        std::getline(gcode_str, line);  // we read the gcode line by line

        // All G1 commands should be translated and rotated. X and Y coords are
        // only pushed to the output when they differ from last time.
        // WT generator can override this by appending the never_skip_tag
        if (boost::starts_with(line, "G1 ")) {
            bool never_skip = false;
            auto it = line.find(WipeTower::never_skip_tag());
            if (it != std::string::npos) {
                // remove the tag and remember we saw it
                never_skip = true;
                line.erase(it, it + WipeTower::never_skip_tag().size());
            }
            std::ostringstream line_out;
            std::istringstream line_str(line);
            line_str >> std::noskipws;  // don't skip whitespace
            char ch = 0;
            line_str >> ch >> ch; // read the "G1"
            while (line_str >> ch) {
                if (ch == 'X' || ch == 'Y')
                    line_str >> (ch == 'X' ? pos.x() : pos.y());
                else
                    line_out << ch;
            }

            line = line_out.str();
            boost::trim(line); // Remove leading and trailing spaces.

            transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;

            if (transformed_pos != old_pos || never_skip || ! line.empty()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3) << "G1";
                if (transformed_pos.x() != old_pos.x() || never_skip)
                    oss << " X" << transformed_pos.x() - extruder_offset.x();
                if (transformed_pos.y() != old_pos.y() || never_skip)
                    oss << " Y" << transformed_pos.y() - extruder_offset.y();
                if (! line.empty())
                    oss << " ";
                line = oss.str() + line;
                old_pos = transformed_pos;
            }
        }

        gcode_out += line + "\n";

        // If this was a toolchange command, we should change current extruder offset
        if (line == "[toolchange_gcode_from_wipe_tower_generator]") {
            extruder_offset = m_extruder_offsets[tcr.new_tool].cast<float>();

            // If the extruder offset changed, add an extra move so everything is continuous
            if (extruder_offset != m_extruder_offsets[tcr.initial_tool].cast<float>()) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(3)
                    << "G1 X" << transformed_pos.x() - extruder_offset.x()
                    << " Y" << transformed_pos.y() - extruder_offset.y()
                    << "\n";
                gcode_out += oss.str();
            }
        }
    }
    return gcode_out;
}


std::string WipeTowerIntegration::prime(GCodeGenerator &gcodegen)
{
    std::string gcode;
    for (const WipeTower::ToolChangeResult& tcr : m_priming) {
        if (! tcr.extrusions.empty())
            gcode += append_tcr(gcodegen, tcr, tcr.new_tool);
    }
    return gcode;
}

std::string WipeTowerIntegration::tool_change(GCodeGenerator &gcodegen, int extruder_id, bool finish_layer)
{
    std::string gcode;
    assert(m_layer_idx >= 0);
    if (gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
        if (m_layer_idx < (int)m_tool_changes.size()) {
            if (!(size_t(m_tool_change_idx) < m_tool_changes[m_layer_idx].size()))
                throw Slic3r::RuntimeError("Wipe tower generation failed, possibly due to empty first layer.");

            // Calculate where the wipe tower layer will be printed. -1 means that print z will not change,
            // resulting in a wipe tower with sparse layers.
            double wipe_tower_z = -1;
            bool ignore_sparse = false;
            if (gcodegen.config().wipe_tower_no_sparse_layers.value) {
                wipe_tower_z = m_last_wipe_tower_print_z;
                ignore_sparse = (m_tool_changes[m_layer_idx].size() == 1 && m_tool_changes[m_layer_idx].front().initial_tool == m_tool_changes[m_layer_idx].front().new_tool && m_layer_idx != 0);
                if (m_tool_change_idx == 0 && !ignore_sparse)
                    wipe_tower_z = m_last_wipe_tower_print_z + m_tool_changes[m_layer_idx].front().layer_height;
            }

            if (!ignore_sparse) {
                gcode += append_tcr(gcodegen, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id, wipe_tower_z);
                m_last_wipe_tower_print_z = wipe_tower_z;
            }
        }
    }
    return gcode;
}

// Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
std::string WipeTowerIntegration::finalize(GCodeGenerator &gcodegen)
{
    std::string gcode;
    if (std::abs(gcodegen.writer().get_position().z() - m_final_purge.print_z) > EPSILON)
        gcode += gcodegen.generate_travel_gcode(
            {{gcodegen.last_pos().x(), gcodegen.last_pos().y(), scaled(m_final_purge.print_z)}},
            "move to safe place for purging"
        );
    gcode += append_tcr(gcodegen, m_final_purge, -1);
    return gcode;
}

} // namespace Slic3r::GCode
