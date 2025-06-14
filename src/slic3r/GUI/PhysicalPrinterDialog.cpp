///|/ Copyright (c) Prusa Research 2020 - 2023 Oleksandra Iushchenko @YuSanka, David Kocík @kocikdav, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "PhysicalPrinterDialog.hpp"
#include "PresetComboBoxes.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "PrintHostDialogs.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "RemovableDriveManager.hpp"
#include "BitmapCache.hpp"
#include "BonjourDialog.hpp"
#include "MsgDialog.hpp"

namespace Slic3r {
namespace GUI {

#define BORDER_W 10

//------------------------------------------
//          PresetForPrinter
//------------------------------------------

PresetForPrinter::PresetForPrinter(PhysicalPrinterDialog* parent, const std::string& preset_name) :
    m_parent(parent)
{
    m_sizer = new wxBoxSizer(wxVERTICAL);

    m_delete_preset_btn = new ScalableButton(parent, wxID_ANY, "cross");
    m_delete_preset_btn->SetFont(wxGetApp().normal_font());
    m_delete_preset_btn->SetToolTip(_L("Delete this preset from this printer device"));
    m_delete_preset_btn->Bind(wxEVT_BUTTON, &PresetForPrinter::DeletePreset, this);

    m_presets_list = new PresetComboBox(parent, Preset::TYPE_PRINTER);
    m_presets_list->set_printer_technology(parent->get_printer_technology());

    m_presets_list->set_selection_changed_function([this](int selection) {
        std::string selected_string = Preset::remove_suffix_modified(m_presets_list->GetString(selection).ToUTF8().data());
        Preset* preset = wxGetApp().preset_bundle->printers.find_preset(selected_string);
        assert(preset);
        Preset& edited_preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        if (preset->name == edited_preset.name)
            preset = &edited_preset;

        // if created physical printer doesn't have any settings, use the settings from the selected preset
        if (m_parent->get_printer()->has_empty_config()) {
            // update Print Host upload from the selected preset
            m_parent->get_printer()->update_from_preset(*preset);
            // update values in parent (PhysicalPrinterDialog) 
        } 
            

        // update PrinterTechnology if it was changed
        if (m_presets_list->set_printer_technology(preset->printer_technology()))
            m_parent->set_printer_technology(preset->printer_technology());
        else 
            m_parent->update(true);

        update_full_printer_name();
    });
    m_presets_list->update(preset_name);

    m_info_line = new wxStaticText(parent, wxID_ANY, _L("This printer will be shown in the presets list as") + ":");

    m_full_printer_name = new wxStaticText(parent, wxID_ANY, "");
    m_full_printer_name->SetFont(wxGetApp().bold_font());

    wxBoxSizer* preset_sizer = new wxBoxSizer(wxHORIZONTAL);
    preset_sizer->Add(m_presets_list        , 1, wxEXPAND);
    preset_sizer->Add(m_delete_preset_btn   , 0, wxEXPAND | wxLEFT, BORDER_W);

    wxBoxSizer* name_sizer = new wxBoxSizer(wxHORIZONTAL);
    name_sizer->Add(m_info_line, 0, wxEXPAND);
    name_sizer->Add(m_full_printer_name, 0, wxEXPAND | wxLEFT, BORDER_W);

    m_sizer->Add(preset_sizer   , 0, wxEXPAND);
    m_sizer->Add(name_sizer, 0, wxEXPAND);
}

PresetForPrinter::~PresetForPrinter()
{
    m_presets_list->Destroy();
    m_delete_preset_btn->Destroy();
    m_info_line->Destroy();
    m_full_printer_name->Destroy();
}

void PresetForPrinter::DeletePreset(wxEvent& event)
{
    m_parent->DeletePreset(this);
}

void PresetForPrinter::update_full_printer_name()
{
    wxString printer_name   = m_parent->get_printer_name();
    wxString preset_name    = m_presets_list->GetString(m_presets_list->GetSelection());

    m_full_printer_name->SetLabelText(printer_name + from_u8(PhysicalPrinter::separator()) + preset_name);
}

std::string PresetForPrinter::get_preset_name()
{
    return into_u8(m_presets_list->GetString(m_presets_list->GetSelection()));
}

void PresetForPrinter::SuppressDelete()
{
    m_delete_preset_btn->Enable(false);
    
    // this case means that now we have only one related preset for the printer
    // So, allow any selection
    m_presets_list->set_printer_technology(ptAny);
    m_presets_list->update();
}

void PresetForPrinter::AllowDelete()
{
    if (!m_delete_preset_btn->IsEnabled())
        m_delete_preset_btn->Enable();

    m_presets_list->set_printer_technology(m_parent->get_printer_technology());
    m_presets_list->update();
}

void PresetForPrinter::on_sys_color_changed()
{
    m_presets_list->sys_color_changed();
    m_delete_preset_btn->sys_color_changed();
}


//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------

PhysicalPrinterDialog::PhysicalPrinterDialog(wxWindow* parent, wxString printer_name) :
    DPIDialog(parent, wxID_ANY, _L("Physical Printer"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER, "physicalprinter"),
    m_printer("", wxGetApp().preset_bundle->physical_printers.default_config())
{
    SetFont(wxGetApp().normal_font());
#ifndef _WIN32
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
    m_default_name = _L("Type here the name of your printer device");
    bool new_printer = true;

    if (printer_name.IsEmpty())
        printer_name = m_default_name;
    else {
        std::string full_name = into_u8(printer_name);
        printer_name = from_u8(PhysicalPrinter::get_short_name(full_name));
        new_printer = false;
    }

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Descriptive name for the printer") + ":");

    m_add_preset_btn = new ScalableButton(this, wxID_ANY, "add_copies");
    m_add_preset_btn->SetFont(wxGetApp().normal_font());
    m_add_preset_btn->SetToolTip(_L("Add preset for this printer device")); 
    m_add_preset_btn->Bind(wxEVT_BUTTON, &PhysicalPrinterDialog::AddPreset, this);

    m_printer_name    = new ::TextInput(this,printer_name);
    wxGetApp().UpdateDarkUI(m_printer_name);
    m_printer_name->Bind(wxEVT_TEXT, [this](wxEvent&) { this->update_full_printer_names(); });
    m_printer_name->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
        if (m_printer_name->GetValue() == m_default_name) m_printer_name->SetValue("");
        e.Skip();
    });
    m_printer_name->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
        if (m_printer_name->GetValue().empty()) m_printer_name->SetValue(m_default_name);
        e.Skip();
    }); 
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    PhysicalPrinter* printer = printers.find_printer(into_u8(printer_name));
    if (!printer) {
        const Preset& preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        m_printer = PhysicalPrinter(into_u8(printer_name), m_printer.config, preset);
        // if printer_name is empty it means that new printer is created, so enable all items in the preset list
        m_presets.emplace_back(new PresetForPrinter(this, preset.name));
    }
    else
    {
        m_printer = *printer;
        const std::set<std::string>& preset_names = printer->get_preset_names();
        for (const std::string& preset_name : preset_names)
            m_presets.emplace_back(new PresetForPrinter(this, preset_name));
    }

    if (m_presets.size() == 1)
        m_presets.front()->SuppressDelete();

    update_full_printer_names();

    m_config = &m_printer.config;

    m_optgroup = new ConfigOptionsGroup(this, _L("Print Host upload"), m_config);
    build_printhost_settings(m_optgroup);

    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    wxGetApp().UpdateDarkUI(btnOK);
    btnOK->Bind(wxEVT_BUTTON, &PhysicalPrinterDialog::OnOK, this);

    wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_CANCEL, this)));

    wxBoxSizer* nameSizer = new wxBoxSizer(wxHORIZONTAL);
    nameSizer->Add(m_printer_name, 1, wxEXPAND);
    nameSizer->Add(m_add_preset_btn, 0, wxEXPAND | wxLEFT, BORDER_W);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);
    for (PresetForPrinter* preset : m_presets)
        m_presets_sizer->Add(preset->sizer(), 1, wxEXPAND | wxTOP, BORDER_W);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(nameSizer           , 0, wxEXPAND | wxLEFT | wxRIGHT, BORDER_W);
    topSizer->Add(m_presets_sizer     , 0, wxEXPAND | wxLEFT | wxRIGHT, BORDER_W);
    topSizer->Add(m_optgroup->sizer   , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(btns                , 0, wxEXPAND | wxALL, BORDER_W); 

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    if (new_printer) {
        m_printer_name->SetFocus();
        m_printer_name->GetTextCtrl()->SelectAll();
    }

    this->Fit();
    this->Layout();

    this->CenterOnScreen();
}

