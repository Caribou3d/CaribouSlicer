///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Lukáš Matěna @lukasmatena, Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Lukáš Hejl @hejllukas, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2021 Li Jiang
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_GUI_App_hpp_
#define slic3r_GUI_App_hpp_

#include <angelscript/include/angelscript.h>
#include <angelscript/add_on/scriptbuilder/scriptbuilder.h>
#include <memory>
#include <string>
#include "ImGuiWrapper.hpp"
#include "ConfigWizard.hpp"
#include "OpenGLManager.hpp"
#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"

#include <wx/app.h>
#include <wx/colour.h>
#include <wx/font.h>
#include <wx/string.h>
#include <wx/snglinst.h>

#include <mutex>
#include <stack>

class wxMenuItem;
class wxMenuBar;
class wxTopLevelWindow;
class wxDataViewCtrl;
class wxBookCtrlBase;
struct wxLanguageInfo;

namespace Slic3r {

class AppConfig;
class PresetBundle;
class PresetUpdater;
class ModelObject;
class PrintHostJobQueue;
class Model;
class AppUpdater;

namespace GUI{

class RemovableDriveManager;
class OtherInstanceMessageHandler;
class MainFrame;
class Sidebar;
class ObjectManipulation;
class ObjectSettings;
class ObjectList;
class ObjectLayers;
class Plater;
class NotificationManager;
class Downloader;
struct GUI_InitParams;
class GalleryDialog;



enum FileType
{
    FT_STL,
    FT_OBJ,
    FT_OBJECT,
    FT_STEP,
    FT_AMF,
    FT_3MF,
    FT_GCODE,
    FT_MODEL,
    FT_PROJECT,
    FT_FONTS,
    FT_GALLERY,

    FT_INI,
    FT_SVG,

    FT_TEX,

    FT_SL1,

    FT_ZIP,

    FT_SIZE,
};

extern wxString file_wildcards(FileType file_type, const std::string &custom_extension = {});

wxString sla_wildcards(OutputFormat formatid, const std::string& custom_extension);

enum ConfigMenuIDs {
    ConfigMenuWizard,
    ConfigMenuSnapshots,
    ConfigMenuTakeSnapshot,
    ConfigMenuUpdateConf,
    ConfigMenuUpdateApp,
    ConfigMenuDesktopIntegration,
    ConfigMenuPreferences,
    ConfigMenuLanguage,
    ConfigMenuFlashFirmware,
    ConfigMenuWifiConfigFile,
    ConfigMenuCnt,
    //ConfigMenuModeSimple,
    //ConfigMenuModeAdvanced,
    //ConfigMenuModeExpert,
};

class Tab;
class ConfigWizard;

static wxString dots("…", wxConvUTF8);

// Does our wxWidgets version support markup?
// https://github.com/prusa3d/PrusaSlicer/issues/4282#issuecomment-634676371
#if wxUSE_MARKUP && wxCHECK_VERSION(3, 1, 1)
    #define SUPPORTS_MARKUP
#endif

class GUI_App : public wxApp
{
public:
    enum class EAppMode : unsigned char
    {
        Editor,
        GCodeViewer
    };

private:
    bool            m_initialized { false };
    bool            m_post_initialized { false };
    bool            m_app_conf_exists{ false };
    bool            m_last_app_conf_lower_version{ false };
    EAppMode        m_app_mode{ EAppMode::Editor };
    bool            m_is_recreating_gui{ false };
    bool            m_opengl_initialized{ false };

    wxColour        m_color_label_modified;
    wxColour        m_color_label_sys;
    wxColour        m_color_label_default;
    wxColour        m_color_label_phony;
    wxColour        m_color_dark_mode_label_modified;
    wxColour        m_color_dark_mode_label_sys;
    wxColour        m_color_dark_mode_label_default;
    wxColour        m_color_dark_mode_label_phony;
    wxColour        m_color_window_default;
    wxColour        m_color_highlight_label_default;
    wxColour        m_color_hovered_btn_label;
    wxColour        m_color_hovered_btn;
    wxColour        m_color_default_btn_label;
    wxColour        m_color_highlight_default;
    wxColour        m_color_selected_btn_bg;
    bool            m_force_colors_update { false };
    //std::vector<std::string>     m_mode_palette; //replaced by Slic3r::GUI::get_app_config()->tags()

    wxFont		    m_small_font;
    wxFont		    m_bold_font;
	wxFont			m_normal_font;
	wxFont			m_code_font;
    wxFont		    m_link_font;

