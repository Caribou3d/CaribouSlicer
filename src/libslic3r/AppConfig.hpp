///|/ Copyright (c) Prusa Research 2017 - 2023 Vojtěch Bubník @bubnikv, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Filip Sykala @Jony01, Enrico Turri @enricoturri1966, Oleksandra Iushchenko @YuSanka, Vojtěch Král @vojtechkral
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_AppConfig_hpp_
#define slic3r_AppConfig_hpp_

#include <set>
#include <map>
#include <string>

#include <boost/algorithm/string/trim_all.hpp>
#include <boost/filesystem/path.hpp>
#ifdef WIN32
#include <boost/nowide/fstream.hpp>
#endif

#include "libslic3r/Config.hpp"
#include "libslic3r/Semver.hpp"

namespace Slic3r {

class AppConfig
{
public:
	enum class EAppMode : unsigned char
	{
		Editor,
		GCodeViewer
	};
	enum class EAppColorType : unsigned char
	{
		Platter,
		Main,
		Highlight,
	};

	enum HardwareType : uint8_t
	{
		//first 4 bits for cpu
		hHasCpu = uint8_t(0x0F),
		hCpuIntel = 1,
		hCpuAmd = 2,
		hCpuApple = 3,
		hCpuArmGeneric = 4,
		hCpuOther = 5,
		//last 4 bits for gpu
		hHasGpu = uint8_t(0xF0),
		hGpuIntel = 1 << 4,
		hGpuAmd = 2 << 4,
		hGpuApple = 3 << 4,
		hGpuArmGeneric = 4 << 4,
		hGpuOther = 5 << 4,
		hGpuNvidia = 6 << 4,
	};

	struct ConfigFileInfo {
		bool correct_checksum{ false };
		bool contains_null{ false };
	};

	struct LayoutEntry {
		std::string name;
		std::string description;
		boost::filesystem::path path;
		Semver version;
		LayoutEntry() {}
		LayoutEntry(std::string name, std::string description, boost::filesystem::path path, Semver version) : name(name), description(description), path(path), version(version) {}
	};
	struct Tag {
		ConfigOptionMode tag;
		std::string name;
		std::string description;
		std::string color_hash; // with the hash, '#' + 6 digits.
		Tag() {}
		Tag(std::string name, std::string description, ConfigOptionMode tag, std::string color_hash) : name(name), description(description), tag(tag), color_hash(color_hash) {}
	};

	explicit AppConfig(EAppMode mode) :
		m_mode(mode)
	{
		this->reset();
	}

	// Clear and reset to defaults.
	void 			   	reset();
	// Override missing or keys with their defaults.
	void 			   	set_defaults();
	void				init_ui_layout();

	// Load the slic3r.ini from a user profile directory (or a datadir, if configured).
	// return error string or empty strinf
	std::string         load();
	// Load from an explicit path.
	std::string         load(const std::string &path);
	// Store the slic3r.ini into a user profile directory (or a datadir, if configured).
	void 			   	save();

	// Does this config need to be saved?
	bool 				dirty() const { return m_dirty; }

	// Const accessor, it will return false if a section or a key does not exist.
	bool get(const std::string &section, const std::string &key, std::string &value) const
	{
		value.clear();
		auto it = m_storage.find(section);
		if (it == m_storage.end())
			return false;
		auto it2 = it->second.find(key);
		if (it2 == it->second.end()) 
			return false;
		value = it2->second;
		return true;
	}
	std::string 		get(const std::string &section, const std::string &key) const
		{ std::string value; this->get(section, key, value); return value; }
	bool  				get_bool(const std::string &section, const std::string &key) const
		{ return this->get(section, key) == "1"; }
	std::string 		get(const std::string &key) const
		{ std::string value; this->get("", key, value); return value; }
	bool  				get_bool(const std::string &key) const
		{ return this->get(key) == "1"; }
	int  				get_int(const std::string &key) const
		{ return atoi(this->get(key).c_str()); }
	bool			    set(const std::string &section, const std::string &key, const std::string &value)
	{
#ifndef NDEBUG
		{
			std::string key_trimmed = key;
			boost::trim_all(key_trimmed);
			assert(key_trimmed == key);
			assert(! key_trimmed.empty());
		}
#endif // NDEBUG
		std::string &old = m_storage[section][key];
		if (old != value) {
			old = value;
			m_dirty = true;
			return true;
		}
		return false;
	}
	bool			    set(const std::string &key, const std::string &value)
		{ return this->set("", key, value);  }
	bool				has(const std::string &section, const std::string &key) const
	{
		auto it = m_storage.find(section);
		if (it == m_storage.end())
			return false;
		auto it2 = it->second.find(key);
		return it2 != it->second.end() && ! it2->second.empty();
	}
	bool				has(const std::string &key) const
		{ return this->has("", key); }

