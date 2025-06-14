///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/
///|/ ported from lib/Slic3r/GUI/OptionsGroup.pm:
///|/ Copyright (c) Prusa Research 2016 - 2018 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka
///|/ Copyright (c) Slic3r 2011 - 2015 Alessandro Ranellucci @alranel
///|/ Copyright (c) 2013 Scott Penrose
///|/ Copyright (c) 2012 Henrik Brix Andersen @henrikbrixandersen
///|/ Copyright (c) 2011 Richard Goodwin
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_OptionsGroup_hpp_
#define slic3r_OptionsGroup_hpp_

#include <wx/stattext.h>
#include <wx/settings.h>

#include <map>
#include <functional>

#include "libslic3r/Config.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "Field.hpp"
#include "I18N.hpp"

// Translate the ifdef 
#ifdef __WXOSX__
    #define wxOSX true
#else
    #define wxOSX false
#endif 
#ifdef __WXGTK3__
    #define wxGTK3 true
#else
    #define wxGTK3 false
#endif

#define BORDER(a, b) ((wxOSX ? a : b))

namespace Slic3r { namespace GUI {

// Thrown if the building of a parameter page is canceled.
class UIBuildCanceled : public std::exception {};
class OG_CustomCtrl;

/// Widget type describes a function object that returns a wxWindow (our widget) and accepts a wxWidget (parent window).
using widget_t = std::function<wxSizer*(wxWindow*)>;//!std::function<wxWindow*(wxWindow*)>;

namespace script {
	class ScriptContainer;
}
/// Wraps a ConfigOptionDef and adds function object for creating a side_widget.
struct Option
{
    ConfigOptionDef opt{ConfigOptionDef()};
    t_config_option_key opt_key;
    int32_t             opt_idx;
    widget_t side_widget{nullptr};
    bool readonly{false};
    // for fake config
    script::ScriptContainer *script = nullptr;
    // std::vector<std::string> depends_on; // moved to configoptiondef

    bool operator==(const Option &rhs) const { return (rhs.opt_key == this->opt_key && rhs.opt_idx == this->opt_idx); }

    Option(const ConfigOptionDef &_opt, int32_t idx = -1);
};
using t_option = std::unique_ptr<Option>;	//!

/// Represents option lines
class Line : public UndoValueUIManager
{
	bool		m_is_separator{ false };
public:
    wxString	label;
    wxString	label_tooltip;
	std::string	label_path;

    size_t		full_width {0}; 
    widget_t	widget {nullptr};
    std::function<wxWindow*(wxWindow*)>	near_label_widget{ nullptr };
	wxWindow*	near_label_widget_win {nullptr};
    wxSizer*	widget_sizer {nullptr};
    wxSizer*	extra_widget_sizer {nullptr};
     // mode for the whole line visibility. Useful when it's a widget. If none, then it's not overriding anything
     // note: not used yet (in custom og), as the comments are out of the visibility loop
    ConfigOptionMode tags_override{comNone};

    void append_option(const Option& option) {
        m_options.push_back(option);
    }
    void append_option(Option&& option) {
        m_options.push_back(std::move(option));
    }
	void append_widget(const widget_t widget) {
		m_extra_widgets.push_back(widget);
    }
	Line(wxString label, wxString tooltip) :
		label(_(label)), label_tooltip(_(tooltip)) {}
	Line() : m_is_separator(true) {}

	Line(const std::string& opt_key, const wxString& label, const wxString& tooltip) :
		label(_(label)), label_tooltip(_(tooltip))
	{
		m_options.push_back(Option({ opt_key, coNone }));
	}

	bool is_separator() const { return m_is_separator; }
	bool has_only_option(const t_config_option_key& opt_key) const { return m_options.size() == 1 && m_options[0].opt_key == opt_key; }
	void clear();

