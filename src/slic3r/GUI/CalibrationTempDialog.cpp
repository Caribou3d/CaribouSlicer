#include "CalibrationTempDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/AppConfig.hpp"
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

void CalibrationTempDialog::create_buttons(wxStdDialogButtonSizer* buttons){
    wxString choices_steps[] = { "5","10" };
    steps = new wxComboBox(this, wxID_ANY, wxString{ "10" }, wxDefaultPosition, wxDefaultSize, 2, choices_steps);
    steps->SetToolTip(_L("Select the step in celcius between two tests."));
    steps->SetSelection(1);

    wxString choices_temp[] = {"100","110","120","130","140","150","160", "170","180","190","200","210","220","230","240","250","260","270","280","290" };
    temp_low = new wxComboBox(this, wxID_ANY, wxString{ "200" }, wxDefaultPosition, wxDefaultSize, 20, choices_temp);
    temp_low->SetToolTip(_L("Select the lower temperature."));
    temp_low->SetSelection(4);

    temp_high = new wxComboBox(this, wxID_ANY, wxString{ "200" }, wxDefaultPosition, wxDefaultSize, 20, choices_temp);
    temp_high->SetToolTip(_L("Select the higher temperature."));
    temp_high->SetSelection(8);

    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Lower temp:")));
    buttons->AddSpacer(15);
    buttons->Add(temp_low);
    buttons->AddSpacer(15);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Upper temp:")));
    buttons->AddSpacer(15);
    buttons->Add(temp_high);
    buttons->AddSpacer(40);
    buttons->Add(new wxStaticText(this, wxID_ANY, _L("Steps:")));
    buttons->AddSpacer(15);
    buttons->Add(steps);
    buttons->AddSpacer(40);

    wxButton* bt = new wxButton(this, wxID_FILE1, _L("Generate"));
    bt->Bind(wxEVT_BUTTON, &CalibrationTempDialog::create_geometry, this);
    buttons->Add(bt);
}

