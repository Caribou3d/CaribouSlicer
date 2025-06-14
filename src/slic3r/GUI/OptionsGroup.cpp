///|/ Copyright (c) Prusa Research 2017 - 2023 Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv, Lukáš Hejl @hejllukas, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral, Enrico Turri @enricoturri1966
///|/ Copyright (c) 2018 Martin Loidl @LoidlM
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
#include "OptionsGroup.hpp"
#include "ConfigExceptions.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "OG_CustomCtrl.hpp"
#include "MsgDialog.hpp"
#include "format.hpp"
#include "Tab.hpp"

#include <utility>
#include <wx/bookctrl.h>
#include <wx/numformatter.h>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include "libslic3r/Exception.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

const t_field& OptionsGroup::build_field(const Option& opt) {
    return build_field(OptionKeyIdx{opt.opt_key, opt.opt_idx}, opt.opt);
}
const t_field& OptionsGroup::build_field(const OptionKeyIdx& key_idx) {
    const ConfigOptionDef& opt = m_options.at(key_idx).opt;
    return build_field(key_idx, opt);
}

const t_field& OptionsGroup::build_field(const OptionKeyIdx& key_idx, const ConfigOptionDef& opt) {
    // Check the gui_type field first, fall through
    // is the normal type.
    switch (opt.gui_type) {
    case ConfigOptionDef::GUIType::select_close:
    case ConfigOptionDef::GUIType::select_open:
    case ConfigOptionDef::GUIType::f_enum_open:
    case ConfigOptionDef::GUIType::i_enum_open:
        m_fields.emplace(key_idx, Choice::Create<Choice>(this->ctrl_parent(), opt, key_idx));
        break;
    case ConfigOptionDef::GUIType::color:
        m_fields.emplace(key_idx, ColourPicker::Create<ColourPicker>(this->ctrl_parent(), opt, key_idx));
        break;
    case ConfigOptionDef::GUIType::slider:
        m_fields.emplace(key_idx, SliderCtrl::Create<SliderCtrl>(this->ctrl_parent(), opt, key_idx));
        break;
    case ConfigOptionDef::GUIType::legend: // StaticText
        m_fields.emplace(key_idx, StaticText::Create<StaticText>(this->ctrl_parent(), opt, key_idx));
        break;
    default:
        switch (opt.type) {
            case coFloatOrPercent:
            case coFloatsOrPercents:
            case coFloat:
            case coFloats:
            case coPercent:
            case coPercents:
            case coString:
            case coStrings:
                m_fields.emplace(key_idx, TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, key_idx));
                break;
            case coBools:
                if (key_idx.idx < 0) {
                    // string field with vector serialization
                    m_fields.emplace(key_idx, TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, key_idx));
                    break;
                }
            case coBool:
                m_fields.emplace(key_idx, CheckBox::Create<CheckBox>(this->ctrl_parent(), opt, key_idx));
                break;
            case coInts:
                if (key_idx.idx < 0) {
                    // string field with vector serialization
                    m_fields.emplace(key_idx, TextCtrl::Create<TextCtrl>(this->ctrl_parent(), opt, key_idx));
                    break;
                }
            case coInt:
                m_fields.emplace(key_idx, SpinCtrl::Create<SpinCtrl>(this->ctrl_parent(), opt, key_idx));
                break;
            case coEnum:
                m_fields.emplace(key_idx, Choice::Create<Choice>(this->ctrl_parent(), opt, key_idx));
                break;
            case coPoint:
            case coPoints:
                m_fields.emplace(key_idx, PointCtrl::Create<PointCtrl>(this->ctrl_parent(), opt, key_idx));
                break;
            case coGraph:
            case coGraphs:
                m_fields.emplace(key_idx, GraphButton::Create<GraphButton>(this->ctrl_parent(), opt, key_idx));
                break;
            case coNone:  assert(false); break;
            default:
                throw Slic3r::LogicError("This control doesn't exist till now"); break;
        }
    }
    // Grab a reference to fields for convenience
    const t_field& field = m_fields[key_idx];
	field->m_on_change = [this](const OptionKeyIdx& opt_key_idx, bool enabled, const boost::any& value) {
			//! This function will be called from Field.
			//! Call OptionGroup._on_change(...)
			if (!m_disabled)
				this->on_change_OG(opt_key_idx, enabled, value);
	};
    field->m_on_kill_focus = [this](const OptionKeyIdx& opt_key_idx) {
			//! This function will be called from Field.
			if (!m_disabled)
				this->on_kill_focus(opt_key_idx);
	};
    field->m_parent = parent();

    if (edit_custom_gcode && opt.is_code) {
        field->m_fn_edit_value = [this](const OptionKeyIdx& opt_key_idx) {
            if (!m_disabled)
                this->edit_custom_gcode(opt_key_idx);
        };
        field->set_edit_tooltip(_L("Edit Custom G-code"));
    }

	field->m_back_to_initial_value = [this](const OptionKeyIdx& opt_key_idx) {
		if (!m_disabled)
			this->back_to_initial_value(opt_key_idx);
	};
	field->m_back_to_sys_value = [this](const OptionKeyIdx& opt_key_idx) {
		if (!m_disabled)
			this->back_to_sys_value(opt_key_idx);
	};

	// assign function objects for callbacks, etc.
    return field;
}

OptionsGroup::OptionsGroup(	wxWindow* _parent, const wxString& title,
                            bool is_tab_opt /* = false */,
                            column_t extra_clmn /* = nullptr */) :
                m_parent(_parent), title(title),
                m_use_custom_ctrl(is_tab_opt),
                staticbox(title!=""), extra_column(extra_clmn)
{
    assert(Tab::fake_build || m_parent);
}