PhysicalPrinterDialog::~PhysicalPrinterDialog()
{
    for (PresetForPrinter* preset : m_presets) {
        delete preset;
        preset = nullptr;
    }
}

/* TODO: test for validity vs currnt method
void PhysicalPrinterDialog::update_printers()
{
    wxBusyCursor wait;

    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));

    wxArrayString printers;
    Field *rs = m_optgroup->get_field("printhost_port");
    try {
        if (! host->get_printers(printers))
            printers.clear();
    } catch (const HostNetworkError &err) {
        printers.clear();
        show_error(this, _L("Connection to printers connected via the print host failed.") + "\n\n" + from_u8(err.what()));
    }
    Choice *choice = dynamic_cast<Choice*>(rs);
    choice->set_values(printers);
    printers.empty() ? rs->disable() : rs->enable();
}*/

void PhysicalPrinterDialog::update_printers()
{
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));

    wxArrayString printers;
    Field *rs = m_optgroup->get_field({"printhost_port", -1});
    try {
        if (!host->get_printers(printers)) {
            std::vector<std::string> slugs;

            Choice* choice = dynamic_cast<Choice*>(rs);
            choice->set_values(slugs);

            rs->widget_disable();
        } else {
            std::vector<std::string> slugs;
            for (int i = 0; i < printers.size(); i++) {
                slugs.push_back(printers[i].ToStdString());
            }

            Choice* choice = dynamic_cast<Choice*>(rs);
            choice->set_values(slugs);
            boost::any val = choice->get_value();
            boost::any any_string_type = std::string("");
            auto value_idx = std::find(slugs.begin(), slugs.end(), m_config->opt<ConfigOptionString>("printhost_port")->value);
            if ((val.empty() || (any_string_type.type() == val.type() && boost::any_cast<std::string>(val) == "")) && !slugs.empty() && value_idx == slugs.end()) {
                m_config->option("printhost_port")->set_any(slugs[0]); // change_opt_value(*m_config, "printhost_port", slugs[0]);
                //choice->set_text_value(slugs[0], false);
                choice->set_any_value(m_config->option("printhost_port")->get_any(), false);
            } else if (value_idx != slugs.end()) {
                choice->set_any_value(m_config->option("printhost_port")->get_any(), false);
            }
            rs->widget_enable();
        }
    } catch (HostNetworkError error) {
        show_error(this, error.what());
    }
}