void CalibrationTempDialog::create_geometry(wxCommandEvent& event_args) {
    Plater* plat = this->main_frame->plater();
    Model& model = plat->model();
    if (!plat->new_project(L("Temperature calibration")))
        return;

    //GLCanvas3D::set_warning_freeze(true);
    std::vector<size_t> objs_idx = plat->load_files(std::vector<std::string>{
            (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / "TempTowerBase.3mf").string()}, true, false, false, false);

    assert(objs_idx.size() == 1);
    const DynamicPrintConfig* print_config = this->gui_app->get_tab(Preset::TYPE_FFF_PRINT)->get_config();
    const DynamicPrintConfig* filament_config = this->gui_app->get_tab(Preset::TYPE_FFF_FILAMENT)->get_config();
    const DynamicPrintConfig* printer_config = this->gui_app->get_tab(Preset::TYPE_PRINTER)->get_config();

    // -- get temps
    const ConfigOptionInts* temperature_config = filament_config->option<ConfigOptionInts>("temperature");
    assert(temperature_config->values.size() >= 1);

    long temp_items_val1 = 1;
    temp_low->GetValue().ToLong(&temp_items_val1);

    long temp_items_val2 = 1;
    temp_high->GetValue().ToLong(&temp_items_val2);

    int temp_low  = std::min(temp_items_val1,temp_items_val2);
    int temp_high = std::max(temp_items_val1,temp_items_val2);

    long step_temp = 5;
    steps->GetValue().ToLong(&step_temp);

    /// --- scale ---
    const ConfigOptionFloats* nozzle_diameter_config = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];

    double layer_height;
    const ConfigOptionFloatOrPercent* first_layer_height_setting = print_config->option<ConfigOptionFloatOrPercent>("first_layer_height");
    double first_layer_height = first_layer_height_setting->get_abs_value(layer_height);

    const ConfigOptionFloat* layer_height_setting = print_config->option<ConfigOptionFloat>("layer_height");
    if (layer_height_setting != nullptr)
        layer_height = layer_height_setting->value;

    double baseheight = 1.4;
    int baselayers = std::round((1.4 - first_layer_height) / layer_height + 1);

    double zScale = (first_layer_height + (baselayers-1) * layer_height) / baseheight;
    double baseheightscaled  = baseheight * zScale;

    //do scaling
    model.objects[objs_idx[0]]->scale(1.0, 1.0, zScale);

    // add other objects
    double partheight = 10.0;
    int partlayers = std::round(partheight / layer_height );
    double zScale2 = partlayers * layer_height / partheight;
    double partheightscaled = partheight * zScale2;

    float zshift = 1.4 * zScale + 4.3;
    for (int16_t temp = temp_high; temp >= temp_low; temp-= step_temp){
        add_part(model.objects[objs_idx[0]], (boost::filesystem::path(Slic3r::resources_dir()) / "calibration" / "filament_temp" / (std::to_string(temp) + ".3mf")).string(),
                Vec3d{ 0.0,0.0,zshift }, Vec3d{ 1.0,1.0,zScale2 });
        zshift += 10;
    }

    // it's rotated but not around the good origin: correct that
    double init_z_rotate_angle = Geometry::deg2rad(plat->config()->opt_float("init_z_rotate"));
    Matrix3d rot_matrix = Eigen::Quaterniond(Eigen::AngleAxisd(init_z_rotate_angle, Vec3d{0,0,1})).toRotationMatrix();
    auto     translate_from_rotation = [&rot_matrix, &model, &objs_idx](int idx, const Vec3d &translation) {
            ModelVolume *vol_parent = model.objects[objs_idx[idx]]->volumes[model.objects[objs_idx[idx]]->volumes.size()-2];
            ModelVolume *vol = model.objects[objs_idx[idx]]->volumes[model.objects[objs_idx[idx]]->volumes.size()-1];
            //Geometry::Transformation trsf = vol->get_transformation();
            //Vec3d rotxtrans = rot_matrix * translation;
            //Vec3d  offset_reste =  trsf.get_offset()- translation;
            //Vec3d  tot = rot_matrix * translation + trsf.get_offset()- translation;
            //Vec3d  tot2 =  translation + trsf.get_offset()- rot_matrix * translation;
            //trsf.set_offset( (rot_matrix *translation) - translation + trsf.get_offset());
            Geometry::Transformation trsf = vol->get_transformation();
            trsf.set_offset( (rot_matrix *translation) + vol_parent->get_offset());
            vol->set_transformation(trsf);
        };

    /// --- main config, please modify object config when possible ---
    DynamicPrintConfig new_print_config = *print_config; //make a copy
    new_print_config.set_key_value("complete_objects", new ConfigOptionBool(false));

    /// -- generate the heat change gcode
    int16_t temperature = temp_high ;
    int parts = std::round((temp_high - temp_low) / step_temp) + 1;

    for (int16_t i = 0; i < parts; i++) {
        double changelayer = partheightscaled * i + baseheightscaled + layer_height;
        model.custom_gcode_per_print_z.gcodes.emplace_back(CustomGCode::Item{ changelayer, CustomGCode::Type::Custom , -1, "", "M104 S" + std::to_string(temperature) + " ; floor " + std::to_string(i+1) + " of the temp tower" });
        temperature -= step_temp;
    }

    /// --- custom config ---
    float brim_width = print_config->option<ConfigOptionFloat>("brim_width")->value;
    if (brim_width < nozzle_diameter * 8) {
        model.objects[objs_idx[0]]->config.set_key_value("brim_width", new ConfigOptionFloat(nozzle_diameter * 8));
    }
    model.objects[objs_idx[0]]->config.set_key_value("brim_ears", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("perimeters", new ConfigOptionInt(1));
    model.objects[objs_idx[0]]->config.set_key_value("extra_perimeters_overhangs", new ConfigOptionBool(true));
    model.objects[objs_idx[0]]->config.set_key_value("bottom_solid_layers", new ConfigOptionInt(2));
    model.objects[objs_idx[0]]->config.set_key_value("top_solid_layers", new ConfigOptionInt(3));
    model.objects[objs_idx[0]]->config.set_key_value("gap_fill_enabled", new ConfigOptionBool(false));
    model.objects[objs_idx[0]]->config.set_key_value("thin_perimeters", new ConfigOptionPercent(100));
    model.objects[objs_idx[0]]->config.set_key_value("layer_height", new ConfigOptionFloat(nozzle_diameter / 2));
    model.objects[objs_idx[0]]->config.set_key_value("fill_density", new ConfigOptionPercent(7));
    model.objects[objs_idx[0]]->config.set_key_value("solid_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinearWGapFill));
    model.objects[objs_idx[0]]->config.set_key_value("top_fill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinearWGapFill));
    //disable ironing post-process, it only slow down things
    model.objects[objs_idx[0]]->config.set_key_value("ironing", new ConfigOptionBool(false));

    //update plater
    //GLCanvas3D::set_warning_freeze(false);
    this->gui_app->get_tab(Preset::TYPE_FFF_PRINT)->load_config(new_print_config);
    plat->on_config_change(new_print_config);
    //this->gui_app->get_tab(Preset::TYPE_PRINTER)->load_config(new_printer_config);
    //plat->on_config_change(new_printer_config);
    plat->changed_objects(objs_idx);
    this->gui_app->get_tab(Preset::TYPE_FFF_PRINT)->update_dirty();
    //this->gui_app->get_tab(Preset::TYPE_PRINTER)->update_dirty();
    plat->is_preview_shown();
    //update everything, easier to code.
    ObjectList* obj = this->gui_app->obj_list();
    obj->update_after_undo_redo();

    plat->reslice();
}

} // namespace GUI
} // namespace Slic3r