Option::Option(const ConfigOptionDef &_opt, int32_t idx /*= -1*/)
    : opt(_opt), opt_key(_opt.opt_key), opt_idx(idx) {
    if (!opt.tooltip.empty()) {
        wxString tooltip;
        if (opt.opt_key.rfind("branching", 0) == 0)
            tooltip = _L("Unavailable for this method.") + "\n";
        tooltip += _(opt.tooltip);

        update_Slic3r_string(tooltip);

        opt.tooltip = into_u8(tooltip);
    }
}

void Line::clear()
{
    if (near_label_widget_win)
        near_label_widget_win = nullptr;

    if (widget_sizer) {
        widget_sizer->Clear(true);
        delete widget_sizer;
        widget_sizer = nullptr;
    }

    if (extra_widget_sizer) {
        extra_widget_sizer->Clear(true);
        delete extra_widget_sizer;
        extra_widget_sizer = nullptr;
    }
}

wxWindow* OptionsGroup::ctrl_parent() const
{
	wxWindow* ret_val = this->custom_ctrl && m_use_custom_ctrl_as_parent ? static_cast<wxWindow*>(this->custom_ctrl) : (this->stb ? static_cast<wxWindow*>(this->stb) : this->parent());
    assert(ret_val);
    return ret_val;
}

bool OptionsGroup::is_legend_line()
{
	if (m_lines.size() == 1) {
		const std::vector<Option>& option_set = m_lines.front().get_options();
		return option_set.empty() || option_set.front().opt.gui_type == ConfigOptionDef::GUIType::legend;
	}
	return false;
}

void OptionsGroup::set_max_win_width(int max_win_width)
{
    if (custom_ctrl)
        custom_ctrl->set_max_win_width(max_win_width);
}

void OptionsGroup::show_field(const t_config_option_key& opt_key, bool show/* = true*/)
{
    Field* field = get_field(OptionKeyIdx::scalar(opt_key));
    if (!field) return;
    wxWindow* win = field->getWindow();
    if (!win) return;
    wxSizerItem* win_item = m_grid_sizer->GetItem(win, true);
    if (!win_item) return;

    const size_t cols = (size_t)m_grid_sizer->GetCols();
    const size_t rows = (size_t)m_grid_sizer->GetEffectiveRowsCount();

    auto show_row = [this, show, cols, win_item](wxSizerItem* item, size_t row_shift) {
        // check if item contanes required win
        if (!item->IsWindow() || item != win_item)
            return false;
        // show/hide hole line contanes this window
        for (size_t i = 0; i < cols; ++i)
            m_grid_sizer->Show(row_shift + i, show);
        return true;
    };

    size_t row_shift = 0;
    for (size_t j = 0; j < rows; ++j) {
        for (size_t i = 0; i < cols; ++i) {
            wxSizerItem* item = m_grid_sizer->GetItem(row_shift + i);
            if (!item)
                continue;
            if (item->IsSizer()) {
                for (wxSizerItem* child_item : item->GetSizer()->GetChildren())
                    if (show_row(child_item, row_shift))
                        return;
            }
            else if (show_row(item, row_shift))
                return;
        }
        row_shift += cols;
    }
}

void OptionsGroup::append_line(const Line& line)
{
	m_lines.push_back(line);

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		)
		return;

	auto option_set = line.get_options();
	for (auto opt : option_set)
        m_options.emplace(OptionKeyIdx{opt.opt_key, opt.opt_idx}, opt);

    //if first control don't have a label, use the line one for the tooltip
    if (!option_set.empty() && (option_set.front().opt.label.empty() || "_" == option_set.front().opt.label)) {
        wxString tooltip = _(option_set.front().opt.tooltip);
        update_Slic3r_string(tooltip);
        m_lines.back().label_tooltip = tooltip;
    }

	// add mode value for current line to m_options_mode
    if (!option_set.empty()){
        m_line_sizer.emplace_back();
        // add index -1 (ie the line, before the fist item inside) to mode of the first option_set.
        m_options_mode.emplace_back();
        m_options_mode.back()[option_set[0].opt.mode].push_back(-1);
    }
}

void OptionsGroup::append_separator()
{
    m_lines.emplace_back(Line());
}