void PhysicalPrinterDialog::build_printhost_settings(ConfigOptionsGroup* m_optgroup)
{
    m_optgroup->m_on_change = [this](const OptionKeyIdx &opt_key_idx, bool enabled, const boost::any &value) {
        assert(enabled);
        if(opt_key_idx.key == "printhost_client_cert_enabled")
            this->m_show_cert_fields = boost::any_cast<bool>(value);
        if (!this->m_show_cert_fields && !m_config->opt_string("printhost_client_cert").empty()) {
            m_config->option("printhost_client_cert")->set_any(std::string("")); //change_opt_value(*m_config, "printhost_client_cert", std::string(""));
            //change_opt_value(*m_config, "printhost_client_cert_password", "");
            m_config->set_deserialize_strict("printhost_client_cert_password", "");
        }
        if (opt_key_idx.key == "host_type" || opt_key_idx.key == "printhost_authorization_type" ||
            opt_key_idx.key == "printhost_client_cert_enabled")
            this->update();
        if (opt_key_idx.key == "print_host")
            this->update_printhost_buttons();
    };

    m_optgroup->append_single_option_line("host_type");

    auto create_sizer_with_btn = [](wxWindow* parent, ScalableButton** btn, const std::string& icon_name, const wxString& label) {
        *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        (*btn)->SetFont(wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(*btn);
        return sizer;
    };

    auto printhost_browse = [=](wxWindow* parent) 
    {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_browse_btn, "browse", _L("Browse") + " " + dots);
        m_printhost_browse_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
            BonjourDialog dialog(this, Preset::printer_technology(m_printer.config));
            if (dialog.show_and_lookup()) {
                m_optgroup->set_value({"print_host", -1}, dialog.get_selected(), true, true);
                m_optgroup->get_field({"print_host", -1})->field_changed();
            }
        });

        return sizer;
    };

    auto print_host_test = [=](wxWindow* parent) {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_test_btn, "test", _L("Test"));

        m_printhost_test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
            if (!host) {
                const wxString text = _L("Could not get a valid Printer Host reference");
                show_error(this, text);
                return;
            }
            wxString msg;
            bool result;
            {
                // Show a wait cursor during the connection test, as it is blocking UI.
                wxBusyCursor wait;
                result = host->test(msg);
            }
            if (result)
                show_info(this, host->get_test_ok_msg(), _L("Success!"));
            else
                show_error(this, host->get_test_failed_msg(msg));
            });

        return sizer;
    };

    auto print_host_printers = [this, create_sizer_with_btn](wxWindow* parent) {
        //add_scaled_button(parent, &m_printhost_port_browse_btn, "browse", _(L("Refresh Printers")), wxBU_LEFT | wxBU_EXACTFIT);
        auto sizer = create_sizer_with_btn(parent, &m_printhost_port_browse_btn, "browse", _(L("Refresh Printers")));
        ScalableButton* btn = m_printhost_port_browse_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
        btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent e) { update_printers(); });
        return sizer;
    };

    // Set a wider width for a better alignment
    Option option    = m_optgroup->create_option_from_def("print_host");
    option.opt.width = Field::def_width_wider();
    Line host_line = m_optgroup->create_single_option_line(option);
    host_line.append_widget(printhost_browse);
    host_line.append_widget(print_host_test);
    m_optgroup->append_line(host_line);

    m_optgroup->append_single_option_line("printhost_authorization_type");

    option           = m_optgroup->create_option_from_def("printhost_apikey");
    option.opt.width = Field::def_width_wider();
    m_optgroup->append_single_option_line(option);

    option           = m_optgroup->create_option_from_def("printhost_port");
    option.opt.width = Field::def_width_wider();
    Line port_line = m_optgroup->create_single_option_line(option);
    port_line.append_widget(print_host_printers);
    m_optgroup->append_line(port_line);

    {
        ConfigOptionDef def;
        def.label = L("Enable 2-way ssl authentication");
        def.type = coBool;
        def.tooltip = L("Use this option to enable 2-way ssl authentication with you printer.");
        this->m_show_cert_fields = !m_config->opt_string("printhost_client_cert").empty();
        def.set_default_value(new ConfigOptionBool{ this->m_show_cert_fields });
        def.opt_key = "printhost_client_cert_enabled";
        Option option(def);
        option.opt.width = Field::def_width_wider();
        m_optgroup->append_single_option_line(option);
    }

    option = m_optgroup->create_option_from_def("printhost_client_cert");
    option.opt.width = Field::def_width_wider();
    Line client_cert_line = m_optgroup->create_single_option_line(option);

    auto printhost_client_cert_browse = [=](wxWindow* parent) {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_client_cert_browse_btn, "browse", _L("Browse") + " " + dots);
        m_printhost_client_cert_browse_btn->Bind(wxEVT_BUTTON, [this, m_optgroup](wxCommandEvent e) {
            static const auto filemasks = _L("Client certificate files (*.pfx, *.p12)|*.pfx;*.p12|All files|*.*");
            wxFileDialog openFileDialog(this, _L("Open Client certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (openFileDialog.ShowModal() != wxID_CANCEL) {
                m_optgroup->set_value(OptionKeyIdx::scalar("printhost_client_cert"), std::move(openFileDialog.GetPath()), true, true);
                m_optgroup->get_field(OptionKeyIdx::scalar("printhost_client_cert"))->field_changed();
            }
            });

        return sizer;
    };

    client_cert_line.append_widget(printhost_client_cert_browse);
    m_optgroup->append_line(client_cert_line);

    auto client_cert_hint =  _u8L("Client certificate (2-way SSL):") + "\n\t" +
        _u8L("Client certificate is optional. It is only needed if you use 2-way ssl.");