    int             m_em_unit; // width of a "m"-symbol in pixels for current system font
                               // Note: for 100% Scale m_em_unit = 10 -> it's a good enough coefficient for a size setting of controls

    std::unique_ptr<wxLocale> 	  m_wxLocale;
    // System language, from locales, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_system = nullptr;
    // Best translation language, provided by Windows or OSX, owned by wxWidgets.
    const wxLanguageInfo		 *m_language_info_best   = nullptr;

    OpenGLManager m_opengl_mgr;

    //AngelScript::PtrRelease<AngelScript::asIScriptEngine> m_script_engine;

    std::unique_ptr<RemovableDriveManager> m_removable_drive_manager;

    std::unique_ptr<ImGuiWrapper> m_imgui;
    std::unique_ptr<PrintHostJobQueue> m_printhost_job_queue;
	std::unique_ptr <OtherInstanceMessageHandler> m_other_instance_message_handler;
    std::unique_ptr <AppUpdater> m_app_updater;
    std::unique_ptr <wxSingleInstanceChecker> m_single_instance_checker;
    std::unique_ptr <Downloader> m_downloader;
    std::string m_instance_hash_string;
	size_t m_instance_hash_int;

public:
    bool            OnInit() override;
    bool            initialized() const { return m_initialized; }

    explicit GUI_App(EAppMode mode = EAppMode::Editor);

    EAppMode get_app_mode() const { return m_app_mode; }
    bool is_editor() const { return m_app_mode == EAppMode::Editor; }
    bool is_gcode_viewer() const { return m_app_mode == EAppMode::GCodeViewer; }
    bool is_recreating_gui() const { return m_is_recreating_gui; }
    std::string logo_name() const { return is_editor() ? SLIC3R_APP_KEY : GCODEVIEWER_APP_KEY; }

    //AngelScript::asIScriptEngine* get_script_engine() const { return m_script_engine.get(); }

    // To be called after the GUI is fully built up.
    // Process command line parameters cached in this->init_params,
    // load configs, STLs etc.
    void            post_init();
    // If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
    // Otherwise HTML formatted for the system info dialog.
    static std::string get_gl_info(bool for_github);
    wxGLContext* init_glcontext(wxGLCanvas& canvas);
    bool init_opengl();

    static unsigned get_colour_approx_luma(const wxColour &colour);
    static bool     dark_mode();
    const wxColour  get_label_default_clr_system(bool is_dark_mode);
    const wxColour  get_label_default_clr_modified(bool is_dark_mode);
    const wxColour  get_label_default_clr_default(bool is_dark_mode);
    const wxColour  get_label_default_clr_phony(bool is_dark_mode);
    const std::vector<std::string> get_mode_default_palette();
    void            init_ui_colours();
    void            update_ui_colours_from_appconfig();
    void            update_label_colours();
    // update color mode for window
    void            UpdateDarkUI(wxWindow *window, bool highlited = false, bool just_font = false);
    // update color mode for whole dialog including all children
    void            UpdateDlgDarkUI(wxDialog* dlg, bool just_buttons_update = false);
    // update color mode for DataViewControl
    void            UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited = false);
    // update color mode for panel including all static texts controls
    void            UpdateAllStaticTextDarkUI(wxWindow* parent);
    void            SetWindowVariantForButton(wxButton* btn);
    void            init_fonts();
	void            update_fonts(const MainFrame *main_frame = nullptr);
    void            set_label_clr_modified(const wxColour& clr);
    void            set_label_clr_sys(const wxColour& clr);
    void            set_label_clr_default(const wxColour& clr);
    void            set_label_clr_phony(const wxColour& clr);

    const wxColour &get_label_clr_modified();
    const wxColour &get_label_clr_sys();
    const wxColour &get_label_clr_default();
    const wxColour &get_label_clr_phony();
    const wxColour &get_window_default_clr() { return m_color_window_default; }

    const std::string       get_html_bg_color(wxWindow* html_parent);

    std::string             get_first_mode_btn_color(ConfigOptionMode mode_id) const;
    std::string             get_last_mode_btn_color(ConfigOptionMode mode_id) const;
#ifdef GUI_TAG_PALETTE
    std::vector<wxColour>   get_mode_palette() const;
    void                    set_mode_palette(const std::vector<wxColour> &palette);
#endif

    const wxColour& get_label_highlight_clr()   { return m_color_highlight_label_default; }
    const wxColour& get_highlight_default_clr() { return m_color_highlight_default; }
    const wxColour& get_color_hovered_btn_label() { return m_color_hovered_btn_label; }
    const wxColour& get_color_default_btn_label() { return m_color_default_btn_label; }
    const wxColour& get_color_hovered_btn() { return m_color_hovered_btn; }
    const wxColour& get_color_selected_btn_bg() { return m_color_selected_btn_bg; }
    void            force_colors_update();
#ifdef _MSW_DARK_MODE
    void            force_menu_update();
#endif //_MSW_DARK_MODE

