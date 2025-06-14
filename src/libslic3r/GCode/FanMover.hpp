#ifndef slic3r_GCode_FanMover_hpp_
#define slic3r_GCode_FanMover_hpp_


#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/GCode/GCodeWriter.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/libslic3r.h"

#include <regex>

namespace Slic3r {

class BufferData {
public:
    //raw string, contains end position
    std::string raw;
    // time to go from start to end
    float time;
    int16_t fan_speed;
    bool is_kickstart;
    // start position
    float x = 0, y = 0, z = 0, e = 0;
    // delta to go to end position
    float dx = 0, dy = 0, dz = 0, de = 0;
    BufferData(std::string line, float time = 0, int16_t fan_speed = 0, float is_kickstart = false) : raw(line), time(time), fan_speed(fan_speed), is_kickstart(is_kickstart){
        //avoid double \n
        if(!line.empty() && line.back() == '\n') line.pop_back();
    }
};

class FanMover
{
private:
    const std::regex regex_fan_speed;
    const float nb_seconds_delay; // in s
    const bool with_D_option;
    const bool relative_e;
    const bool only_overhangs;
    const float kickstart; // in s

    GCodeReader m_parser{};
    const GCodeWriter& m_writer;

    //current value (at the back of the buffer), when parsing a new line
    GCodeExtrusionRole current_role = GCodeExtrusionRole::Custom;
    // in unit/second
    double m_current_speed = 1000 / 60.0;
    bool m_is_custom_gcode = false;
    uint16_t m_current_extruder = 0;

    // variable for when you add a line (front of the buffer)
    int m_front_buffer_fan_speed = 1;
    int m_back_buffer_fan_speed = 1;
    BufferData m_current_kickstart{"",-1,0};
    float m_current_kickstart_duration = 0;

    //buffer
    std::list<BufferData> m_buffer;
    double m_buffer_time_size = 0;

    // The output of process_layer()
    std::string m_process_output;

public:
    FanMover(const GCodeWriter& writer, const float nb_seconds_delay, const bool with_D_option, const bool relative_e,
        const bool only_overhangs, const float kickstart)
        : regex_fan_speed("S[0-9]+"), 
        nb_seconds_delay(nb_seconds_delay>0 ? std::max(0.01f,nb_seconds_delay) : 0),
        with_D_option(with_D_option)
        , relative_e(relative_e), only_overhangs(only_overhangs), kickstart(kickstart), m_writer(writer){}

    // Adds the gcode contained in the given string to the analysis and returns it after removing the workcodes
    const std::string& process_gcode(const std::string& gcode, bool flush);

private:
    BufferData& put_in_buffer(BufferData&& data) {
        assert(data.time >= 0 && data.time < 1000000 && !std::isnan(data.time));
         m_buffer_time_size += data.time;
        if (data.fan_speed >= 0 && !m_buffer.empty() && m_buffer.back().fan_speed >= 0) {
            // erase last item
            m_buffer.back() = data;
        } else {
            m_buffer.emplace_back(data);
        }
        return m_buffer.back();
    }
    std::list<BufferData>::iterator remove_from_buffer(std::list<BufferData>::iterator data) {
        assert(data->time >= 0 && data->time < 1000000 && !std::isnan(data->time));
        m_buffer_time_size -= data->time;
        return m_buffer.erase(data);
    }
    // Processes the given gcode line
    void _process_gcode_line(GCodeReader& reader, const GCodeReader::GCodeLine& line);
    void _process_ACTIVATE_EXTRUDER(const std::string_view command);
    void _process_T(const std::string_view command);
    void _put_in_middle_G1(std::list<BufferData>::iterator item_to_split, float nb_sec, BufferData&& line_to_write, float max_time);
    void _print_in_middle_G1(BufferData& line_to_split, float nb_sec, const std::string& line_to_write);
    void _remove_slow_fan(int16_t min_speed, float past_sec);
    void write_buffer_data();
    std::string _set_fan(int16_t speed, std::string_view comment);
};

} // namespace Slic3r


#endif /* slic3r_GCode_FanMover_hpp_ */