#ifdef __APPLE__
    client_cert_hint += "\n\t" +
        _u8L("To use a client cert on MacOS, you might need to add your certificate to your keychain and make sure it's trusted.") + "\n\t" +
        _u8L("You can either use a path to your certificate or the name of your certificate as you can find it in your Keychain");
#endif //__APPLE__

    Line clientcert_hint{ "", "" };
    clientcert_hint.full_width = 1;
    clientcert_hint.widget = [this, client_cert_hint](wxWindow* parent) {
        auto txt = new wxStaticText(parent, wxID_ANY, client_cert_hint);
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(txt);
        return sizer;
    };
    m_optgroup->append_line(clientcert_hint);

    option           = m_optgroup->create_option_from_def("printhost_client_cert_password");
    option.opt.width = Field::def_width_wider();
    m_optgroup->append_single_option_line(option);

    const auto ca_file_hint = _u8L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate.");

    if (Http::ca_file_supported()) {
        option           = m_optgroup->create_option_from_def("printhost_cafile");
        option.opt.width = Field::def_width_wider();
        Line cafile_line = m_optgroup->create_single_option_line(option);

        auto printhost_cafile_browse = [=](wxWindow* parent) {
            auto sizer = create_sizer_with_btn(parent, &m_printhost_cafile_browse_btn, "browse", _L("Browse") + " " + dots);
            m_printhost_cafile_browse_btn->Bind(wxEVT_BUTTON, [this, m_optgroup](wxCommandEvent e) {
                static const auto filemasks = _L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*");
                wxFileDialog openFileDialog(this, _L("Open CA certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                if (openFileDialog.ShowModal() != wxID_CANCEL) {
                    m_optgroup->set_value(OptionKeyIdx::scalar("printhost_cafile"), openFileDialog.GetPath(), true, true);
                    m_optgroup->get_field(OptionKeyIdx::scalar("printhost_cafile"))->field_changed();
                }
                });

            return sizer;
        };

        cafile_line.append_widget(printhost_cafile_browse);
        m_optgroup->append_line(cafile_line);

        Line cafile_hint{ "", "" };
        cafile_hint.full_width = 1;
        cafile_hint.widget = [ca_file_hint](wxWindow* parent) {
            auto txt = new wxStaticText(parent, wxID_ANY, ca_file_hint);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt);
            return sizer;
        };
        m_optgroup->append_line(cafile_hint);
    }
    else {
        
        Line line{ "", "" };
        line.full_width = 1;

        line.widget = [ca_file_hint](wxWindow* parent) {
            std::string info = _u8L("HTTPS CA File") + ":\n\t" +
                (boost::format(_u8L("On this system, %s uses HTTPS certificates from the system Certificate Store or Keychain.")) % SLIC3R_APP_NAME).str() +
                "\n\t" + _u8L("To use a custom CA file, please import your CA file into Certificate Store / Keychain.");

            //auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\n\t%2%") % info % ca_file_hint).str()));
            auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\t%2%") % info % ca_file_hint).str()));
            txt->SetFont(wxGetApp().normal_font());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt, 1, wxEXPAND);
            return sizer;
        };
        m_optgroup->append_line(line);
    }

    for (const std::string& opt_key : std::vector<std::string>{ "printhost_user", "printhost_password" }) {        
        option           = m_optgroup->create_option_from_def(opt_key);
        option.opt.width = Field::def_width_wider();
        m_optgroup->append_single_option_line(option);
    }

