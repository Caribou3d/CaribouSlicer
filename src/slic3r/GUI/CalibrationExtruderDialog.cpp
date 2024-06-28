#include "CalibrationExtruderDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Utils.hpp"
#include "GLCanvas3D.hpp"
#include "GUI.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "Tab.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include <wx/file.h>
#include "wxExtensions.hpp"

#if ENABLE_SCROLLABLE
static wxSize get_screen_size(wxWindow* window)
{
    const auto idx = wxDisplay::GetFromWindow(window);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);
    return display.GetClientArea().GetSize();
}
#endif // ENABLE_SCROLLABLE

namespace Slic3r {
namespace GUI {

void CalibrationExtruderDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_dimension[] = {"60","80","100"};
    dimension = new wxComboBox(this, wxID_ANY, wxString{ "80" }, wxDefaultPosition, wxDefaultSize, 3, choices_dimension);
    dimension->SetToolTip(_L("You can choose the size of the cube."
        " It's a simple scale, you can modify it in the right panel yourself if you prefer. It's just quicker to select it here."));
    dimension->SetSelection(1);

    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Dimension:")));
    buttons->AddSpacer(10);
    buttons->Add(dimension);
    buttons->AddSpacer(10);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("mm")));
    buttons->AddSpacer(40);
    wxButton* bt = new wxButton(this, wxID_FILE1, _(L("Generate")));
    bt->Bind(wxEVT_BUTTON, &CalibrationExtruderDialog::create_geometry_v, this);
    buttons->Add(bt);
}

void CalibrationExtruderDialog::create_geometry() {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    plat->new_project();

    //GLCanvas3D::set_warning_freeze(true);
      std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration"/"extrusionmultiplier"/ "low_cube.3mf").string()}, true, false, false, false);

    assert(objs_idx.size() == 1);
    const DynamicPrintConfig* filamentConfig = this->gui_app->get_tab(Preset::TYPE_FFF_FILAMENT)->get_config();
    const DynamicPrintConfig* printerConfig = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();

    /// --- scale ---
    //model is created for a 0.4 nozzle, scale xy with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    //float nozzle_diameter = nozzle_diameter_config->values[0];
    float nozzle_diameter = nozzle_diameter_config->get_at(0);

    int idx_scale = dimension->GetSelection();
    double xyScale = 1;

    if (idx_scale == 0){
        xyScale = 0.75;
    } else if (idx_scale == 2) {
        xyScale = 1.25;
    }

    double zScale = 1;
    //do scaling
    model.objects[objs_idx[0]]->scale(xyScale, xyScale, zScale);

    /// --- custom config ---

    model.objects[objs_idx[0]]->config.set_key_value("perimeter_reverse", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(0));
    model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(1));
    model.objects[objs_idx[0]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(0));
    model.objects[objs_idx[0]]->config.set_key_value("fill_density", new ConfigOptionPercent(0));
    model.objects[objs_idx[0]]->config.set_key_value("perimeter_generator", new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Classic));
    model.objects[objs_idx[0]]->config.set_key_value("support_material", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("support_material_enforce_layers", new ConfigOptionInt(0));
    model.objects[objs_idx[0]]->config.set_key_value("exact_last_layer_height", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("ensure_vertical_shell_thickness", new ConfigOptionBool(true));
    model.objects[objs_idx[0]]->config.set_key_value("infill_dense", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("extra_perimeters", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("extra_perimeters_overhangs", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("extra_perimeters_odd_layers", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("overhangs_reverse", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("gap_fill_last", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("solid_over_perimeters", new ConfigOptionInt(0));
    model.objects[objs_idx[0]]->config.set_key_value("seam_notch_all", new ConfigOptionFloatOrPercent(0, false));
    model.objects[objs_idx[0]]->config.set_key_value("seam_notch_inner", new ConfigOptionFloatOrPercent(0, false));
    model.objects[objs_idx[0]]->config.set_key_value("seam_notch_outer", new ConfigOptionFloatOrPercent(0, false));
    //model.objects[objs_idx[0]]->config.set_key_value("spiral_vase", new ConfigOptionBool(true));

    //update plater
    plat->changed_objects(objs_idx);
    plat->is_preview_shown();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();

    plat->reslice();
    plat->select_view_3D("Preview");
}

} // namespace GUI
} // namespace Slic3r
