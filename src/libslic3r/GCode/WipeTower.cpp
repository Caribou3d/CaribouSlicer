///|/ Copyright (c) Prusa Research 2017 - 2023 Lukáš Matěna @lukasmatena, Vojtěch Bubník @bubnikv, Enrico Turri @enricoturri1966
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2020 Paul Arden @ardenpm
///|/ Copyright (c) 2019 Thomas Moore
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "WipeTower.hpp"

#include <cassert>
#include <iostream>
#include <vector>
#include <numeric>
#include <memory>
#include <sstream>
#include <iomanip>

#include "ClipperUtils.hpp"
#include "GCodeProcessor.hpp"
#include "BoundingBox.hpp"
#include "LocalesUtils.hpp"
#include "Flow.hpp"
#include "Geometry.hpp"
#include "Surface.hpp"
#include "Fill/FillRectilinear.hpp"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace Slic3r
{

class WipeTowerWriter
{
public:
	WipeTowerWriter(float layer_height, float line_width, GCodeFlavor flavor, std::vector<std::string> tool_name, const std::vector<WipeTower::FilamentParameters>& filament_parameters) :
		m_current_pos(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		m_current_z(0.f),
		m_current_feedrate(0.f),
		m_layer_height(layer_height),
		m_extrusion_flow(0.f),
		m_preview_suppressed(false),
		m_elapsed_time(0.f),
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_default_analyzer_line_width(line_width),
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_gcode_flavor(flavor),
        m_tool_name(tool_name),
        m_filpar(filament_parameters)
        {
            // adds tag for analyzer:
            std::ostringstream str;
            str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height) << m_layer_height << "\n"; // don't rely on GCodeAnalyzer knowing the layer height - it knows nothing at priming
            str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role) << gcode_extrusion_role_to_string(GCodeExtrusionRole::WipeTower) << "\n";
            m_gcode += str.str();
            change_analyzer_line_width(line_width);
    }

    WipeTowerWriter& change_analyzer_line_width(float line_width) {
        // adds tag for analyzer:
        std::stringstream str;
        str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width) << line_width << "\n";
        m_gcode += str.str();
        return *this;
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    WipeTowerWriter& change_analyzer_mm3_per_mm(float len, float e) {
        static const float area = float(M_PI) * 1.75f * 1.75f / 4.f;
        float mm3_per_mm = (len == 0.f ? 0.f : area * e / len);
        // adds tag for processor:
        std::stringstream str;
        str << ";" << GCodeProcessor::Mm3_Per_Mm_Tag << mm3_per_mm << "\n";
        m_gcode += str.str();
        return *this;
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

	WipeTowerWriter& 			 set_initial_position(const Vec2f &pos, float width = 0.f, float depth = 0.f, float internal_angle = 0.f) {
        m_wipe_tower_width = width;
        m_wipe_tower_depth = depth;
        m_internal_angle = internal_angle;
		m_start_pos = this->rotate(pos);
		m_current_pos = pos;
		return *this;
	}

    WipeTowerWriter& 			 set_position(const Vec2f &pos) { m_current_pos = pos; return *this; }

    WipeTowerWriter&				 set_initial_tool(size_t tool) { m_current_tool = tool; return *this; }

	WipeTowerWriter&				 set_z(float z) 
		{ m_current_z = z; return *this; }

	WipeTowerWriter& 			 set_extrusion_flow(float flow)
		{ m_extrusion_flow = flow; return *this; }

	WipeTowerWriter&				 set_y_shift(float shift) {
        m_current_pos.y() -= shift-m_y_shift;
        m_y_shift = shift;
        return (*this);
    }

    WipeTowerWriter &disable_linear_advance() {
        m_gcode += "[toolchange_gcode_disable_linear_advance]\n";
        return *this;
    }

    WipeTowerWriter &enable_linear_advance() {
        m_gcode += "[toolchange_gcode_enable_linear_advance]\n";
        return *this;
    }

	// Suppress / resume G-code preview in Slic3r. Slic3r will have difficulty to differentiate the various
	// filament loading and cooling moves from normal extrusion moves. Therefore the writer
	// is asked to suppres output of some lines, which look like extrusions.
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
	WipeTowerWriter& 			 suppress_preview() { change_analyzer_line_width(0.f); m_preview_suppressed = true; return *this; }
	WipeTowerWriter& 			 resume_preview()   { change_analyzer_line_width(m_default_analyzer_line_width); m_preview_suppressed = false; return *this; }
#else
    WipeTowerWriter& 			 suppress_preview() { m_preview_suppressed = true; return *this; }
	WipeTowerWriter& 			 resume_preview()   { m_preview_suppressed = false; return *this; }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

	WipeTowerWriter& 			 feedrate(float f)
	{
        if (f != m_current_feedrate) {
			m_gcode += "G1" + set_format_F(f) + "\n";
            m_current_feedrate = f;
        }
		return *this;
	}

	const std::string&   gcode() const { return m_gcode; }
	const std::vector<WipeTower::Extrusion>& extrusions() const { return m_extrusions; }
	float                x()     const { return m_current_pos.x(); }
	float                y()     const { return m_current_pos.y(); }
	const Vec2f& pos()   const { return m_current_pos; }
	const Vec2f	 start_pos_rotated() const { return m_start_pos; }
	const Vec2f  		 pos_rotated() const { return this->rotate(m_current_pos); }
	float 				 elapsed_time() const { return m_elapsed_time; }
    float                get_and_reset_used_filament_length() { float temp = m_used_filament_length; m_used_filament_length = 0.f; return temp; }

	// Extrude with an explicitely provided amount of extrusion.
	WipeTowerWriter& extrude_explicit(float x, float y, float e, float f = 0.f, bool record_length = false, bool limit_volumetric_flow = true)
	{
		if (x == m_current_pos.x() && y == m_current_pos.y() && e == 0.f && (f == 0.f || f == m_current_feedrate))
			// Neither extrusion nor a travel move.
			return *this;

		float dx = x - m_current_pos.x();
		float dy = y - m_current_pos.y();
        float len = std::sqrt(dx*dx+dy*dy);
        if (record_length)
            m_used_filament_length += e;

		// Now do the "internal rotation" with respect to the wipe tower center
		Vec2f rotated_current_pos(this->pos_rotated());
		Vec2f rot(this->rotate(Vec2f(x,y)));                               // this is where we want to go

        if (! m_preview_suppressed && e > 0.f && len > 0.f) {
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
            change_analyzer_mm3_per_mm(len, e);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
			// Width of a squished extrusion, corrected for the roundings of the squished extrusions.
			// This is left zero if it is a travel move.
            float width = e * m_filpar[0].filament_area / (len * m_layer_height);
			// Correct for the roundings of a squished extrusion.
			width += m_layer_height * float(1. - M_PI / 4.);
			if (m_extrusions.empty() || m_extrusions.back().pos != rotated_current_pos)
				m_extrusions.emplace_back(WipeTower::Extrusion(rotated_current_pos, 0, m_current_tool));
			m_extrusions.emplace_back(WipeTower::Extrusion(rot, width, m_current_tool));
		}

        std::string gcode;
        if (std::abs(rot.x() - rotated_current_pos.x()) > (float)EPSILON)
            gcode += set_format_X(rot.x());

        if (std::abs(rot.y() - rotated_current_pos.y()) > (float)EPSILON)
            gcode += set_format_Y(rot.y());


        if (e != 0.f)
            gcode += set_format_E(e);

        if (f != 0.f && f != m_current_feedrate) {
            if (limit_volumetric_flow) {
                float e_speed = e / (((len == 0.f) ? std::abs(e) : len) / f * 60.f);
                f /= std::max(1.f, e_speed / m_filpar[m_current_tool].max_e_speed);
                if (len > 0 && m_filpar[m_current_tool].max_speed > 0) {
                    // don't forget to go from speed (mm/s) to Feedrate (mm/min)
                    f = std::min(f, m_filpar[m_current_tool].max_speed * 60.f);
                }
            }
            gcode += set_format_F(f);
        }

        m_current_pos.x() = x;
        m_current_pos.y() = y;

        if (!gcode.empty()) {
            // Update the elapsed time with a rough estimate.
            m_elapsed_time += ((len == 0.f) ? std::abs(e) : len) / m_current_feedrate * 60.f;
            m_gcode += "G1" + gcode + "\n";
        }
		return *this;
	}

	WipeTowerWriter& extrude_explicit(const Vec2f &dest, float e, float f = 0.f, bool record_length = false, bool limit_volumetric_flow = true)
		{ return extrude_explicit(dest.x(), dest.y(), e, f, record_length, limit_volumetric_flow); }

	// Travel to a new XY position. f=0 means use the current value.
	WipeTowerWriter& travel(float x, float y, float f = 0.f)
		{ return extrude_explicit(x, y, 0.f, f); }

	WipeTowerWriter& travel(const Vec2f &dest, float f = 0.f) 
		{ return extrude_explicit(dest.x(), dest.y(), 0.f, f); }

	// Extrude a line from current position to x, y with the extrusion amount given by m_extrusion_flow.
	WipeTowerWriter& extrude(float x, float y, float f = 0.f)
	{
		float dx = x - m_current_pos.x();
		float dy = y - m_current_pos.y();
        return extrude_explicit(x, y, std::sqrt(dx*dx+dy*dy) * m_extrusion_flow, f, true);
	}

	WipeTowerWriter& extrude(const Vec2f &dest, const float f = 0.f) 
		{ return extrude(dest.x(), dest.y(), f); }

    WipeTowerWriter& rectangle(const Vec2f& ld,float width,float height,const float f = 0.f)
    {
        Vec2f corners[4];
        corners[0] = ld;
        corners[1] = ld + Vec2f(width,0.f);
        corners[2] = ld + Vec2f(width,height);
        corners[3] = ld + Vec2f(0.f,height);
        int index_of_closest = 0;
        if (x()-ld.x() > ld.x()+width-x())    // closer to the right
            index_of_closest = 1;
        if (y()-ld.y() > ld.y()+height-y())   // closer to the top
            index_of_closest = (index_of_closest==0 ? 3 : 2);

        travel(corners[index_of_closest].x(), y());      // travel to the closest corner
        travel(x(),corners[index_of_closest].y());

        int i = index_of_closest;
        do {
            ++i;
            if (i==4) i=0;
            extrude(corners[i], f);
        } while (i != index_of_closest);
        return (*this);
    }

    WipeTowerWriter& rectangle(const WipeTower::box_coordinates& box, const float f = 0.f)
    {
        rectangle(Vec2f(box.ld.x(), box.ld.y()),
                  box.ru.x() - box.lu.x(),
                  box.ru.y() - box.rd.y(), f);
        return (*this);
    }

	WipeTowerWriter& load(float e, float f = 0.f)
	{
		if (e == 0.f && (f == 0.f || f == m_current_feedrate))
			return *this;
		m_gcode += "G1";
		if (e != 0.f)
			m_gcode += set_format_E(e);
		if (f != 0.f && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	WipeTowerWriter& retract(float e, float f = 0.f)
		{ return load(-e, f); }

// Loads filament while also moving towards given points in x-axis (x feedrate is limited by cutting the distance short if necessary)
    WipeTowerWriter& load_move_x_advanced(float farthest_x, float loading_dist, float loading_speed, float max_x_speed = 50.f)
    {
        float time = std::abs(loading_dist / loading_speed); // time that the move must take
        float x_distance = std::abs(farthest_x - x());       // max x-distance that we can travel
        float x_speed = x_distance / time;                   // x-speed to do it in that time

        if (x_speed > max_x_speed) {
            // Necessary x_speed is too high - we must shorten the distance to achieve max_x_speed and still respect the time.
            x_distance = max_x_speed * time;
            x_speed = max_x_speed;
        }

        float end_point = x() + (farthest_x > x() ? 1.f : -1.f) * x_distance;
        return extrude_explicit(end_point, y(), loading_dist, x_speed * 60.f, false, false);
    }

	// Elevate the extruder head above the current print_z position.
	WipeTowerWriter& z_hop(float hop, float f = 0.f)
	{ 
		m_gcode += std::string("G1") + set_format_Z(m_current_z + hop);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	// Lower the extruder head back to the current print_z position.
	WipeTowerWriter& z_hop_reset(float f = 0.f) 
		{ return z_hop(0, f); }

	// Move to x1, +y_increment,
	// extrude quickly amount e to x2 with feed f.
	WipeTowerWriter& ram(float x1, float x2, float dy, float e0, float e, float f)
	{
		extrude_explicit(x1, m_current_pos.y() + dy, e0, f, true, false);
		extrude_explicit(x2, m_current_pos.y(), e, 0.f, true, false);
		return *this;
	}

	// Let the end of the pulled out filament cool down in the cooling tube
	// by moving up and down and moving the print head left / right
	// at the current Y position to spread the leaking material.
	WipeTowerWriter& cool(float x1, float x2, float e1, float e2, float f)
	{
		extrude_explicit(x1, m_current_pos.y(), e1, f, false, false);
		extrude_explicit(x2, m_current_pos.y(), e2, false, false);
		return *this;
	}

    WipeTowerWriter& set_tool(size_t tool)
	{
		m_current_tool = tool;
		return *this;
	}

    //add skinnydip move (dip in, pause, dip out, pause)
    WipeTowerWriter& skinnydip_move(float distance, float downspeed, int meltpause, float upspeed, int coolpause) 
    {
        this->append("; SKINNYDIP START\n");
        //char all[320] =""; //don't use snprintf, as this use the locale for '.' or ',' choice, and we need the '.' -> use same method as the rest of the class.
        //snprintf(all, 80, "G1 E%.4f F%.0f\n", distance, downspeed*60 );
        this->append("G1");
        this->append(set_format_E(distance));
        this->append(set_format_F(downspeed * 60));
        this->append("\n");
        //snprintf(all, 80, "G4 P%d\n", meltpause);
        {
            char buf[64];
            sprintf(buf, "G4 P%d\n", meltpause);
            this->append(std::string(buf));
        }
        //snprintf(all, 80,  "G1 E-%.4f F%.0f\n", distance, upspeed*60);
        this->append("G1");
        this->append(set_format_E(-distance));
        this->append(set_format_F(upspeed * 60));
        this->append("\n");
        //snprintf(all, 80, "G4 P%d\n", coolpause);
        {
            char buf[64];
            sprintf(buf, "G4 P%d\n", coolpause);
            this->append(std::string(buf));
        }
        this->append("; SKINNYDIP END\n");
        return *this;
    }

    //add toolchange_temp -skinnydip
    WipeTowerWriter& wait_for_toolchange_temp(int tc_temp, bool fan_on, int fan_speed, bool fast) 
    {
        //char all[128];
        if (fan_on == true){
            set_fan(fan_speed, " ;Part fan on to cool hotend");
        }
        //sprintf(all, "M109 S%d ;SKINNYDIP TOOLCHANGE WAIT FOR TEMP %s\n", tc_temp, fast ? "FAST MODE":"NORMAL MODE");
        //this->append(all);
        set_extruder_temp(tc_temp, this->m_current_tool, true, ";SKINNYDIP TOOLCHANGE WAIT FOR TEMP " + fast ? "FAST MODE" : "NORMAL MODE");
        if (fan_on == true){
            set_fan(m_last_fan_speed, " ;restore cooling");
        }
        return *this;
    }

    //begin toolchange_temp -skinnydip
    WipeTowerWriter& begin_toolchange_temp(int tc_temp, bool fast) 
    {
        //char tdbuf[128];
        //sprintf(tdbuf, "M104 S%d  ;SKINNYDIP BEGIN TOOLCHANGE TEMP %s\n", tc_temp, fast ? "FAST MODE":"NORMAL MODE");
        //m_gcode += tdbuf;
        set_extruder_temp(tc_temp, this->m_current_tool, false, ";SKINNYDIP BEGIN TOOLCHANGE TEMP " + fast ? "FAST MODE" : "NORMAL MODE");
        return *this;
    }

    //restore toolchange_temp -skinnydip
    WipeTowerWriter& restore_pre_toolchange_temp(int tc_temp, bool fast) 
    {
        //char tdbuf[128];
        //sprintf(tdbuf, "M104 S%d  ;RESTORE PRE-TOOLCHANGE TEMP %s\n", tc_temp, fast ? "FAST MODE":"NORMAL MODE");
        //m_gcode += tdbuf;
        set_extruder_temp(tc_temp, this->m_current_tool , false, ";RESTORE PRE-TOOLCHANGE TEMP " + fast ? "FAST MODE" : "NORMAL MODE");
        return *this;
    }

	// Set extruder temperature, don't wait by default.
    WipeTowerWriter& set_extruder_temp(unsigned int temperature, size_t tool, bool wait = false, std::string comment = "")
    {
        if (wait && (this->m_gcode_flavor == gcfMakerWare || this->m_gcode_flavor == (gcfSailfish)))
            return *this;

        std::string code;
        if (wait && this->m_gcode_flavor != (gcfTeacup) && this->m_gcode_flavor != (gcfRepRap) && this->m_gcode_flavor != (gcfSprinter)) {
            code = "M109";
        } else {
            if (this->m_gcode_flavor == (gcfRepRap)) { // M104 is deprecated on RepRapFirmware
                code = "G10";
            } else {
                code = "M104";
            }
        }

        std::ostringstream gcode;
        gcode << code << " ";
        if (this->m_gcode_flavor == (gcfMach3) || this->m_gcode_flavor == (gcfMachinekit)) {
            gcode << "P";
        } else if (this->m_gcode_flavor == (gcfRepRap)) {
            gcode << "P" << tool << " S";
        } else if ((this->m_gcode_flavor == (gcfMarlinFirmware) || this->m_gcode_flavor == (gcfMarlinLegacy)) && wait) {
            gcode << "R";
        }
        else {
            gcode << "S";
        }
        gcode << temperature;
        bool multiple_tools = false; // ?
        if (this->m_current_tool != -1 && (multiple_tools || this->m_gcode_flavor == (gcfMakerWare) || this->m_gcode_flavor == (gcfSailfish))) {
            if (this->m_gcode_flavor != (gcfRepRap)) {
                gcode << " T" << tool;
            }
        }
    
        if(!comment.empty())
            gcode << " ; " << comment << "\n";

        if ((this->m_gcode_flavor == (gcfTeacup) || this->m_gcode_flavor == (gcfRepRap)) && wait)
            gcode << "M116 ; wait for temperature to be reached\n";

        gcode << "\n";
        m_gcode += gcode.str();
        return *this;
    }

    // Wait for a period of time (seconds).
	WipeTowerWriter& wait(float time)
	{
        if (time==0.f)
            return *this;
        m_gcode += "G4 S" + Slic3r::float_to_string_decimal_point(time, 3) + "\n";
		return *this;
    }

	// Set speed factor override percentage.
	WipeTowerWriter& speed_override(int speed)
	{
        m_gcode += "M220 S" + std::to_string(speed) + "\n";
		return *this;
    }

	// Let the firmware back up the active speed override value.
	WipeTowerWriter& speed_override_backup()
    {
        // This is only supported by Prusa at this point (https://github.com/prusa3d/PrusaSlicer/issues/3114)
        if (m_gcode_flavor == gcfMarlinLegacy || m_gcode_flavor == gcfMarlinFirmware)
            m_gcode += "M220 B\n";
		return *this;
    }

	// Let the firmware restore the active speed override value.
	WipeTowerWriter& speed_override_restore()
	{
        if (m_gcode_flavor == gcfMarlinLegacy || m_gcode_flavor == gcfMarlinFirmware)
            m_gcode += "M220 R\n";
        else
            m_gcode += "M220 S100\n";
		return *this;
    }

	// Set digital trimpot motor
	WipeTowerWriter& set_extruder_trimpot(int current)
	{
        if (m_gcode_flavor == gcfKlipper)
            return *this;
        if (m_gcode_flavor == gcfRepRap || m_gcode_flavor == gcfSprinter)
            m_gcode += "M906 E";
        else
            m_gcode += "M907 E";
        m_gcode += std::to_string(current) + "\n";
		return *this;
    }

	WipeTowerWriter& flush_planner_queue()
	{ 
		m_gcode += "G4 S0\n"; 
		return *this;
	}

	// Reset internal extruder counter.
	WipeTowerWriter& reset_extruder()
	{ 
		m_gcode += "G92 E0\n";
		return *this;
	}

	WipeTowerWriter& comment_with_value(const char *comment, int value)
	{
        m_gcode += std::string(";") + comment + std::to_string(value) + "\n";
		return *this;
    }


    WipeTowerWriter& set_fan(unsigned speed, const std::string &comment)
	{
        assert(comment.empty() || comment[0] == ';' || (comment.size() > 1 && comment[0] == ' ' && comment[1] == ';'));
		if (speed == m_last_fan_speed)
			return *this;
		if (speed == 0 && (gcfTeacup != m_gcode_flavor && gcfRepRap != m_gcode_flavor))
			m_gcode += "M107" + comment + "\n";
		else
            m_gcode += "M106 S" + std::to_string(unsigned(255.0 * speed / 100.0)) + comment + "\n";
		m_last_fan_speed = speed;
		return *this;
	}

	WipeTowerWriter& append(const std::string& text) { m_gcode += text; return *this; }

    const std::vector<Vec2f>& wipe_path() const
    {
        return m_wipe_path;
    }

    WipeTowerWriter& add_wipe_point(const Vec2f& pt)
    {
        m_wipe_path.push_back(rotate(pt));
        return *this;
    }

    WipeTowerWriter& add_wipe_point(float x, float y)
    {
        return add_wipe_point(Vec2f(x, y));
    }

private:
	Vec2f         m_start_pos;
	Vec2f         m_current_pos;
    std::vector<Vec2f>  m_wipe_path;
	float    	  m_current_z;
	float 	  	  m_current_feedrate;
    size_t        m_current_tool;
	float 		  m_layer_height;
	float 	  	  m_extrusion_flow;
	bool		  m_preview_suppressed;
	std::string   m_gcode;
	std::vector<WipeTower::Extrusion> m_extrusions;
	float         m_elapsed_time;
	float   	  m_internal_angle = 0.f;
	float		  m_y_shift = 0.f;
	float 		  m_wipe_tower_width = 0.f;
	float		  m_wipe_tower_depth = 0.f;
    unsigned      m_last_fan_speed = 0;
    int           current_temp = -1;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    const float   m_default_analyzer_line_width;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    float         m_used_filament_length = 0.f;
    GCodeFlavor   m_gcode_flavor;
    std::vector<std::string> m_tool_name;
    const std::vector<WipeTower::FilamentParameters>& m_filpar;

	std::string   set_format_X(float x)
    {
        m_current_pos.x() = x;
        return " X" + Slic3r::float_to_string_decimal_point(x, 3);
	}

	std::string   set_format_Y(float y) {
        m_current_pos.y() = y;
        return " Y" + Slic3r::float_to_string_decimal_point(y, 3);
	}

	std::string   set_format_Z(float z) {
        return " Z" + Slic3r::float_to_string_decimal_point(z, 3);
	}

	std::string   set_format_E(float e) {
        return " E" + Slic3r::float_to_string_decimal_point(e, 4);
	}

	std::string   set_format_F(float f) {
        char buf[64];
        sprintf(buf, " F%d", int(floor(f + 0.5f)));
        m_current_feedrate = f;
        return buf;
	}

	WipeTowerWriter& operator=(const WipeTowerWriter &rhs);

	// Rotate the point around center of the wipe tower about given angle (in degrees)
	Vec2f rotate(Vec2f pt) const
	{
		pt.x() -= m_wipe_tower_width / 2.f;
		pt.y() += m_y_shift - m_wipe_tower_depth / 2.f;
	    double angle = m_internal_angle * float(M_PI/180.);
	    double c = cos(angle);
	    double s = sin(angle);
	    return Vec2f(float(pt.x() * c - pt.y() * s) + m_wipe_tower_width / 2.f, float(pt.x() * s + pt.y() * c) + m_wipe_tower_depth / 2.f);
	}

}; // class WipeTowerWriter



WipeTower::ToolChangeResult WipeTower::construct_tcr(WipeTowerWriter& writer,
                                                     bool priming,
                                                     size_t old_tool) const
{
    ToolChangeResult result;
    result.priming      = priming;
    result.initial_tool = int(old_tool);
    result.new_tool     = int(m_current_tool);
    result.print_z      = m_z_pos;
    result.layer_height = m_layer_height;
    result.elapsed_time = writer.elapsed_time();
    result.start_pos    = writer.start_pos_rotated();
    result.end_pos      = priming ? writer.pos() : writer.pos_rotated();
    result.gcode        = std::move(writer.gcode());
    result.extrusions   = std::move(writer.extrusions());
    result.wipe_path    = std::move(writer.wipe_path());
    return result;
}



WipeTower::WipeTower(const PrintConfig& config, const PrintObjectConfig& default_object_config, const PrintRegionConfig& default_region_config, const std::vector<std::vector<float>>& wiping_matrix, size_t initial_tool) :
    m_config(&config),
    m_object_config(&default_object_config),
    m_region_config(&default_region_config),
    m_semm(config.single_extruder_multi_material.value),
    m_wipe_tower_pos(config.wipe_tower_x, config.wipe_tower_y),
    m_wipe_tower_width(float(config.wipe_tower_width)),
    m_wipe_tower_rotation_angle(float(config.wipe_tower_rotation_angle)),
    m_speed(float(config.wipe_tower_speed)),
    m_wipe_tower_cone_angle(float(config.wipe_tower_cone_angle)),
    m_extra_spacing(float(config.wipe_tower_extra_spacing/100.)),
    m_y_shift(0.f),
    m_z_pos(0.f),
    m_bridging(float(config.wipe_tower_bridging)),
    m_no_sparse_layers(config.wipe_tower_no_sparse_layers),
    m_gcode_flavor(config.gcode_flavor),
    m_travel_speed(config.travel_speed),
    m_infill_speed(default_region_config.infill_speed),
    m_perimeter_speed(default_region_config.perimeter_speed),
    m_current_tool(initial_tool),
    wipe_volumes(wiping_matrix)
{
    // be sure the speed is positive (0 == disabled)
    if (m_speed < 0) {
        m_speed = 0;
    }
    // Read absolute value of first layer speed, if given as percentage,
    // it is taken over wipe_tower_speed.
    m_first_layer_speed = default_object_config.first_layer_speed.get_abs_value(m_speed);
    if (m_first_layer_speed <= 0.f) { // just to make sure autospeed doesn't break it.
        m_first_layer_speed = m_speed;
    }

    // Autospeed may be used...
    if (m_infill_speed <= 0.f)
        m_infill_speed = 80.f;
    if (m_perimeter_speed <= 0.f)
        m_perimeter_speed = 80.f;


    // If this is a single extruder MM printer, we will use all the SE-specific config values.
    // Otherwise, the defaults will be used to turn off the SE stuff.
    if (m_semm) {
        m_cooling_tube_retraction = float(config.cooling_tube_retraction);
        m_cooling_tube_length     = float(config.cooling_tube_length);
        m_parking_pos_retraction  = float(config.parking_pos_retraction);
        m_extra_loading_move      = float(config.extra_loading_move);
        m_set_extruder_trimpot = config.high_current_on_filament_swap;
    }

    // Calculate where the priming lines should be - very naive test not detecting parallelograms etc.
    const std::vector<Vec2d>& bed_points = config.bed_shape.get_values();
    BoundingBoxf bb(bed_points);
    m_bed_width = float(bb.size().x());
    m_bed_shape = (bed_points.size() == 4 ? RectangularBed : CircularBed);

    if (m_bed_shape == CircularBed) {
        // this may still be a custom bed, check that the points are roughly on a circle
        double r2 = std::pow(m_bed_width/2., 2.);
        double lim2 = std::pow(m_bed_width/10., 2.);
        Vec2d center = bb.center();
        for (const Vec2d& pt : bed_points)
            if (std::abs(std::pow(pt.x()-center.x(), 2.) + std::pow(pt.y()-center.y(), 2.) - r2) > lim2) {
                m_bed_shape = CustomBed;
                break;
            }
    }

    m_bed_bottom_left = m_bed_shape == RectangularBed
                  ? Vec2f(bed_points.front().x(), bed_points.front().y())
                  : Vec2f::Zero();
}



void WipeTower::set_extruder(size_t idx)
{
    //while (m_filpar.size() < idx+1)   // makes sure the required element is in the vector
    m_filpar.push_back(FilamentParameters());

    m_filpar[idx].material = m_config->filament_type.get_at(idx);
    if (m_config->wipe_tower_extruder == 0) {
        m_filpar[idx].is_soluble = m_config->filament_soluble.get_at(idx);
    } else {
        m_filpar[idx].is_soluble = (idx != size_t(m_config->wipe_tower_extruder - 1));
    }
    m_filpar[idx].temperature = m_config->temperature.get_at(idx);
    m_filpar[idx].first_layer_temperature = m_config->first_layer_temperature.get_at(idx);

    // If this is a single extruder MM printer, we will use all the SE-specific config values.
    // Otherwise, the defaults will be used to turn off the SE stuff.
    if (m_semm) {
        m_filpar[idx].loading_speed           = float(m_config->filament_loading_speed.get_at(idx));
        m_filpar[idx].loading_speed_start     = float(m_config->filament_loading_speed_start.get_at(idx));
        m_filpar[idx].unloading_speed         = float(m_config->filament_unloading_speed.get_at(idx));
        m_filpar[idx].unloading_speed_start   = float(m_config->filament_unloading_speed_start.get_at(idx));
        m_filpar[idx].delay                   = float(m_config->filament_toolchange_delay.get_at(idx));
        m_filpar[idx].cooling_moves           = m_config->filament_cooling_moves.get_at(idx);
        m_filpar[idx].cooling_initial_speed   = float(m_config->filament_cooling_initial_speed.get_at(idx));
        m_filpar[idx].cooling_final_speed     = float(m_config->filament_cooling_final_speed.get_at(idx));
        //start skinnydip
        m_filpar[idx].filament_enable_toolchange_temp     = m_config->filament_enable_toolchange_temp.get_at(idx);     // skinnydip
        m_filpar[idx].filament_toolchange_temp            = m_config->filament_toolchange_temp.get_at(idx);            // skinnydip
        m_filpar[idx].filament_enable_toolchange_part_fan = m_config->filament_enable_toolchange_part_fan.get_at(idx); // skinnydip
        m_filpar[idx].filament_toolchange_part_fan_speed  = m_config->filament_toolchange_part_fan_speed.get_at(idx);  // skinnydip
        m_filpar[idx].filament_use_skinnydip              = m_config->filament_use_skinnydip.get_at(idx);              // skinnydip
        m_filpar[idx].filament_use_fast_skinnydip         = m_config->filament_use_fast_skinnydip.get_at(idx);         // skinnydip
        m_filpar[idx].filament_skinnydip_distance         = float(m_config->filament_skinnydip_distance.get_at(idx));  // skinnydip
        m_filpar[idx].filament_melt_zone_pause            = m_config->filament_melt_zone_pause.get_at(idx);            // skinnydip
        m_filpar[idx].filament_cooling_zone_pause         = m_config->filament_cooling_zone_pause.get_at(idx);         // skinnydip
        m_filpar[idx].filament_dip_insertion_speed        = float(m_config->filament_dip_insertion_speed.get_at(idx)); // skinnydip
        m_filpar[idx].filament_dip_extraction_speed       = float(m_config->filament_dip_extraction_speed.get_at(idx));// skinnydip
        //end_skinnydip
    }

    m_filpar[idx].filament_area = float((M_PI/4.f) * pow(m_config->filament_diameter.get_at(idx), 2)); // all extruders are assumed to have the same filament diameter at this point
    float nozzle_diameter = float(m_config->nozzle_diameter.get_at(idx));
    m_filpar[idx].nozzle_diameter = nozzle_diameter; // to be used in future with (non-single) multiextruder MM

    float max_speed = float(m_config->filament_max_speed.get_at(idx));
    if (max_speed > 0.f)
        m_filpar[idx].max_speed = max_speed;
    float max_vol_speed = float(m_config->filament_max_volumetric_speed.get_at(idx));
    if (max_vol_speed!= 0.f)
        m_filpar[idx].max_e_speed = (max_vol_speed / filament_area());

    m_nozzle_diameter = nozzle_diameter; // all extruders are now assumed to have the same diameter
    m_perimeter_width = m_config->wipe_tower_extrusion_width.get_abs_value(nozzle_diameter); // all extruders are now assumed to have the same diameter

    if (m_semm) {
        std::istringstream stream{m_config->filament_ramming_parameters.get_at(idx)};
        float speed = 0.f;
        stream >> m_filpar[idx].ramming_line_width_multiplicator >> m_filpar[idx].ramming_step_multiplicator;
        m_filpar[idx].ramming_line_width_multiplicator /= 100;
        m_filpar[idx].ramming_step_multiplicator /= 100;
        while (stream >> speed)
            m_filpar[idx].ramming_speed.push_back(speed);
        // ramming_speed now contains speeds to be used for every 0.25s piece of the ramming line.
        // This allows to have the ramming flow variable. The 0.25s value is how it is saved in config
        // and the same time step has to be used when the ramming is performed.
    } else {
        // We will use the same variables internally, but the correspondence to the configuration options will be different.
        float vol  = m_config->filament_multitool_ramming_volume.get_at(idx);
        float flow = m_config->filament_multitool_ramming_flow.get_at(idx);
        m_filpar[idx].multitool_ramming = m_config->filament_multitool_ramming.get_at(idx);
        m_filpar[idx].ramming_line_width_multiplicator = 2.;
        m_filpar[idx].ramming_step_multiplicator = 1.;

        // Now the ramming speed vector. In this case it contains just one value (flow).
        // The time is calculated and saved separately. This is here so that the MM ramming
        // is not limited by the 0.25s granularity - it is not possible to create a SEMM-style
        // ramming_speed vector that would respect both the volume and flow (because of 
        // rounding issues with small volumes and high flow).
        m_filpar[idx].ramming_speed.push_back(flow);
        m_filpar[idx].multitool_ramming_time = vol/flow;
    }

    m_used_filament_length.resize(std::max(m_used_filament_length.size(), idx + 1)); // makes sure that the vector is big enough so we don't have to check later
}



// Returns gcode to prime the nozzles at the front edge of the print bed.
std::vector<WipeTower::ToolChangeResult> WipeTower::prime(
	// print_z of the first layer.
	float 						first_layer_height, 
	// Extruder indices, in the order to be primed. The last extruder will later print the wipe tower brim, print brim and the object.
	const std::vector<uint16_t> &tools,
	// If true, the last priming are will be the same as the other priming areas, and the rest of the wipe will be performed inside the wipe tower.
	// If false, the last priming are will be large enough to wipe the last extruder sufficiently.
    bool 						/*last_wipe_inside_wipe_tower*/)
{
	this->set_layer(first_layer_height, first_layer_height, tools.size(), true, false);
	m_current_tool 		= tools.front();
    
    // The Prusa i3 MK2 has a working space of [0, -2.2] to [250, 210].
    // Due to the XYZ calibration, this working space may shrink slightly from all directions,
    // therefore the homing position is shifted inside the bed by 0.2 in the firmware to [0.2, -2.0].
//	box_coordinates cleaning_box(xy(0.5f, - 1.5f), m_wipe_tower_width, wipe_area);

    float prime_section_width = std::min((m_bed_shape == CircularBed ? 0.45f : 0.9f) * m_bed_width / tools.size(), 60.f);
    box_coordinates cleaning_box(Vec2f(0.02f * m_bed_width, 0.01f + m_perimeter_width/2.f), prime_section_width, 100.f);
    if (m_bed_shape == CircularBed) {
        cleaning_box = box_coordinates(Vec2f(0.f, 0.f), prime_section_width, 100.f);
        float total_width_half = tools.size() * prime_section_width / 2.f;
        if (m_config->priming_position.value == Vec2d(0,0)) {
            cleaning_box.translate(-total_width_half, -std::sqrt(std::max(0.f, std::pow(m_bed_width/2, 2.f) - std::pow(1.05f * total_width_half, 2.f))));
        } else {
            cleaning_box.translate(m_config->priming_position.value.x(), m_config->priming_position.value.y());
        }
    } else {
        if (m_config->priming_position.value == Vec2d(0,0)) {
            cleaning_box.translate(m_bed_bottom_left);
        } else {
            cleaning_box.translate(m_config->priming_position.value.x(), m_config->priming_position.value.y());
        }
    }
    std::vector<ToolChangeResult> results;

    // Iterate over all priming toolchanges and push respective ToolChangeResults into results vector.
    for (size_t idx_tool = 0; idx_tool < tools.size(); ++ idx_tool) {
        size_t old_tool = m_current_tool;

        WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_config->tool_name.get_values(), m_filpar);
        writer.set_extrusion_flow(m_extrusion_flow)
              .set_z(m_z_pos + m_config->z_offset.value)
              .set_initial_tool(m_current_tool);

        // This is the first toolchange - initiate priming
        if (idx_tool == 0) {
            writer.append(";--------------------\n"
                          "; CP PRIMING START\n")
                  .append(";--------------------\n")
                  .speed_override_backup()
                  .speed_override(100)
                  .set_initial_position(Vec2f::Zero())	// Always move to the starting position
                  .travel(cleaning_box.ld, 7200);
            if (m_set_extruder_trimpot)
                writer.set_extruder_trimpot(750); 			// Increase the extruder driver current to allow fast ramming.
        }
        else
            writer.set_initial_position(results.back().end_pos);


        uint16_t tool = tools[idx_tool];
        m_left_to_right = true;
        toolchange_Change(writer, tool); // Select the tool, set a speed override for soluble and flex materials.
        writer.speed_override(int(100 * get_speed_reduction()));
        toolchange_Load(writer, cleaning_box); // Prime the tool.
        if (idx_tool + 1 == tools.size()) {
            // Last tool should not be unloaded, but it should be wiped enough to become of a pure color.
            toolchange_Wipe(writer, cleaning_box, wipe_volumes[tools[idx_tool-1]][tool]);
        } else {
            // Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
            //writer.travel(writer.x(), writer.y() + m_perimeter_width, 7200);
            toolchange_Wipe(writer, cleaning_box , 20.f);
            box_coordinates box = cleaning_box;
            box.translate(0.f, writer.y() - cleaning_box.ld.y() + m_perimeter_width);
            toolchange_Unload(writer, box , m_filpar[tools[idx_tool + 1]].first_layer_temperature, idx_tool + 1);
            cleaning_box.translate(prime_section_width, 0.f);
            writer.travel(cleaning_box.ld, 7200);
        }
        ++ m_num_tool_changes;


        // Ask our writer about how much material was consumed:
        if (m_current_tool < m_used_filament_length.size())
            m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

        // This is the last priming toolchange - finish priming
        if (idx_tool+1 == tools.size()) {
            // Reset the extruder current to a normal value.
            if (m_set_extruder_trimpot)
                writer.set_extruder_trimpot(550);
            writer.speed_override_restore()
                  .feedrate(m_travel_speed * 60.f)
                  .flush_planner_queue()
                  .reset_extruder()
                  .append("; CP PRIMING END\n"
                          ";------------------\n"
                          "\n\n");
        }

        results.emplace_back(construct_tcr(writer, true, old_tool));
    }

    m_old_temperature = -1; // If the priming is turned off in config, the temperature changing commands will not actually appear
                            // in the output gcode - we should not remember emitting them (we will output them twice in the worst case)

	return results;
}

WipeTower::ToolChangeResult WipeTower::tool_change(size_t tool)
{
    size_t old_tool = m_current_tool;

    float wipe_area = 0.f;
	float wipe_volume = 0.f;
	
	// Finds this toolchange info
	if (tool != (unsigned int)(-1))
	{
		for (const auto &b : m_layer_info->tool_changes)
			if ( b.new_tool == tool ) {
                wipe_volume = b.wipe_volume;
				wipe_area = b.required_depth * m_layer_info->extra_spacing;
				break;
			}
	}
	else {
		// Otherwise we are going to Unload only. And m_layer_info would be invalid.
	}

    box_coordinates cleaning_box(
		Vec2f(m_perimeter_width / 2.f, m_perimeter_width / 2.f),
		m_wipe_tower_width - m_perimeter_width,
        (tool != (unsigned int)(-1) ? wipe_area+m_depth_traversed-0.5f*m_perimeter_width
                                    : m_wipe_tower_depth-m_perimeter_width));

	WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_config->tool_name.get_values(), m_filpar);
	writer.set_extrusion_flow(m_extrusion_flow)
		.set_z(m_z_pos + m_config->z_offset.value)
		.set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift + (tool!=(unsigned int)(-1) && (m_current_shape == SHAPE_REVERSED) ? m_layer_info->depth - m_layer_info->toolchanges_depth(): 0.f))
		.append(";--------------------\n"
				"; CP TOOLCHANGE START\n");

    if (tool != (unsigned)(-1)) {
        writer.comment_with_value(" toolchange #", m_num_tool_changes + 1); // the number is zero-based
        writer.append(std::string("; material : " + (m_current_tool < m_filpar.size() ? m_filpar[m_current_tool].material : "(NONE)") + " -> " + m_filpar[tool].material + "\n").c_str())
              .append(";--------------------\n");
    }

    writer.speed_override_backup();
	writer.speed_override(100);

	Vec2f initial_position = cleaning_box.ld + Vec2f(0.f, m_depth_traversed);
    writer.set_initial_position(initial_position, m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    // Increase the extruder driver current to allow fast ramming.
	if (m_set_extruder_trimpot)
		writer.set_extruder_trimpot(750);

    // Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
    if (tool != (unsigned int)-1){ 			// This is not the last change.
        toolchange_Unload(writer, cleaning_box,
                          is_first_layer() ? m_filpar[tool].first_layer_temperature : m_filpar[tool].temperature, tool);
        toolchange_Change(writer, tool); // Change the tool, set a speed override for soluble and flex materials.
        writer.speed_override(int(100 * get_speed_reduction()));
        toolchange_Load(writer, cleaning_box);
        writer.travel(writer.x(), writer.y()-m_perimeter_width); // cooling and loading were done a bit down the road
        writer.speed_override(int(100 * get_speed_reduction()));
        toolchange_Wipe(writer, cleaning_box, wipe_volume);     // Wipe the newly loaded filament until the end of the assigned wipe area.
        ++ m_num_tool_changes;
    } else
        toolchange_Unload(writer, cleaning_box, m_filpar[m_current_tool].temperature, m_current_tool);

    m_depth_traversed += wipe_area;

	if (m_set_extruder_trimpot)
		writer.set_extruder_trimpot(550);    // Reset the extruder current to a normal value.
	writer.speed_override_restore();
    writer.feedrate(m_travel_speed * 60.f)
          .flush_planner_queue()
          .reset_extruder()
          .append("; CP TOOLCHANGE END\n"
                  ";------------------\n"
                  "\n\n");

    // Ask our writer about how much material was consumed:
    if (m_current_tool < m_used_filament_length.size())
        m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

   return construct_tcr(writer, false, old_tool);
}


// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
void WipeTower::toolchange_Unload(
	WipeTowerWriter &writer,
	const box_coordinates 	&cleaning_box,
	const int 				 new_temperature,
    const size_t             next_tool)
{
	float xl = cleaning_box.ld.x() + 1.f * m_perimeter_width;
	float xr = cleaning_box.rd.x() - 1.f * m_perimeter_width;

	const float line_width = m_perimeter_width * m_filpar[m_current_tool].ramming_line_width_multiplicator;       // desired ramming line thickness
	const float y_step = line_width * m_filpar[m_current_tool].ramming_step_multiplicator * m_extra_spacing; // spacing between lines in mm

    const Vec2f ramming_start_pos = Vec2f(xl, cleaning_box.ld.y() + m_depth_traversed + y_step/2.f);

    writer.append("; CP TOOLCHANGE UNLOAD\n")
          .change_analyzer_line_width(line_width);

	unsigned i = 0;										// iterates through ramming_speed
	m_left_to_right = true;								// current direction of ramming
	float remaining = xr - xl ;							// keeps track of distance to the next turnaround
	float e_done = 0;									// measures E move done from each segment   

    const bool do_ramming = m_semm || m_filpar[m_current_tool].multitool_ramming;
    bool pa_enabled = true;

    if (do_ramming) {
        writer.travel(ramming_start_pos); // move to starting position
        writer.disable_linear_advance();
        pa_enabled = false;
    }
    else
        writer.set_position(ramming_start_pos);

    // if the ending point of the ram would end up in mid air, align it with the end of the wipe tower:
    if (do_ramming && (m_layer_info > m_plan.begin() && m_layer_info < m_plan.end() && (m_layer_info-1!=m_plan.begin() || !m_adhesion ))) {

        // this is y of the center of previous sparse infill border
        float sparse_beginning_y = 0.f;
        if (m_current_shape == SHAPE_REVERSED)
            sparse_beginning_y += ((m_layer_info-1)->depth - (m_layer_info-1)->toolchanges_depth())
                                      - ((m_layer_info)->depth-(m_layer_info)->toolchanges_depth()) ;
        else
            sparse_beginning_y += (m_layer_info-1)->toolchanges_depth() + m_perimeter_width;

        float sum_of_depths = 0.f;
        for (const auto& tch : m_layer_info->tool_changes) {  // let's find this toolchange
            if (tch.old_tool == m_current_tool) {
                sum_of_depths += tch.ramming_depth;
                float ramming_end_y = sum_of_depths;
                ramming_end_y -= (y_step/m_extra_spacing-m_perimeter_width) / 2.f;   // center of final ramming line

                if ( (m_current_shape == SHAPE_REVERSED   && ramming_end_y < sparse_beginning_y - 0.5f*m_perimeter_width  ) ||
                     (m_current_shape == SHAPE_NORMAL && ramming_end_y > sparse_beginning_y + 0.5f*m_perimeter_width  )  )
                {
                    writer.extrude(xl + tch.first_wipe_line-1.f*m_perimeter_width,writer.y());
                    remaining -= tch.first_wipe_line-1.f*m_perimeter_width;
                }
                break;
            }
            sum_of_depths += tch.required_depth;
        }
    }
    

    // Disable linear/pressure advance for ramming, as it can mess up the ramming procedure
    if (pa_enabled && i < m_filpar[m_current_tool].ramming_speed.size()) {
        writer.disable_linear_advance();
        pa_enabled = false;
    }

    // now the ramming itself:
    while (do_ramming && i < m_filpar[m_current_tool].ramming_speed.size())
    {
        // The time step is different for SEMM ramming and the MM ramming. See comments in set_extruder() for details.
        const float time_step = m_semm ? 0.25f : m_filpar[m_current_tool].multitool_ramming_time;

        const float x = volume_to_length(m_filpar[m_current_tool].ramming_speed[i] * time_step, line_width, m_layer_height);
        const float e = m_filpar[m_current_tool].ramming_speed[i] * time_step / filament_area(); // transform volume per sec to E move;
        const float dist = std::min(x - e_done, remaining);		  // distance to travel for either the next time_step, or to the next turnaround
        const float actual_time = dist/x * time_step;
        writer.ram(writer.x(), writer.x() + (m_left_to_right ? 1.f : -1.f) * dist, 0.f, 0.f, e * (dist / x), dist / (actual_time / 60.f));
        remaining -= dist;

		if (remaining < WT_EPSILON)	{ // we reached a turning point
			writer.travel(writer.x(), writer.y() + y_step, 7200);
			m_left_to_right = !m_left_to_right;
			remaining = xr - xl;
		}
		e_done += dist; // subtract what was actually done
		if (e_done > x - WT_EPSILON) { // current segment finished
			++i;
			e_done = 0;
		}
	}
	Vec2f end_of_ramming(writer.x(),writer.y());
    writer.change_analyzer_line_width(m_perimeter_width);   // so the next lines are not affected by ramming_line_width_multiplier

    // Retraction:
    float old_x = writer.x();
    float turning_point = (!m_left_to_right ? xl : xr );
    if (m_semm && (m_cooling_tube_retraction != 0 || m_cooling_tube_length != 0)) {
        
        // set toolchange temperature just prior to filament being extracted from melt zone and wait for set point
        //(SKINNYDIP--normal mode only)
        if ((m_filpar[m_current_tool].filament_enable_toolchange_temp == true) && 
                (m_filpar[m_current_tool].filament_use_fast_skinnydip == false)) {
            writer.wait_for_toolchange_temp(m_filpar[m_current_tool].filament_toolchange_temp, 
                                            m_filpar[m_current_tool].filament_enable_toolchange_part_fan,
                                            m_filpar[m_current_tool].filament_toolchange_part_fan_speed,
                                            false); //normal mode
        }

        float total_retraction_distance = m_cooling_tube_retraction + m_cooling_tube_length/2.f - 15.f; // the 15mm is reserved for the first part after ramming
        writer.suppress_preview()
              .retract(15.f, m_filpar[m_current_tool].unloading_speed_start * 60.f) // feedrate 5000mm/min = 83mm/s
              .retract(0.70f * total_retraction_distance, 1.0f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .retract(0.20f * total_retraction_distance, 0.5f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .retract(0.10f * total_retraction_distance, 0.3f * m_filpar[m_current_tool].unloading_speed * 60.f)
              .resume_preview();
    }

    // Wipe tower should only change temperature with single extruder MM. Otherwise, all temperatures should
    // be already set and there is no need to change anything. Also, the temperature could be changed
    // for wrong extruder.
    // additionally, we are suppressing this temperature change if skinnydip fast mode is active because it will happen later
    //if no toolchange temperatures are being used, just set the temperature of the next material.
    if (m_semm && (m_filpar[m_current_tool].filament_enable_toolchange_temp == false)){  
        if (new_temperature != 0 && (new_temperature != m_old_temperature || is_first_layer())) {     // Set the extruder temperature, but don't wait.
            // If the required temperature is the same as last time, don't emit the M104 again (if user adjusted the value, it would be reset)
            // However, always change temperatures on the first layer (this is to avoid issues with priming lines turned off).
            writer.set_extruder_temp(new_temperature, next_tool, false);
                m_old_temperature = new_temperature;
        }
    }
    //otherwise, if toolchange temperature changes are on and in normal mode, return to the previously set temperature 
    else if (m_semm && (m_filpar[m_current_tool].filament_enable_toolchange_temp && (m_filpar[m_current_tool].filament_use_fast_skinnydip == false))) {
        if (new_temperature != 0)
            writer.restore_pre_toolchange_temp(new_temperature, false); //skinnydip normal mode only
        else
            writer.restore_pre_toolchange_temp(m_filpar[m_current_tool].temperature, false); //skinnydip normal mode only
    }

    // Cooling:
    if (m_semm) {
    //begin to cool extruder to toolchange temperature during cooling moves (only if using skinnydip fast mode)
            if ((m_filpar[m_current_tool].filament_enable_toolchange_temp == true) && 
                        (m_filpar[m_current_tool].filament_use_fast_skinnydip == true)) {
                    writer.begin_toolchange_temp(m_filpar[m_current_tool].filament_toolchange_temp, true);    //skinnydip fast mode only
            }
    }
 

    // Generate Cooling Moves
    const int& number_of_moves = m_filpar[m_current_tool].cooling_moves;
    if (m_semm && number_of_moves > 0) {
        const float& initial_speed = m_filpar[m_current_tool].cooling_initial_speed;
        const float& final_speed   = m_filpar[m_current_tool].cooling_final_speed;

        float speed_inc = (final_speed - initial_speed) / (2.f * number_of_moves - 1.f);

        writer.suppress_preview()
              .travel(writer.x(), writer.y() + y_step);
        old_x = writer.x();
        turning_point = xr-old_x > old_x-xl ? xr : xl;
        for (int i=0; i<number_of_moves; ++i) {
            float speed = initial_speed + speed_inc * 2*i;
            writer.load_move_x_advanced(turning_point, m_cooling_tube_length, speed);
            speed += speed_inc;
            writer.load_move_x_advanced(old_x, -m_cooling_tube_length, speed);
        }
    }
    
    //BEGIN SKINNYDIP SECTION
    if (m_semm) {
    //wait for extruder to reach toolchange temperature after cooling moves complete (SKINNYDIP--fast mode only)
        if ((m_filpar[m_current_tool].filament_enable_toolchange_temp == true) && (m_filpar[m_current_tool].filament_use_fast_skinnydip == true)) {
            writer.wait_for_toolchange_temp(m_filpar[m_current_tool].filament_toolchange_temp, 
                                            m_filpar[m_current_tool].filament_enable_toolchange_part_fan,
                                            m_filpar[m_current_tool].filament_toolchange_part_fan_speed,
                                            true); //fast mode
        }
    }

    if (m_semm) {
        //Generate a skinnydip move
        if (m_filpar[m_current_tool].filament_use_skinnydip == true) {
        writer.suppress_preview()
              .skinnydip_move(m_filpar[m_current_tool].filament_skinnydip_distance, 
                                m_filpar[m_current_tool].filament_dip_insertion_speed,
                                m_filpar[m_current_tool].filament_melt_zone_pause,
                                m_filpar[m_current_tool].filament_dip_extraction_speed,
                                m_filpar[m_current_tool].filament_cooling_zone_pause)
              .resume_preview();
        }
    }

    //ensure that proper hotend temperature is restored after skinnydip has finished meddling, 
    //honor first layer temperature settings if applicable 

    if ((!is_first_layer()) && (m_filpar[m_current_tool].filament_enable_toolchange_temp == true) &&
            (m_filpar[m_current_tool].filament_use_fast_skinnydip == true)) {
        //begin to restore pre toolchange temp after skinnydip move completes without delay  (SKINNYDIP--fast method)
        if (new_temperature != 0)
            writer.restore_pre_toolchange_temp(new_temperature, true); //skinnydip fast mode only
        else
            writer.restore_pre_toolchange_temp(m_filpar[m_current_tool].temperature, true); //skinnydip fast mode only
    }
    //the following temperature change is suppressed if using skinnydip normal mode since it has already happened
    else if (is_first_layer() && (m_filpar[m_current_tool].filament_enable_toolchange_temp == true) &&
                                            (m_filpar[m_current_tool].filament_use_fast_skinnydip == true)){
        // obey first layer temperature setting
        if (new_temperature != 0 && (new_temperature != m_old_temperature || is_first_layer()) ) {
            writer.restore_pre_toolchange_temp(new_temperature, true); //skinnydip fast mode only
            m_old_temperature = new_temperature;
        }
    }

    if (m_semm) {
        // let's wait is necessary:
        writer.wait(m_filpar[m_current_tool].delay);
        // we should be at the beginning of the cooling tube again - let's move to parking position:
        writer.retract(-m_cooling_tube_length / 2.f + m_parking_pos_retraction - m_cooling_tube_retraction, 2000);
    }

	// this is to align ramming and future wiping extrusions, so the future y-steps can be uniform from the start:
    // the perimeter_width will later be subtracted, it is there to not load while moving over just extruded material
    Vec2f pos = Vec2f(end_of_ramming.x(), end_of_ramming.y() + (y_step/m_extra_spacing-m_perimeter_width) / 2.f + m_perimeter_width);
    if (do_ramming)
        writer.travel(pos, 2400.f);
    else
        writer.set_position(pos);

    if (!pa_enabled) {
        writer.enable_linear_advance();
    }

	writer.resume_preview()
		  .flush_planner_queue();
}

// Change the tool, set a speed override for soluble and flex materials.
void WipeTower::toolchange_Change(
	WipeTowerWriter &writer,
    const size_t 	new_tool)
{
    // Ask the writer about how much of the old filament we consumed:
    if (m_current_tool < m_used_filament_length.size())
    	m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();

    // This is where we want to place the custom gcodes. We will use placeholders for this.
    // These will be substituted by the actual gcodes when the gcode is generated.
    //writer.append("[end_filament_gcode]\n");
    writer.append("[toolchange_gcode_from_wipe_tower_generator]\n");

    // Travel to where we assume we are. Custom toolchange or some special T code handling (parking extruder etc)
    // gcode could have left the extruder somewhere, we cannot just start extruding. We should also inform the
    // postprocessor that we absolutely want to have this in the gcode, even if it thought it is the same as before.
    Vec2f current_pos = writer.pos_rotated();
    writer.feedrate(m_travel_speed * 60.f) // see https://github.com/prusa3d/PrusaSlicer/issues/5483
          .append(std::string("G1 X") + Slic3r::float_to_string_decimal_point(current_pos.x())
                             +  " Y"  + Slic3r::float_to_string_decimal_point(current_pos.y())
                             + never_skip_tag() + "\n"
    );

    writer.append("[deretraction_from_wipe_tower_generator]\n");

    // The toolchange Tn command will be inserted later, only in case that the user does
    // not provide a custom toolchange gcode.
	writer.set_tool(new_tool); // This outputs nothing, the writer just needs to know the tool has changed.
    //writer.append("[start_filament_gcode]\n");

    //ensure the Z is at the right position
    writer.append("G1 Z{layer_z}" + never_skip_tag() + "\n");

	writer.flush_planner_queue();
	m_current_tool = new_tool;
}

void WipeTower::toolchange_Load(
	WipeTowerWriter &writer,
	const box_coordinates  &cleaning_box)
{
    if (m_semm && (m_parking_pos_retraction != 0 || m_extra_loading_move != 0)) {
        float xl = cleaning_box.ld.x() + m_perimeter_width * 0.75f;
        float xr = cleaning_box.rd.x() - m_perimeter_width * 0.75f;
        float oldx = writer.x();	// the nozzle is in place to do the first wiping moves, we will remember the position

        // Load the filament while moving left / right, so the excess material will not create a blob at a single position.
        float turning_point = ( oldx-xl < xr-oldx ? xr : xl );
        float edist = m_parking_pos_retraction+m_extra_loading_move;

        writer.append("; CP TOOLCHANGE LOAD\n")
              .suppress_preview()
              .load(0.2f * edist, 60.f * m_filpar[m_current_tool].loading_speed_start)
              .load_move_x_advanced(turning_point, 0.7f * edist,        m_filpar[m_current_tool].loading_speed)  // Fast phase
              .load_move_x_advanced(oldx,          0.1f * edist, 0.1f * m_filpar[m_current_tool].loading_speed)  // Super slow*/

              .travel(oldx, writer.y()) // in case last move was shortened to limit x feedrate
              .resume_preview();

        // Reset the extruder current to the normal value.
        if (m_set_extruder_trimpot)
            writer.set_extruder_trimpot(550);
    }
}

float WipeTower::get_speed_reduction() const
{
    float speed_override = m_config->filament_max_wipe_tower_speed.get_at(m_current_tool) / 100.f;
    if (speed_override <= 0) {
        speed_override = 1.f;
        std::string material_upp = boost::algorithm::to_upper_copy(m_filpar[m_current_tool].material);
        if (m_filpar[m_current_tool].is_soluble) speed_override = 0.35f;
        if (material_upp == "PVA") speed_override = (m_z_pos < 0.80f) ? 0.60f : 0.80f;
        if (material_upp == "SCAFF") speed_override = 0.35f;
        if (material_upp == "FLEX") speed_override = 0.35f;
    }
    return speed_override;
}

// Wipe the newly loaded filament until the end of the assigned wipe area.
void WipeTower::toolchange_Wipe(
	WipeTowerWriter &writer,
	const box_coordinates  &cleaning_box,
	float wipe_volume)
{
	// Increase flow on first layer, slow down print.
    writer.set_extrusion_flow(m_extrusion_flow * (is_first_layer() ? 1.18f : 1.f))
		  .append("; CP TOOLCHANGE WIPE\n");
	float speed_factor = 1.f;
	const float& xl = cleaning_box.ld.x();
	const float& xr = cleaning_box.rd.x();

    // Speed override for the material. Go slow for flex and soluble materials.
    speed_factor *= get_speed_reduction();
    speed_factor *= 60.f;  // mm/s -> mm/min

	// Variables x_to_wipe and traversed_x are here to be able to make sure it always wipes at least
    //   the ordered volume, even if it means violating the box. This can later be removed and simply
    // wipe until the end of the assigned area.

	float x_to_wipe = volume_to_length(wipe_volume, m_perimeter_width, m_layer_height) * (is_first_layer() ? m_extra_spacing : 1.f);
	float dy = (is_first_layer() ? 1.f : m_extra_spacing) * m_perimeter_width; // Don't use the extra spacing for the first layer.
    // All the calculations in all other places take the spacing into account for all the layers.

    // all speed in mm/s, will converted in mm/min when muliplied by speed_factor
    float max_speed = std::numeric_limits<float>::max();
    //stay in limits
    if (this->m_config->filament_max_speed.get_at(this->m_current_tool) > 0) {
        max_speed = float(this->m_config->filament_max_speed.get_at(this->m_current_tool));
    }
    float target_speed = m_speed; //mm/s
    if (is_first_layer() && m_first_layer_speed > 0)
        target_speed = m_first_layer_speed;
    if (target_speed <= 0)
        target_speed = m_infill_speed;
    target_speed = std::min(max_speed, target_speed);
    float wipe_speed = std::min(max_speed, float(m_config->wipe_tower_wipe_starting_speed.get_abs_value(target_speed)));
    if (wipe_speed <= 0) {
        wipe_speed = target_speed;
    }

    // if there is less than 2.5*m_perimeter_width to the edge, advance straightaway (there is likely a blob anyway)
    if ((m_left_to_right ? xr-writer.x() : writer.x()-xl) < 2.5f*m_perimeter_width) {
        writer.travel((m_left_to_right ? xr-m_perimeter_width : xl+m_perimeter_width),writer.y()+dy);
        m_left_to_right = !m_left_to_right;
    }
    
    // now the wiping itself:
	for (int i = 0; true; ++i)	{
		if (i!=0 && this->m_config->filament_max_speed.get_at(this->m_current_tool) > 0) {
            if      (wipe_speed < 0.34f * target_speed) wipe_speed = 0.375f * target_speed;
            else if (wipe_speed < 0.377 * target_speed) wipe_speed = 0.458f * target_speed;
            else if (wipe_speed < 0.46f * target_speed) wipe_speed = 0.875f * target_speed;
            else wipe_speed = std::min(target_speed, wipe_speed + 50.f);
		}

		float traversed_x = writer.x();
		if (m_left_to_right)
            writer.extrude(xr - (i % 4 == 0 ? 0 : 1.5f*m_perimeter_width), writer.y(), wipe_speed * speed_factor);
		else
            writer.extrude(xl + (i % 4 == 1 ? 0 : 1.5f*m_perimeter_width), writer.y(), wipe_speed * speed_factor);

        if (writer.y()+float(EPSILON) > cleaning_box.lu.y()-0.5f*m_perimeter_width)
            break;		// in case next line would not fit

		traversed_x -= writer.x();
        x_to_wipe -= std::abs(traversed_x);
		if (x_to_wipe < WT_EPSILON) {
            writer.travel(m_left_to_right ? xl + 1.5f*m_perimeter_width : xr - 1.5f*m_perimeter_width, writer.y(), 7200);
			break;
		}
		// stepping to the next line:
        writer.extrude(writer.x() + (i % 4 == 0 ? -1.f : (i % 4 == 1 ? 1.f : 0.f)) * 1.5f*m_perimeter_width, writer.y() + dy);
		m_left_to_right = !m_left_to_right;
	}

    // We may be going back to the model - wipe the nozzle. If this is followed
    // by finish_layer, this wipe path will be overwritten.
    writer.add_wipe_point(writer.x(), writer.y())
          .add_wipe_point(writer.x(), writer.y() - dy)
          .add_wipe_point(! m_left_to_right ? m_wipe_tower_width : 0.f, writer.y() - dy);

    if (m_layer_info != m_plan.end() && m_current_tool != m_layer_info->tool_changes.back().new_tool)
        m_left_to_right = !m_left_to_right;

    writer.set_extrusion_flow(m_extrusion_flow); // Reset the extrusion flow.
}




WipeTower::ToolChangeResult WipeTower::finish_layer()
{
	assert(! this->layer_finished());
    m_current_layer_finished = true;

    size_t old_tool = m_current_tool;

	WipeTowerWriter writer(m_layer_height, m_perimeter_width, m_gcode_flavor, m_config->tool_name.get_values(), m_filpar);
	writer.set_extrusion_flow(m_extrusion_flow)
		.set_z(m_z_pos + m_config->z_offset.value)
		.set_initial_tool(m_current_tool)
        .set_y_shift(m_y_shift - (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f));


	// Slow down on the 1st layer.
    bool first_layer = is_first_layer();
	float speed_factor = 60.f;
    float print_speed = m_speed;
    if (first_layer && m_first_layer_speed > 0)
        print_speed = m_first_layer_speed;
    if (print_speed <= 0)
        print_speed = m_infill_speed;
    speed_factor *= get_speed_reduction();
    float feedrate = m_speed * speed_factor;
	float current_depth = m_layer_info->depth - m_layer_info->toolchanges_depth();
    box_coordinates fill_box(Vec2f(m_perimeter_width, m_layer_info->depth-(current_depth-m_perimeter_width)),
                             m_wipe_tower_width - 2 * m_perimeter_width, current_depth-m_perimeter_width);


    writer.set_initial_position((m_left_to_right ? fill_box.ru : fill_box.lu), // so there is never a diagonal travel
                                 m_wipe_tower_width, m_wipe_tower_depth, m_internal_rotation);

    bool toolchanges_on_layer = m_layer_info->toolchanges_depth() > WT_EPSILON;
    box_coordinates wt_box(Vec2f(0.f, (m_current_shape == SHAPE_REVERSED ? m_layer_info->toolchanges_depth() : 0.f)),
                        m_wipe_tower_width, m_layer_info->depth + m_perimeter_width);

    // inner perimeter of the sparse section, if there is space for it:
    if (fill_box.ru.y() - fill_box.rd.y() > m_perimeter_width - WT_EPSILON)
        writer.rectangle(fill_box.ld, fill_box.rd.x()-fill_box.ld.x(), fill_box.ru.y()-fill_box.rd.y(), feedrate);

    // we are in one of the corners, travel to ld along the perimeter:
    if (writer.x() > fill_box.ld.x()+EPSILON) writer.travel(fill_box.ld.x(),writer.y());
    if (writer.y() > fill_box.ld.y()+EPSILON) writer.travel(writer.x(),fill_box.ld.y());

    // Extrude infill to support the material to be printed above.
    const float dy = (fill_box.lu.y() - fill_box.ld.y() - m_perimeter_width);
    float left = fill_box.lu.x() + 2*m_perimeter_width;
    float right = fill_box.ru.x() - 2 * m_perimeter_width;
    if (dy > m_perimeter_width)
    {
        writer.travel(fill_box.ld + Vec2f(m_perimeter_width * 2, 0.f))
              .append(";--------------------\n"
                      "; CP EMPTY GRID START\n")
              .comment_with_value(" layer #", m_num_layer_changes + 1);

        // Is there a soluble filament wiped/rammed at the next layer?
        // If so, the infill should not be sparse.
        bool solid_infill = m_layer_info+1 == m_plan.end()
                          ? false
                          : std::any_of((m_layer_info+1)->tool_changes.begin(),
                                        (m_layer_info+1)->tool_changes.end(),
                                   [this](const WipeTowerInfo::ToolChange& tch) {
                                       return m_filpar[tch.new_tool].is_soluble
                                           || m_filpar[tch.old_tool].is_soluble;
                                   });
        solid_infill |= first_layer && m_adhesion;

        if (solid_infill) {
            float sparse_factor = 1.5f; // 1=solid, 2=every other line, etc.
            if (first_layer) { // the infill should touch perimeters
                left  -= m_perimeter_width;
                right += m_perimeter_width;
                sparse_factor = 1.f;
            }
            float y = fill_box.ld.y() + m_perimeter_width;
            int n = dy / (m_perimeter_width * sparse_factor);
            float spacing = (dy-m_perimeter_width)/(n-1);
            int i=0;
            for (i=0; i<n; ++i) {
                writer.extrude(writer.x(), y, feedrate)
                      .extrude(i%2 ? left : right, y);
                y = y + spacing;
            }
            writer.extrude(writer.x(), fill_box.lu.y());
        } else {
            // Extrude an inverse U at the left of the region and the sparse infill.
            writer.extrude(fill_box.lu + Vec2f(m_perimeter_width * 2, 0.f), feedrate);

            const int n = 1+int((right-left)/m_bridging);
            const float dx = (right-left)/n;
            for (int i=1;i<=n;++i) {
                float x=left+dx*i;
                writer.travel(x,writer.y());
                writer.extrude(x,i%2 ? fill_box.rd.y() : fill_box.ru.y());
            }
        }

        writer.append("; CP EMPTY GRID END\n"
                      ";------------------\n\n\n\n\n\n\n");
    }

    const float spacing = m_perimeter_width - m_layer_height*float(1.-M_PI_4);

    // This block creates the stabilization cone.
    // First define a lambda to draw the rectangle with stabilization.
    auto supported_rectangle = [this, &writer, spacing](const box_coordinates& wt_box, double feedrate, bool infill_cone) -> Polygon {
        const auto [R, support_scale] = get_wipe_tower_cone_base(m_wipe_tower_width, m_wipe_tower_height, m_wipe_tower_depth, m_wipe_tower_cone_angle);

        double z = m_no_sparse_layers ? (m_current_height + m_layer_info->height) : m_layer_info->z; // the former should actually work in both cases, but let's stay on the safe side (the 2.6.0 is close)

        double r = std::tan(Geometry::deg2rad(m_wipe_tower_cone_angle/2.f)) * (m_wipe_tower_height - z);
        Vec2f center = (wt_box.lu + wt_box.rd) / 2.;
        double w = wt_box.lu.y() - wt_box.ld.y();
        enum Type {
            Arc,
            Corner,
            ArcStart,
            ArcEnd
        };

        // First generate vector of annotated point which form the boundary.
        std::vector<std::pair<Vec2f, Type>> pts = {{wt_box.ru, Corner}};        
        if (double alpha_start = std::asin((0.5*w)/r); ! std::isnan(alpha_start) && r > 0.5*w+0.01) {
            for (double alpha = alpha_start; alpha < M_PI-alpha_start+0.001; alpha+=(M_PI-2*alpha_start) / 40.)
                pts.emplace_back(Vec2f(center.x() + r*std::cos(alpha)/support_scale, center.y() + r*std::sin(alpha)), alpha == alpha_start ? ArcStart : Arc);
            pts.back().second = ArcEnd;
        }        
        pts.emplace_back(wt_box.lu, Corner);
        pts.emplace_back(wt_box.ld, Corner);
        for (int i=int(pts.size())-3; i>0; --i)
            pts.emplace_back(Vec2f(pts[i].first.x(), 2*center.y()-pts[i].first.y()), i == int(pts.size())-3 ? ArcStart : i == 1 ? ArcEnd : Arc);
        pts.emplace_back(wt_box.rd, Corner);

        // Create a Polygon from the points.
        Polygon poly;
        for (const auto& [pt, tag] : pts)
            poly.points.push_back(Point::new_scale(pt));

        // Prepare polygons to be filled by infill.
        Polylines polylines;
        if (infill_cone && m_wipe_tower_width > 2*spacing && m_wipe_tower_depth > 2*spacing) {
            ExPolygons infill_areas;
            ExPolygon wt_contour(poly);
            Polygon wt_rectangle(Points{Point::new_scale(wt_box.ld), Point::new_scale(wt_box.rd), Point::new_scale(wt_box.ru), Point::new_scale(wt_box.lu)});
            wt_rectangle = offset(wt_rectangle, scale_(-spacing/2.)).front();
            wt_contour = offset_ex(wt_contour, scale_(-spacing/2.)).front();
            infill_areas = diff_ex(wt_contour, wt_rectangle);
            if (infill_areas.size() == 2) {
                ExPolygon& bottom_expoly = infill_areas.front().contour.points.front().y() < infill_areas.back().contour.points.front().y() ? infill_areas[0] : infill_areas[1];
                std::unique_ptr<Fill> filler(Fill::new_from_type(ipMonotonicLines));
                filler->angle = Geometry::deg2rad(45.f);
                FillParams params;
                params.density = 1.f;
                Surface surface(stPosBottom | stDensSolid, bottom_expoly);
                filler->bounding_box = get_extents(bottom_expoly);
                filler->init_spacing(spacing, params);
                polylines = filler->fill_surface(&surface, params);
                if (! polylines.empty()) {
                    if (polylines.front().points.front().x() > polylines.back().points.back().x()) {
                        std::reverse(polylines.begin(), polylines.end());
                        for (Polyline& p : polylines)
                            p.reverse();
                    }
                }
            }
        }

        // Find the closest corner and travel to it.
        int start_i = 0;
        double min_dist = std::numeric_limits<double>::max();
        for (int i=0; i<int(pts.size()); ++i) {
            if (pts[i].second == Corner) {
                double dist = (pts[i].first - Vec2f(writer.x(), writer.y())).squaredNorm();
                if (dist < min_dist) {
                    min_dist = dist;
                    start_i = i;
                }
            }
        }
        writer.travel(pts[start_i].first);

        // Now actually extrude the boundary (and possibly infill):
        int i = start_i+1 == int(pts.size()) ? 0 : start_i + 1;
        while (i != start_i) {
            writer.extrude(pts[i].first, feedrate);
            if (pts[i].second == ArcEnd) {
                // Extrude the infill.
                if (! polylines.empty()) {
                    // Extrude the infill and travel back to where we were.
                    bool mirror = ((pts[i].first.y() - center.y()) * (unscale(polylines.front().points.front()).y() - center.y())) < 0.;
                    for (const Polyline& line : polylines) {
                        writer.travel(center - (mirror ? 1.f : -1.f) * (unscale(line.points.front()).cast<float>() - center));
                        for (size_t i=0; i<line.points.size(); ++i)
                            writer.extrude(center - (mirror ? 1.f : -1.f) * (unscale(line.points[i]).cast<float>() - center));
                    }
                    writer.travel(pts[i].first);
                }
            }
            if (++i == int(pts.size()))
                i = 0;
        }
        writer.extrude(pts[start_i].first, feedrate);
        return poly;
    };

    feedrate = first_layer ? m_first_layer_speed * 60.f : m_perimeter_speed * 60.f;

    // outer contour (always)
    bool infill_cone = first_layer && m_wipe_tower_width > 2*spacing && m_wipe_tower_depth > 2*spacing;
    Polygon poly = supported_rectangle(wt_box, feedrate, infill_cone);


    // brim (first layer only)
    if (first_layer) {
        box_coordinates box = wt_box;
        //same as print::brimflow()
		PrintRegionConfig brim_region_config = *m_region_config;
        brim_region_config.parent = m_object_config;
        const Slic3r::Flow brim_flow = 
             Flow::new_from_config_width(
            frPerimeter,
            *Flow::extrusion_width_option("brim", brim_region_config),
            *Flow::extrusion_spacing_option("brim", brim_region_config),
            (float)m_nozzle_diameter,
            (float)m_layer_height,
            (m_current_tool < m_config->nozzle_diameter.size()) ? m_object_config->get_computed_value("filament_max_overlap", m_current_tool) : 1
        );
        const double spacing = brim_flow.spacing();
        // How many perimeters shall the brim have?
        size_t loops_num = (m_config->wipe_tower_brim_width.get_abs_value(m_nozzle_diameter) + spacing / 2) / spacing;
        

        writer.set_extrusion_flow(brim_flow.mm3_per_mm() / filament_area())
          .set_z(m_z_pos + m_config->z_offset.value) // Let the writer know the current Z position as a base for Z-hop.
          .set_initial_tool(m_current_tool)
          .append(";-------------------------------------\n"
              "; CP WIPE TOWER FIRST LAYER BRIM START\n");

        box.expand(brim_flow.spacing()- brim_flow.width()); // ensure that the brim is attached to the wipe tower
        for (size_t i = 0; i < loops_num; ++i) {
            poly = offset(poly, scale_(spacing)).front();
            int cp = poly.closest_point_index(Point::new_scale(writer.x(), writer.y()));
            writer.travel(unscale(poly.points[cp]).cast<float>());
            for (int i=cp+1; true; ++i ) {
                if (i==int(poly.points.size()))
                    i = 0;
                writer.extrude(unscale(poly.points[i]).cast<float>());
                if (i == cp)
                    break;
            }
        }

        writer.append("; CP WIPE TOWER FIRST LAYER BRIM END\n"
                  ";-----------------------------------\n");
        // Save actual brim width to be later passed to the Print object, which will use it
        // for skirt calculation and pass it to GLCanvas for precise preview box
        m_wipe_tower_brim_width_real = loops_num * spacing;
    }

    // Now prepare future wipe.
    int i = poly.closest_point_index(Point::new_scale(writer.x(), writer.y()));
    writer.add_wipe_point(writer.pos());
    writer.add_wipe_point(unscale(poly.points[i==0 ? int(poly.points.size())-1 : i-1]).cast<float>());

    // Ask our writer about how much material was consumed.
    // Skip this in case the layer is sparse and config option to not print sparse layers is enabled.
    if (! m_no_sparse_layers || toolchanges_on_layer || first_layer) {
        if (m_current_tool < m_used_filament_length.size())
            m_used_filament_length[m_current_tool] += writer.get_and_reset_used_filament_length();
        m_current_height += m_layer_info->height;
    }

    return construct_tcr(writer, false, old_tool);
}

// Static method to get the radius and x-scaling of the stabilizing cone base.
std::pair<double, double> WipeTower::get_wipe_tower_cone_base(double width, double height, double depth, double angle_deg)
{
    double R = std::tan(Geometry::deg2rad(angle_deg/2.)) * height;
    double fake_width = 0.66 * width;
    double diag = std::hypot(fake_width / 2., depth / 2.);
    double support_scale = 1.;
    if (R > diag) {
        double w = fake_width;
        double sin = 0.5 * depth / diag;
        double tan = depth / w;
        double t = (R - diag) * sin;
        support_scale = (w / 2. + t / tan + t * tan) / (w / 2.);
    }
    return std::make_pair(R, support_scale);
}

// Static method to extract wipe_volumes[from][to] from the configuration.
std::vector<std::vector<float>> WipeTower::extract_wipe_volumes(const ConfigBase& config)
{
    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_matrix(cast<float>(config.option<ConfigOptionFloats>("wiping_volumes_matrix")->get_values()));

    // The values shall only be used when SEMM is enabled. The purging for other printers
    // is determined by filament_minimal_purge_on_wipe_tower.
    if (! config.option("single_extruder_multi_material")->get_bool())
        std::fill(wiping_matrix.begin(), wiping_matrix.end(), 0.f);

    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    const unsigned int number_of_extruders = (unsigned int)(sqrt(wiping_matrix.size())+EPSILON);
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(wiping_matrix.begin()+i*number_of_extruders, wiping_matrix.begin()+(i+1)*number_of_extruders));

    // Also include filament_minimal_purge_on_wipe_tower. This is needed for the preview.
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        for (unsigned int j = 0; j<number_of_extruders; ++j)
            wipe_volumes[i][j] = std::max<float>(wipe_volumes[i][j], config.option("filament_minimal_purge_on_wipe_tower")->get_float(j));

    return wipe_volumes;
}

// Appends a toolchange into m_plan and calculates neccessary depth of the corresponding box
void WipeTower::plan_toolchange(float z_par, float layer_height_par, uint16_t old_tool,
                                uint16_t new_tool, float wipe_volume)
{
	assert(m_plan.empty() || m_plan.back().z <= z_par + WT_EPSILON);	// refuses to add a layer below the last one

	if (m_plan.empty() || m_plan.back().z + WT_EPSILON < z_par) // if we moved to a new layer, we'll add it to m_plan first
		m_plan.push_back(WipeTowerInfo(z_par, layer_height_par));

    if (m_first_layer_idx == size_t(-1) && (! m_no_sparse_layers || old_tool != new_tool || m_plan.size() == 1))
        m_first_layer_idx = m_plan.size() - 1;

    if (old_tool == new_tool)	// new layer without toolchanges - we are done
        return;

	// this is an actual toolchange - let's calculate depth to reserve on the wipe tower
    float depth = 0.f;
	float width = m_wipe_tower_width - 3*m_perimeter_width; 
	float length_to_extrude = volume_to_length(0.25f * std::accumulate(m_filpar[old_tool].ramming_speed.begin(), m_filpar[old_tool].ramming_speed.end(), 0.f),
										m_perimeter_width * m_filpar[old_tool].ramming_line_width_multiplicator,
										layer_height_par);
	depth = (int(length_to_extrude / width) + 1) * (m_perimeter_width * m_filpar[old_tool].ramming_line_width_multiplicator * m_filpar[old_tool].ramming_step_multiplicator);
    float ramming_depth = depth;
    length_to_extrude = width*((length_to_extrude / width)-int(length_to_extrude / width)) - width;
    float first_wipe_line = -length_to_extrude;
    length_to_extrude += volume_to_length(wipe_volume, m_perimeter_width, layer_height_par);
    length_to_extrude = std::max(length_to_extrude,0.f);

	depth += (int(length_to_extrude / width) + 1) * m_perimeter_width;
	depth *= m_extra_spacing;

	m_plan.back().tool_changes.push_back(WipeTowerInfo::ToolChange(old_tool, new_tool, depth, ramming_depth, first_wipe_line, wipe_volume));
}



void WipeTower::plan_tower()
{
	// Calculate m_wipe_tower_depth (maximum depth for all the layers) and propagate depths downwards
	m_wipe_tower_depth = 0.f;
	for (auto& layer : m_plan)
		layer.depth = 0.f;
    m_wipe_tower_height = m_plan.empty() ? 0.f : m_plan.back().z;
    m_current_height = 0.f;
	
    for (int layer_index = int(m_plan.size()) - 1; layer_index >= 0; --layer_index)
	{
		float this_layer_depth = std::max(m_plan[layer_index].depth, m_plan[layer_index].toolchanges_depth());
		m_plan[layer_index].depth = this_layer_depth;
		
		if (this_layer_depth > m_wipe_tower_depth - m_perimeter_width)
			m_wipe_tower_depth = this_layer_depth + m_perimeter_width;

		for (int i = layer_index - 1; i >= 0 ; i--)
		{
			if (m_plan[i].depth - this_layer_depth < 2*m_perimeter_width )
				m_plan[i].depth = this_layer_depth;
		}
	}
}

void WipeTower::save_on_last_wipe()
{
    for (m_layer_info=m_plan.begin();m_layer_info<m_plan.end();++m_layer_info) {
        set_layer(m_layer_info->z, m_layer_info->height, 0, m_layer_info->z == m_plan.front().z, m_layer_info->z == m_plan.back().z);
        if (m_layer_info->tool_changes.size()==0)   // we have no way to save anything on an empty layer
            continue;

        // Which toolchange will finish_layer extrusions be subtracted from?
        int idx = first_toolchange_to_nonsoluble(m_layer_info->tool_changes);

        if (idx == -1) {
            // In this case, finish_layer will be called at the very beginning.
            finish_layer().total_extrusion_length_in_plane();
        }

        for (int i=0; i<int(m_layer_info->tool_changes.size()); ++i) {
            auto& toolchange = m_layer_info->tool_changes[i];
            tool_change(toolchange.new_tool);

            if (i == idx) {
                float width = m_wipe_tower_width - 3*m_perimeter_width; // width we draw into
                float length_to_save = finish_layer().total_extrusion_length_in_plane();
                float length_to_wipe = volume_to_length(toolchange.wipe_volume,
                                      m_perimeter_width, m_layer_info->height)  - toolchange.first_wipe_line - length_to_save;

                length_to_wipe = std::max(length_to_wipe,0.f);
                float depth_to_wipe = m_perimeter_width * (std::floor(length_to_wipe/width) + ( length_to_wipe > 0.f ? 1.f : 0.f ) );

                toolchange.required_depth = (toolchange.ramming_depth + depth_to_wipe) * m_extra_spacing;
            }
        }
    }
}


// Return index of first toolchange that switches to non-soluble extruder
// ot -1 if there is no such toolchange.
int WipeTower::first_toolchange_to_nonsoluble(
        const std::vector<WipeTowerInfo::ToolChange>& tool_changes) const
{
    for (size_t idx=0; idx<tool_changes.size(); ++idx)
        if (! m_filpar[tool_changes[idx].new_tool].is_soluble)
            return idx;
    return -1;
}

static WipeTower::ToolChangeResult merge_tcr(WipeTower::ToolChangeResult& first,
                                             WipeTower::ToolChangeResult& second)
{
    assert(first.new_tool == second.initial_tool);
    WipeTower::ToolChangeResult out = first;
    if (first.end_pos != second.start_pos)
        out.gcode += "G1 X" + Slic3r::float_to_string_decimal_point(second.start_pos.x(), 3)
                     + " Y" + Slic3r::float_to_string_decimal_point(second.start_pos.y(), 3)
                     + " F7200\n";
    out.gcode += second.gcode;
    out.extrusions.insert(out.extrusions.end(), second.extrusions.begin(), second.extrusions.end());
    out.end_pos = second.end_pos;
    out.wipe_path = second.wipe_path;
    out.initial_tool = first.initial_tool;
    out.new_tool = second.new_tool;
    return out;
}


// Processes vector m_plan and calls respective functions to generate G-code for the wipe tower
// Resulting ToolChangeResults are appended into vector "result"
void WipeTower::generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &result)
{
	if (m_plan.empty())
        return;

	plan_tower();
    for (int i=0;i<5;++i) {
        save_on_last_wipe();
        plan_tower();
    }

    m_layer_info = m_plan.begin();
    m_current_height = 0.f;

    // we don't know which extruder to start with - we'll set it according to the first toolchange
    for (const auto& layer : m_plan) {
        if (!layer.tool_changes.empty()) {
            m_current_tool = layer.tool_changes.front().old_tool;
            break;
        }
    }

    m_used_filament_length.assign(m_used_filament_length.size(), 0.f); // reset used filament stats
    assert(m_used_filament_length_until_layer.empty());
    m_used_filament_length_until_layer.emplace_back(0.f, m_used_filament_length);

    m_old_temperature = -1; // reset last temperature written in the gcode

	for (const WipeTower::WipeTowerInfo& layer : m_plan)
	{
    std::vector<WipeTower::ToolChangeResult> layer_result;
        set_layer(layer.z, layer.height, 0, false/*layer.z == m_plan.front().z*/, layer.z == m_plan.back().z);
        m_internal_rotation += 180.f;

        if (m_layer_info->depth < m_wipe_tower_depth - m_perimeter_width)
			m_y_shift = (m_wipe_tower_depth-m_layer_info->depth-m_perimeter_width)/2.f;

        int idx = first_toolchange_to_nonsoluble(layer.tool_changes);
        ToolChangeResult finish_layer_tcr;

        if (idx == -1) {
            // if there is no toolchange switching to non-soluble, finish layer
            // will be called at the very beginning. That's the last possibility
            // where a nonsoluble tool can be.
            finish_layer_tcr = finish_layer();
        }

        for (int i=0; i<int(layer.tool_changes.size()); ++i) {
            layer_result.emplace_back(tool_change(layer.tool_changes[i].new_tool));
            if (i == idx) // finish_layer will be called after this toolchange
                finish_layer_tcr = finish_layer();
        }

        if (layer_result.empty()) {
            // there is nothing to merge finish_layer with
            layer_result.emplace_back(std::move(finish_layer_tcr));
        }
        else {
            if (idx == -1) {
                layer_result[0] = merge_tcr(finish_layer_tcr, layer_result[0]);
                layer_result[0].force_travel = true;
            }
            else
                layer_result[idx] = merge_tcr(layer_result[idx], finish_layer_tcr);
        }

		result.emplace_back(std::move(layer_result));

        if (m_used_filament_length_until_layer.empty() || m_used_filament_length_until_layer.back().first != layer.z)
            m_used_filament_length_until_layer.emplace_back();
        m_used_filament_length_until_layer.back() = std::make_pair(layer.z, m_used_filament_length);
	}
}



std::vector<std::pair<float, float>> WipeTower::get_z_and_depth_pairs() const
{
    std::vector<std::pair<float, float>> out = {{0.f, m_wipe_tower_depth}};
    for (const WipeTowerInfo& wti : m_plan) {
        assert(wti.depth < wti.depth + WT_EPSILON);
        if (wti.depth < out.back().second - WT_EPSILON)
            out.emplace_back(wti.z, wti.depth);
    }
    if (out.back().first < m_wipe_tower_height - WT_EPSILON)
        out.emplace_back(m_wipe_tower_height, 0.f);
    return out;
}

} // namespace Slic3r