#ifdef WIN32
    option           = m_optgroup->create_option_from_def("printhost_ssl_ignore_revoke");
    option.opt.width = Field::def_width_wider();
    m_optgroup->append_single_option_line(option);
#endif

    m_optgroup->activate();

    const auto opt = m_config->option<ConfigOptionEnum<PrintHostType>>("host_type");
    m_last_host_type = opt->value;
    m_opened_as_connect = (m_last_host_type == htPrusaConnect);

    Field* printhost_field = m_optgroup->get_field(OptionKeyIdx::scalar("print_host"));
    if (printhost_field)
    {
        text_ctrl* temp = dynamic_cast<text_ctrl*>(printhost_field->getWindow());
        if (temp) {
            temp->Bind(wxEVT_TEXT, ([printhost_field, temp](wxEvent& e)
            {
#ifndef __WXGTK__
                e.Skip();
                temp->GetToolTip()->Enable(true);
#endif // __WXGTK__
                // Remove all leading and trailing spaces from the input
                std::string trimed_str, str = trimed_str = temp->GetValue().ToStdString();
                boost::trim(trimed_str);
                if (trimed_str != str)
                    temp->SetValue(trimed_str);

                TextCtrl* field = dynamic_cast<TextCtrl*>(printhost_field);
                if (field)
                    field->propagate_value();
            }), temp->GetId());
        }
    }

    // Always fill in the "printhost_port" combo box from the config and select it.
    {
        Choice* choice = dynamic_cast<Choice*>(m_optgroup->get_field(OptionKeyIdx::scalar("printhost_port")));
        const std::vector<std::string> choice_values = { m_config->opt_string("printhost_port") };
        choice->set_values(choice_values);
        choice->set_selection();
    }

    update(true);
}

void PhysicalPrinterDialog::update_printhost_buttons()
{
    std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
    m_printhost_test_btn->Enable(!m_config->opt_string("print_host").empty() && host->can_test());
    m_printhost_browse_btn->Enable(host->has_auto_discovery());
}