    const wxFont&   small_font()            { return m_small_font; }
    const wxFont&   bold_font()             { return m_bold_font; }
    const wxFont&   normal_font()           { return m_normal_font; }
    const wxFont&   code_font()             { return m_code_font; }
    const wxFont&   link_font()             { return m_link_font; }
    int             em_unit() const         { return m_em_unit; }
    bool            tabs_as_menu() const;
    bool            suppress_round_corners() const;
    wxSize          get_min_size(wxWindow* display_win) const;
    int             get_max_font_pt_size();
    float           toolbar_icon_scale(const bool is_limited = false) const;
    void            set_auto_toolbar_icon_scale(float scale) const;
    void            check_printer_presets();

    void            recreate_GUI(const wxString& message);
    void            system_info();
    void            keyboard_shortcuts();
    void            change_calibration_dialog(const wxDialog* have_to_destroy = nullptr, wxDialog* new_one = nullptr);
    void            html_dialog();
    void            flow_ratio_dialog();
    void            flow_speed_dialog();
    void            filament_temperature_dialog();
    void            bridge_tuning_dialog();
    void            over_bridge_dialog();
    void            calibration_cube_dialog();
    void            calibration_first_layer_dialog();
    void            calibration_first_layer_patch_dialog();
    void            calibration_extrusion_multiplier_dialog();
	void            calibration_retraction_dialog();
    void            calibration_pressureadv_dialog();
    void            freecad_script_dialog();
    void            tiled_canvas_dialog();
    //void            support_tuning(); //have to do multiple, in a submenu
    void            load_project(wxWindow *parent, wxString& input_file) const;
    void            import_model(wxWindow *parent, wxArrayString& input_files) const;
    void            import_zip(wxWindow* parent, wxString& input_file) const;
    void            load_gcode(wxWindow* parent, wxString& input_file) const;

    static bool     catch_error(std::function<void()> cb, const std::string& err);

    void            persist_window_geometry(wxTopLevelWindow *window, bool default_maximized = false);
    void            update_ui_from_settings();

    bool            switch_language();
    bool            load_language(wxString language, bool initial);

    Tab*            get_tab(Preset::Type type, bool only_completed = true);
    ConfigOptionMode get_mode();
    bool            save_mode(const ConfigOptionMode mode) ;
    void            update_mode();

    void            add_config_menu(wxMenuBar *menu);
    // Compare the content of get_saved_preset() with get_edited_preset() configs, return true if they differ.
    bool            has_unsaved_preset_changes() const;
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return true if they differ.
    bool            has_current_preset_changes() const;
    void            update_saved_preset_from_current_preset();
    std::vector<const PresetCollection*> get_active_preset_collections() const;
    bool            check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice = true, bool use_dont_save_insted_of_discard = false);
    void            apply_keeped_preset_modifications();
    bool            check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes = nullptr);
    bool            can_load_project();
    bool            check_print_host_queue();
    bool            checked_tab(Tab* tab);
    void            load_current_presets(bool check_printer_presets = true);

    wxString        current_language_code() const { return m_wxLocale->GetCanonicalName(); }
	// Translate the language code to a code, for which Prusa Research maintains translations. Defaults to "en_US".
    wxString 		current_language_code_safe() const;
    bool            is_localized() const { return m_wxLocale->GetLocale() != "English"; }

    void            open_preferences(const std::string& highlight_option = std::string(), const std::string& group_name = std::string());

    virtual bool OnExceptionInMainLoop() override;
    // Calls wxLaunchDefaultBrowser if user confirms in dialog.
    // Add "Rememeber my choice" checkbox to question dialog, when it is forced or a "suppress_hyperlinks" option has empty value
    bool            open_browser_with_warning_dialog(const wxString& url, wxWindow* parent = nullptr, bool allow_remember_choice = true, int flags = 0);
#ifdef __APPLE__
    void            OSXStoreOpenFiles(const wxArrayString &files) override;
    // wxWidgets override to get an event on open files.
    void            MacOpenFiles(const wxArrayString &fileNames) override;
    void            MacOpenURL(const wxString& url) override;