void OptionsGroup::activate_line(Line& line)
{
    if (line.is_separator())
        return;

    m_use_custom_ctrl_as_parent = false;

	if (line.full_width && (
		line.widget != nullptr ||
		!line.get_extra_widgets().empty())
		) {
        if (line.widget != nullptr) {
			// description lines
            sizer->Add(line.widget(this->ctrl_parent()), 0, wxEXPAND | wxALL, wxOSX ? 0 : 15);
            return;
        }
		if (!line.get_extra_widgets().empty()) {
			const auto h_sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(h_sizer, 1, wxEXPAND | wxALL, wxOSX ? 0 : 15);

            bool is_first_item = true;
			for (auto extra_widget : line.get_extra_widgets()) {
				h_sizer->Add(extra_widget(this->ctrl_parent()), is_first_item ? 1 : 0, wxLEFT, 15);
				is_first_item = false;
			}
			return;
		}
    }

    const std::vector<Option>& option_set = line.get_options();
    bool is_legend_line = !option_set.empty() && option_set.front().opt.gui_type == ConfigOptionDef::GUIType::legend;

    if (!custom_ctrl && m_use_custom_ctrl) {
        custom_ctrl = new OG_CustomCtrl(is_legend_line || !staticbox ? this->parent() : static_cast<wxWindow*>(this->stb), this);
        wxGetApp().UpdateDarkUI(custom_ctrl);
		if (is_legend_line)
			sizer->Add(custom_ctrl, 0, wxEXPAND | wxLEFT, wxOSX ? 0 : 10);
		else
            sizer->Add(custom_ctrl, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);
    }

	// Set sidetext width for a better alignment of options in line
	// "m_show_modified_btns==true" means that options groups are in tabs
	if (option_set.size() > 1 && m_use_custom_ctrl) {
		sidetext_width = Field::def_width_thinner();
	}

	// if we have a single option with no label, no sidetext just add it directly to sizer
    if (option_set.size() == 1 && title_width == 0 && option_set.front().opt.full_width &&
		option_set.front().opt.sidetext.size() == 0 && option_set.front().side_widget == nullptr &&
		line.get_extra_widgets().size() == 0) {

		const auto& option = option_set.front();
		const auto& field = build_field(option);

		if (is_window_field(field))
			sizer->Add(field->getWindow(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		if (is_sizer_field(field))
			sizer->Add(field->getSizer(), 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);
		return;
	}

    if (custom_ctrl)
        m_use_custom_ctrl_as_parent = true;

    // if we have an extra column, build it
    if (extra_column) {
        m_extra_column_item_ptrs.push_back(extra_column(this->ctrl_parent(), line));
        m_grid_sizer->Add(m_extra_column_item_ptrs.back(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 3);
	}

    // Build a (title) label if we have it
    wxStaticText* label=nullptr;
    if (title_width != 0) {
        if (custom_ctrl) {
            if (line.near_label_widget)
                line.near_label_widget_win = line.near_label_widget(this->ctrl_parent());
        }
        else {
            if (!line.near_label_widget || !line.label.IsEmpty()) {
                // Only create the label if it is going to be displayed.
                long label_style = staticbox ? 0 : wxALIGN_RIGHT;
#ifdef __WXGTK__
                // workaround for correct text align of the StaticBox on Linux
                // flags wxALIGN_RIGHT and wxALIGN_CENTRE don't work when Ellipsize flags are _not_ given.
                // Text is properly aligned only when Ellipsize is checked.
                label_style |= staticbox ? 0 : wxST_ELLIPSIZE_END;
#endif /* __WXGTK__ */
                label = new wxStaticText(this->ctrl_parent(), wxID_ANY, line.label + (line.label.IsEmpty() ? "" : ": "),
                    wxDefaultPosition, wxSize(title_width * wxGetApp().em_unit(), -1), label_style);
                label->SetBackgroundStyle(wxBG_STYLE_PAINT);
                label->SetFont(wxGetApp().normal_font());
                label->Wrap(title_width * wxGetApp().em_unit()); // avoid a Linux/GTK bug
            }
            if (!line.near_label_widget)
                m_grid_sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, line.label.IsEmpty() ? 0 : 5);
            else if (!line.label.IsEmpty()) {
                // If we're here, we have some widget near the label
                // so we need a horizontal sizer to arrange these things
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                m_grid_sizer->Add(sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
                sizer->Add(label, 0, (staticbox ? 0 : wxALIGN_RIGHT | wxRIGHT) | wxALIGN_CENTER_VERTICAL, 5);
            }
            if (label != nullptr && line.label_tooltip != "")
                label->SetToolTip(line.label_tooltip);
        }
    }

    // If there's a widget, build it and add the result to the sizer.
	if (line.widget != nullptr) {
		auto wgt = line.widget(this->ctrl_parent());
        if (custom_ctrl)
            line.widget_sizer = wgt;
        else
            m_grid_sizer->Add(wgt, 0, wxEXPAND | wxBOTTOM | wxTOP, (wxOSX || line.label.IsEmpty()) ? 0 : 5);
		return;
	}

	// If we're here, we have more than one option or a single option with sidetext
    // so we need a horizontal sizer to arrange these things
    wxBoxSizer* h_sizer{ nullptr };
    if (!custom_ctrl) {
        // but this sizer is currently used just for NON-custom_ctrl cases
        h_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_grid_sizer->Add(h_sizer, 0, wxEXPAND | (staticbox ? wxALL : wxBOTTOM | wxTOP | wxLEFT), staticbox ? 0 : 1);
    }

    // If we have a single option with no sidetext just add it directly to the grid sizer
    if (option_set.size() == 1 && option_set.front().opt.sidetext.size() == 0 &&
        option_set.front().side_widget == nullptr && line.get_extra_widgets().size() == 0 &&
        !option_set.front().opt.can_be_disabled) {
        const auto& option = option_set.front();
        const auto& field = build_field(option);

        if (!custom_ctrl) {
            if (is_window_field(field))
                h_sizer->Add(field->getWindow(), option.opt.full_width ? 1 : 0,
                    wxBOTTOM | wxTOP | (option.opt.full_width ? int(wxEXPAND) : int(wxALIGN_CENTER_VERTICAL)), (wxOSX || !staticbox) ? 0 : 2);
            if (is_sizer_field(field))
                h_sizer->Add(field->getSizer(), 1, (option.opt.full_width ? int(wxEXPAND) : int(wxALIGN_CENTER_VERTICAL)), 0);
        }
        return;
	}

    for (const Option& opt : option_set) {
        // add field
        auto& field = build_field(opt);

        if (!custom_ctrl) { // TODO review if this if shoudln't be always true
            ConfigOptionDef option = opt.opt;
            // add label if any
            if ((option_set.size() > 1 || line.label.IsEmpty()) && !option.label.empty()) {
                const std::string opt_label = (option.label == "_") ? "" :
                    (option.label.empty() || option.label.back() != '_') ?
                                                                      option.label :
                                                                      option.label.substr(0, option.label.size() - 1);
                // those two parameter names require localization with context
                const wxString str_label = _(opt_label);
                bool no_dots = str_label.empty() || option.label.back() == '_';
                label = new wxStaticText(this->ctrl_parent(), wxID_ANY,
                   (no_dots ? str_label : (str_label + ": ")), wxDefaultPosition, //wxDefaultSize);
                   (option.label_width >= 0) ? ((option.label_width != 0) ? wxSize(option.label_width*wxGetApp().em_unit(), -1) : wxDefaultSize) :
                                             ((label_width > 0) ? wxSize(label_width * wxGetApp().em_unit(), -1) : (wxDefaultSize)),
                                         wxALIGN_RIGHT);
                label->SetBackgroundStyle(wxBG_STYLE_PAINT);
                label->SetFont(wxGetApp().normal_font());
                if (option.label_width > 0 || label_width >0) {
                    label->Wrap((option.label_width > 0 ? option.label_width : label_width)* wxGetApp().em_unit()); // avoid a Linux/GTK bug
                }
                m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
                h_sizer->Add(label, 0, wxALIGN_CENTER_VERTICAL, 0);
            }

            //add enable button
            if (opt.opt.can_be_disabled) {
                //h_sizer->Add(new wxStaticText(this->ctrl_parent(), wxID_ANY, " "), 0, wxALIGN_CENTER_VERTICAL, 0);
                h_sizer->Add(field->create_enable_widget(this->ctrl_parent()), 0, wxALIGN_CENTER_VERTICAL, 0);
                //h_sizer->Add(new wxStaticText(this->ctrl_parent(), wxID_ANY,"!"), 0, wxALIGN_CENTER_VERTICAL, 0);
            }

            if (option_set.size() == 1 && option_set.front().opt.full_width)
            {
                const auto v_sizer = new wxBoxSizer(wxVERTICAL);
                m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
                h_sizer->Add(v_sizer, 1, wxEXPAND);
                is_sizer_field(field) ?
                    v_sizer->Add(field->getSizer(), 0, wxEXPAND) :
                    v_sizer->Add(field->getWindow(), 0, wxEXPAND);
                break;
            }

            m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
            is_sizer_field(field) ?
                h_sizer->Add(field->getSizer(), 0, wxALIGN_CENTER_VERTICAL, 0) :
                h_sizer->Add(field->getWindow(), 0, wxALIGN_CENTER_VERTICAL, 0);

            // add sidetext if any
            if ((!option.sidetext.empty() || sidetext_width > 0) && option.sidetext_width != 0){
                wxString textstring;
                if(!option.sidetext.empty())
                    if (option.sidetext.at(option.sidetext.size() - 1) != '_') {
                        textstring = _(option.sidetext);
                    } else {
                        textstring = option.sidetext.substr(0, option.sidetext.size() - 1);
                    }
                wxSize wxsize{ -1,-1 };
                if (option.sidetext_width >= 0) {
                    if (option.sidetext_width != 0)
                        wxsize = wxSize{ option.sidetext_width * wxGetApp().em_unit(), -1 };
                } else if (sidetext_width > 0)
                    wxsize = wxSize{ sidetext_width * wxGetApp().em_unit(),-1 };
                wxStaticText *sidetext = new wxStaticText(	this->ctrl_parent(), wxID_ANY, textstring,
                                                    wxDefaultPosition, wxsize
                                                    /*wxDefaultSize*/, wxALIGN_LEFT);
                sidetext->SetBackgroundStyle(wxBG_STYLE_PAINT);
                sidetext->SetFont(wxGetApp().normal_font());
                m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
                h_sizer->Add(sidetext, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);

            }

            // add side widget if any
            if (opt.side_widget != nullptr) {
                m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
                h_sizer->Add(opt.side_widget(this->ctrl_parent())/*!.target<wxWindow>()*/, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 1);    //! requires verification
            }

            if (opt.opt_key != option_set.back().opt_key || opt.opt_idx != option_set.back().opt_idx) {//! instead of (opt != option_set.back())
                m_options_mode.back()[opt.opt.mode].push_back(h_sizer->GetItemCount());
                h_sizer->AddSpacer(6);
            }
        }
    }

    // add extra sizers if any
    for (auto extra_widget : line.get_extra_widgets())
    {
        if (line.get_extra_widgets().size() == 1 && !staticbox)
        {
            // extra widget for non-staticbox option group (like for the frequently used parameters on the sidebar) should be wxALIGN_RIGHT
            const auto v_sizer = new wxBoxSizer(wxVERTICAL);
            h_sizer->Add(v_sizer, option_set.size() == 1 ? 0 : 1, wxEXPAND);
            v_sizer->Add(extra_widget(this->ctrl_parent()), 0, wxALIGN_RIGHT);
            return;
        }

        line.extra_widget_sizer = extra_widget(this->ctrl_parent());
        if (!custom_ctrl)
            h_sizer->Add(line.extra_widget_sizer, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 4);        //! requires verification
    }
}

// create all controls for the option group from the m_lines
bool OptionsGroup::activate(std::function<void()> throw_if_canceled/* = [](){}*/, int horiz_alignment/* = wxALIGN_LEFT*/)
{
	if (sizer)//(!sizer->IsEmpty())
		return false;

	try {
		if (staticbox) {
			stb = new wxStaticBox(m_parent, wxID_ANY, _(title));
			if (!wxOSX) stb->SetBackgroundStyle(wxBG_STYLE_PAINT);
			stb->SetFont(wxOSX ? wxGetApp().normal_font() : wxGetApp().bold_font());
			wxGetApp().UpdateDarkUI(stb);
		}
		else
			stb = nullptr;
		sizer = (staticbox ? new wxStaticBoxSizer(stb, wxVERTICAL) : new wxBoxSizer(wxVERTICAL));

		auto num_columns = 1U;
		size_t grow_col = 1;

		if (label_width == 0)
			grow_col = 0;
		else
			num_columns++;

		if (extra_column) {
			num_columns++;
			grow_col++;
		}

		m_grid_sizer = new wxFlexGridSizer(0, num_columns, 1, 0);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->SetFlexibleDirection(wxBOTH);
		static_cast<wxFlexGridSizer*>(m_grid_sizer)->AddGrowableCol(grow_col);

		sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX || !staticbox ? 0 : 5);

		// activate lines
		for (Line& line: m_lines) {
			throw_if_canceled();
			activate_line(line);
		}

        ctrl_horiz_alignment = horiz_alignment;
        if (custom_ctrl)
            custom_ctrl->init_max_win_width();
	} catch (UIBuildCanceled&) {
		auto p = sizer;
		this->clear();
		p->Clear(true);
		delete p;
		throw;
	}

	return true;
}
// delete all controls from the option group
void OptionsGroup::clear(bool destroy_custom_ctrl)
{
	if (!sizer)
		return;

	m_grid_sizer = nullptr;
	sizer = nullptr;

    for (Line& line : m_lines)
        line.clear();

    if (custom_ctrl) {
        for (auto const &item : m_fields) {
            wxWindow* win = item.second.get()->getWindow();
            if (win)
                win = nullptr;
        }
        if (destroy_custom_ctrl)
            custom_ctrl->Destroy();
        else
            custom_ctrl = nullptr;
    }

	m_extra_column_item_ptrs.clear();
	m_fields.clear();
}

Line OptionsGroup::create_single_option_line(const Option& option, const std::string& path/* = std::string()*/) const
{
    wxString tooltip = _(option.opt.tooltip);
    update_Slic3r_string(tooltip);
    // note: Line constructor already do _()
	Line retval{ _(option.opt.label), tooltip };
	retval.label_path = path;
    if(option.opt.label.empty()) {
        retval.append_option(option);
    } else {
        //remove label from option
        Option tmp(option);
        tmp.opt.label = std::string("");
        retval.append_option(tmp);
    }
    return retval;
}

void OptionsGroup::on_change_OG(const OptionKeyIdx& opt_key_idx, bool enabled, const boost::any& value) {
    auto it = m_options.find(opt_key_idx);
    if (it != m_options.end() && it->second.opt.is_script && it->second.script) {
        it->second.script->call_script_function_set(it->second.opt, value);
    }else if (m_on_change != nullptr)
        m_on_change(opt_key_idx, enabled, value);
}

void OptionsGroup::update_script_presets(bool init) {
    for (auto& key_opt : m_options) {
        if (key_opt.second.opt.is_script) {
            if (init || get_field(key_opt.first)) {
                boost::any val = key_opt.second.script->call_script_function_get_value(key_opt.second.opt);
                if (val.empty()) {
                    MessageDialog(nullptr, "Error, can't find the script to get the value for the widget '" + key_opt.first.key + "'", _L("Error"), wxOK | wxICON_ERROR).ShowModal();
                } else {
                    this->set_value(key_opt.first, val, true, false);
                }
            } //if not, it will set at ConfigOptionsGroup::reload_config()
        }
    }
}
bool ConfigOptionsGroup::has_option_def(const OptionKeyIdx &opt_key_idx)
{
    return this->m_options.find(opt_key_idx) != this->m_options.end();
}
const Option *ConfigOptionsGroup::get_option_def(const OptionKeyIdx &opt_key_idx)
{
    auto it = this->m_options.find(opt_key_idx);
    return it == m_options.end() ? nullptr : &it->second;
}

bool ConfigOptionsGroup::has_option(const t_config_option_key& opt_key, int opt_index /*= -1*/)
{
    if (!m_config->has(opt_key)) {
        BOOST_LOG_TRIVIAL(error) << "No " << opt_key << " in ConfigOptionsGroup config.";
    }

    return m_opt_set.find(OptionKeyIdx{opt_key, opt_index}) != m_opt_set.end();
}

Option ConfigOptionsGroup::create_option_from_def(const t_config_option_key &opt_key, int32_t opt_index /*= -1*/)
{
    if (!m_config->has(opt_key)) {
        BOOST_LOG_TRIVIAL(error) << "No " << opt_key << " in ConfigOptionsGroup config.";
    }
    //m_opt_set.emplace(opt_key, opt_index);
    m_opt_set.insert(OptionKeyIdx{opt_key, opt_index});

    return Option(*m_config->def()->get(opt_key), opt_index);
}

void ConfigOptionsGroup::register_to_search(const t_config_option_key& opt_key, const ConfigOptionDef& option_def, int32_t opt_index /*= -1*/, bool reset)
{
    // fill group and category values just for options from Settings Tab
    wxGetApp().sidebar().get_searcher().add_key(OptionKeyIdx{opt_key, opt_index}, static_cast<Preset::Type>(this->config_type()), this->title, this->config_category(), option_def, reset);
}

void ConfigOptionsGroup::on_change_OG(const OptionKeyIdx &opt_key_idx, bool enabled, const boost::any &value) {
    auto it = m_opt_set.find(opt_key_idx);
    if(it != m_opt_set.end()) {
        // in m_opt_set > real opt (not a script & can call change_opt_value)
        this->change_opt_value(opt_key_idx.key, enabled, value, opt_key_idx.idx);
    }
    OptionsGroup::on_change_OG(opt_key_idx, enabled, value);
}

void ConfigOptionsGroup::back_to_initial_value(const OptionKeyIdx &opt_key_idx) {
    if (m_get_initial_config == nullptr)
        return;
    back_to_config_value(m_get_initial_config(), opt_key_idx);
}

void ConfigOptionsGroup::back_to_sys_value(const OptionKeyIdx &opt_key_idx) {
    if (m_get_sys_config == nullptr)
        return;
    if (!have_sys_config())
        return;
    back_to_config_value(m_get_sys_config(), opt_key_idx);
}

void ConfigOptionsGroup::back_to_config_value(const DynamicPrintConfig& config, const OptionKeyIdx& opt_key_idx)
{
    boost::any value;
    bool enabled = true;
    auto it_opt = m_options.find(opt_key_idx);
    auto it_opt_set = m_opt_set.find(opt_key_idx);
    const int opt_index = opt_key_idx.idx;
    if (opt_key_idx.key == "bed_shape") {
        for (const std::string &key : {"bed_custom_texture", "bed_custom_model"}) {
            const ConfigOptionString *option = config.option<ConfigOptionString>(key);
            assert(option);
            this->change_opt_value(key, option->is_enabled(), option->value);
        }
    }
    if (opt_key_idx.key == "extruders_count") {
        auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats *>(config.option("nozzle_diameter"));
        value = int(nozzle_diameter->size());
    } else if (opt_key_idx.key == "milling_count") {
        auto *milling_diameter = dynamic_cast<const ConfigOptionFloats *>(config.option("milling_diameter"));
        value = int(milling_diameter->size());
    } else if (it_opt != m_options.end() && it_opt->second.opt.is_script) {
        // when a scripted key is reset, reset its deps
        // call the reset function if it exits
        if (!it_opt->second.script->call_script_function_reset(it_opt->second.opt)) {
            // Fucntion doesn't exists, reset the fields from the 'depends'
            // reset in all tabs
            // first set_key_value
            PrinterTechnology printer_technology = wxGetApp().get_current_printer_technology();
            std::vector<Tab*> tab_list = wxGetApp().tabs_list;
            std::set<size_t> modified_tabs_idx;
            for (const std::string& dep_key : it_opt->second.opt.depends_on) {
                for (const PresetCollection* preset_collection : wxGetApp().get_active_preset_collections())
                    for (size_t tab_idx = 0; tab_idx < tab_list.size(); tab_idx++) {
                    Tab* tab = tab_list[tab_idx];
                    if (tab != nullptr && tab->completed() && tab->supports_printer_technology(printer_technology)) {
                        const DynamicPrintConfig& initial_conf = tab->m_presets->get_selected_preset().config;
                        DynamicPrintConfig& edited_conf = tab->m_presets->get_edited_preset().config;
                        if (initial_conf.has(dep_key) && edited_conf.has(dep_key)) {
                            ConfigOption* conf_opt = initial_conf.option(dep_key)->clone();
                            //set the conf
                            edited_conf.set_key_value(dep_key, conf_opt);
                            modified_tabs_idx.insert(tab_idx);
                        }
                    }
                }
            }
            // now that all keys are set, call the on_value_change to propagate the changes in one go.
            for (const std::string& dep_key : it_opt->second.opt.depends_on) {
                for (size_t tab_idx : modified_tabs_idx) {
                    Tab* tab = tab_list[tab_idx];
                    const DynamicPrintConfig& initial_conf = tab->m_presets->get_selected_preset().config;
                    DynamicPrintConfig& edited_conf = tab->m_presets->get_edited_preset().config;
                    if (initial_conf.has(dep_key) && edited_conf.has(dep_key)) {
                        const ConfigOption* conf_opt = initial_conf.option(dep_key);
                        assert(conf_opt->is_scalar());
                        // update the field
                        tab->set_value(OptionKeyIdx::scalar(dep_key), conf_opt->get_any(), conf_opt->is_enabled());
                        tab->on_value_change(OptionKeyIdx::scalar(dep_key), conf_opt->get_any());
                    }
                }
            }
            for (size_t tab_idx : modified_tabs_idx) {
                // update the decorations
                tab_list[tab_idx]->update_changed_ui();
            }
        }
        return;
    } else if (it_opt_set == m_opt_set.end() ||
               // This option doesn't have corresponded field
             is_option_without_field(opt_key_idx.key) ) {
        const ConfigOption* option = config.option(opt_key_idx.key);
        assert(option);
        assert(opt_key_idx.idx < 0);
        this->change_opt_value(opt_key_idx.key, option->is_enabled(), option->get_any(), opt_key_idx.idx);
        OptionsGroup::on_change_OG(opt_key_idx, option->is_enabled(), value);
        return;
    } else {
        const ConfigOption* option = config.option(opt_key_idx.key);
        assert(option);
        value = option->get_any(opt_key_idx.idx);
        enabled = option->is_enabled(opt_key_idx.idx);
    }

    if (this->set_value(opt_key_idx, value, enabled, false)) {
        // assert(config.option(opt_short_key)); // extruder_count: not a real config item
        on_change_OG(opt_key_idx, config.has(opt_key_idx.key) ? config.option(opt_key_idx.key)->is_enabled(opt_key_idx.idx) : true, get_value(opt_key_idx));
    }
}

void ConfigOptionsGroup::on_kill_focus(const OptionKeyIdx& opt_key)
{
    //not used: Deprecated
    //if (m_fill_empty_value)
    //    m_fill_empty_value(opt_key);
    //else
    reload_config();
}

void ConfigOptionsGroup::reload_config() {
    for (auto &opt_key_idx : m_opt_set) {
        const ConfigOption *option = m_config->option(opt_key_idx.key);
        this->set_value(opt_key_idx, option->get_any(opt_key_idx.idx), option->is_enabled(opt_key_idx.idx), false);
    }
    update_script_presets();
}

void ConfigOptionsGroup::Hide()
{
    Show(false);
}

void ConfigOptionsGroup::Show(const bool show)
{
    sizer->ShowItems(show);
#if 0//#ifdef __WXGTK__
    m_panel->Show(show);
    m_grid_sizer->Show(show);
#endif /* __WXGTK__ */
}

std::vector<size_t> get_visible_idx(const std::map<ConfigOptionMode, std::vector<size_t>>& map, ConfigOptionMode mode) {
    std::vector<size_t> ret;
    for (const auto& entry : map) {
        if (entry.first == comNone || (entry.first & mode) == mode)
            ret.insert(ret.end(), entry.second.begin(), entry.second.end());
    }
    return ret;
}
std::vector<size_t> get_invisible_idx(const std::map<ConfigOptionMode, std::vector<size_t>>& map, ConfigOptionMode mode) {
    std::vector<size_t> ret;
    for (const auto& entry : map) {
        if (entry.first != comNone && (entry.first & mode) != mode)
            ret.insert(ret.end(), entry.second.begin(), entry.second.end());
    }
    return ret;
}

bool ConfigOptionsGroup::is_visible(ConfigOptionMode mode)
{
    if (m_options_mode.empty())
        return true;

    int opt_mode_size = m_options_mode.size();
    if (opt_mode_size == 1 && m_options_mode[0].size() == 1 && m_options_mode[0].begin()->second.size() == 1)
        return get_invisible_idx(m_options_mode[0], mode).empty();

    size_t hidden_row_cnt = 0;
    for (size_t i = 0; i < opt_mode_size; i++) {
        if ((m_options_mode[i].size() == 1
            && m_options_mode[i].begin()->second.size() == 1
            && m_options_mode[i].begin()->second[0] == (size_t)-1
            && (m_options_mode[i].begin()->first != comNone && (m_options_mode[i].begin()->first & mode) != mode))
            || get_visible_idx(m_options_mode[i], mode).empty()) {
            hidden_row_cnt++;
        }
    }

    return hidden_row_cnt != opt_mode_size;
}

bool ConfigOptionsGroup::update_visibility(ConfigOptionMode mode)
{
    if (m_options_mode.empty() || !m_grid_sizer)
        return true;

    if (custom_ctrl) {
        bool show = custom_ctrl->update_visibility(mode);
        this->Show(show);
        return show;
    }

    int opt_mode_size = m_options_mode.size();
    if (m_grid_sizer->GetEffectiveRowsCount() != opt_mode_size &&
        opt_mode_size == 1 && m_options_mode[0].size() == 1 && m_options_mode[0].begin()->second.size() == 1)
        return get_invisible_idx(m_options_mode[0], mode).empty();

    Show(true);

    int idx_item = 0;
    int hidden_row_cnt = 0;
    const int cols = m_grid_sizer->GetCols();
    assert(opt_mode_size == m_line_sizer.size());
    for (int i = 0; i < opt_mode_size; i++) {
        if ((m_options_mode[i].size() == 1 
            && m_options_mode[i].begin()->second.size() == 1 
            && m_options_mode[i].begin()->second[0] == (size_t)-1 
            && (m_options_mode[i].begin()->first != comNone && (m_options_mode[i].begin()->first & mode) != mode))
                || get_visible_idx(m_options_mode[i], mode).empty()) {
            hidden_row_cnt++;
            for (size_t idx =0; idx < cols; idx++)
                m_grid_sizer->Show(idx_item + idx, false);
        }else
            for (size_t idx : get_invisible_idx(m_options_mode[i], mode))
                if(idx != (size_t)-1) m_line_sizer[i]->Show(idx, false);
        idx_item += cols;
    }

    if (hidden_row_cnt == opt_mode_size) {
        sizer->ShowItems(false);
        return false;
    }
    return true;
}

void ConfigOptionsGroup::msw_rescale()
{
    // update bitmaps for extra column items (like "mode markers" or buttons on settings panel)
    if (rescale_extra_column_item)
        for (auto extra_col : m_extra_column_item_ptrs)
            rescale_extra_column_item(extra_col);

    // update undo buttons : rescale bitmaps
    for (const auto& field : m_fields)
        field.second->msw_rescale();

    auto rescale = [](wxSizer* sizer) {
        for (wxSizerItem* item : sizer->GetChildren())
            if (item->IsWindow()) {
                wxWindow* win = item->GetWindow();
                // check if window is ScalableButton
                ScalableButton* sc_btn = dynamic_cast<ScalableButton*>(win);
                if (sc_btn) {
                    sc_btn->SetSize(sc_btn->GetBestSize());
                    return;
                }
                // check if window is wxButton
                wxButton* btn = dynamic_cast<wxButton*>(win);
                if (btn) {
                    btn->SetSize(btn->GetBestSize());
                    return;
                }
            }
    };

    // scale widgets and extra widgets if any exists
    for (const Line& line : m_lines) {
        if (line.widget_sizer)
            rescale(line.widget_sizer);
        if (line.extra_widget_sizer)
            rescale(line.extra_widget_sizer);
    }

    if (custom_ctrl)
        custom_ctrl->msw_rescale();
}

void ConfigOptionsGroup::sys_color_changed()
{
#ifdef _WIN32
    if (staticbox && stb) {
        wxGetApp().UpdateAllStaticTextDarkUI(stb);
        // update bitmaps for extra column items (like "delete" buttons on settings panel)
        for (auto extra_col : m_extra_column_item_ptrs)
            wxGetApp().UpdateDarkUI(extra_col);
    }

    if (custom_ctrl)
        wxGetApp().UpdateDarkUI(custom_ctrl);
#endif

    auto update = [](wxSizer* sizer) {
        for (wxSizerItem* item : sizer->GetChildren())
            if (item->IsWindow()) {
                wxWindow* win = item->GetWindow();
                // check if window is ScalableButton
                if (ScalableButton* sc_btn = dynamic_cast<ScalableButton*>(win)) {
                    sc_btn->sys_color_changed();
                    return;
                }
                wxGetApp().UpdateDarkUI(win, dynamic_cast<wxButton*>(win) != nullptr);
            }
    };

    // scale widgets and extra widgets if any exists
    for (const Line& line : m_lines) {
        if (line.widget_sizer)
            update(line.widget_sizer);
        if (line.extra_widget_sizer)
            update(line.extra_widget_sizer);
    }

    // update undo buttons : rescale bitmaps
    for (const auto &field : m_fields)
        field.second->sys_color_changed();
}

void ConfigOptionsGroup::refresh()
{
    if (custom_ctrl)
        custom_ctrl->Refresh();
}

std::pair<OG_CustomCtrl *, bool *> ConfigOptionsGroup::get_custom_ctrl_with_blinking_ptr(
    const t_config_option_key &opt_key, int32_t opt_index /* = -1*/) {
    Field *field = get_field({opt_key, opt_index});

    if (field)
        return {custom_ctrl, field->get_blink_ptr()};

    for (Line &line : m_lines)
        for (const Option &opt : line.get_options())
            if (opt.opt_key == opt_key && opt.opt_idx == opt_index && line.widget)
                return {custom_ctrl, line.get_blink_ptr()};

    return {nullptr, nullptr};
}

// Change an option on m_config, possibly call ModelConfig::touch().
void ConfigOptionsGroup::change_opt_value(const t_config_option_key& opt_key, bool enable, const boost::any& value, int opt_index /*= -1*/)
{
    if (m_config_mutable) {
        ConfigOption *opt = m_config_mutable->option(opt_key);
        assert(opt);
        if (opt) {
            opt->set_any(value, opt_index);
            opt->set_enabled(enable, opt_index);
        }
        assert(!m_modelconfig);
    } else {
        assert(m_modelconfig);
        m_modelconfig->set_any(opt_key, enable, value, opt_index);
    }
}

wxString OptionsGroup::get_url(const std::string& path_end)
{
    if (path_end.empty())
        return wxEmptyString;

    wxString language = wxGetApp().current_language_code_safe();
    wxString lang_marker = language.IsEmpty() ? "en" : language.BeforeFirst('_');

    return wxString( SLIC3R_DOC_URL /*"https://help.prusa3d.com/"*/) + lang_marker + wxString("/article/" + path_end);
}

bool OptionsGroup::launch_browser(const std::string& path_end)
{
    return wxGetApp().open_browser_with_warning_dialog(OptionsGroup::get_url(path_end), wxGetApp().mainframe->m_tabpanel);
}

// list of options, which doesn't have a related filed
static const std::set<t_config_option_key> options_without_field = {
    "compatible_printers",
    "compatible_prints",
    "bed_shape",
    "filament_ramming_parameters",
    "extruder_extrusion_multiplier_speed",
    "gcode_substitutions",
};

bool OptionsGroup::is_option_without_field(const t_config_option_key& opt_key)
{
    return  options_without_field.find(opt_key) != options_without_field.end();
}


//-------------------------------------------------------------------------------------------
// ogStaticText
//-------------------------------------------------------------------------------------------

ogStaticText::ogStaticText(wxWindow* parent, const wxString& text) :
    wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize)
{
    if (!text.IsEmpty()) {
		Wrap(60 * wxGetApp().em_unit());
		GetParent()->Layout();
    }
}


void ogStaticText::SetText(const wxString& value, bool wrap/* = true*/)
{
	SetLabel(value);
    if (wrap) Wrap(60 * wxGetApp().em_unit());
	GetParent()->Layout();
}

void ogStaticText::SetPathEnd(const std::string& link)
{
#ifndef __linux__

    Bind(wxEVT_ENTER_WINDOW, [this, link](wxMouseEvent& event) {
        SetToolTip(OptionsGroup::get_url(get_app_config()->get("suppress_hyperlinks") != "disable" ? link : std::string()));
        FocusText(true);
        event.Skip();
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& event) { FocusText(false); event.Skip(); });

    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (HasCapture())
            return;
        this->CaptureMouse();
        event.Skip();
    });
    Bind(wxEVT_LEFT_UP, [link, this](wxMouseEvent& event) {
        if (!HasCapture())
            return;
        ReleaseMouse();
        OptionsGroup::launch_browser(link);
        event.Skip();
    });

#else

    // Workaround: On Linux wxStaticText doesn't receive wxEVT_ENTER(LEAVE)_WINDOW events,
    // so implement this behaviour trough wxEVT_MOTION events for this control and it's parent
    Bind(wxEVT_MOTION, [link, this](wxMouseEvent& event) {
        SetToolTip(OptionsGroup::get_url(get_app_config()->get("suppress_hyperlinks") != "disable" ? link : std::string()));
        FocusText(true);
        event.Skip();
    });
    GetParent()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        FocusText(false);
        event.Skip();
    });

    // On Linux a mouse capturing causes a totally application freeze
    Bind(wxEVT_LEFT_UP, [link, this](wxMouseEvent& event) {
        OptionsGroup::launch_browser(link);
        event.Skip();
    });

#endif
}

void ogStaticText::FocusText(bool focus)
{
    if (get_app_config()->get("suppress_hyperlinks") == "disable")
        return;

    SetFont(focus ? Slic3r::GUI::wxGetApp().link_font() :
        Slic3r::GUI::wxGetApp().normal_font());
#ifdef __linux__
    this->GetContainingSizer()->Layout();
#endif
    Refresh();
}

} // GUI
} // Slic3r