void PhysicalPrinterDialog::update(bool printer_change)
{
    m_optgroup->reload_config();

    const PrinterTechnology tech = Preset::printer_technology(*m_config);
    // Only offer the host type selection for FFF, for SLA it's always the SL1 printer (at the moment)
    bool supports_multiple_printers = false;
    if (tech == ptFFF) {
        update_host_type(printer_change);
        const auto opt = m_config->option<ConfigOptionEnum<PrintHostType>>("host_type");
        m_optgroup->show_field("host_type");

        if (opt && opt->value == htPrusaLink) { // PrusaConnect does NOT allow http digest
            m_optgroup->show_field("printhost_authorization_type");
            AuthorizationType auth_type = m_config->option<ConfigOptionEnum<AuthorizationType>>("printhost_authorization_type")->value;
            m_optgroup->show_field("printhost_apikey", auth_type == AuthorizationType::atKeyPassword);
            for (const char* opt_key : { "printhost_user", "printhost_password" })
                m_optgroup->show_field(opt_key, auth_type == AuthorizationType::atUserPassword); 
        } else {
            m_optgroup->hide_field("printhost_authorization_type");
            m_optgroup->show_field("printhost_apikey", true);
            for (const std::string& opt_key : std::vector<std::string>{ "printhost_user", "printhost_password" })
                m_optgroup->hide_field(opt_key);
            supports_multiple_printers = opt && opt->value == htRepetier;
        }
        // Hide Browse and Test buttons for Connect
        if (opt && opt->value == htPrusaConnect) {
            m_printhost_browse_btn->Hide();
            // hide show hostname and PrusaConnect address
            Field* printhost_field = m_optgroup->get_field(OptionKeyIdx::scalar("print_host"));
            text_ctrl* printhost_win = printhost_field ? dynamic_cast<text_ctrl*>(printhost_field->getWindow()) : nullptr;
            if (!m_opened_as_connect && printhost_win && m_last_host_type != htPrusaConnect){
                m_stored_host = printhost_win->GetValue();
                printhost_win->SetValue(L"https://connect.prusa3d.com");
            }
        } else {
            m_printhost_browse_btn->Show();
            // hide PrusaConnect address and show hostname
            Field* printhost_field = m_optgroup->get_field(OptionKeyIdx::scalar("print_host"));
            text_ctrl* printhost_win = printhost_field ? dynamic_cast<text_ctrl*>(printhost_field->getWindow()) : nullptr;
            if (!m_opened_as_connect && printhost_win && m_last_host_type == htPrusaConnect) {
                wxString temp_host = printhost_win->GetValue();
                printhost_win->SetValue(m_stored_host);
                m_stored_host = temp_host;
            }
        }
        if (opt)
            m_last_host_type = opt->value;          

        // hide api key for klipper
        if (opt && opt->value == htKlipper) {
            m_optgroup->hide_field("printhost_apikey");
        }

        // hide api key and ca file for MPMDv2
        if (opt && opt->value == htMPMDv2) {
            m_optgroup->hide_field("printhost_apikey");
            m_optgroup->hide_field("printhost_cafile");
        }

        // Hide client cert options if disabled
        m_optgroup->show_field("printhost_client_cert", this->m_show_cert_fields);
        m_optgroup->show_field("printhost_client_cert_password", this->m_show_cert_fields);
    }
    else {
        m_optgroup->set_value(OptionKeyIdx::scalar("host_type"), int(PrintHostType::htOctoPrint), true, false);
        m_optgroup->hide_field("host_type");

        m_optgroup->show_field("printhost_authorization_type");

        AuthorizationType auth_type = m_config->option<ConfigOptionEnum<AuthorizationType>>("printhost_authorization_type")->value;
        m_optgroup->show_field("printhost_apikey", auth_type == AuthorizationType::atKeyPassword);

        for (const char *opt_key : { "printhost_user", "printhost_password" })
            m_optgroup->show_field(opt_key, auth_type == AuthorizationType::atUserPassword);
    }

    {
        std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
        if (m_printhost_test_btn)
            m_printhost_test_btn->Enable(host->can_test());

        if (m_printhost_browse_btn)
            m_printhost_browse_btn->Enable(host->has_auto_discovery());

        m_printhost_port_browse_btn->Enable(supports_multiple_printers);

        m_optgroup->show_field("printhost_port", supports_multiple_printers);
        if (supports_multiple_printers) {
            update_printers();
        }
    }


    this->Fit();
    this->Layout();
#ifdef __WXMSW__
    this->Refresh();
#endif
}