#endif /* __APPLE */

    Sidebar&            sidebar();
    ObjectManipulation* obj_manipul();
    ObjectSettings*     obj_settings();
    ObjectList*         obj_list();
    ObjectLayers*       obj_layers();
    Plater*             plater();
    const Plater*        plater() const;
    Model&      		model();
    NotificationManager* notification_manager();
    GalleryDialog *      gallery_dialog();
    Downloader*          downloader();

    // Parameters extracted from the command line to be passed to GUI after initialization.
    GUI_InitParams* init_params { nullptr };

    std::unique_ptr<AppConfig> app_config;

    std::unique_ptr<PresetBundle> preset_bundle;

    std::unique_ptr<PresetUpdater> preset_updater;
    MainFrame*      mainframe{ nullptr };
    Plater*         plater_{ nullptr };
    std::mutex      not_modal_dialog_mutex;
    wxDialog*       not_modal_dialog = nullptr;

	PresetUpdater*  get_preset_updater() { return preset_updater.get(); }
    PrinterTechnology get_current_printer_technology() const;

    wxBookCtrlBase* tab_panel() const ;
    int             extruders_cnt() const;
    int             extruders_edited_cnt() const;

    std::vector<Tab *>      tabs_list;

	RemovableDriveManager* removable_drive_manager() { return m_removable_drive_manager.get(); }
	OtherInstanceMessageHandler* other_instance_message_handler() { return m_other_instance_message_handler.get(); }
    wxSingleInstanceChecker* single_instance_checker() {return m_single_instance_checker.get();}

	void        init_single_instance_checker(const std::string &name, const std::string &path);
	void        set_instance_hash (const size_t hash) { m_instance_hash_int = hash; m_instance_hash_string = std::to_string(hash); }
    std::string get_instance_hash_string ()           { return m_instance_hash_string; }
	size_t      get_instance_hash_int ()              { return m_instance_hash_int; }

    ImGuiWrapper* imgui() { return m_imgui.get(); }

    PrintHostJobQueue& printhost_job_queue() { return *m_printhost_job_queue.get(); }

    void            open_web_page_localized(const std::string &http_address);
    bool            may_switch_to_SLA_preset(const wxString& caption);
    bool            run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page = ConfigWizard::SP_WELCOME);
    void            show_desktop_integration_dialog();
    void            show_downloader_registration_dialog();

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
    // temporary and debug only -> extract thumbnails from selected gcode and save them as png files
    void            gcode_thumbnails_debug();
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

    GLShaderProgram* get_shader(const std::string& shader_name) { return m_opengl_mgr.get_shader(shader_name); }
    GLShaderProgram* get_current_shader() { return m_opengl_mgr.get_current_shader(); }

    bool is_gl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_version_greater_or_equal_to(major, minor); }
    bool is_glsl_version_greater_or_equal_to(unsigned int major, unsigned int minor) const { return m_opengl_mgr.get_gl_info().is_glsl_version_greater_or_equal_to(major, minor); }
    int  GetSingleChoiceIndex(const wxString& message, const wxString& caption, const wxArrayString& choices, int initialSelection);

#ifdef __WXMSW__
    void            associate_3mf_files();
    void            associate_stl_files();
    void            associate_gcode_files();
    void            associate_bgcode_files();
#endif // __WXMSW__


    // URL download - PrusaSlicer gets system call to open prusaslicer:// URL which should contain address of download
    void            start_download(std::string url);

    void            open_wifi_config_dialog(bool forced, const wxString& drive_path = {});
    bool            get_wifi_config_dialog_shown() const { return m_wifi_config_dialog_shown; }
private:
    bool            on_init_inner();
	void            init_app_config();
    // returns old config path to copy from if such exists,
    // returns an empty string if such config path does not exists or if it cannot be loaded.
    std::string     check_older_app_config(Semver current_version, bool backup);
    void            window_pos_save(wxTopLevelWindow* window, const std::string &name);
    void            window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized = false);
    void            window_pos_sanitize(wxTopLevelWindow* window);
    bool            select_language();

    bool            config_wizard_startup();
    // Returns true if the configuration is fine.
    // Returns true if the configuration is not compatible and the user decided to rather close the slicer instead of reconfiguring.
	bool            check_updates(const bool verbose);
    void            on_version_read(wxCommandEvent& evt);
    // if the data from version file are already downloaded, shows dialogs to start download of new version of app
    void            app_updater(bool from_user);
    // inititate read of version file online in separate thread
    void            app_version_check(bool from_user);

    bool                    m_datadir_redefined { false };
    bool                    m_wifi_config_dialog_shown { false };
};

DECLARE_APP(GUI_App)

} // GUI
} // Slic3r

#endif // slic3r_GUI_App_hpp_