    const std::vector<widget_t>&	get_extra_widgets() const {return m_extra_widgets;}
	const std::vector<Option>&		get_options() const { return m_options; }
	std::vector<Option>&			get_options() { return m_options; }

private:
	std::vector<Option>		m_options;//! {std::vector<Option>()};
    std::vector<widget_t>	m_extra_widgets;//! {std::vector<widget_t>()};
};

using column_t = std::function<wxWindow*(wxWindow* parent, const Line&)>;

using t_optionfield_map = std::map<OptionKeyIdx, t_field>;

class OptionsGroup {
protected:
	wxStaticBox*	stb {nullptr};
public:
    const bool		staticbox {true}; // false if title is empty.
    const wxString	title;
    int             title_width = 20;// {200};
    bool            no_title = false;
    wxSizer*		sizer {nullptr};
	OG_CustomCtrl*  custom_ctrl{ nullptr };
	int				ctrl_horiz_alignment{ wxALIGN_LEFT};
    column_t		extra_column {nullptr};
    t_change		m_on_change { nullptr };
	// To be called when the field loses focus, to assign a new initial value to the field.
	// Used by the relative position / rotation / scale manipulation fields of the Object Manipulation UI.
    //t_kill_focus    m_fill_empty_value { nullptr }; //not used: Deprecated
	std::function<DynamicPrintConfig()>	m_get_initial_config{ nullptr };
	std::function<DynamicPrintConfig()>	m_get_sys_config{ nullptr };
	std::function<bool()>	have_sys_config{ nullptr };

    std::function<void(wxWindow* win)> rescale_extra_column_item { nullptr };
    std::function<void(wxWindow* win)> rescale_near_label_widget { nullptr };

    std::function<void(const OptionKeyIdx& opt_key_idx)> edit_custom_gcode { nullptr };
    
    wxFont			sidetext_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
    wxFont			label_font {wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT) };
    int             sidetext_width{ -1 };
    int             label_width{ -1 };
	//int				sublabel_width{ -1 }; //never set

    /// Returns a copy of the pointer of the parent wxWindow.
    /// Accessor function is because users are not allowed to change the parent
    /// but defining it as const means a lot of const_casts to deal with wx functions.
	inline wxWindow* parent() const { assert(m_parent); return m_parent; }

    wxWindow*   ctrl_parent() const;

	void		append_line(const Line& line);
	// create controls for the option group
	void		activate_line(Line& line);

	// create all controls for the option group from the m_lines
	bool		activate(std::function<void()> throw_if_canceled = [](){}, int horiz_alignment = wxALIGN_LEFT);
	// delete all controls from the option group
	void		clear(bool destroy_custom_ctrl = false);

	// ask for each script option to recompute their value. If init is true, it will ask for get/set even if the Field isn't created.
	void		update_script_presets(bool init = false);

    Line		create_single_option_line(const Option& option, const std::string& path = std::string()) const;
    void		append_single_option_line(const Option& option, const std::string& path = std::string()) { append_line(create_single_option_line(option, path)); }
	void		append_separator();

    // return a non-owning pointer reference 
    inline Field*	get_field(const OptionKeyIdx& id) const{
							if (m_fields.find(id) == m_fields.end()) return nullptr;
							return m_fields.at(id).get();
    }

    inline Line*	get_line(const t_config_option_key& id) {
		for (Line& line : m_lines)
			if (line.has_only_option(id))
				return &line;
		return nullptr;
    }

    bool set_value(const OptionKeyIdx &id,
                   const boost::any &value,
                   bool enabled,
                   bool change_event /*= false*/) {
        if (auto it = m_fields.find(id); it != m_fields.end()) {
            it->second->set_enable_bitmap_checked(enabled);
            it->second->set_any_value(value, change_event);
            return true;
        }
        return false;
    }
    boost::any get_value(const OptionKeyIdx &id) {
        boost::any out;
        if (m_fields.find(id) == m_fields.end())
            ;
        else
            out = m_fields.at(id)->get_value();
        return out;
    }

    // used by PhysicalPrinterDialog.cpp only
	void			show_field(const t_config_option_key& opt_key, bool show = true);
	void			hide_field(const t_config_option_key& opt_key) {  show_field(opt_key, false);  }

	void			set_name(const wxString& new_name) { stb->SetLabel(new_name); }
	wxString		get_name() const { return stb->GetLabel(); }

	inline void		enable() { for (auto& field : m_fields) field.second->widget_enable(); }
    inline void		disable() { for (auto& field : m_fields) field.second->widget_disable(); }
	void			set_grid_vgap(int gap) { m_grid_sizer->SetVGap(gap); }

    void            hide_labels() { label_width = 0; title_width = 0;}

	OptionsGroup(	wxWindow* _parent, const wxString& title, bool is_tab_opt = false, 
                    column_t extra_clmn = nullptr);
	virtual ~OptionsGroup() { clear(true); }