	bool				erase(const std::string &section, const std::string &key);

	bool                has_section(const std::string &section) const
		{ return m_storage.find(section) != m_storage.end(); }
	const std::map<std::string, std::string>& get_section(const std::string &section) const
		{ auto it = m_storage.find(section); assert(it != m_storage.end()); return it->second; }
	bool 				set_section(const std::string &section, std::map<std::string, std::string> data);
	bool 				clear_section(const std::string &section);

	typedef std::map<std::string, std::map<std::string, std::set<std::string>>> VendorMap;
	bool                get_variant(const std::string &vendor, const std::string &model, const std::string &variant) const;
	bool                set_variant(const std::string &vendor, const std::string &model, const std::string &variant, bool enable);
	bool                set_vendors(const AppConfig &from) { return this->set_vendors(from.vendors()); }
	bool 				set_vendors(const VendorMap &vendors);
	bool 				set_vendors(VendorMap &&vendors);
	const VendorMap&    vendors() const { return m_vendors; }

	// return recent/skein_directory or recent/config_directory or empty string.
	std::string 		get_last_dir() const;
	bool 				update_config_dir(const std::string &dir);
	bool 				update_skein_dir(const std::string &dir);

	//std::string 		get_last_output_dir(const std::string &alt) const;
	//void                update_last_output_dir(const std::string &dir);
	std::string 		get_last_output_dir(const std::string& alt, const bool removable = false) const;
	bool                update_last_output_dir(const std::string &dir, const bool removable = false);

	bool                get_show_overwrite_dialog() const { return get("show_overwrite_dialog") != "0"; }

	// create color
	uint32_t			create_color(float saturation, float value, EAppColorType color_template = EAppColorType::Main);

	// reset the current print / filament / printer selections, so that 
	// the  PresetBundle::load_selections(const AppConfig &config) call will select
	// the first non-default preset when called.
    void                reset_selections();

	// Get the default config path from Slic3r::data_dir().
	std::string			config_path() const;

    // Get the current path to ui_layout directory
    boost::filesystem::path  layout_config_path();
    LayoutEntry              get_ui_layout();
    std::vector<LayoutEntry> get_ui_layouts() { return m_ui_layout; }

    // Tags
    std::vector<Tag>         tags() { return m_tags; }

    // splashscreen
    std::string              splashscreen(bool is_editor);

    // Hardware
    HardwareType			 hardware() { return m_hardware; }
    void					 set_hardware_type(HardwareType hard);

	// Returns true if the user's data directory comes from before Slic3r 1.40.0 (no updating)
	bool 				legacy_datadir() const { return m_legacy_datadir; }
	void 				set_legacy_datadir(bool value) { m_legacy_datadir = value; }

	// Get the Slic3r version check url.
	// This returns a hardcoded string unless it is overriden by "version_check_url" in the ini file.
	std::string 		version_check_url() const;
	// Get the Slic3r url to vendor index archive zip.
	std::string  		index_archive_url() const;
	// Get the Slic3r url to folder with vendor profile files.
	std::string 		profile_folder_url() const;


