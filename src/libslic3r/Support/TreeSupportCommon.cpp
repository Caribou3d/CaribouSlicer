///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
// Tree supports by Thomas Rahm, losely based on Tree Supports by CuraEngine.
// Original source of Thomas Rahm's tree supports:
// https://github.com/ThomasRahm/CuraEngine
//
// Original CuraEngine copyright:
// Copyright (c) 2021 Ultimaker B.V.
// CuraEngine is released under the terms of the AGPLv3 or higher.

#include "TreeSupportCommon.hpp"
#include "libslic3r/Slicing.hpp"

namespace Slic3r::FFFTreeSupport {

TreeSupportMeshGroupSettings::TreeSupportMeshGroupSettings(const PrintObject &print_object)
{
    const PrintConfig       &print_config       = print_object.print()->config();
    const PrintObjectConfig &config             = print_object.config();
    const SlicingParameters &slicing_params     = print_object.slicing_parameters();
//    const std::vector<unsigned int>  printing_extruders = print_object.object_extruders();

    // Support must be enabled and set to Tree style.
    assert(config.support_material || config.support_material_enforce_layers > 0);
    assert(config.support_material_style.value == smsTree || config.support_material_style.value == smsOrganic);

    const double layer_height_mm = check_z_step(config.layer_height.value, print_config.z_step);
    // Calculate maximum external perimeter width over all printing regions, taking into account the default layer height.
    double external_perimeter_width = 0.;
    for (size_t region_id = 0; region_id < print_object.num_printing_regions(); ++ region_id) {
        const PrintRegion &region = print_object.printing_region(region_id);
        external_perimeter_width = std::max<double>(external_perimeter_width, region.flow(print_object, frExternalPerimeter, layer_height_mm, 2 /*not first layer, even layer*/).width());
    }

    this->layer_height              = scale_t(layer_height_mm);
    this->resolution                = scale_t(print_config.resolution_internal.value);
    // Arache feature <- why? it's not even editable when the organic support are activated! And it doesn't take into account the %! I'll fix it to 25% of external_perimeter_width.
    this->min_feature_size          = scale_t(external_perimeter_width * 0.25); //config.min_feature_size.value);
    // +1 makes the threshold inclusive
    this->support_angle             = 0.5 * M_PI - std::clamp<double>((config.support_material_threshold + 1) * M_PI / 180., 0., 0.5 * M_PI);
    this->support_line_width        = support_material_flow(&print_object, layer_height_mm).scaled_width();
    this->support_roof_line_width   = support_material_interface_flow(&print_object, layer_height_mm).scaled_width();
    //FIXME add it to SlicingParameters and reuse in both tree and normal supports?
    this->support_bottom_enable = config.support_material_interface_layers.value > 0 &&
        (!config.support_material_bottom_interface_layers.is_enabled() ||
         config.support_material_bottom_interface_layers.value > 0);
    this->support_bottom_height     = !this->support_bottom_enable ? 0 :
        this->layer_height * (config.support_material_bottom_interface_layers.is_enabled() ?
            config.support_material_bottom_interface_layers.value :
            config.support_material_interface_layers.value);

    this->support_material_buildplate_only = config.support_material_buildplate_only;
    this->support_xy_distance       = scaled<coord_t>(config.support_material_xy_spacing.get_abs_value(external_perimeter_width));
    // Separation of interfaces, it is likely smaller than support_xy_distance.
    this->support_xy_distance_overhang = std::min(this->support_xy_distance, scaled<coord_t>(0.5 * external_perimeter_width));
    // compute separate support_material_contact_distance_type. As organic support only support one layer height,
    // it has to be done here, where we have access to the printobject
    this->support_top_distance      = scaled<coord_t>(slicing_params.gap_support_object);
    this->support_bottom_distance   = scaled<coord_t>(slicing_params.gap_object_support);
    if (config.support_material_contact_distance_type.value == zdNone) {
        this->support_top_distance      = 0;
        this->support_bottom_distance   = 0;
    } else if (config.support_material_contact_distance_type.value == zdFilament && print_object.layers().size() > 0 &&
        print_object.layers().front()->regions().size() > 0) {
        //get one region, with organic support there is only one layer height anyway
        assert(print_object.num_printing_regions() > 0);
        const LayerRegion *lr = print_object.layers().front()->regions().front();
        assert(is_approx(lr->layer()->height, layer_height_mm, EPSILON) || lr->layer()->id() == 0);
        coord_t diff_lh_filamenth = scale_t(lr->bridging_height_avg()) - this->layer_height;
        this->support_top_distance += diff_lh_filamenth;
        this->support_bottom_distance += diff_lh_filamenth;
    }
//    this->support_interface_skip_height =
//    this->support_infill_angles     =
    this->support_roof_enable       = config.support_material_interface_layers.value > 0;
    this->support_roof_layers       = this->support_roof_enable ? config.support_material_interface_layers.value : 0;
    this->support_floor_enable = this->support_roof_enable &&
        (!config.support_material_bottom_interface_layers.is_enabled() ||
         config.support_material_bottom_interface_layers.value > 0);
    this->support_floor_layers = !this->support_floor_enable ? 0 :
        config.support_material_bottom_interface_layers.is_enabled() ?
                                                               config.support_material_bottom_interface_layers.value :
                                                               this->support_roof_layers;
    //    this->minimum_roof_area         =
//    this->support_roof_angles       =
    this->support_roof_pattern      = config.support_material_top_interface_pattern.value;
    this->support_pattern           = config.support_material_pattern;
    this->support_line_spacing      = scaled<coord_t>(config.support_material_spacing.value);
//    this->support_bottom_offset     =
//    this->support_wall_count        = config.support_material_with_sheath ? 1 : 0;
    this->support_wall_count        = 1;
    this->support_roof_line_distance = scaled<coord_t>(config.support_material_interface_spacing.value) + this->support_roof_line_width;
//    this->minimum_support_area      =
//    this->minimum_bottom_area       =
//    this->support_offset            =
    this->support_tree_branch_distance = scaled<coord_t>(config.support_tree_branch_distance.value);
    this->support_tree_angle          = std::clamp<double>(config.support_tree_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    this->support_tree_angle_slow     = std::clamp<double>(config.support_tree_angle_slow * M_PI / 180., 0., this->support_tree_angle - EPSILON);
    this->support_tree_branch_diameter = scaled<coord_t>(config.support_tree_branch_diameter.value);
    this->support_tree_branch_diameter_angle = std::clamp<double>(config.support_tree_branch_diameter_angle * M_PI / 180., 0., 0.5 * M_PI - EPSILON);
    this->support_tree_top_rate       = config.support_tree_top_rate.value; // percent
//    this->support_tree_tip_diameter = this->support_line_width;
    this->support_tree_tip_diameter = std::clamp(scaled<coord_t>(config.support_tree_tip_diameter.value), coord_t(0), this->support_tree_branch_diameter);
}

TreeSupportSettings::TreeSupportSettings(const TreeSupportMeshGroupSettings &mesh_group_settings, const SlicingParameters &slicing_params)
    : support_line_width(mesh_group_settings.support_line_width),
      layer_height(mesh_group_settings.layer_height),
      branch_radius(mesh_group_settings.support_tree_branch_diameter / 2),
      min_radius(mesh_group_settings.support_tree_tip_diameter / 2), // The actual radius is 50 microns larger as the resulting branches will be increased by 50 microns to avoid rounding errors effectively increasing the xydistance
      maximum_move_distance((mesh_group_settings.support_tree_angle < M_PI / 2.) ? (coord_t)(tan(mesh_group_settings.support_tree_angle) * layer_height) : std::numeric_limits<coord_t>::max()),
      maximum_move_distance_slow((mesh_group_settings.support_tree_angle_slow < M_PI / 2.) ? (coord_t)(tan(mesh_group_settings.support_tree_angle_slow) * layer_height) : std::numeric_limits<coord_t>::max()),
      support_bottom_layers(mesh_group_settings.support_bottom_enable ? (mesh_group_settings.support_bottom_height + layer_height / 2) / layer_height : 0),
      // Ensure lines always stack nicely even if layer height is large.
      tip_layers(std::max((branch_radius - min_radius) / (support_line_width / 3), branch_radius / layer_height)),
      branch_radius_increase_per_layer(tan(mesh_group_settings.support_tree_branch_diameter_angle) * layer_height),
      max_to_model_radius_increase(mesh_group_settings.support_tree_max_diameter_increase_by_merges_when_support_to_model / 2),
      min_dtt_to_model(round_up_divide(mesh_group_settings.support_tree_min_height_to_model, layer_height)),
      increase_radius_until_radius(mesh_group_settings.support_tree_branch_diameter / 2),
      increase_radius_until_layer(increase_radius_until_radius <= branch_radius ? tip_layers * (increase_radius_until_radius / branch_radius) : (increase_radius_until_radius - branch_radius) / branch_radius_increase_per_layer),
      support_rests_on_model(! mesh_group_settings.support_material_buildplate_only),
      xy_distance(mesh_group_settings.support_xy_distance),
      xy_min_distance(std::min(mesh_group_settings.support_xy_distance, mesh_group_settings.support_xy_distance_overhang)),
      bp_radius(mesh_group_settings.support_tree_bp_diameter / 2),
      // Increase by half a line overlap, but not faster than 40 degrees angle (0 degrees means zero increase in radius).
      bp_radius_increase_per_layer(std::min(tan(0.7) * layer_height, 0.5 * support_line_width)),
      z_distance_bottom_layers(size_t(round(double(mesh_group_settings.support_bottom_distance) / double(layer_height)))),
      z_distance_top_layers(size_t(round(double(mesh_group_settings.support_top_distance) / double(layer_height)))),
//              support_infill_angles(mesh_group_settings.support_infill_angles),
      support_roof_angles(mesh_group_settings.support_roof_angles),
      roof_pattern(mesh_group_settings.support_roof_pattern),
      support_pattern(mesh_group_settings.support_pattern),
      support_roof_line_width(mesh_group_settings.support_roof_line_width),
      support_line_spacing(mesh_group_settings.support_line_spacing),
      support_bottom_offset(mesh_group_settings.support_bottom_offset),
      support_wall_count(mesh_group_settings.support_wall_count),
      resolution(mesh_group_settings.resolution),
      support_roof_line_distance(mesh_group_settings.support_roof_line_distance), // in the end the actual infill has to be calculated to subtract interface from support areas according to interface_preference.
      settings(mesh_group_settings),
      min_feature_size(mesh_group_settings.min_feature_size)
{
    // At least one tip layer must be defined.
    assert(tip_layers > 0);

    layer_start_bp_radius = (bp_radius - branch_radius) / bp_radius_increase_per_layer;

    if (TreeSupportSettings::soluble) {
        // safeOffsetInc can only work in steps of the size xy_min_distance in the worst case => xy_min_distance has to be a bit larger than 0 in this worst case and should be large enough for performance to not suffer extremely
        // When for all meshes the z bottom and top distance is more than one layer though the worst case is xy_min_distance + min_feature_size
        // This is not the best solution, but the only one to ensure areas can not lag though walls at high maximum_move_distance.
        xy_min_distance = std::max(xy_min_distance, scaled<coord_t>(0.1));
        xy_distance     = std::max(xy_distance, xy_min_distance);
    }

//            const std::unordered_map<std::string, InterfacePreference> interface_map = { { "support_area_overwrite_interface_area", InterfacePreference::SupportAreaOverwritesInterface }, { "interface_area_overwrite_support_area", InterfacePreference::InterfaceAreaOverwritesSupport }, { "support_lines_overwrite_interface_area", InterfacePreference::SupportLinesOverwriteInterface }, { "interface_lines_overwrite_support_area", InterfacePreference::InterfaceLinesOverwriteSupport }, { "nothing", InterfacePreference::Nothing } };
//            interface_preference = interface_map.at(mesh_group_settings.get<std::string>("support_interface_priority"));
//FIXME this was the default
//            interface_preference = InterfacePreference::SupportLinesOverwriteInterface;
    //interface_preference = InterfacePreference::SupportAreaOverwritesInterface;
    interface_preference = InterfacePreference::InterfaceAreaOverwritesSupport;

    if (slicing_params.raft_layers() > 0) {
        // Fill in raft_layers with the heights of the layers below the first object layer.
        // First layer
        double z = slicing_params.first_print_layer_height;
        this->raft_layers.emplace_back(z);
        // Raft base layers
        for (size_t i = 1; i < slicing_params.base_raft_layers; ++ i) {
            z += slicing_params.base_raft_layer_height;
            this->raft_layers.emplace_back(z);
        }
        // Raft interface layers
        for (size_t i = 0; i + 1 < slicing_params.interface_raft_layers; ++ i) {
            z += slicing_params.interface_raft_layer_height;
            this->raft_layers.emplace_back(z);
        }
        // Raft contact layer
        if (slicing_params.raft_layers() > 1) {
            z = slicing_params.raft_contact_top_z;
            this->raft_layers.emplace_back(z);
        }
        if (double dist_to_go = slicing_params.object_print_z_min - z; dist_to_go > EPSILON) {
            // Layers between the raft contacts and bottom of the object.
            auto nsteps = int(ceil(dist_to_go / slicing_params.max_suport_layer_height));
            double step = dist_to_go / nsteps;
            for (int i = 0; i < nsteps; ++ i) {
                z += step;
                this->raft_layers.emplace_back(z);
            }
        }
    }
}

#if defined(TREE_SUPPORT_SHOW_ERRORS) && defined(_WIN32)
    #define TREE_SUPPORT_SHOW_ERRORS_WIN32
    #include <windows.h>
#endif

// Shared with generate_support_areas()
bool g_showed_critical_error = false;
bool g_showed_performance_warning = false;

void tree_supports_show_error(std::string_view message, bool critical)
{ // todo Remove!  ONLY FOR PUBLIC BETA!!
    printf("Error: %s, critical: %d\n", message.data(), int(critical));
#ifdef TREE_SUPPORT_SHOW_ERRORS_WIN32
    static bool showed_critical = false;
    static bool showed_performance = false;
    auto bugtype = std::string(critical ? " This is a critical bug. It may cause missing or malformed branches.\n" : "This bug should only decrease performance.\n");
    bool show    = (critical && !g_showed_critical_error) || (!critical && !g_showed_performance_warning);
    (critical ? g_showed_critical_error : g_showed_performance_warning) = true;
    if (show)
        MessageBoxA(nullptr, std::string("TreeSupport_2 MOD detected an error while generating the tree support.\nPlease report this back to me with profile and model.\nRevision 5.0\n" + std::string(message) + "\n" + bugtype).c_str(),
            "Bug detected!", MB_OK | MB_SYSTEMMODAL | MB_SETFOREGROUND | MB_ICONWARNING);
#endif // TREE_SUPPORT_SHOW_ERRORS_WIN32
}

} // namespace Slic3r::FFFTreeSupport