    wxGridSizer*        get_grid_sizer() { return m_grid_sizer; }
	const std::vector<Line>& get_lines() const { return m_lines; }
	std::vector<Line>&  set_lines() { return m_lines; }
	Line*				get_last_line()  { return m_lines.empty() ? nullptr : &m_lines[m_lines.size()-1]; }
	bool				is_legend_line();
	// if we have to set the same control alignment for different option groups, 
    // we have to set same max contrtol width to all of them
	void				set_max_win_width(int max_win_width);
	void				set_use_custom_ctrl(bool use_custom_ctrl) { m_use_custom_ctrl = use_custom_ctrl; }
	//const std::map<OptionKeyIdx, Option>& get_options() { return m_options; }

	bool				is_activated() { return sizer != nullptr; }

protected:
    // this is only appended by append_line
    std::map<OptionKeyIdx, Option> m_options;
    wxWindow*				m_parent {nullptr};
    // vector: an entry per line
    //map : mode -> items idx in the line
    std::vector<std::map<ConfigOptionMode, std::vector<size_t>>> m_options_mode;
    std::vector<wxSizer*>                   m_line_sizer;
    std::vector<wxWindow*>                  m_extra_column_item_ptrs;

    std::vector<Line>                       m_lines;

    /// Field list, contains unique_ptrs of the derived type.
    /// using types that need to know what it is beyond the public interface 
    /// need to cast based on the related ConfigOptionDef.
    t_optionfield_map		m_fields;
    bool					m_disabled {false}; //seems deprecated
    wxGridSizer*			m_grid_sizer {nullptr};
	// "true" if option is created in preset tabs
	bool					m_use_custom_ctrl{ false };

	// "true" if control should be created on custom_ctrl
	bool					m_use_custom_ctrl_as_parent { false };

	// This panel is needed for correct showing of the ToolTips for Button, StaticText and CheckBox
	// Tooltips on GTK doesn't work inside wxStaticBoxSizer unless you insert a panel 
	// inside it before you insert the other controls.
#if 0//#ifdef__WXGTK__
	wxPanel*				m_panel {nullptr};
#endif /* __WXGTK__ */

    /// Generate a wxSizer or wxWindow from a configuration option
    /// Precondition: opt resolves to a known ConfigOption
    /// Postcondition: fields contains a wx gui object.
    const t_field      &build_field(const OptionKeyIdx &key_idx, const ConfigOptionDef &opt);
    const t_field      &build_field(const OptionKeyIdx &key_idx);
    const t_field      &build_field(const Option &opt);

    virtual void		on_kill_focus(const OptionKeyIdx& opt_key_idx) {};
	virtual void		on_change_OG(const OptionKeyIdx& opt_key_idx, bool enable, const boost::any& value);
	virtual void		back_to_initial_value(const OptionKeyIdx& opt_key_idx) {}
	virtual void		back_to_sys_value(const OptionKeyIdx& opt_key_idx) {}

public:
	static wxString		get_url(const std::string& path_end);
	static bool			launch_browser(const std::string& path_end);
	static bool			is_option_without_field(const t_config_option_key& opt_key);
};

class ConfigOptionsGroup: public OptionsGroup {
public:
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, ConfigBase* config = nullptr,
						bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		OptionsGroup(parent, title, is_tab_opt, extra_clmn), m_config(config), m_modelconfig(nullptr), m_config_mutable(config) {}
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, ModelConfig* config, 
						bool is_tab_opt = false, column_t extra_clmn = nullptr) :
		OptionsGroup(parent, title, is_tab_opt, extra_clmn), m_config(&config->get()), m_modelconfig(config), m_config_mutable(nullptr) {}
	ConfigOptionsGroup(	wxWindow* parent, const wxString& title, bool is_tab_opt) :
		OptionsGroup(parent, title, is_tab_opt, nullptr) {}
    ~ConfigOptionsGroup() override = default;

	const wxString& config_category() const throw() { return m_config_category; }
	int config_type() const throw() { return m_config_type; }
    // TODO: is it really useful? called by Tab::update_changed_tree_ui(), but can't he call options()?
    const std::set<OptionKeyIdx> &opt_set() const throw() { return m_opt_set; }
	void		copy_for_freq_settings(const ConfigOptionsGroup& origin) { this->m_opt_set = origin.m_opt_set; }