void PhysicalPrinterDialog::update_host_type(bool printer_change)
{
    if (m_presets.empty())
        return;
    struct {
        bool supported { true };
        wxString label;
    } link, connect;
    // allowed models are: all MINI, all MK3 and newer, MK2.5 and MK2.5S  
    auto model_supports_prusalink = [](const std::string& model) {
        return model.size() >= 2 &&
                (( boost::starts_with(model, "MK") && model[2] > '2' && model[2] <= '9')
                || boost::starts_with(model, "MINI")
                || boost::starts_with(model, "MK2.5")
                || boost::starts_with(model, "XL")
                );
    };
    // allowed models are: all MK3/S and MK2.5/S. 
    // Since 2.6.2 also MINI, which makes list of supported printers same for both services.
    // Lets keep these 2 functions separated for now.
    auto model_supports_prusaconnect = [](const std::string& model) {
        return model.size() >= 2 &&
                ((boost::starts_with(model, "MK") && model[2] > '2' && model[2] <= '9')
                || boost::starts_with(model, "MINI")
                || boost::starts_with(model, "MK2.5")
                || boost::starts_with(model, "XL")
                );
    };

    // set all_presets_are_prusalink_supported
    for (PresetForPrinter* prstft : m_presets) {
        std::string preset_name = prstft->get_preset_name();
        if (Preset* preset = wxGetApp().preset_bundle->printers.find_preset(preset_name)) {
            std::string model_id = preset->config.opt_string("printer_model");            
            if (preset->vendor) {
                if (boost::starts_with(preset->vendor->name , "Prusa")) {
                    const std::vector<VendorProfile::PrinterModel>& models = preset->vendor->models;
                    auto it = std::find_if(models.begin(), models.end(),
                        [model_id](const VendorProfile::PrinterModel& model) { return model.id == model_id; });
                    if (it != models.end() && model_supports_prusalink(it->family))
                        continue;
                }
            }
            else if (model_supports_prusalink(model_id))
                continue;
        }
        link.supported = false;
        break;
    }

    // set all_presets_are_prusaconnect_supported
    for (PresetForPrinter* prstft : m_presets) {
        std::string preset_name = prstft->get_preset_name();
        Preset* preset = wxGetApp().preset_bundle->printers.find_preset(preset_name);
        if (!preset) {
            connect.supported = false;
            break;
        }
        std::string model_id = preset->config.opt_string("printer_model");
        if (preset->vendor && preset->vendor->name != "Prusa Research") {
            connect.supported = false;
            break;
        }
        if (preset->vendor && preset->vendor->name != "Prusa Research") {
            connect.supported = false;
            break;
        }
        // model id should be enough for this case
        if (!model_supports_prusaconnect(model_id)) {
            connect.supported = false;
            break;
        }
    }

    Field* ht = m_optgroup->get_field(OptionKeyIdx::scalar("host_type"));
    wxArrayString types;
    int last_in_conf = m_config->option("host_type")->get_int(); //  this is real position in last choice


    // Append localized enum_labels
// TODO: review if it's good this time. supermerill/SuperSlicer#2395 f5afec0                                   
    assert(ht->m_opt.enum_def->labels().size() == ht->m_opt.enum_def->values().size());
    for (size_t i = 0; i < ht->m_opt.enum_def->labels().size(); ++ i) {
        wxString label = _(ht->m_opt.enum_def->label(i));
        if (const std::string &value = ht->m_opt.enum_def->value(i);
            value == "prusalink") {
            link.label = label;
            if (!link.supported)
                continue;
        } else if (value == "prusaconnect") {
            connect.label = label;
            if (!connect.supported)
                continue;
        }

        types.Add(label);
    }

    Choice* choice = dynamic_cast<Choice*>(ht);
    choice->set_values(types);
    int32_t index_in_choice = (printer_change ? std::clamp(last_in_conf - ((int32_t)ht->m_opt.enum_def->values().size() - (int32_t)types.size()), 0, (int32_t)ht->m_opt.enum_def->values().size() - 1) : last_in_conf);
    choice->set_any_value(index_in_choice, false);
    if (link.supported && link.label == _(ht->m_opt.enum_def->label(index_in_choice)))
        m_config->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htPrusaLink));
    else if (connect.supported && connect.label == _(ht->m_opt.enum_def->label(index_in_choice)))
        m_config->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htPrusaConnect));
    else {
        int host_type = std::clamp(index_in_choice + ((int)ht->m_opt.enum_def->values().size() - (int)types.size()), 0, (int)ht->m_opt.enum_def->values().size() - 1);
        PrintHostType type = static_cast<PrintHostType>(host_type);
        m_config->set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(type));
    }
}


wxString PhysicalPrinterDialog::get_printer_name()
{
    return m_printer_name->GetValue();
}

void PhysicalPrinterDialog::update_full_printer_names()
{
    // check input symbols for usability

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    wxString printer_name = m_printer_name->GetValue();
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        size_t pos = printer_name.find_first_of(unusable_symbols[i]);
        if (pos != std::string::npos) {
            wxString str = printer_name.SubString(pos, 1);
            printer_name.Remove(pos, 1);
            InfoDialog(this, format_wxstr("%1%: \"%2%\" ", _L("Unexpected character"),  str), 
                       _L("The following characters are not allowed in the name") + ": " + unusable_symbols).ShowModal();
            m_printer_name->SetValue(printer_name);
            m_printer_name->GetTextCtrl()->SetInsertionPointEnd();
            return;
        }
    }

    for (PresetForPrinter* preset : m_presets)
        preset->update_full_printer_name();

    this->Layout();
}

void PhysicalPrinterDialog::set_printer_technology(PrinterTechnology pt)
{
    m_config->set_key_value("printer_technology", new ConfigOptionEnum<PrinterTechnology>(pt));
    update(true);
}

PrinterTechnology PhysicalPrinterDialog::get_printer_technology()
{
    return m_printer.printer_technology();
}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    m_optgroup->msw_rescale();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void PhysicalPrinterDialog::on_sys_color_changed()
{
    m_add_preset_btn->sys_color_changed();
    m_printhost_browse_btn->sys_color_changed();
    m_printhost_test_btn->sys_color_changed();
    if (m_printhost_cafile_browse_btn)
        m_printhost_cafile_browse_btn->sys_color_changed();

    for (PresetForPrinter* preset : m_presets)
        preset->on_sys_color_changed();
}