	// Returns the original Slic3r version found in the ini file before it was overwritten
	// by the current version
	Semver 				orig_version() const { return m_orig_version; }

	// Does the config file exist?
	bool 				exists() const;

    std::vector<std::string> get_recent_projects() const;
    bool set_recent_projects(const std::vector<std::string>& recent_projects);

	bool set_mouse_device(const std::string& name, double translation_speed, double translation_deadzone, float rotation_speed, float rotation_deadzone, double zoom_speed, bool swap_yz, bool invert_x, bool invert_y, bool invert_z, bool invert_yaw, bool invert_pitch, bool invert_roll);
	std::vector<std::string> get_mouse_device_names() const;
	bool get_mouse_device_translation_speed(const std::string& name, double& speed) const
		{ return get_3dmouse_device_numeric_value(name, "translation_speed", speed); }
    bool get_mouse_device_translation_deadzone(const std::string& name, double& deadzone) const
		{ return get_3dmouse_device_numeric_value(name, "translation_deadzone", deadzone); }
    bool get_mouse_device_rotation_speed(const std::string& name, float& speed) const
		{ return get_3dmouse_device_numeric_value(name, "rotation_speed", speed); }
    bool get_mouse_device_rotation_deadzone(const std::string& name, float& deadzone) const
		{ return get_3dmouse_device_numeric_value(name, "rotation_deadzone", deadzone); }
	bool get_mouse_device_zoom_speed(const std::string& name, double& speed) const
		{ return get_3dmouse_device_numeric_value(name, "zoom_speed", speed); }
	bool get_mouse_device_swap_yz(const std::string& name, bool& swap) const
		{ return get_3dmouse_device_numeric_value(name, "swap_yz", swap); }
	bool get_mouse_device_invert_x(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_x", invert); }
	bool get_mouse_device_invert_y(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_y", invert); }
	bool get_mouse_device_invert_z(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_z", invert); }
	bool get_mouse_device_invert_yaw(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_yaw", invert); }
	bool get_mouse_device_invert_pitch(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_pitch", invert); }
	bool get_mouse_device_invert_roll(const std::string& name, bool& invert) const
		{ return get_3dmouse_device_numeric_value(name, "invert_roll", invert); }

	static const std::string SECTION_FILAMENTS;
    static const std::string SECTION_MATERIALS;
    static const std::string SECTION_EMBOSS_STYLE;

#ifdef WIN32
	static std::string appconfig_md5_hash_line(const std::string_view data);
	static ConfigFileInfo check_config_file_and_verify_checksum(boost::nowide::ifstream& ifs);
#endif
private:
	template<typename T>
	bool get_3dmouse_device_numeric_value(const std::string &device_name, const char *parameter_name, T &out) const 
	{
	    std::string key = std::string("mouse_device:") + device_name;
	    auto it = m_storage.find(key);
	    if (it == m_storage.end())
	        return false;
	    auto it_val = it->second.find(parameter_name);
	    if (it_val == it->second.end())
	        return false;
        out = T(string_to_double_decimal_point(it_val->second));
	    return true;
	}

	// Type of application: Editor or GCodeViewer
	EAppMode													m_mode { EAppMode::Editor };
	// Map of section, name -> value
	std::map<std::string, std::map<std::string, std::string>> 	m_storage;
	// Map of enabled vendors / models / variants
	VendorMap                                                   m_vendors;
	// Has any value been modified since the config.ini has been last saved or loaded?
	bool														m_dirty;
	// Original version found in the ini file before it was overwritten
	Semver                                                      m_orig_version;
	// Whether the existing version is before system profiles & configuration updating
	bool                                                        m_legacy_datadir;
    // ui_layout installed
    std::vector<LayoutEntry>                                    m_ui_layout;
    // tags installed
	std::vector<Tag>                                            m_tags;
	//splashscreen
	std::pair<std::string,std::string>                          m_default_splashscreen;
	// hardware type
	HardwareType												m_hardware;
};

} // namespace Slic3r

#endif /* slic3r_AppConfig_hpp_ */