	void 		set_config_category_and_type(const wxString &category, int type) { m_config_category = category; m_config_type = type; }
	void        set_config(ConfigBase* config) { 
		m_config = config; m_modelconfig = nullptr; m_config_mutable = config;
	}

    bool has_option_def(const OptionKeyIdx &opt_key_idx);
    const Option* get_option_def(const OptionKeyIdx &opt_key_idx);
	//these 'has' and 'get' are about m_opt_set and not m_options. it's the option + id
    bool has_option(const t_config_option_key &opt_key, int opt_index = -1);
	// more like "create option from def" (old "get_option")
	Option		create_option_from_def(const t_config_option_key& opt_key, int32_t opt_index = -1);
	void		register_to_search(const t_config_option_key& opt_key, const ConfigOptionDef& option_def, int32_t opt_index, bool reset);
	Option		get_option_and_register(const std::string& opt_key, int32_t opt_index = -1) {
        Option opt = create_option_from_def(opt_key, opt_index);
		if(m_use_custom_ctrl) // fill group and category values just for options from Settings Tab
			register_to_search(opt_key, opt.opt, opt_index, true);
		return opt;
	}
	Line		create_single_option_line(const std::string& title, const std::string& path = std::string(), int idx = -1) /*const*/{
		Option option = get_option_and_register(title, idx);
		return OptionsGroup::create_single_option_line(option, path);
	}
	Line		create_single_option_line(const Option& option, const std::string& path = std::string()) const {
		return OptionsGroup::create_single_option_line(option, path);
	}
	void		append_single_option_line(const Option& option, const std::string& path = std::string())	{
		OptionsGroup::append_single_option_line(option, path);
	}
	void		append_single_option_line(const std::string title, const std::string& path = std::string(), int idx = -1)
	{
		Option option = get_option_and_register(title, idx);
		append_single_option_line(option, path);
	}

	void		on_change_OG(const OptionKeyIdx& opt_key_idx, bool enable, const boost::any& value) override;
	void		back_to_initial_value(const OptionKeyIdx& opt_key_idx) override;
	void		back_to_sys_value(const OptionKeyIdx& opt_key_idx) override;
	void		back_to_config_value(const DynamicPrintConfig& config, const OptionKeyIdx& opt_key_idx);
    void		on_kill_focus(const OptionKeyIdx& opt_key_idx) override;
	void		reload_config();
    // return value shows visibility : false => all options are hidden
    void        Hide();
    void        Show(const bool show);
    bool        is_visible(ConfigOptionMode mode);
    bool        update_visibility(ConfigOptionMode mode);
    void        msw_rescale();
    void        sys_color_changed();
    void        refresh();
	std::pair<OG_CustomCtrl*, bool*>	get_custom_ctrl_with_blinking_ptr(const t_config_option_key& opt_key, int32_t opt_index/* = -1*/);

private:
    // Reference to libslic3r config or ModelConfig::get(), non-owning pointer.
    // The reference is const, so that the spots which modify m_config are clearly
    // demarcated by const_cast and m_config_changed_callback is called afterwards.
    //const DynamicConfig*		m_config {nullptr};
    const ConfigBase           *m_config{nullptr};
    // if nom_modelconfig, we can set.
    ConfigBase                 *m_config_mutable{nullptr};
    // If the config is modelconfig, then ModelConfig::touch() has to be called after value change.
    ModelConfig                *m_modelconfig{nullptr};
    // this contains all the option key used to create options, even if not added to a line.
    //TODO: investigate more the diff between m_opt_set and m_options
    // scripted are in m_options but not in m_opt_set
    std::set<OptionKeyIdx>      m_opt_set;
    wxString                    m_config_category;
    int                         m_config_type;

    // Change an option on m_config, possibly call ModelConfig::touch().
	void 	change_opt_value(const t_config_option_key& opt_key, bool enable, const boost::any& value, int opt_index = -1);
};

//  Static text shown among the options.
class ogStaticText :public wxStaticText{
public:
	ogStaticText() {}
	ogStaticText(wxWindow* parent, const wxString& text);
	~ogStaticText() {}

	void		SetText(const wxString& value, bool wrap = true);
	// Set special path end. It will be used to generation of the hyperlink on info page
	void		SetPathEnd(const std::string& link);
	void		FocusText(bool focus);
};

}}

#endif /* slic3r_OptionsGroup_hpp_ */