void PhysicalPrinterDialog::OnOK(wxEvent& event)
{
    wxString printer_name = m_printer_name->GetValue();
    if (printer_name.IsEmpty() || printer_name == m_default_name) {
        warning_catcher(this, _L("You have to enter a printer name."));
        return;
    }

    Field* printhost_field = m_optgroup->get_field(OptionKeyIdx::scalar("print_host"));
    text_ctrl* printhost_win = printhost_field ? dynamic_cast<text_ctrl*>(printhost_field->getWindow()) : nullptr;
    const auto opt = m_config->option<ConfigOptionEnum<PrintHostType>>("host_type");
    if (opt && opt->value == htPrusaConnect) {
        if (printhost_win && printhost_win->GetValue() != L"https://connect.prusa3d.com"){
            InfoDialog msg(this, _L("Warning"), _L("URL of PrusaConnect is different from https://connect.prusa3d.com. Do you want to continue?"), true, wxYES_NO);
            if(msg.ShowModal() != wxID_YES){
                printhost_win->SetValue(L"https://connect.prusa3d.com");
                return;
            }
        }
    }
    

    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    const PhysicalPrinter* existing = printers.find_printer(into_u8(printer_name), false);
    if (existing && into_u8(printer_name) != printers.get_selected_printer_name())
    {
        wxString msg_text = from_u8((boost::format(_u8L("Printer with name \"%1%\" already exists.")) % existing->name/*printer_name*/).str());
        msg_text += "\n" + _L("Replace?");
        //wxMessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxYES | wxNO);
        MessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxYES | wxNO);

        if (dialog.ShowModal() == wxID_NO)
            return;

        m_printer.name = existing->name;
    }

    std::set<std::string> repeat_presets;
    m_printer.reset_presets();
    for (PresetForPrinter* preset : m_presets) {
        if (!m_printer.add_preset(preset->get_preset_name()))
            repeat_presets.emplace(preset->get_preset_name());
    }

    if (!repeat_presets.empty())
    {
        wxString repeatable_presets = "\n";
        int repeat_cnt = 0;
        for (const std::string& preset_name : repeat_presets) {
            repeatable_presets += "    " + from_u8(preset_name) + "\n";
            repeat_cnt++;
        }
        repeatable_presets += "\n";

        wxString msg_text = format_wxstr(_L_PLURAL("Following printer preset is duplicated:%1%"
                                                   "The above preset for printer \"%2%\" will be used just once.",
                                                   "Following printer presets are duplicated:%1%"
                                                   "The above presets for printer \"%2%\" will be used just once.", repeat_cnt), repeatable_presets, printer_name);
        //wxMessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxOK | wxCANCEL);
        MessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxOK | wxCANCEL);
        if (dialog.ShowModal() == wxID_CANCEL)
            return;
    }

    std::string renamed_from;
    // temporary save previous printer name if it was edited
    if (m_printer.name != into_u8(m_default_name) &&
        m_printer.name != into_u8(printer_name))
        renamed_from = m_printer.name;

    //update printer name, if it was changed
    m_printer.set_name(into_u8(printer_name));

    //remove client_cert if not enabled
    if (!this->m_show_cert_fields) {
        m_config->set("printhost_client_cert", "");
        m_config->set("printhost_client_cert_password", "");
    }

    // save new physical printer
    printers.save_printer(m_printer, renamed_from);

    if (m_printer.preset_names.find(printers.get_selected_printer_preset_name()) == m_printer.preset_names.end()) {
        // select first preset for this printer
        printers.select_printer(m_printer);
        // refresh preset list on Printer Settings Tab
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(printers.get_selected_printer_preset_name());
    }
    else
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->update_preset_choice();

    event.Skip();
}

void PhysicalPrinterDialog::AddPreset(wxEvent& event)
{
    m_presets.emplace_back(new PresetForPrinter(this));
    // enable DELETE button for the first preset, if was disabled
    m_presets.front()->AllowDelete();

    m_presets_sizer->Add(m_presets.back()->sizer(), 1, wxEXPAND | wxTOP, BORDER_W);
    update_full_printer_names();
    this->Fit();

    update_host_type(true);
}

void PhysicalPrinterDialog::DeletePreset(PresetForPrinter* preset_for_printer)
{
    if (m_presets.size() == 1) {
        wxString msg_text = _L("It's not possible to delete the last related preset for the printer.");
        //wxMessageDialog dialog(nullptr, msg_text, _L("Information"), wxICON_INFORMATION | wxOK);
        MessageDialog dialog(nullptr, msg_text, _L("Information"), wxICON_INFORMATION | wxOK);
        dialog.ShowModal();
        return;
    }

    assert(preset_for_printer);
    auto it = std::find(m_presets.begin(), m_presets.end(), preset_for_printer);
    if (it == m_presets.end())
        return;

    const int remove_id = it - m_presets.begin();
    m_presets_sizer->Remove(remove_id);
    delete preset_for_printer;
    m_presets.erase(it);

    if (m_presets.size() == 1)
        m_presets.front()->SuppressDelete();

    this->Layout();
    this->Fit();

    update_host_type(true);
}
}}    // namespace Slic3r::GUI
