///|/ Copyright (c) Prusa Research 2018 - 2023 Vojtěch Bubník @bubnikv, Oleksandra Iushchenko @YuSanka, Lukáš Matěna @lukasmatena, Tomáš Mészáros @tamasmeszaros, David Kocík @kocikdav, Enrico Turri @enricoturri1966, Lukáš Hejl @hejllukas, Filip Sykala @Jony01, Vojtěch Král @vojtechkral
///|/ Copyright (c) 2022 KARBOWSKI Piotr
///|/ Copyright (c) 2022 ole00 @ole00
///|/ Copyright (c) 2020 Tuomas Kuosmanen @tigert
///|/ Copyright (c) 2020 Klaus Naumann
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "libslic3r/Technologies.hpp"
#include "GUI_App.hpp"
#include "GUI_Init.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_Factories.hpp"
#include "format.hpp"

// Localization headers: include libslic3r version first so everything in this file
// uses the slic3r/GUI version (the macros will take precedence over the functions).
// Also, there is a check that the former is not included from slic3r module.
// This is the only place where we want to allow that, so define an override macro.
#define SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "libslic3r/I18N.hpp"
#undef SLIC3R_ALLOW_LIBSLIC3R_I18N_IN_SLIC3R
#include "slic3r/GUI/I18N.hpp"

#include <algorithm>
#include <iterator>
#include <exception>
#include <cstdlib>
#include <regex>
#include <string_view>
#include <boost/nowide/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/dll/runtime_symbol_info.hpp>

#include <wx/stdpaths.h>
#include <wx/imagpng.h>
#include <wx/display.h>
#include <wx/menu.h>
#include <wx/menuitem.h>
#include <wx/filedlg.h>
#include <wx/progdlg.h>
#include <wx/dir.h>
#include <wx/wupdlock.h>
#include <wx/filefn.h>
#include <wx/sysopt.h>
#include <wx/richmsgdlg.h>
#include <wx/log.h>
#include <wx/intl.h>

#include <wx/dialog.h>
#include <wx/textctrl.h>
#include <wx/splash.h>
#include <wx/fontutil.h>

#include "exif.h"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Color.hpp"
#include "libslic3r/Format/SLAArchiveFormatRegistry.hpp"

#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "3DScene.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "GLCanvas3D.hpp"

#include "../Utils/PresetUpdater.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/Process.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/AppUpdater.hpp"
#include "../Utils/WinRegistry.hpp"
#include "slic3r/Config/Snapshot.hpp"
#include "CalibrationBridgeDialog.hpp"
#include "CalibrationCubeDialog.hpp"
#include "CalibrationFirstLayerDialog.hpp"
#include "CalibrationFirstLayerPatchDialog.hpp"
#include "CalibrationExtruderDialog.hpp"
#include "CalibrationFlowDialog.hpp"
#include "CalibrationFlowSpeedDialog.hpp"
#include "CalibrationOverBridgeDialog.hpp"
#include "CalibrationTempDialog.hpp"
#include "CalibrationRetractionDialog.hpp"
#include "CalibrationPressureAdvDialog.hpp"
#include "ConfigSnapshotDialog.hpp"
#include "CreateMMUTiledCanvas.hpp"
#include "FreeCADDialog.hpp"
#include "FirmwareDialog.hpp"
#include "Preferences.hpp"
#include "Tab.hpp"
#include "SysInfoDialog.hpp"
#include "KBShortcutsDialog.hpp"
#include "UpdateDialogs.hpp"
#include "Mouse3DController.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "NotificationManager.hpp"
#include "UnsavedChangesDialog.hpp"
#include "SavePresetDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "DesktopIntegrationDialog.hpp"
#include "SendSystemInfoDialog.hpp"
#include "Downloader.hpp"
#include "PhysicalPrinterDialog.hpp"
#include "WifiConfigDialog.hpp"
#include "Widgets/UIColors.hpp"

#include "BitmapCache.hpp"
#include "Notebook.hpp"

#ifdef __WXMSW__
#include <dbt.h>
#include <shlobj.h>
#ifdef _MSW_DARK_MODE
#include <wx/msw/dark_mode.h>
#endif // _MSW_DARK_MODE
#endif
#ifdef _WIN32
#include <boost/dll/runtime_symbol_info.hpp>
#endif

#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
#include <boost/beast/core/detail/base64.hpp>
#include <boost/nowide/fstream.hpp>
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

// Needed for forcing menu icons back under gtk2 and gtk3
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    #include <gtk/gtk.h>
#endif

using namespace std::literals;

namespace Slic3r {
namespace GUI {

class MainFrame;

class SplashScreen : public wxSplashScreen
{
public:
    SplashScreen(const wxBitmap& bitmap, double scaling, long splashStyle, int milliseconds, wxPoint pos = wxDefaultPosition, wxString author = "")
        : wxSplashScreen(bitmap, splashStyle, milliseconds, static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, wxDefaultPosition, wxDefaultSize,
#ifdef __APPLE__
            wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR | wxSTAY_ON_TOP
#else
            wxSIMPLE_BORDER | wxFRAME_NO_TASKBAR
#endif // !__APPLE__
        ), m_author(author), m_scale(scaling)
    {
        wxASSERT(bitmap.IsOk());

        this->SetPosition(pos);
        // The size of the SplashScreen can be hanged after its moving to another display
        // So, update it from a bitmap size
        this->SetClientSize(bitmap.GetWidth(), bitmap.GetHeight());
        this->CenterOnScreen();

        // set background image
        m_main_bitmap   = bitmap;

        // init constant texts and scale fonts
        init_constant_text();

        // this font will be used for the action string
        m_action_font = m_constant_text.credits_font.Bold();

        // draw logo and constant info text
        Decorate();
    }

    void SetText(const wxString& text)
    {
        set_bitmap(m_main_bitmap);
        if (!text.empty()) {
            wxBitmap bitmap(m_main_bitmap);

            wxMemoryDC memDC;
            memDC.SelectObject(bitmap);

            memDC.SetFont(m_action_font);
///            memDC.SetTextForeground(wxColour(237, 107, 33)); // ed6b21
            uint32_t color = Slic3r::GUI::wxGetApp().app_config->create_color(0.86f, 0.93f);
            memDC.SetTextForeground(wxColour(color & 0xFF, (color & 0xFF00) >> 8, (color & 0xFF0000) >> 16));
            memDC.DrawText(text, int(get_margin() * 2), m_action_line_y_position);

            memDC.SelectObject(wxNullBitmap);
            set_bitmap(bitmap);
#ifdef __WXOSX__
            // without this code splash screen wouldn't be updated under OSX
            wxYield();
#endif
        }
    }

    static wxBitmap MakeBitmap(wxBitmap bmp, double &scaling)
    {
        if (!bmp.IsOk())
            return wxNullBitmap;

        // create dark grey background for the splashscreen
        // It will be 5/3 of the weight of the bitmap
        int width = lround((double)5 / 3 * bmp.GetWidth());
        int height = bmp.GetHeight();

        wxDisplay main_display;
        wxRect display_size = main_display.GetClientArea();
        wxSize ppi = main_display.GetPPI();
        //display_size.width = 1920;
        // the scaling factor is for text, not pictures.
        //double scaling = main_display.GetScaleFactor();

        if (ppi.x != 0 && ppi.y != 0) {
            scaling = scaling * ppi.y / 80.;
        }

        //check if the spashscreen fit
        scaling = std::min( scaling, std::min(
            (display_size.width /*- display_size.x*/) * 0.8 / width,
            (display_size.height /*- display_size.y*/) * 0.8 / height));
        // if screen very small, use all the space avaialble
        if (scaling < 0.5) {
            scaling = std::min(
                (display_size.width /*- display_size.x*/) / width,
                (display_size.height /*- display_size.y*/) / height);
        }
        if (scaling > 10) {
            // error
            scaling = 1;
            assert(false);
        }
        if (scaling > 1) {
            // don't grow with fractional scaling
            if (scaling > 1.8) {
                scaling = 2 * int(scaling * 0.56);
            } else if (scaling > 1.4) {
                scaling = 1.5;
            } else {
                scaling = 1.;
            }
        } else if (scaling > 0.1) {
            scaling = int(scaling * 10) / 10.;
        } else {
            scaling = 1;
            assert(false);
        }
        width *= scaling;
        height *= scaling;

        wxImage image(width, height);
        unsigned char* imgdata_ = image.GetData();
        for (int i = 0; i < width * height; ++i) {
            *imgdata_++ = 51;
            *imgdata_++ = 51;
            *imgdata_++ = 51;
        }

        wxBitmap new_bmp(image);

        wxMemoryDC memDC;
        memDC.SelectObject(new_bmp);
        double xscale,yscale;
        memDC.GetUserScale(&xscale, &yscale);
        memDC.SetUserScale(xscale * scaling, yscale * scaling);
        memDC.DrawBitmap(bmp, (width / scaling) - (bmp.GetWidth()), 0, true);

        return new_bmp;
    }


    void Decorate()
    {
        if (!m_main_bitmap.IsOk())
            return;

        // draw text to the box at the left of the splashscreen.
        // this box will be 2/5 of the weight of the bitmap, and be at the left.
        int width = lround(m_main_bitmap.GetWidth() * 0.4);

        // load bitmap for logo
        int logo_size = lround(width * 0.25);
        wxBitmapBundle *logo_bmp_ptr = get_bmp_bundle(wxGetApp().logo_name(), 0, logo_size);
        if (logo_bmp_ptr == nullptr)
            return;

//#ifdef __APPLE__
//        wxBitmap logo_bmp = logo_bmp_ptr->GetBitmap(logo_bmp_ptr->GetDefaultSize() * mac_max_scaling_factor());
//#else
//        wxBitmap logo_bmp = logo_bmp_ptr->GetBitmapFor(this);
//#endif
        wxBitmap logo_bmp = logo_bmp_ptr->GetBitmap(logo_bmp_ptr->GetDefaultSize());

        wxCoord margin = get_margin();

        wxRect banner_rect(wxPoint(0, logo_bmp.GetSize().y), wxPoint(width, m_main_bitmap.GetHeight()));
        banner_rect.Deflate(margin, 2 * margin);

        // use a memory DC to draw directly onto the bitmap
        wxMemoryDC memDc(m_main_bitmap);

        // draw logo
        memDc.DrawBitmap(logo_bmp, margin, margin, true);

        // draw the (white) labels inside of our black box (at the left of the splashscreen)
        memDc.SetTextForeground(wxColour(180, 180, 180));

        memDc.SetFont(m_constant_text.title_font);
        memDc.DrawLabel(m_constant_text.title, banner_rect, wxALIGN_TOP | wxALIGN_LEFT);

        int title_height = memDc.GetTextExtent(m_constant_text.title).GetY();
        banner_rect.SetTop(banner_rect.GetTop() + title_height);
        banner_rect.SetHeight(banner_rect.GetHeight() - title_height);

        memDc.SetFont(m_constant_text.version_font);
        memDc.DrawLabel(m_constant_text.version, banner_rect, wxALIGN_TOP | wxALIGN_LEFT);
        int version_height = memDc.GetTextExtent(m_constant_text.version).GetY();

        memDc.SetFont(m_constant_text.credits_font);
        wxString credit_and_author = m_constant_text.credits;
        if (!m_author.empty())
            credit_and_author += "\n\n" + m_author;
        int credits_height = memDc.GetMultiLineTextExtent(credit_and_author).GetY();
        //print from bottom
        memDc.DrawLabel(credit_and_author, banner_rect, wxALIGN_BOTTOM | wxALIGN_LEFT);

        //memDc.SetBrush(wxBrush(wxColour(255, 0, 0)));
        //memDc.DrawLine(wxPoint(banner_rect.x, banner_rect.y), wxPoint(banner_rect.x + banner_rect.width, banner_rect.y));
        //memDc.DrawLine(wxPoint(banner_rect.x, banner_rect.y), wxPoint(banner_rect.x, banner_rect.y + credits_height));
        //memDc.DrawLine(wxPoint(banner_rect.x + banner_rect.width, banner_rect.y + credits_height), wxPoint(banner_rect.x + banner_rect.width, banner_rect.y));
        //memDc.DrawLine(wxPoint(banner_rect.x + banner_rect.width, banner_rect.y + credits_height), wxPoint(banner_rect.x, banner_rect.y + credits_height));

        int text_height    = memDc.GetTextExtent("text").GetY();

        // calculate position for the dynamic text
        int logo_and_header_height = margin + logo_size + title_height + version_height;
        m_action_line_y_position = logo_and_header_height + 0.5 * (m_main_bitmap.GetHeight() - margin - credits_height - logo_and_header_height - text_height);
    }

private:
    wxBitmap    m_main_bitmap;
    wxFont      m_action_font;
    int         m_action_line_y_position;
    const double m_scale;
    wxString    m_author;

    int get_margin() {
        // 1% margin (20 px for 2000 width)
        return int(m_main_bitmap.GetWidth() * 0.01f);
    }

    struct ConstantText
    {
        wxString title;
        wxString version;
        wxString credits;

        wxFont   title_font;
        wxFont   version_font;
        wxFont   credits_font;

        void init(wxFont init_font)
        {
            // title
            title = wxGetApp().is_editor() ? SLIC3R_APP_NAME : GCODEVIEWER_APP_NAME;

            // dynamically get the version to display
            version = _L("Version") + " " + std::string(SLIC3R_VERSION_FULL);

            // credits infornation
            credits = "\n" + title + " " +
                _L("is based on PrusaSlicer by Prusa Research.") + "\n\n" +
                _L("PrusaSlicer is based on Slic3r by Alessandro Ranellucci and the RepRap community.") + "\n\n" +
                _L("Licensed under GNU AGPLv3.");

            title_font = version_font = credits_font = init_font;
        }
    }
    m_constant_text;

    void init_constant_text()
    {
        m_constant_text.init(get_default_font(this));

        // As default we use a system font for current display.
        // Scale fonts in respect to banner width

        int text_banner_width = lround(0.4 * m_main_bitmap.GetWidth()) - roundl(get_margin()); // banner_width - margins

        int default_width_title = GetTextExtent(m_constant_text.title).GetX();
        float title_font_scale = (float)text_banner_width / default_width_title;
        if (title_font_scale > 2.f) {
            title_font_scale = std::max(2.f, (float)(text_banner_width * 0.5f) / default_width_title);
        }
        scale_font(m_constant_text.title_font, title_font_scale);

        float version_font_scale = (float)text_banner_width / GetTextExtent(m_constant_text.version).GetX();
        if (version_font_scale > 1.f && version_font_scale > title_font_scale * 0.6f) {
            if (title_font_scale > 1.6f) {
                version_font_scale = title_font_scale * 0.6f;
            } else {
                version_font_scale = std::min(1.f, title_font_scale * 0.9f);
            }
        }
        scale_font(m_constant_text.version_font, version_font_scale);

        // The width of the credits information string doesn't respect to the banner width some times.
        // So, scale credits_font in the respect to the longest string width
        int   longest_string_width = word_wrap_string(m_constant_text.credits);
        float font_scale = (float)text_banner_width / longest_string_width;
        scale_font(m_constant_text.credits_font, font_scale);
    }

    void set_bitmap(wxBitmap& bmp)
    {
        m_window->SetBitmap(bmp);
        m_window->Refresh();
        m_window->Update();
    }

    void scale_bitmap(wxBitmap& bmp, float scale)
    {
        if (scale == 1.0)
            return;

        wxImage image = bmp.ConvertToImage();
        if (!image.IsOk() || image.GetWidth() == 0 || image.GetHeight() == 0)
            return;

        int width   = int(scale * image.GetWidth());
        int height  = int(scale * image.GetHeight());
        image.Rescale(width, height, wxIMAGE_QUALITY_BILINEAR);

        bmp = wxBitmap(std::move(image));
    }

    void scale_font(wxFont& font, float scale)
    {
#ifdef __WXMSW__
        // Workaround for the font scaling in respect to the current active display,
        // not for the primary display, as it's implemented in Font.cpp
        // See https://github.com/wxWidgets/wxWidgets/blob/master/src/msw/font.cpp
        // void wxNativeFontInfo::SetFractionalPointSize(float pointSizeNew)
        wxNativeFontInfo nfi= *font.GetNativeFontInfo();
        float pointSizeNew  = wxDisplay(this).GetScaleFactor() * scale * font.GetPointSize();
        nfi.lf.lfHeight     = nfi.GetLogFontHeightAtPPI(pointSizeNew, get_dpi_for_window(this));
        nfi.pointSize       = pointSizeNew;
        font = wxFont(nfi);
#else
        font.Scale(scale);
#endif //__WXMSW__
    }

    // wrap a string for the strings no longer then line_len symbols
    // return extent of the longest string
    int word_wrap_string(wxString& input)
    {
        size_t line_len = 56;// count of symbols in one line
        int idx = -1;
        size_t cur_len = 0;

        wxString longest_sub_string;
        auto get_longest_sub_string = [input](wxString &longest_sub_str, size_t cur_len, size_t i) {
            if (cur_len > longest_sub_str.Len())
                longest_sub_str = input.SubString(i - cur_len + 1, i);
        };

        for (size_t i = 0; i < input.Len(); i++)
        {
            cur_len++;
            if (input[i] == ' ')
                idx = i;
            if (input[i] == '\n')
            {
                get_longest_sub_string(longest_sub_string, cur_len, i);
                idx = -1;
                cur_len = 0;
            }
            if (cur_len >= line_len && idx >= 0)
            {
                get_longest_sub_string(longest_sub_string, cur_len, i);
                input[idx] = '\n';
                cur_len = i - static_cast<size_t>(idx);
            }
        }

        return GetTextExtent(longest_sub_string).GetX();
    }
};


#ifdef __linux__
bool static check_old_linux_datadir(const wxString& app_name) {
    // If we are on Linux and the datadir does not exist yet, look into the old
    // location where the datadir was before version 2.3. If we find it there,
    // tell the user that he might wanna migrate to the new location.
    // (https://github.com/prusa3d/PrusaSlicer/issues/2911)
    // To be precise, the datadir should exist, it is created when single instance
    // lock happens. Instead of checking for existence, check the contents.

    namespace fs = boost::filesystem;

    std::string new_path = Slic3r::data_dir();

    wxString dir;
    if (! wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty() )
        dir = wxFileName::GetHomeDir() + wxS("/.config");
    std::string default_path = (dir + "/" + app_name).ToUTF8().data();

    if (new_path != default_path) {
        // This happens when the user specifies a custom --datadir.
        // Do not show anything in that case.
        return true;
    }

    fs::path data_dir = fs::path(new_path);
    if (! fs::is_directory(data_dir))
        return true; // This should not happen.

    int file_count = std::distance(fs::directory_iterator(data_dir), fs::directory_iterator());

    if (file_count <= 1) { // just cache dir with an instance lock
        std::string old_path = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();

        if (fs::is_directory(old_path)) {
            wxString msg = from_u8((boost::format(_u8L("Starting with %1% 2.3, configuration "
                "directory on Linux has changed (according to XDG Base Directory Specification) to \n%2%.\n\n"
                "This directory did not exist yet (maybe you run the new version for the first time).\nHowever, "
                "an old %1% configuration directory was detected in \n%3%.\n\n"
                "Consider moving the contents of the old directory to the new location in order to access "
                "your profiles, etc.\nNote that if you decide to downgrade %1% in future, it will use the old "
                "location again.\n\n"
                "What do you want to do now?")) % SLIC3R_APP_NAME % new_path % old_path).str());
            wxString caption = from_u8((boost::format(_u8L("%s - BREAKING CHANGE")) % SLIC3R_APP_NAME).str());
            RichMessageDialog dlg(nullptr, msg, caption, wxYES_NO);
            dlg.SetYesNoLabels(_L("Quit, I will move my data now"), _L("Start the application"));
            if (dlg.ShowModal() != wxID_NO)
                return false;
        }
    } else {
        // If the new directory exists, be silent. The user likely already saw the message.
    }
    return true;
}
#endif

#ifdef _WIN32
#if 0 // External Updater is replaced with AppUpdater.cpp
static bool run_updater_win()
{
    // find updater exe
    boost::filesystem::path path_updater = boost::dll::program_location().parent_path() / "prusaslicer-updater.exe";
    // run updater. Original args: /silent -restartapp prusa-slicer.exe -startappfirst
    std::string msg;
    bool res = create_process(path_updater, L"/silent", msg);
    if (!res)
        BOOST_LOG_TRIVIAL(error) << msg;
    return res;
}
#endif // 0
#endif // _WIN32

struct FileWildcards {
    std::string_view              title;
    std::vector<std::string_view> file_extensions;
};



static const FileWildcards file_wildcards_by_type[FT_SIZE] = {
    /* FT_STL */     { "STL files"sv,       { ".stl"sv } },
    /* FT_OBJ */     { "OBJ files"sv,       { ".obj"sv } },
    /* FT_OBJECT */  { "Object files"sv,    { ".stl"sv, ".obj"sv } },
    /* FT_STEP */    { "STEP files"sv,      { ".stp"sv, ".step"sv } },
    /* FT_AMF */     { "AMF files"sv,       { ".amf"sv, ".zip.amf"sv, ".xml"sv } },
    /* FT_3MF */     { "3MF files"sv,       { ".3mf"sv } },
    /* FT_GCODE */   { "G-code files"sv,    { ".gcode"sv, ".gco"sv, ".bgcode"sv, ".bgc"sv, ".g"sv, ".ngc"sv } },
    /* FT_MODEL */   { "Known files"sv,     { ".stl"sv, ".obj"sv, ".3mf"sv, ".amf"sv, ".zip.amf"sv, ".xml"sv, ".step"sv, ".stp"sv, ".svg"sv } },
    /* FT_PROJECT */ { "Project files"sv,   { ".3mf"sv, ".amf"sv, ".zip.amf"sv } },
    /* FT_FONTS */   { "Font files"sv,      { ".ttc"sv, ".ttf"sv } },
    /* FT_GALLERY */ { "Known files"sv,     { ".stl"sv, ".obj"sv } },

    /* FT_INI */     { "INI files"sv,       { ".ini"sv } },
    /* FT_SVG */     { "SVG files"sv,       { ".svg"sv } },

    /* FT_TEX */     { "Texture"sv,         { ".png"sv, ".svg"sv } },

    /* FT_SL1 (deprecated, overriden by sla_wildcards) */     { "Masked SLA files"sv, { ".sl1"sv, ".sl1s"sv, ".pwmx"sv } },

    /* FT_ZIP */     { "Zip files"sv, { ".zip"sv } },
};

// This function produces a Win32 file dialog file template mask to be consumed by wxWidgets on all platforms.
// The function accepts a custom extension parameter. If the parameter is provided, the custom extension
// will be added as a fist to the list. This is important for a "file save" dialog on OSX, which strips
// an extension from the provided initial file name and substitutes it with the default extension (the first one in the template).
static wxString file_wildcards(const FileWildcards &wildcards, const std::string &custom_extension)
{
    std::string title;
    std::string mask;
    std::string custom_ext_lower;

    // Collects items for each of the extensions one by one.
    wxString out_one_by_one;
    auto add_single = [&out_one_by_one](const std::string_view title, const std::string_view ext) {
        out_one_by_one += GUI::format_wxstr("|%s (*%s)|*%s", title, ext, ext);
    };

    if (! custom_extension.empty()) {
        // Generate a custom extension into the title mask and into the list of extensions.
        // Add default version (upper, lower or mixed) first based on custom extension provided.
        title = std::string("*") + custom_extension;
        mask = title;
        add_single(wildcards.title, custom_extension);
        custom_ext_lower = boost::to_lower_copy(custom_extension);
        const std::string custom_ext_upper = boost::to_upper_copy(custom_extension);
        if (custom_ext_lower == custom_extension) {
            // Add one more variant - the upper case extension.
            mask += ";*";
            mask += custom_ext_upper;
            add_single(wildcards.title, custom_ext_upper);
        } else if (custom_ext_upper == custom_extension) {
            // Add one more variant - the lower case extension.
            mask += ";*";
            mask += custom_ext_lower;
            add_single(wildcards.title, custom_ext_lower);
        }
    }

    for (const std::string_view &ext : wildcards.file_extensions)
        // Only add an extension if it was not added first as the custom extension.
        if (ext != custom_ext_lower) {
            if (title.empty()) {
                title = "*";
                title += ext;
                mask  = title;
            } else {
                title += ", *";
                title += ext;
                mask  += ";*";
                mask  += ext;
            }
            mask += ";*";
            mask += boost::to_upper_copy(std::string(ext));
            add_single(wildcards.title, ext);
        }

    return GUI::format_wxstr("%s (%s)|%s", wildcards.title, title, mask) + out_one_by_one;
}

wxString file_wildcards(FileType file_type, const std::string &custom_extension)
{
    return file_wildcards(file_wildcards_by_type[file_type], custom_extension);
}

wxString sla_wildcards(OutputFormat formatid, const std::string& custom_extension)
{
    const ArchiveEntry *entry = get_archive_entry(formatid);
    wxString ret;

    if (entry) {
        FileWildcards wc;
        std::string tr_title = I18N::translate_utf8(entry->desc);
        // TRN %s = type of file
        tr_title = GUI::format(_u8L("%s files"), tr_title);
        wc.title = tr_title;

        std::vector<std::string> exts = get_extensions(*entry);

        wc.file_extensions.reserve(exts.size());
        for (std::string &ext : exts) {
            ext.insert(ext.begin(), '.');
            wc.file_extensions.emplace_back(ext);
        }

        ret = file_wildcards(wc, custom_extension);
    }

    if (ret.empty())
        ret = file_wildcards(FT_SL1, custom_extension);

    return ret;
}

static std::string libslic3r_translate_callback(const char *s) { return wxGetTranslation(wxString(s, wxConvUTF8)).utf8_str().data(); }

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
static void register_win32_dpi_event()
{
    enum { WM_DPICHANGED_ = 0x02e0 };

    wxWindow::MSWRegisterMessageHandler(WM_DPICHANGED_, [](wxWindow *win, WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam) {
        const int dpi = wParam & 0xffff;
        const auto rect = reinterpret_cast<PRECT>(lParam);
        const wxRect wxrect(wxPoint(rect->top, rect->left), wxPoint(rect->bottom, rect->right));

        DpiChangedEvent evt(EVT_DPI_CHANGED_SLICER, dpi, wxrect);
        win->GetEventHandler()->AddPendingEvent(evt);

        return true;
    });
}
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN

static GUID GUID_DEVINTERFACE_HID = { 0x4D1E55B2, 0xF16F, 0x11CF, 0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30 };

static void register_win32_device_notification_event()
{
    wxWindow::MSWRegisterMessageHandler(WM_DEVICECHANGE, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
		PDEV_BROADCAST_HDR lpdb = (PDEV_BROADCAST_HDR)lParam;
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
		        plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME) {
//					printf("DBT_DEVICEARRIVAL %d - Media has arrived: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
			        plater->GetEventHandler()->AddPendingEvent(HIDDeviceAttachedEvent(EVT_HID_DEVICE_ATTACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
            break;
		case DBT_DEVICEREMOVECOMPLETE:
			if (lpdb->dbch_devicetype == DBT_DEVTYP_VOLUME)
                plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
			else if (lpdb->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
				PDEV_BROADCAST_DEVICEINTERFACE lpdbi = (PDEV_BROADCAST_DEVICEINTERFACE)lpdb;
//				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_VOLUME)
//					printf("DBT_DEVICEARRIVAL %d - Media was removed: %ws\n", msg_count, lpdbi->dbcc_name);
				if (lpdbi->dbcc_classguid == GUID_DEVINTERFACE_HID)
        			plater->GetEventHandler()->AddPendingEvent(HIDDeviceDetachedEvent(EVT_HID_DEVICE_DETACHED, boost::nowide::narrow(lpdbi->dbcc_name)));
			}
			break;
        default:
            break;
        }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(MainFrame::WM_USER_MEDIACHANGED, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        // Some messages are sent to top level windows by default, some messages are sent to only registered windows, and we explictely register on MainFrame only.
        auto main_frame = dynamic_cast<MainFrame*>(win);
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
        if (plater == nullptr)
            // Maybe some other top level window like a dialog or maybe a pop-up menu?
            return true;
        wchar_t sPath[MAX_PATH];
        if (lParam == SHCNE_MEDIAINSERTED || lParam == SHCNE_MEDIAREMOVED) {
            struct _ITEMIDLIST* pidl = *reinterpret_cast<struct _ITEMIDLIST**>(wParam);
            if (! SHGetPathFromIDList(pidl, sPath)) {
                BOOST_LOG_TRIVIAL(error) << "MediaInserted: SHGetPathFromIDList failed";
                return false;
            }
        }
        switch (lParam) {
        case SHCNE_MEDIAINSERTED:
        {
            //printf("SHCNE_MEDIAINSERTED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeAttachedEvent(EVT_VOLUME_ATTACHED));
            break;
        }
        case SHCNE_MEDIAREMOVED:
        {
            //printf("SHCNE_MEDIAREMOVED %S\n", sPath);
            plater->GetEventHandler()->AddPendingEvent(VolumeDetachedEvent(EVT_VOLUME_DETACHED));
            break;
        }
	    default:
//          printf("Unknown\n");
            break;
	    }
        return true;
    });

    wxWindow::MSWRegisterMessageHandler(WM_INPUT, [](wxWindow *win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
        auto main_frame = dynamic_cast<MainFrame*>(Slic3r::GUI::find_toplevel_parent(win));
        auto plater = (main_frame == nullptr) ? nullptr : main_frame->plater();
//        if (wParam == RIM_INPUTSINK && plater != nullptr && main_frame->IsActive()) {
        if (wParam == RIM_INPUT && plater != nullptr && main_frame->IsActive()) {
        RAWINPUT raw;
			UINT rawSize = sizeof(RAWINPUT);
			::GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &rawSize, sizeof(RAWINPUTHEADER));
			if (raw.header.dwType == RIM_TYPEHID && plater->get_mouse3d_controller().handle_raw_input_win32(raw.data.hid.bRawData, raw.data.hid.dwSizeHid))
				return true;
		}
        return false;
    });

	wxWindow::MSWRegisterMessageHandler(WM_COPYDATA, [](wxWindow* win, WXUINT /* nMsg */, WXWPARAM wParam, WXLPARAM lParam) {
		COPYDATASTRUCT* copy_data_structure = { 0 };
		copy_data_structure = (COPYDATASTRUCT*)lParam;
		if (copy_data_structure->dwData == 1) {
			LPCWSTR arguments = (LPCWSTR)copy_data_structure->lpData;
			Slic3r::GUI::wxGetApp().other_instance_message_handler()->handle_message(boost::nowide::narrow(arguments));
		}
		return true;
		});
}
#endif // WIN32

static void generic_exception_handle()
{
    // Note: Some wxWidgets APIs use wxLogError() to report errors, eg. wxImage
    // - see https://docs.wxwidgets.org/3.1/classwx_image.html#aa249e657259fe6518d68a5208b9043d0
    //
    // wxLogError typically goes around exception handling and display an error dialog some time
    // after an error is logged even if exception handling and OnExceptionInMainLoop() take place.
    // This is why we use wxLogError() here as well instead of a custom dialog, because it accumulates
    // errors if multiple have been collected and displays just one error message for all of them.
    // Otherwise we would get multiple error messages for one missing png, for example.
    //
    // If a custom error message window (or some other solution) were to be used, it would be necessary
    // to turn off wxLogError() usage in wx APIs, most notably in wxImage
    // - see https://docs.wxwidgets.org/trunk/classwx_image.html#aa32e5d3507cc0f8c3330135bc0befc6a

    try {
        throw;
    } catch (const std::bad_alloc& ex) {
        // bad_alloc in main thread is most likely fatal. Report immediately to the user (wxLogError would be delayed)
        // and terminate the app so it is at least certain to happen now.
        wxString errmsg = wxString::Format(_L("%s has encountered an error. It was likely caused by running out of memory. "
                              "If you are sure you have enough RAM on your system, this may also be a bug and we would "
                              "be glad if you reported it.\n\nThe application will now terminate."), SLIC3R_APP_NAME);
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Fatal error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("std::bad_alloc exception: %1%") % ex.what();
        std::terminate();
    } catch (const boost::io::bad_format_string& ex) {
        wxString errmsg = wxString::Format(_L( "%s has encountered a localization error. "
                             "Please report to %s team, what language was active and in which scenario "
                             "this issue happened. Thank you.\n\nThe application will now terminate."), SLIC3R_APP_NAME, SLIC3R_APP_NAME);
        wxMessageBox(errmsg + "\n\n" + wxString(ex.what()), _L("Critical error"), wxOK | wxICON_ERROR);
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        std::terminate();
        throw;
    } catch (const std::exception &ex) {
        wxLogError(format_wxstr(_L("Internal error: %1%"), ex.what()));
        BOOST_LOG_TRIVIAL(error) << boost::format("Uncaught exception: %1%") % ex.what();
        throw;
    }
}

void GUI_App::post_init()
{
    assert(initialized());
    if (! this->initialized())
        throw Slic3r::RuntimeError("Calling post_init() while not yet initialized");

    if (this->is_gcode_viewer()) {
        if (! this->init_params->input_files.empty())
            this->plater()->load_gcode(wxString::FromUTF8(this->init_params->input_files[0].c_str()));
    }
    else if (this->init_params->start_downloader) {
        start_download(this->init_params->download_url);
    } else {
        if (! this->init_params->preset_substitutions.empty())
            show_substitutions_info(this->init_params->preset_substitutions);

#if 0
        // Load the cummulative config over the currently active profiles.
        //FIXME if multiple configs are loaded, only the last one will have an effect.
        // We need to decide what to do about loading of separate presets (just print preset, just filament preset etc).
        // As of now only the full configs are supported here.
        if (!m_print_config.empty())
            this->gui->mainframe->load_config(m_print_config);
#endif
        if (! this->init_params->load_configs.empty())
            // Load the last config to give it a name at the UI. The name of the preset may be later
            // changed by loading an AMF or 3MF.
            //FIXME this is not strictly correct, as one may pass a print/filament/printer profile here instead of a full config.
            this->mainframe->load_config_file(this->init_params->load_configs.back());
        // If loading a 3MF file, the config is loaded from the last one.
        if (!this->init_params->input_files.empty()) {
            wxArrayString fns;
            for (const std::string& name : this->init_params->input_files)
                fns.Add(from_u8(name));
            if (plater()->load_files(fns) && this->init_params->input_files.size() == 1) {
                // Update application titlebar when opening a project file
                const std::string& filename = this->init_params->input_files.front();
                if (boost::algorithm::iends_with(filename, ".amf") ||
                    boost::algorithm::iends_with(filename, ".amf.xml") ||
                    boost::algorithm::iends_with(filename, ".3mf"))
                    this->plater()->set_project_filename(from_u8(filename));
            }
            if (this->init_params->delete_after_load) {
                for (const std::string& p : this->init_params->input_files) {
                    boost::system::error_code ec;
                    boost::filesystem::remove(boost::filesystem::path(p), ec);
                    if (ec) {
                        BOOST_LOG_TRIVIAL(error) << ec.message();
                    }
                }
            }
        }
        if (! this->init_params->extra_config.empty())
            this->mainframe->load_config(this->init_params->extra_config);
    }

    // show "Did you know" notification
    if (app_config->get_bool("show_hints") && ! is_gcode_viewer())
        plater_->get_notification_manager()->push_hint_notification(true);

    // The extra CallAfter() is needed because of Mac, where this is the only way
    // to popup a modal dialog on start without screwing combo boxes.
    // This is ugly but I honestly found no better way to do it.
    // Neither wxShowEvent nor wxWindowCreateEvent work reliably.
    if (this->preset_updater) { // G-Code Viewer does not initialize preset_updater.

#if 0 // This code was moved to EVT_CONFIG_UPDATER_SYNC_DONE bind - after preset_updater finishes synchronization.
        if (! this->check_updates(false))
            // Configuration is not compatible and reconfigure was refused by the user. Application is closing.
            return;
#endif
        CallAfter([this] {
            // preset_updater->sync downloads profile updates on background so it must begin after config wizard finished.
            bool cw_showed = this->config_wizard_startup();
            this->preset_updater->sync(preset_bundle.get(), this);
            if (! cw_showed) {
                // The CallAfter is needed as well, without it, GL extensions did not show.
                // Also, we only want to show this when the wizard does not, so the new user
                // sees something else than "we want something" on the first start.
                // don't show_send_system_info_dialog_if_needed, only prusa has the server to receive the data anyway.
                // show_send_system_info_dialog_if_needed();
            }
            // app version check is asynchronous and triggers blocking dialog window, better call it last
            this->app_version_check(false);
        });
    }

    // Set Slic3r version and save to Slic3r.ini or Slic3rGcodeViewer.ini.
    app_config->set("version", SLIC3R_VERSION_FULL);

#ifdef _WIN32
    // Sets window property to mainframe so other instances can indentify it.
    OtherInstanceMessageHandler::init_windows_properties(mainframe, m_instance_hash_int);
#endif //WIN32
}

IMPLEMENT_APP(GUI_App)

GUI_App::GUI_App(EAppMode mode)
    : wxApp()
    , m_app_mode(mode)
    , m_em_unit(10)
    , m_imgui()
	, m_removable_drive_manager(std::make_unique<RemovableDriveManager>())
	, m_other_instance_message_handler(std::make_unique<OtherInstanceMessageHandler>())
    , m_downloader(std::make_unique<Downloader>())
{
	//app config initializes early becasuse it is used in instance checking in PrusaSlicer.cpp
	this->init_app_config();
    //ImGuiWrapper need the app config to get the colors
    m_imgui.reset(new ImGuiWrapper{});
    // init app downloader after path to datadir is set
    m_app_updater = std::make_unique<AppUpdater>();
}

// If formatted for github, plaintext with OpenGL extensions enclosed into <details>.
// Otherwise HTML formatted for the system info dialog.
std::string GUI_App::get_gl_info(bool for_github)
{
    return OpenGLManager::get_gl_info().to_string(for_github);
}

wxGLContext* GUI_App::init_glcontext(wxGLCanvas& canvas)
{
#if ENABLE_GL_CORE_PROFILE
    return m_opengl_mgr.init_glcontext(canvas, init_params != nullptr ? init_params->opengl_version : std::make_pair(0, 0),
        init_params != nullptr ? init_params->opengl_compatibiity_profile : false, init_params != nullptr ? init_params->opengl_debug : false);
#else
    return m_opengl_mgr.init_glcontext(canvas);
#endif // ENABLE_GL_CORE_PROFILE
}

bool GUI_App::init_opengl()
{
    bool initialized = m_opengl_mgr.init_gl();
    if (!m_opengl_initialized && initialized) {
        AppConfig::HardwareType hard_cpu = AppConfig::HardwareType::hCpuOther; // TODO for x86 if needed
        AppConfig::HardwareType hard_gpu = AppConfig::HardwareType::hGpuOther;
        // Delayed init for x86
#ifdef __APPLE__
        // intel apple
        hard_cpu = AppConfig::HardwareType::hCpuIntel;
        try {
            std::string gpu_vendor = OpenGLManager::get_gl_info().get_vendor();
            if (boost::contains(gpu_vendor, "Intel") || boost::contains(gpu_vendor, "INTEL"))
                hard_gpu = AppConfig::HardwareType::hGpuIntel;
            if (boost::contains(gpu_vendor, "ATI") || boost::contains(gpu_vendor, "AMD"))
                hard_gpu = AppConfig::HardwareType::hGpuAmd;
        } catch (std::exception &ex) {}
#else
        try {
            std::string gpu_vendor = OpenGLManager::get_gl_info().get_vendor();
            if (boost::contains(gpu_vendor, "Intel") || boost::contains(gpu_vendor, "INTEL"))
                hard_gpu = AppConfig::HardwareType::hGpuIntel;
            if (boost::contains(gpu_vendor, "ATI") || boost::contains(gpu_vendor, "AMD"))
                hard_gpu = AppConfig::HardwareType::hGpuAmd;
            if (boost::contains(gpu_vendor, "Nvidia") || boost::contains(gpu_vendor, "NVIDIA"))
                hard_gpu = AppConfig::HardwareType::hGpuNvidia;
            if (boost::contains(gpu_vendor, "Apple") || boost::contains(gpu_vendor, "APPLE")) {
                assert(false); // apple gpu are only in _M_ARM64
            }
        } catch (std::exception &) {}
#endif
        app_config->set_hardware_type(AppConfig::HardwareType(hard_cpu + hard_gpu));
    }
    m_opengl_initialized = true;
    return initialized;
}

// gets path to PrusaSlicer.ini, returns semver from first line comment
static std::optional<Semver> parse_semver_from_ini(std::string path)
{
    boost::nowide::ifstream stream(path);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    std::string body = buffer.str();
    size_t start = body.find("PrusaSlicer ");
    if (start == std::string::npos)
        return std::nullopt;
    body = body.substr(start + 12);
    size_t end = body.find_first_of(" \n");
    if (end < body.size())
        body.resize(end);
    return Semver::parse(body);
}

void GUI_App::init_app_config()
{
    #ifdef SLIC3R_ALPHA
        // Profiles for the alpha are stored into the CaribouSlicer-alpha directory to not mix with the current release.
        SetAppName(SLIC3R_APP_KEY "V2-alpha");
    #elif SLIC3R_BETA
        // Profiles for the beta are stored into the CaribouSlicer-beta directory to not mix with the current release.
        SetAppName(SLIC3R_APP_KEY "V2-beta");
    #else
        SetAppName(SLIC3R_APP_KEY);
    #endif


//	SetAppDisplayName(SLIC3R_APP_NAME);

	// Set the Slic3r data directory at the Slic3r XS module.
	// Unix: ~/ .Slic3rP
	// Windows : "C:\Users\username\AppData\Roaming\Slic3r" or "C:\Documents and Settings\username\Application Data\Slic3r"
	// Mac : "~/Library/Application Support/Slic3r"

    if (data_dir().empty()) {
        //check if there is a "configuration" directory
#ifdef __APPLE__
        //... next to the app bundle on MacOs
        if (boost::filesystem::exists(boost::filesystem::path{ resources_dir() } / ".." / ".." / ".." / "configuration")) {
            set_data_dir((boost::filesystem::path{ resources_dir() } / ".." / ".." / ".." / "configuration").string());
        } else
#endif
        //... next to the resources directory
        if (boost::filesystem::exists(boost::filesystem::path{ resources_dir() } / ".." / "configuration")) {
            set_data_dir((boost::filesystem::path{ resources_dir() } / ".." / "configuration").string());
        } else {
#ifndef __linux__
            set_data_dir(wxStandardPaths::Get().GetUserDataDir().ToUTF8().data());
        }
#else
            // Since version 2.3, config dir on Linux is in ${XDG_CONFIG_HOME}.
            // https://github.com/prusa3d/PrusaSlicer/issues/2911
            wxString dir;
            if (!wxGetEnv(wxS("XDG_CONFIG_HOME"), &dir) || dir.empty())
                dir = wxFileName::GetHomeDir() + wxS("/.config");
            set_data_dir((dir + "/" + GetAppName()).ToUTF8().data());
        }
#endif
    } else {
        m_datadir_redefined = true;
    }

	if (!app_config) {
        app_config.reset(new AppConfig(is_editor() ? AppConfig::EAppMode::Editor : AppConfig::EAppMode::GCodeViewer));
#ifdef _M_ARM64
        AppConfig::HardwareType hard_cpu = AppConfig::HardwareType::hCpuOther; // TODO for x86 if needed
        AppConfig::HardwareType hard_gpu = AppConfig::HardwareType::hGpuOther;
#ifdef __APPLE__
        // Arm apple
        hard_cpu = AppConfig::HardwareType::hCpuApple;
        hard_gpu = AppConfig::HardwareType::hGpuApple;
        app_config->set_hardware_type(AppConfig::HardwareType(hard_cpu + hard_gpu));
#else
        // Arm
        hard_cpu = AppConfig::HardwareType::hCpuArmGeneric;
        hard_gpu = AppConfig::HardwareType::hGpuArmGeneric;
        app_config->set_hardware_type(AppConfig::HardwareType(hard_cpu + hard_gpu));
#endif
#else
        // x86 (not-apple)
        //can't know the gpu before the openg init, so it's delayed. until it
#endif
    }
	// load settings
	m_app_conf_exists = app_config->exists();
	if (m_app_conf_exists) {
        std::string error = app_config->load();
        if (!error.empty()) {
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            if (is_editor()) {
                throw Slic3r::RuntimeError(
                    format(_u8L("Error parsing %1% config file, it is probably corrupted. "
                        "Try to manually delete the file to recover from the error. Your user profiles will not be affected."
                        "\n\n%2%\n\n%3%"), SLIC3R_APP_NAME,  app_config->config_path(), error));
            } else {
                throw Slic3r::RuntimeError(
                    format(_u8L("Error parsing %1% config file, it is probably corrupted. "
                        "Try to manually delete the file to recover from the error."
                        "\n\n%2%\n\n%3%"), GCODEVIEWER_APP_NAME, app_config->config_path(), error));
            }
        }
    }
    //add callback to font size from app_config
    DPIAware_::get_font_size = [this]()->int { return atoi(app_config->get("font_size").c_str()); };
}

// returns old config path to copy from if such exists,
// returns an empty string if such config path does not exists or if it cannot be loaded.
std::string GUI_App::check_older_app_config(Semver current_version, bool backup)
{
    std::string older_data_dir_path;

    // If the config folder is redefined - do not check
    if (m_datadir_redefined)
        return {};

    // find other version app config (alpha / beta / release)
    std::string             config_path = app_config->config_path();
    boost::filesystem::path parent_file_path(config_path);
    std::string             filename = parent_file_path.filename().string();
    parent_file_path.remove_filename().remove_filename();

    std::vector<boost::filesystem::path> candidates;

    if (SLIC3R_APP_KEY "-alpha" != GetAppName()) candidates.emplace_back(parent_file_path / SLIC3R_APP_KEY "-alpha" / filename);
    if (SLIC3R_APP_KEY "-beta" != GetAppName())  candidates.emplace_back(parent_file_path / SLIC3R_APP_KEY "-beta" / filename);
    if (SLIC3R_APP_KEY != GetAppName())          candidates.emplace_back(parent_file_path / SLIC3R_APP_KEY / filename);

    Semver last_semver = current_version;
    for (const auto& candidate : candidates) {
        if (boost::filesystem::exists(candidate)) {
            // parse
            std::optional<Semver>other_semver = parse_semver_from_ini(candidate.string());
            if (other_semver && *other_semver > last_semver) {
                last_semver = *other_semver;
                older_data_dir_path = candidate.parent_path().string();
            }
        }
    }
    if (older_data_dir_path.empty())
        return {};
    BOOST_LOG_TRIVIAL(info) << "last app config file used: " << older_data_dir_path;
    // ask about using older data folder
    InfoDialog msg(nullptr
        , format_wxstr(_L("You are opening %1% version %2%."), SLIC3R_APP_NAME, SLIC3R_VERSION)
        , backup ?
        format_wxstr(_L(
            "The active configuration was created by <b>%1% %2%</b>,"
            "\nwhile a newer configuration was found in <b>%3%</b>"
            "\ncreated by <b>%1% %4%</b>."
            "\n\nShall the newer configuration be imported?"
            "\nIf so, your active configuration will be backed up before importing the new configuration."
        )
            , SLIC3R_APP_NAME, current_version.to_string(), older_data_dir_path, last_semver.to_string())
        : format_wxstr(_L(
            "An existing configuration was found in <b>%3%</b>"
            "\ncreated by <b>%1% %2%</b>."
            "\n\nShall this configuration be imported?"
        )
            , SLIC3R_APP_NAME, last_semver.to_string(), older_data_dir_path)
        , true, wxYES_NO);

    if (backup) {
        msg.SetButtonLabel(wxID_YES, _L("Import"));
        msg.SetButtonLabel(wxID_NO, _L("Don't import"));
    }

    if (msg.ShowModal() == wxID_YES) {
        std::string snapshot_id;
        if (backup) {
            const Config::Snapshot* snapshot{ nullptr };
            if (! GUI::Config::take_config_snapshot_cancel_on_error(*app_config, Config::Snapshot::SNAPSHOT_USER, "",
                _u8L("Continue and import newer configuration?"), &snapshot))
                return {};
            if (snapshot) {
                // Save snapshot ID before loading the alternate AppConfig, as loading the alternate AppConfig may fail.
                snapshot_id = snapshot->id;
                assert(! snapshot_id.empty());
                app_config->set("on_snapshot", snapshot_id);
            } else
                BOOST_LOG_TRIVIAL(error) << "Failed to take congiguration snapshot";
        }

        // load app config from older file
        std::string error = app_config->load((boost::filesystem::path(older_data_dir_path) / filename).string());
        if (!error.empty()) {
            // Error while parsing config file. We'll customize the error message and rethrow to be displayed.
            if (is_editor()) {
                throw Slic3r::RuntimeError(
                    format(_u8L("Error parsing %1% config file, it is probably corrupted. "
                        "Try to manually delete the file to recover from the error. Your user profiles will not be affected.") +
                        "\n\n%2%\n\n%3%", SLIC3R_APP_NAME, app_config->config_path(), error));
            }
            else {
                throw Slic3r::RuntimeError(
                    format(_u8L("Error parsing %1% config file, it is probably corrupted. "
                        "Try to manually delete the file to recover from the error.") +
                        "\n\n%2%\n\n%3%", GCODEVIEWER_APP_NAME, app_config->config_path(), error));
            }
        }
        if (!snapshot_id.empty())
            app_config->set("on_snapshot", snapshot_id);
        m_app_conf_exists = true;
        return older_data_dir_path;
    }
    return {};
}

void GUI_App::init_single_instance_checker(const std::string &name, const std::string &path)
{
    BOOST_LOG_TRIVIAL(debug) << "init wx instance checker " << name << " "<< path;
    m_single_instance_checker = std::make_unique<wxSingleInstanceChecker>(boost::nowide::widen(name), boost::nowide::widen(path));
}

bool GUI_App::OnInit()
{
    try {
        return on_init_inner();
    } catch (const std::exception&) {
        generic_exception_handle();
        return false;
    }
}

static int get_app_font_pt_size(const AppConfig* app_config)
{
    if (!app_config->has("font_pt_size"))
        return -1;
    const int font_pt_size     = atoi(app_config->get("font_pt_size").c_str());
    const int max_font_pt_size = wxGetApp().get_max_font_pt_size();

    return (font_pt_size > max_font_pt_size) ? max_font_pt_size : font_pt_size;
}

bool GUI_App::on_init_inner()
{
    // TODO: remove this when all asserts are gone.
    wxDisableAsserts();

    // Set initialization of image handlers before any UI actions - See GH issue #7469
    wxInitAllImageHandlers();

#if defined(_WIN32) && ! defined(_WIN64)
    // Win32 32bit build.
    if (wxPlatformInfo::Get().GetArchName().substr(0, 2) == "64") {
        RichMessageDialog dlg(nullptr,
            format_wxstr(_L("You are running a 32 bit build of %s on 64-bit Windows."
                "\n32 bit build of %s will likely not be able to utilize all the RAM available in the system."
                "\nPlease download and install a 64 bit build of %s."
                "\nDo you wish to continue?"),SLIC3R_APP_NAME,SLIC3R_APP_NAME,SLIC3R_APP_NAME),
            SLIC3R_APP_NAME, wxICON_QUESTION | wxYES_NO);
        if (dlg.ShowModal() != wxID_YES)
            return false;
    }
#endif // _WIN64

    // Forcing back menu icons under gtk2 and gtk3. Solution is based on:
    // https://docs.gtk.org/gtk3/class.Settings.html
    // see also https://docs.wxwidgets.org/3.0/classwx_menu_item.html#a2b5d6bcb820b992b1e4709facbf6d4fb
    // TODO: Find workaround for GTK4
#if defined(__WXGTK20__) || defined(__WXGTK3__)
    g_object_set (gtk_settings_get_default (), "gtk-menu-images", TRUE, NULL);
#endif

    // Verify resources path
    const wxString resources_dir = from_u8(Slic3r::resources_dir());
    wxCHECK_MSG(wxDirExists(resources_dir), false,
        wxString::Format("Resources path does not exist or is not a directory: %s", resources_dir));

#ifdef __linux__
    if (! check_old_linux_datadir(GetAppName())) {
        std::cerr << "Quitting, user chose to move their data to new location." << std::endl;
        return false;
    }
#endif

    // Enable this to get the default Win32 COMCTRL32 behavior of static boxes.
//    wxSystemOptions::SetOption("msw.staticbox.optimized-paint", 0);
    // Enable this to disable Windows Vista themes for all wxNotebooks. The themes seem to lead to terrible
    // performance when working on high resolution multi-display setups.
//    wxSystemOptions::SetOption("msw.notebook.themed-background", 0);

//     Slic3r::debugf "wxWidgets version %s, Wx version %s\n", wxVERSION_STRING, wxVERSION;

    // !!! Initialization of UI settings as a language, application color mode, fonts... have to be done before first UI action.
    // Like here, before the show InfoDialog in check_older_app_config()

    // If load_language() fails, the application closes.
    load_language(wxString(), true);
#ifdef _MSW_DARK_MODE
    bool init_dark_color_mode = app_config->get_bool("dark_color_mode");
    bool init_sys_menu_enabled = app_config->get_bool("sys_menu_enabled");
    NppDarkMode::InitDarkMode(init_dark_color_mode, init_sys_menu_enabled);
#endif
    // initialize label colors and fonts
    init_ui_colours();
    init_fonts();

    std::string older_data_dir_path;
    if (m_app_conf_exists) {
        if (app_config->orig_version().valid() && app_config->orig_version() < *Semver::parse(SLIC3R_VERSION)) {
            // Only copying configuration if it was saved with a newer slicer than the one currently running.
            older_data_dir_path = check_older_app_config(app_config->orig_version(), true);
            m_last_app_conf_lower_version = true;
        }
    } else {
        // No AppConfig exists, fresh install. Always try to copy from an alternate location, don't make backup of the current configuration.
        older_data_dir_path = check_older_app_config(Semver(), false);
        if (!older_data_dir_path.empty())
            m_last_app_conf_lower_version = true;
    }

    if (std::string current_layout_name = app_config->get("ui_layout"); current_layout_name != "Standard") {
        AppConfig::LayoutEntry my_layout = app_config->get_ui_layout();
        // check if version is up to date
        std::optional<Semver> current_ver = Semver::parse(app_config->get("version"));
        assert(current_ver);
        current_ver->set_patch(0); // disregard patch
        if (my_layout.version < *current_ver) {
            current_ver = Semver::parse(app_config->get("version"));
            //ask if the user don't want to switch to an up-to-date layout.
            wxString title = format_wxstr(_L("You are opening %1% version %2%."), SLIC3R_APP_NAME, SLIC3R_VERSION);
            wxString message = format_wxstr(_L(
                    "The active gui layout used '%1%' was created for version %2%,"
                    "\nwhile the current software is now at version %3%."
                    "\nDo you prefer to continue using this outdated layout?"
                    "\nIf not, the layout will be rolled back to the '%4%' layout"
                ), my_layout.name, my_layout.version.to_string(), current_ver->to_string(), "Default");
            InfoDialog msg(nullptr, title, message, true, wxYES_NO);
            if (msg.ShowModal() == wxID_NO) {
                app_config->set("ui_layout", "Standard");
                // reload (colors, tags)
                app_config->save();
                app_config->load();
            }
        }
    }

    if (std::string current_layout_name = app_config->get("ui_layout"); current_layout_name != "Standard") {
        AppConfig::LayoutEntry my_layout = app_config->get_ui_layout();
        // check if version is up to date
        std::optional<Semver> current_ver = Semver::parse(app_config->get("version"));
        assert(current_ver);
        current_ver->set_patch(0); // disregard patch
        if (my_layout.version < *current_ver) {
            current_ver = Semver::parse(app_config->get("version"));
            //ask if the user don't want to switch to an up-to-date layout.
            wxString title = format_wxstr(_L("You are opening %1% version %2%."), SLIC3R_APP_NAME, SLIC3R_VERSION);
            wxString message = format_wxstr(_L(
                    "The active gui layout used '%1%' was created for version %2%,"
                    "\nwhile the current software is now at version %3%."
                    "\nDo you prefer to continue using this outdated layout?"
                    "\nIf not, the layout will be rolled back to the '%4%' layout"
                ), my_layout.name, my_layout.version.to_string(), current_ver->to_string(), "Default");
            InfoDialog msg(nullptr, title, message, true, wxYES_NO);
            if (msg.ShowModal() == wxID_NO) {
                app_config->set("ui_layout", "Standard");
                // reload (colors, tags)
                app_config->save();
                app_config->load();
            }
        }
    }

#ifdef _MSW_DARK_MODE
    // app_config can be updated in check_older_app_config(), so check if dark_color_mode and sys_menu_enabled was changed
    if (bool new_dark_color_mode = app_config->get_bool("dark_color_mode");
        init_dark_color_mode != new_dark_color_mode) {
        NppDarkMode::SetDarkMode(new_dark_color_mode);
        init_ui_colours();
    }
    if (bool new_sys_menu_enabled = app_config->get_bool("sys_menu_enabled");
        init_sys_menu_enabled != new_sys_menu_enabled)
        NppDarkMode::SetSystemMenuForApp(new_sys_menu_enabled);
#endif
    update_ui_colours_from_appconfig();

    if (is_editor()) {
        std::string msg = Http::tls_global_init();
        std::string ssl_cert_store = app_config->get("tls_accepted_cert_store_location");
        bool ssl_accept = app_config->get("tls_cert_store_accepted") == "yes" && ssl_cert_store == Http::tls_system_cert_store();

        if (!msg.empty() && !ssl_accept) {
            RichMessageDialog
                dlg(nullptr,
                    wxString::Format(_L("%s\nDo you want to continue?"), msg),
                    SLIC3R_APP_NAME, wxICON_QUESTION | wxYES_NO);
            dlg.ShowCheckBox(_L("Remember my choice"));
            if (dlg.ShowModal() != wxID_YES) return false;

            app_config->set("tls_cert_store_accepted",
                dlg.IsCheckBoxChecked() ? "yes" : "no");
            app_config->set("tls_accepted_cert_store_location",
                dlg.IsCheckBoxChecked() ? Http::tls_system_cert_store() : "");
        }
    }

    SplashScreen* scrn = nullptr;
    double        scrn_scaling = 1.;
    if (app_config->get_bool("show_splash_screen")) {
        wxBitmap bmp;
        std::string file_name = app_config->splashscreen(is_editor());
        wxString artist;
        if (!file_name.empty()) {
            boost::filesystem::path splash_screen_path = (boost::filesystem::path(Slic3r::resources_dir()) / "splashscreen" / file_name);
            if (boost::filesystem::exists(splash_screen_path)) {
                wxString path_str = wxString::FromUTF8((splash_screen_path).string().c_str());
        // make a bitmap with dark grey banner on the left side
                bmp = SplashScreen::MakeBitmap(wxBitmap(path_str, wxBITMAP_TYPE_JPEG), scrn_scaling);

                //get the artist name from metadata
            int result;
            void** ifdArray = nullptr;
            ExifTagNodeInfo* tag;
                ifdArray = exif_createIfdTableArray(path_str.c_str(), &result);
                if (result > 0 && ifdArray) {
                    tag = exif_getTagInfo(ifdArray, IFD_0TH, TAG_Artist);
                    if (tag) {
                        if (!tag->error) {
                            wxString artist_name = wxString::FromUTF8((char*)tag->byteData);
                            artist_name = artist_name.Trim();
                            if (!artist_name.empty()) {
                                artist = (_L("Artwork model by") + " " + artist_name);
                            }
                        }
                    }
                }
            }
        }

        // Detect position (display) to show the splash screen
        // Now this position is equal to the mainframe position
        wxPoint splashscreen_pos = wxDefaultPosition;
        bool default_splashscreen_pos = true;
        if (app_config->has("window_mainframe") && app_config->get_bool("restore_win_position")) {
            std::optional<WindowMetrics> metrics = WindowMetrics::deserialize(app_config->get("window_mainframe"));
            default_splashscreen_pos = metrics.has_value();
            if (!default_splashscreen_pos)
                splashscreen_pos = metrics->get_rect().GetPosition();
        }

        if (!default_splashscreen_pos) {
            // workaround for crash related to the positioning of the window on secondary monitor
            get_app_config()->set("restore_win_position", "crashed_at_splashscreen_pos");
            get_app_config()->save();
        }

        // make a bitmap with dark grey banner on the left side
        if (!bmp.IsOk()) {
            bmp = SplashScreen::MakeBitmap(get_bmp_bundle(SLIC3R_APP_KEY, 600)->GetPreferredBitmapSizeAtScale(1.0), scrn_scaling);
        }
        scrn = new SplashScreen(bmp, scrn_scaling, wxSPLASH_CENTRE_ON_SCREEN | wxSPLASH_TIMEOUT, 4000, splashscreen_pos, artist);

        if (!default_splashscreen_pos)
            // revert "restore_win_position" value if application wasn't crashed
            get_app_config()->set("restore_win_position", "1");
#ifndef __linux__
        wxYield();
#endif
        scrn->SetText(_L("Loading configuration")+ dots);
    }

    preset_bundle.reset(nullptr);
    PresetBundle* new_preset_bundle = new PresetBundle();

    // just checking for existence of Slic3r::data_dir is not enough : it may be an empty directory
    // supplied as argument to --datadir; in that case we should still run the wizard
    new_preset_bundle->setup_directories();

    if (! older_data_dir_path.empty()) {
        new_preset_bundle->import_newer_configs(older_data_dir_path);
    }

    if (is_editor()) {
#ifdef __WXMSW__
        if (app_config->get("associate_3mf") == "1")
        if (app_config->get_bool("associate_3mf"))
            associate_3mf_files();
        if (app_config->get_bool("associate_stl"))
            associate_stl_files();
#endif // __WXMSW__

        preset_updater.reset(new PresetUpdater());
        Bind(EVT_SLIC3R_VERSION_ONLINE, &GUI_App::on_version_read, this);
        Bind(EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE, [this](const wxCommandEvent& evt) {
            if (this->plater_ != nullptr && (m_app_updater->get_triggered_by_user() || app_config->get("notify_release") == "all")) {
                std::string evt_string = into_u8(evt.GetString());
                if (*Semver::parse(SLIC3R_VERSION) < *Semver::parse(evt_string)) {
                    auto notif_type = (evt_string.find("beta") != std::string::npos ? NotificationType::NewBetaAvailable : NotificationType::NewAlphaAvailable);
                    this->plater_->get_notification_manager()->push_version_notification( notif_type
                        , NotificationManager::NotificationLevel::ImportantNotificationLevel
                        , Slic3r::format(_u8L("New prerelease version %1% is available."), evt_string)
                        , _u8L("See Releases page.")
                        , [](wxEvtHandler* evnthndlr) {wxGetApp().open_browser_with_warning_dialog("https://github.com/" SLIC3R_GITHUB "/releases"); return true; }
                    );
    }
            }
            });
        Bind(EVT_SLIC3R_APP_DOWNLOAD_PROGRESS, [this](const wxCommandEvent& evt) {
            //lm:This does not force a render. The progress bar only updateswhen the mouse is moved.
            if (this->plater_ != nullptr)
                this->plater_->get_notification_manager()->set_download_progress_percentage((float)std::stoi(into_u8(evt.GetString())) / 100.f );
        });

        Bind(EVT_SLIC3R_APP_DOWNLOAD_FAILED, [this](const wxCommandEvent& evt) {
            if (this->plater_ != nullptr)
                this->plater_->get_notification_manager()->close_notification_of_type(NotificationType::AppDownload);
            if(!evt.GetString().IsEmpty())
                show_error(nullptr, evt.GetString());
        });

        Bind(EVT_SLIC3R_APP_OPEN_FAILED, [](const wxCommandEvent& evt) {
            show_error(nullptr, evt.GetString());
        });

        Bind(EVT_CONFIG_UPDATER_SYNC_DONE, [this](const wxCommandEvent& evt) {
            this->check_updates(false);
        });

    }
    else {
#ifdef __WXMSW__
        if (app_config->get_bool("associate_gcode"))
            associate_gcode_files();
        if (app_config->get_bool("associate_bgcode"))
            associate_bgcode_files();
#endif // __WXMSW__
    }

    std::string delayed_error_load_presets;
    wxImage::AddHandler(new wxJPEGHandler());
    // Suppress the '- default -' presets.
    new_preset_bundle->set_default_suppressed(app_config->get_bool("no_defaults"));
    try {
        // Enable all substitutions (in both user and system profiles), but log the substitutions in user profiles only.
        // If there are substitutions in system profiles, then a "reconfigure" event shall be triggered, which will force
        // installation of a compatible system preset, thus nullifying the system preset substitutions.
        init_params->preset_substitutions = new_preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSystemSilent);
    } catch (const std::exception &ex) {
        delayed_error_load_presets = ex.what();
    }

    //now that new_preset_bundle is initialized, we can publish it
    preset_bundle.reset(new_preset_bundle);

#ifdef WIN32
#if !wxVERSION_EQUAL_OR_GREATER_THAN(3,1,3)
    register_win32_dpi_event();
#endif // !wxVERSION_EQUAL_OR_GREATER_THAN
    register_win32_device_notification_event();
#endif // WIN32

    // Let the libslic3r know the callback, which will translate messages on demand.
    Slic3r::I18N::set_translate_callback(libslic3r_translate_callback);

    // application frame
    if (scrn && is_editor())
        scrn->SetText(_L("Preparing settings tabs") + dots);

    if (!delayed_error_load_presets.empty())
        show_error(nullptr, delayed_error_load_presets);

    mainframe = new MainFrame(get_app_font_pt_size(app_config.get()));
    // hide settings tabs after first Layout
    if (is_editor())
        mainframe->select_tab(MainFrame::ETabType::LastPlater);
    else
        mainframe->select_tab(MainFrame::ETabType::PlaterGcode);

    sidebar().obj_list()->init_objects(); // propagate model objects to object list
//     update_mode(); // !!! do that later
    SetTopWindow(mainframe);

    plater_->init_notification_manager();

    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));

    if (is_gcode_viewer()) {
        mainframe->update_layout();
        if (plater_ != nullptr)
            // ensure the selected technology is ptFFF
            plater_->set_printer_technology(ptFFF);
    }
    else
        load_current_presets();

    // Save the active profiles as a "saved into project".
    update_saved_preset_from_current_preset();

    if (plater_ != nullptr) {
        // Save the names of active presets and project specific config into ProjectDirtyStateManager.
        plater_->reset_project_dirty_initial_presets();
        // Update Project dirty state, update application title bar.
        plater_->update_project_dirty_from_presets();
    }

    mainframe->Show(true);

    obj_list()->set_min_height();

    update_mode(); // update view mode after fix of the object_list size

#ifdef __APPLE__
    other_instance_message_handler()->bring_instance_forward();
#endif //__APPLE__

    Bind(wxEVT_IDLE, [this](wxIdleEvent& event)
    {
        if (! plater_)
            return;

        this->obj_manipul()->update_if_dirty();

        // An ugly solution to GH #5537 in which GUI_App::init_opengl (normally called from events wxEVT_PAINT
        // and wxEVT_SET_FOCUS before GUI_App::post_init is called) wasn't called before GUI_App::post_init and OpenGL wasn't initialized.
        // Since issue #9774 Where same problem occured on MacOS Ventura, we decided to have this check on MacOS as well.

#if defined(__linux__) || defined(__APPLE__)
        if (!m_post_initialized && m_opengl_initialized) {
#else
        if (!m_post_initialized) {
#endif
            m_post_initialized = true;

#ifdef WIN32
            this->mainframe->register_win32_callbacks();
#endif
            this->post_init();
        }

        if (m_post_initialized && app_config->dirty())
            app_config->save();
    });

    m_initialized = true;

    if (const std::string& crash_reason = app_config->get("restore_win_position");
        boost::starts_with(crash_reason,"crashed"))
    {
        wxString preferences_item = _L("Restore window position on start");
        InfoDialog dialog(nullptr,
            format_wxstr(_L("%1% started after a crash"), SLIC3R_APP_NAME),
            format_wxstr(_L("%5% crashed last time when attempting to set window position.\n"
                "We are sorry for the inconvenience, it unfortunately happens with certain multiple-monitor setups.\n"
                "More precise reason for the crash: \"%1%\".\n"
                "For more information see our GitHub issue tracker: \"%2%\" and \"%3%\"\n\n"
                "To avoid this problem, consider disabling \"%4%\" in \"Preferences\". "
                "Otherwise, the application will most likely crash again next time."),
                "<b>" + from_u8(crash_reason) + "</b>",
                "<a href=http://github.com/prusa3d/PrusaSlicer/issues/2939>#2939</a>",
                "<a href=http://github.com/prusa3d/PrusaSlicer/issues/5573>#5573</a>",
                "<b>" + preferences_item + "</b>",
                SLIC3R_APP_NAME),
            true, wxYES_NO);

        dialog.SetButtonLabel(wxID_YES, format_wxstr(_L("Disable \"%1%\""), preferences_item));
        dialog.SetButtonLabel(wxID_NO,  format_wxstr(_L("Leave \"%1%\" enabled") , preferences_item));

        auto answer = dialog.ShowModal();
        if (answer == wxID_YES)
            app_config->set("restore_win_position", "0");
        else if (answer == wxID_NO)
            app_config->set("restore_win_position", "1");
    }

    return true;
}

unsigned GUI_App::get_colour_approx_luma(const wxColour &colour)
{
    double r = colour.Red();
    double g = colour.Green();
    double b = colour.Blue();

    return std::round(std::sqrt(
        r * r * .241 +
        g * g * .691 +
        b * b * .068
        ));
}

bool GUI_App::dark_mode()
{
#if __APPLE__
    // The check for dark mode returns false positive on 10.12 and 10.13,
    // which allowed setting dark menu bar and dock area, which is
    // is detected as dark mode. We must run on at least 10.14 where the
    // proper dark mode was first introduced.
    return wxPlatformInfo::Get().CheckOSVersion(10, 14) && mac_dark_mode();
#else
    if (wxGetApp().app_config->has("dark_color_mode"))
        return wxGetApp().app_config->get_bool("dark_color_mode");
    return check_dark_mode();
#endif
}

const wxColour GUI_App::get_label_default_clr_system(bool is_dark_mode)
{
    return is_dark_mode ? wxColour(115, 220, 103) : wxColour(26, 132, 57);
}

const wxColour GUI_App::get_label_default_clr_modified(bool is_dark_mode)
{
    return is_dark_mode ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
}

const wxColour GUI_App::get_label_default_clr_default(bool is_dark_mode)
{
    return is_dark_mode ? wxColour(230, 230, 230) : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
}

const wxColour GUI_App::get_label_default_clr_phony(bool is_dark_mode)
{
    return is_dark_mode ? wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT) : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT);
}

const wxColour &GUI_App::get_label_clr_default() {
    return dark_mode() ? m_color_dark_mode_label_default : m_color_label_default;
}

const wxColour &GUI_App::get_label_clr_modified() {
    return dark_mode() ? m_color_dark_mode_label_modified : m_color_label_modified;
}

const wxColour &GUI_App::get_label_clr_sys() {
    return dark_mode() ? m_color_dark_mode_label_sys : m_color_label_sys;
}

const wxColour &GUI_App::get_label_clr_phony() {
    return dark_mode() ? m_color_dark_mode_label_phony : m_color_label_phony;
}

#ifdef GUI_TAG_PALETTE
std::map<ConfigOptionMode, std::string> GUI_App::get_mode_default_palette()
{
    std::map<ConfigOptionMode, std::string> tag_color_map;
    //default (minimal tags to work)
    tag_color_map[ConfigOptionMode::comSimple] = "#7DF028";
    tag_color_map[ConfigOptionMode::comAdvanced] = "#FFDC00";
    tag_color_map[ConfigOptionMode::comExpert] = "#E70000";
    //get from color.ini
    for (Tag app_config->tags()) {
        tag_color_map[tag] = color_hash
    }
    return tag_color_map;
}
#endif

void GUI_App::init_ui_colours()
{
    m_color_label_modified  = get_label_default_clr_modified(false);
    m_color_label_sys       = get_label_default_clr_system(false);
    m_color_label_default   = get_label_default_clr_default(false);
    m_color_label_phony     = get_label_default_clr_phony(false);
    m_color_dark_mode_label_modified = get_label_default_clr_modified(true);
    m_color_dark_mode_label_sys      = get_label_default_clr_system(true);
    m_color_dark_mode_label_default  = get_label_default_clr_default(true);
    m_color_dark_mode_label_phony    = get_label_default_clr_phony(true);
#ifdef GUI_TAG_PALETTE
    m_mode_palette = get_mode_default_palette();
#endif

    const bool is_dark_mode = dark_mode();
#ifdef _MSW_DARK_MODE
    m_color_highlight_label_default = is_dark_mode ? wxColour(230, 230, 230): wxSystemSettings::GetColour(/*wxSYS_COLOUR_HIGHLIGHTTEXT*/wxSYS_COLOUR_WINDOWTEXT);
    m_color_highlight_default       = is_dark_mode ? wxColour(78, 78, 78)   : wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT);
    // Prusa: is_dark_mode ? wxColour(253, 111, 40) : wxColour(252, 77, 1); (fd6f28 & fc4d01) SV: 84 99 ; 100 99 (with light hue diff)
    // custom background color for the notbook (tab) button
    m_color_hovered_btn             = is_dark_mode ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
    // text color on hover on any button
    m_color_hovered_btn_label       = is_dark_mode ? wxColour(253, 111, 40) : wxColour(252, 77, 1);
    // m_color_default_btn_label: color of the ok button text in normal state (default button when clickingon enter). And graph line
    m_color_default_btn_label       = is_dark_mode ? wxColour(255, 181, 100): wxColour(203, 61, 0);
    // Prusa: is_dark_mode ? wxColour(95, 73, 62)   : wxColour(228, 220, 216); (f2ba9e & e4dcd8) SV: 35 37 ;  5 90
    m_color_selected_btn_bg         = is_dark_mode ? wxColour(95, 73, 62)   : wxColour(228, 220, 216);
#endif
    m_color_window_default          = is_dark_mode ? wxColour(43, 43, 43)   : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
}

void GUI_App::update_ui_colours_from_appconfig()
{
    // load label colors
    if (app_config->has("label_clr_sys")) {
        auto str = app_config->get("label_clr_sys");
        if (!str.empty())
            m_color_label_sys = wxColour(str);
    }
    if (app_config->has("label_clr_dark_mode_sys")) {
        auto str = app_config->get("label_clr_dark_mode_sys");
        if (str != "")
            m_color_dark_mode_label_sys = wxColour(str);
    }

    if (app_config->has("label_clr_modified")) {
        auto str = app_config->get("label_clr_modified");
        if (!str.empty())
            m_color_label_modified = wxColour(str);
    }
    if (app_config->has("label_clr_dark_mode_modified")) {
        auto str = app_config->get("label_clr_dark_mode_modified");
        if (str != "")
            m_color_dark_mode_label_modified = wxColour(str);
    }

    if (app_config->has("label_clr_default")) {
        auto str = app_config->get("label_clr_default");
        if (str != "")
            m_color_label_default = wxColour(str);
    }
    if (app_config->has("label_clr_dark_mode_default")) {
        auto str = app_config->get("label_clr_dark_mode_default");
        if (str != "")
            m_color_dark_mode_label_default = wxColour(str);
    }
    if (app_config->has("label_clr_phony")) {
        auto str = app_config->get("label_clr_phony");
        if (str != "")
            m_color_label_phony = wxColour(str);
    }
    if (app_config->has("label_clr_dark_mode_phony")) {
        auto str = app_config->get("label_clr_dark_mode_phony");
        if (str != "")
            m_color_dark_mode_label_phony = wxColour(str);
    }
#ifdef GUI_TAG_PALETTE
    // load mode markers colors
    if (app_config->has("mode_palette")) {
        const auto colors = app_config->get("mode_palette");
        if (!colors.empty()) {
            //m_mode_palette.clear(); // don't clear, keep default.
            if (!unescape_strings_cstyle(colors, m_mode_palette))
                m_mode_palette = get_mode_default_palette();
        }
    }
#endif

    Slic3r::GUI::Widget::set_clr_border_hovered(change_endian_int24(
        app_config->create_color(0.86f, 0.93f, AppConfig::EAppColorType::Highlight)));
    Slic3r::GUI::Widget::set_clr_background_focused(change_endian_int24(
        app_config->create_color(0.86f, 0.93f, AppConfig::EAppColorType::Main)));

#ifdef _WIN32
    const bool is_dark_mode = dark_mode();
    m_color_hovered_btn = is_dark_mode ? color_from_int(app_config->create_color(0.84f, 0.99f, AppConfig::EAppColorType::Main)) :
        color_from_int(app_config->create_color(1.00f, 0.99f, AppConfig::EAppColorType::Main));
    m_color_hovered_btn_label = is_dark_mode ? color_from_int(app_config->create_color(0.84f, 0.99f, AppConfig::EAppColorType::Highlight)) :
        color_from_int(app_config->create_color(1.00f, 0.99f, AppConfig::EAppColorType::Highlight));
    m_color_default_btn_label = is_dark_mode ? color_from_int(app_config->create_color(0.9f, 0.80f, AppConfig::EAppColorType::Highlight)) :
        color_from_int(app_config->create_color(1.00f, 0.80f, AppConfig::EAppColorType::Highlight));
    m_color_selected_btn_bg = is_dark_mode ? color_from_int(app_config->create_color(0.35f, 0.37f, AppConfig::EAppColorType::Main)) :
        color_from_int(app_config->create_color(0.05f, 0.9f, AppConfig::EAppColorType::Main));
#endif

    //also update imgui color cache... can be moved if you have a better placee it
    m_imgui->reset_color();
}

void GUI_App::update_label_colours()
{
    for (Tab* tab : tabs_list)
        tab->update_label_colours();
}

#ifdef _WIN32
static bool is_focused(HWND hWnd)
{
    HWND hFocusedWnd = ::GetFocus();
    return hFocusedWnd && hWnd == hFocusedWnd;
}

static bool is_default(wxWindow* win)
{
    wxTopLevelWindow* tlw = find_toplevel_parent(win);
    if (!tlw)
        return false;

    return win == tlw->GetDefaultItem();
}
#endif

void GUI_App::UpdateDarkUI(wxWindow* window, bool highlited/* = false*/, bool just_font/* = false*/)
{
#ifdef _WIN32
    const bool is_dark_mode = dark_mode();
    bool is_focused_button = false;
    bool is_default_button = false;
    if (wxButton* btn = dynamic_cast<wxButton*>(window)) {
        if (!(btn->GetWindowStyle() & wxNO_BORDER)) {
            btn->SetWindowStyle(btn->GetWindowStyle() | wxNO_BORDER);
            highlited = true;
        }
        // button marking
        {
            auto mark_button = [this, btn, highlited](const bool mark) {
                if (btn->GetLabel().IsEmpty()) {
                    btn->SetBackgroundColour(mark          ? m_color_selected_btn_bg :
                                                 highlited ? m_color_highlight_default :
                                                             m_color_window_default);
                } else {
                    btn->SetForegroundColour(
                        mark ? m_color_hovered_btn_label :
                               (is_default(btn) ? m_color_default_btn_label : get_label_clr_default()));
                }
                btn->Refresh();
                btn->Update();
            };

            // hovering
            btn->Bind(wxEVT_ENTER_WINDOW, [mark_button](wxMouseEvent& event) { mark_button(true); event.Skip(); });
            btn->Bind(wxEVT_LEAVE_WINDOW, [mark_button, btn](wxMouseEvent& event) { mark_button(is_focused(btn->GetHWND())); event.Skip(); });
            // focusing
            btn->Bind(wxEVT_SET_FOCUS,    [mark_button](wxFocusEvent& event) { mark_button(true); event.Skip(); });
            btn->Bind(wxEVT_KILL_FOCUS,   [mark_button](wxFocusEvent& event) { mark_button(false); event.Skip(); });

            is_focused_button = is_focused(btn->GetHWND());
            is_default_button = is_default(btn);
            if (is_focused_button || is_default_button)
                mark_button(is_focused_button);
        }
    }
    else if (wxTextCtrl* text = dynamic_cast<wxTextCtrl*>(window)) {
        if (text->GetBorder() != wxBORDER_SIMPLE)
            text->SetWindowStyle(text->GetWindowStyle() | wxBORDER_SIMPLE);
    }
    else if (wxCheckListBox* list = dynamic_cast<wxCheckListBox*>(window)) {
        list->SetWindowStyle(list->GetWindowStyle() | wxBORDER_SIMPLE);
        list->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
        for (size_t i = 0; i < list->GetCount(); i++)
            if (wxOwnerDrawn* item = list->GetItem(i)) {
                item->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
                item->SetTextColour(is_dark_mode ? m_color_dark_mode_label_default : m_color_label_default);
            }
        return;
    }
    else if (dynamic_cast<wxListBox*>(window)) {
        window->SetWindowStyle(window->GetWindowStyle() | wxBORDER_SIMPLE);
    }
    else if (wxCollapsiblePane* pane = dynamic_cast<wxCollapsiblePane*>(window)) {
        if (!(pane->GetWindowStyle() & wxNO_BORDER)) {
            pane->SetWindowStyle(pane->GetWindowStyle() | wxNO_BORDER);
        }
        pane->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
        pane->SetForegroundColour(is_dark_mode ? m_color_dark_mode_label_default : m_color_label_default);
        wxWindowList& lst = pane->GetChildren();
        for (size_t i = 0; i < lst.size(); i++)
            if (wxWindow* item = lst[i]) {
                item->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
                item->SetForegroundColour(is_dark_mode ? m_color_dark_mode_label_default : m_color_label_default);
            }
    }

    if (!just_font)
        window->SetBackgroundColour(highlited ? m_color_highlight_default : m_color_window_default);
    if (!is_focused_button && !is_default_button)
        window->SetForegroundColour(is_dark_mode ? m_color_dark_mode_label_default : m_color_label_default);
#endif
}

// recursive function for scaling fonts for all controls in Window
#ifdef _WIN32
static void update_dark_children_ui(wxWindow* window, bool just_buttons_update = false)
{
    bool is_btn = dynamic_cast<wxButton*>(window) != nullptr;
    if (!(just_buttons_update && !is_btn))
        wxGetApp().UpdateDarkUI(window, is_btn);

    auto children = window->GetChildren();
    for (auto child : children) {
        update_dark_children_ui(child);
    }
}
#endif

// Note: Don't use this function for Dialog contains ScalableButtons
void GUI_App::UpdateDlgDarkUI(wxDialog* dlg, bool just_buttons_update/* = false*/)
{
#ifdef _WIN32
    update_dark_children_ui(dlg, just_buttons_update);
#endif
}
void GUI_App::UpdateDVCDarkUI(wxDataViewCtrl* dvc, bool highlited/* = false*/)
{
#ifdef _WIN32
    UpdateDarkUI(dvc, highlited ? dark_mode() : false);
#ifdef _MSW_DARK_MODE
    if (!dvc->HasFlag(wxDV_NO_HEADER))
        dvc->RefreshHeaderDarkMode(&m_normal_font);
#endif //_MSW_DARK_MODE
    if (dvc->HasFlag(wxDV_ROW_LINES))
        dvc->SetAlternateRowColour(m_color_highlight_default);
    if (dvc->GetBorder() != wxBORDER_SIMPLE)
        dvc->SetWindowStyle(dvc->GetWindowStyle() | wxBORDER_SIMPLE);
#endif
}

void GUI_App::UpdateAllStaticTextDarkUI(wxWindow* parent)
{
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(parent);

    auto children = parent->GetChildren();
    for (auto child : children) {
        if (dynamic_cast<wxStaticText*>(child))
            child->SetForegroundColour(dark_mode() ? m_color_dark_mode_label_default : m_color_label_default);
    }
#endif
}

void GUI_App::SetWindowVariantForButton(wxButton* btn)
{
#ifdef __APPLE__
    // This is a limit imposed by OSX. The way the native button widget is drawn only allows it to be stretched horizontally,
    // and the vertical size is fixed. (see https://stackoverflow.com/questions/29083891/wxpython-button-size-being-ignored-on-osx)
    // But standard height is possible to change using SetWindowVariant method (see https://docs.wxwidgets.org/3.0/window_8h.html#a879bccd2c987fedf06030a8abcbba8ac)
    if (m_normal_font.GetPointSize() > 15) {
        btn->SetWindowVariant(wxWINDOW_VARIANT_LARGE);
        btn->SetFont(m_normal_font);
    }
#endif
}

int GUI_App::get_max_font_pt_size()
{
    const unsigned disp_count = wxDisplay::GetCount();
    for (unsigned i = 0; i < disp_count; i++) {
        const wxRect display_rect = wxDisplay(i).GetGeometry();
        if (display_rect.width >= 2560 && display_rect.height >= 1440)
            return 20;
    }
    return 15;
}

void GUI_App::init_fonts()
{
    m_small_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    m_bold_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
    m_normal_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);

#ifdef __WXMAC__
    m_small_font.SetPointSize(11);
    m_bold_font.SetPointSize(13);
#endif /*__WXMAC__*/

    // wxSYS_OEM_FIXED_FONT and wxSYS_ANSI_FIXED_FONT use the same as
    // DEFAULT in wxGtk. Use the TELETYPE family as a work-around
    m_code_font = wxFont(wxFontInfo().Family(wxFONTFAMILY_TELETYPE));
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void GUI_App::update_fonts(const MainFrame *main_frame)
{
    /* Only normal and bold fonts are used for an application rescale,
     * because of under MSW small and normal fonts are the same.
     * To avoid same rescaling twice, just fill this values
     * from rescaled MainFrame
     */
	if (main_frame == nullptr)
		main_frame = this->mainframe;
    m_normal_font   = main_frame->normal_font();
    m_small_font    = m_normal_font;
    m_bold_font     = main_frame->normal_font().Bold();
    m_link_font     = m_bold_font.Underlined();
    m_em_unit       = main_frame->em_unit();
    m_code_font.SetPointSize(m_normal_font.GetPointSize());
}

void GUI_App::set_label_clr_modified(const wxColour& clr)
{
    if (dark_mode()) {
        if (m_color_dark_mode_label_modified == clr)
            return;
        m_color_dark_mode_label_modified = clr;
        const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
        app_config->set("label_clr_dark_mode_modified", str);
        return;
    }
    if (m_color_label_modified == clr)
        return;
    m_color_label_modified = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->set("label_clr_modified", str);
}

void GUI_App::set_label_clr_sys(const wxColour& clr)
{
    if (dark_mode()) {
        if (m_color_dark_mode_label_sys == clr)
            return;
        m_color_dark_mode_label_sys = clr;
        const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
        app_config->set("label_clr_dark_mode_sys", str);
        return;
    }
    if (m_color_label_sys == clr)
        return;
    m_color_label_sys = clr;
    const std::string str = encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
    app_config->set("label_clr_sys", str);
}

void GUI_App::set_label_clr_default(const wxColour& clr) {
    if (dark_mode()) {
        if (m_color_dark_mode_label_default == clr)
            return;
        m_color_dark_mode_label_default = clr;
        auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
        std::string str = clr_str.ToStdString();
        app_config->set("label_clr_dark_mode_default", str);
        app_config->save();
        return;
    }
    if (m_color_label_default == clr)
        return;
    m_color_label_default = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->set("label_clr_default", str);
    app_config->save();
}

void GUI_App::set_label_clr_phony(const wxColour& clr) {
    if (dark_mode()) {
        if (m_color_dark_mode_label_phony == clr)
            return;
        m_color_dark_mode_label_phony = clr;
        auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
        std::string str = clr_str.ToStdString();
        app_config->set("label_clr_dark_mode_phony", str);
        app_config->save();
        return;
    }
    if (m_color_label_phony == clr)
        return;
    m_color_label_phony = clr;
    auto clr_str = wxString::Format(wxT("#%02X%02X%02X"), clr.Red(), clr.Green(), clr.Blue());
    std::string str = clr_str.ToStdString();
    app_config->set("label_clr_phony", str);
    app_config->save();
}

const std::string GUI_App::get_html_bg_color(wxWindow* html_parent)
{
    wxColour    bgr_clr = html_parent->GetBackgroundColour();
#ifdef __APPLE__
    // On macOS 10.13 and older the background color returned by wxWidgets
    // is wrong, which leads to https://github.com/prusa3d/PrusaSlicer/issues/7603
    // and https://github.com/prusa3d/PrusaSlicer/issues/3775. wxSYS_COLOUR_WINDOW
    // may not match the window background exactly, but it seems to never end up
    // as black on black.

    if (wxPlatformInfo::Get().GetOSMajorVersion() == 10
        && wxPlatformInfo::Get().GetOSMinorVersion() < 14)
        bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
#endif

    return encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));
}

std::string GUI_App::get_first_mode_btn_color(ConfigOptionMode mode_id) const
{
    assert(0 <= size_t(mode_id));

    for (const AppConfig::Tag& tag : get_app_config()->tags()) {
        // get the first good tag.
        if ((tag.tag & mode_id) == tag.tag) {
            return tag.color_hash;
        }
    }
    return "";
}

std::string GUI_App::get_last_mode_btn_color(ConfigOptionMode mode_id) const
{
    assert(0 <= size_t(mode_id));
    assert(size_t(mode_id)< get_app_config()->tags().size());
    for (size_t idx_p1 = get_app_config()->tags().size(); idx_p1 > 0; --idx_p1) {
        const AppConfig::Tag& tag = get_app_config()->tags()[idx_p1-1];
        // get the first good tag.
        if ((tag.tag & mode_id) == tag.tag) {
            // store the pointer so we can return a valid reference.
            return tag.color_hash;
        }
    }
    return"";
}

#ifdef GUI_TAG_PALETTE
std::map<ConfigOptionMode, wxColour> GUI_App::get_mode_palette() const
{
    std::map<ConfigOptionMode, wxColour> ret_map;
    //if(size_t(mode_id) < m_mode_palette.size()
    for (const AppConfig::Tag& tag : get_app_config()->tags()) {
        ret_map[tag.tag] = wxColor(tag.color_hash);
    }
    return ret_map;
}

void GUI_App::set_mode_palette(std::map<ConfigOptionMode, wxColour>& palette)
{
    bool save = false;

    for (const auto& [tag, wxcolor] : palette) {
        const wxColour& clr = wxcolor;
        std::string color_str = clr == wxTransparentColour ? std::string("") : encode_color(ColorRGB(clr.Red(), clr.Green(), clr.Blue()));
        if (auto it = m_mode_palette.find(tag); it == map.end() || it->second != color_str) {
            it->second = color_str;
            save = true;
        }
    }

    if (save) {
        mainframe->update_mode_markers();
        app_config->set("mode_palette", escape_strings_cstyle(m_mode_palette));
    }
}
#endif

bool GUI_App::tabs_as_menu() const
{
    return app_config->get_bool("tabs_as_menu"); // || dark_mode();
}

bool GUI_App::suppress_round_corners() const
{
    return true;// app_config->get("suppress_round_corners") == "1";
}

wxSize GUI_App::get_min_size(wxWindow* display_win) const
{
    wxSize min_size(76*m_em_unit, 49 * m_em_unit);

    const wxDisplay display = wxDisplay(display_win);
    wxRect display_rect = display.GetGeometry();
    display_rect.width  *= 0.75;
    display_rect.height *= 0.75;

    if (min_size.x > display_rect.GetWidth())
        min_size.x = display_rect.GetWidth();
    if (min_size.y > display_rect.GetHeight())
        min_size.y = display_rect.GetHeight();

    return min_size;
}

float GUI_App::toolbar_icon_scale(const bool is_limited/* = false*/) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit*0.1f;
#endif // __APPLE__

    const std::string& use_val  = app_config->get("use_custom_toolbar_size");
    const std::string& val      = app_config->get("custom_toolbar_size");
    const std::string& auto_val = app_config->get("auto_toolbar_size");

    if (val.empty() || auto_val.empty() || use_val.empty())
        return icon_sc;

    int int_val = use_val == "0" ? 100 : atoi(val.c_str());
    // correct value in respect to auto_toolbar_size
    int_val = std::min(atoi(auto_val.c_str()), int_val);

    if (is_limited && int_val < 50)
        int_val = 50;

    return 0.01f * int_val * icon_sc;
}

void GUI_App::set_auto_toolbar_icon_scale(float scale) const
{
#ifdef __APPLE__
    const float icon_sc = 1.0f; // for Retina display will be used its own scale
#else
    const float icon_sc = m_em_unit * 0.1f;
#endif // __APPLE__

    long int_val = std::min(int(std::lround(scale / icon_sc * 100)), 100);
    std::string val = std::to_string(int_val);

    app_config->set("auto_toolbar_size", val);
}

// check user printer_presets for the containing information about "Print Host upload"
void GUI_App::check_printer_presets()
{
    std::vector<std::string> preset_names = PhysicalPrinter::presets_with_print_host_information(preset_bundle->printers);
    if (preset_names.empty())
        return;

    wxString msg_text =  _L("You have the following presets with saved options for \"Print Host upload\"") + ":";
    for (const std::string& preset_name : preset_names)
        msg_text += "\n    \"" + from_u8(preset_name) + "\",";
    msg_text.RemoveLast();
    msg_text += "\n\n" + format(_L("But since this version of %s we don't show this information in Printer Settings anymore.\n"
                            "Settings will be available in physical printers settings."), SLIC3R_APP_NAME) + "\n\n" +
                         _L("By default new Printer devices will be named as \"Printer N\" during its creation.\n"
                            "Note: This name can be changed later from the physical printers settings");

    //wxMessageDialog(nullptr, msg_text, _L("Information"), wxOK | wxICON_INFORMATION).ShowModal();
    MessageDialog(nullptr, msg_text, _L("Information"), wxOK | wxICON_INFORMATION).ShowModal();

    preset_bundle->physical_printers.load_printers_from_presets(preset_bundle->printers);
}

void GUI_App::recreate_GUI(const wxString& msg_name)
{
    m_is_recreating_gui = true;

    mainframe->shutdown();

    wxProgressDialog dlg(msg_name, msg_name, 100, nullptr, wxPD_AUTO_HIDE);
    dlg.Pulse();
    dlg.Update(10, _L("Recreating") + dots);

    // re-init app config for new tags, colors, layout.
    this->init_app_config();

    MainFrame *old_main_frame = mainframe;
    mainframe = new MainFrame(get_app_font_pt_size(app_config.get()));
    if (is_editor())
        // hide settings tabs after first Layout
        mainframe->select_tab(MainFrame::ETabType::LastPlater);
    else
        mainframe->select_tab(MainFrame::ETabType::PlaterGcode);
    // Propagate model objects to object list.
    sidebar().obj_list()->init_objects();
    SetTopWindow(mainframe);

    dlg.Update(30, _L("Recreating") + dots);
    old_main_frame->Destroy();

    dlg.Update(80, _L("Loading of current presets") + dots);
    m_printhost_job_queue.reset(new PrintHostJobQueue(mainframe->printhost_queue_dlg()));
    load_current_presets();
    mainframe->Show(true);

    dlg.Update(90, _L("Loading of a mode view") + dots);

    obj_list()->set_min_height();
    update_mode();

    // #ys_FIXME_delete_after_testing  Do we still need this  ?
//     CallAfter([]() {
//         // Run the config wizard, don't offer the "reset user profile" checkbox.
//         config_wizard_startup(true);
//     });

    m_is_recreating_gui = false;
}

void GUI_App::system_info()
{
    SysInfoDialog dlg;
    dlg.ShowModal();
}

void GUI_App::keyboard_shortcuts()
{
    KBShortcutsDialog dlg;
    dlg.ShowModal();
}

void GUI_App::change_calibration_dialog(const wxDialog* have_to_destroy, wxDialog* new_one)
{
    if (have_to_destroy == nullptr) {
        wxDialog* to_destroy = nullptr;
        {
            //hove to ensure that this release is "atomic"
            std::unique_lock<std::mutex> lock(not_modal_dialog_mutex);
            to_destroy = not_modal_dialog;
            not_modal_dialog = nullptr;
        }
        if (to_destroy != nullptr) {
            to_destroy->Destroy();
        }
    } else {
        //hove to ensure that these two command are "atomic"
        std::unique_lock<std::mutex> lock(not_modal_dialog_mutex);
        if (not_modal_dialog == have_to_destroy) {
            not_modal_dialog = nullptr;
        }
    }
    if (new_one != nullptr) {
        {
            //hove to ensure that these command are "atomic"
            std::unique_lock<std::mutex> lock(not_modal_dialog_mutex);
            if (not_modal_dialog != nullptr)
                not_modal_dialog->Destroy();
            not_modal_dialog = new_one;
        }
        new_one->Show();
    }
}

void GUI_App::html_dialog()
{
    change_calibration_dialog(nullptr, new HtmlDialog(this, mainframe,"Introduction to calibrations", "/calibration", "introduction.html"));
}
void GUI_App::flow_ratio_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationFlowDialog(this, mainframe));
}
void GUI_App::flow_speed_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationFlowSpeedDialog(this, mainframe));
}
void GUI_App::over_bridge_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationOverBridgeDialog(this, mainframe));
}
void GUI_App::bridge_tuning_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationBridgeDialog(this, mainframe));
}
void GUI_App::filament_temperature_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationTempDialog(this, mainframe));
}
void GUI_App::calibration_cube_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationCubeDialog(this, mainframe));
}
void GUI_App::calibration_first_layer_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationFirstLayerDialog(this, mainframe));
}
void GUI_App::calibration_first_layer_patch_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationFirstLayerPatchDialog(this, mainframe));
}
void GUI_App::calibration_extrusion_multiplier_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationExtruderDialog(this, mainframe));
}
void GUI_App::calibration_retraction_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationRetractionDialog(this, mainframe));
}
void GUI_App::calibration_pressureadv_dialog()
{
    change_calibration_dialog(nullptr, new CalibrationPressureAdvDialog(this, mainframe));
}
void GUI_App::freecad_script_dialog()
{
    change_calibration_dialog(nullptr, new FreeCADDialog(this, mainframe));
}
void GUI_App::tiled_canvas_dialog()
{
    change_calibration_dialog(nullptr, new CreateMMUTiledCanvas(this, mainframe));
}

// static method accepting a wxWindow object as first parameter
bool GUI_App::catch_error(std::function<void()> cb,
    //                       wxMessageDialog* message_dialog,
    const std::string& err /*= ""*/)
{
    if (!err.empty()) {
        if (cb)
            cb();
        //         if (message_dialog)
        //             message_dialog->(err, "Error", wxOK | wxICON_ERROR);
        show_error(/*this*/nullptr, err);
        return true;
    }
    return false;
}

// static method accepting a wxWindow object as first parameter
void fatal_error(wxWindow* parent)
{
    show_error(parent, "");
    //     exit 1; // #ys_FIXME
}

#ifdef _WIN32

#ifdef _MSW_DARK_MODE
static void update_scrolls(wxWindow* window)
{
    wxWindowList::compatibility_iterator node = window->GetChildren().GetFirst();
    while (node)
    {
        wxWindow* win = node->GetData();
        if (dynamic_cast<wxScrollHelper*>(win) ||
            dynamic_cast<wxTreeCtrl*>(win) ||
            dynamic_cast<wxTextCtrl*>(win))
            NppDarkMode::SetDarkExplorerTheme(win->GetHWND());

        update_scrolls(win);
        node = node->GetNext();
    }
}
#endif //_MSW_DARK_MODE


#ifdef _MSW_DARK_MODE
void GUI_App::force_menu_update()
{
    NppDarkMode::SetSystemMenuForApp(app_config->get_bool("sys_menu_enabled"));
}
#endif //_MSW_DARK_MODE

void GUI_App::force_colors_update()
{
#ifdef _MSW_DARK_MODE
    NppDarkMode::SetDarkMode(app_config->get_bool("dark_color_mode"));
    if (WXHWND wxHWND = wxToolTip::GetToolTipCtrl())
        NppDarkMode::SetDarkExplorerTheme((HWND)wxHWND);
    NppDarkMode::SetDarkTitleBar(mainframe->GetHWND());
    NppDarkMode::SetDarkTitleBar(mainframe->m_settings_dialog.GetHWND());
#endif //_MSW_DARK_MODE
    m_force_colors_update = true;
}
#endif //_WIN32

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void GUI_App::update_ui_from_settings()
{
    update_label_colours();
    if(mainframe) {
#ifdef _WIN32
    // Upadte UI colors before Update UI from settings
    if (m_force_colors_update) {
        m_force_colors_update = false;
        mainframe->force_color_changed();
        mainframe->diff_dialog.force_color_changed();
        mainframe->preferences_dialog->force_color_changed();
        mainframe->printhost_queue_dlg()->force_color_changed();
#ifdef _MSW_DARK_MODE
        update_scrolls(mainframe);
        if (mainframe->get_layout() == MainFrame::ESettingsLayout::Dlg) {
            // update for tabs bar
            UpdateDarkUI(&mainframe->m_settings_dialog);
            mainframe->m_settings_dialog.Fit();
            mainframe->m_settings_dialog.Refresh();
            // update scrollbars
            update_scrolls(&mainframe->m_settings_dialog);
        }
#endif //_MSW_DARK_MODE
        }
#endif
        mainframe->update_ui_from_settings();
    }
}

void GUI_App::persist_window_geometry(wxTopLevelWindow *window, bool default_maximized)
{
    const std::string name = into_u8(window->GetName());

    window->Bind(wxEVT_CLOSE_WINDOW, [=](wxCloseEvent &event) {
        window_pos_save(window, name);
        event.Skip();
    });

    window_pos_restore(window, name, default_maximized);

    on_window_geometry(window, [=]() {
        window_pos_sanitize(window);
    });
}

void GUI_App::load_project(wxWindow *parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (3MF/AMF):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_PROJECT), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::import_model(wxWindow *parent, wxArrayString& input_files) const
{
    input_files.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one or more files (STL/3MF/STEP/OBJ/AMF/SVG):"),
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        dialog.GetPaths(input_files);
}

void GUI_App::import_zip(wxWindow* parent, wxString& input_file) const
{
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose ZIP file") + ":",
        from_u8(app_config->get_last_dir()), "",
        file_wildcards(FT_ZIP), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

void GUI_App::load_gcode(wxWindow* parent, wxString& input_file) const
{
    input_file.Clear();
    wxFileDialog dialog(parent ? parent : GetTopWindow(),
        _L("Choose one file (GCODE/GCO/G/BGCODE/BGC/NGC):"),
        app_config->get_last_dir(), "",
        file_wildcards(FT_GCODE), wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
}

bool GUI_App::switch_language()
{
    if (select_language()) {
        recreate_GUI(_L("Changing of an application language") + dots);
        return true;
    } else {
        return false;
    }
}

#ifdef __linux__
static const wxLanguageInfo* linux_get_existing_locale_language(const wxLanguageInfo* language,
                                                                const wxLanguageInfo* system_language)
{
    constexpr size_t max_len = 50;
    char path[max_len] = "";
    std::vector<std::string> locales;
    const std::string lang_prefix = into_u8(language->CanonicalName.BeforeFirst('_'));

    // Call locale -a so we can parse the output to get the list of available locales
    // We expect lines such as "en_US.utf8". Pick ones starting with the language code
    // we are switching to. Lines with different formatting will be removed later.
    FILE* fp = popen("locale -a", "r");
    if (fp != NULL) {
        while (fgets(path, max_len, fp) != NULL) {
            std::string line(path);
            line = line.substr(0, line.find('\n'));
            if (boost::starts_with(line, lang_prefix))
                locales.push_back(line);
        }
        pclose(fp);
    }

    // locales now contain all candidates for this language.
    // Sort them so ones containing anything about UTF-8 are at the end.
    std::sort(locales.begin(), locales.end(), [](const std::string& a, const std::string& b)
    {
        auto has_utf8 = [](const std::string & s) {
            auto S = boost::to_upper_copy(s);
            return S.find("UTF8") != std::string::npos || S.find("UTF-8") != std::string::npos;
        };
        return ! has_utf8(a) && has_utf8(b);
    });

    // Remove the suffix behind a dot, if there is one.
    for (std::string& s : locales)
        s = s.substr(0, s.find("."));

    // We just hope that dear Linux "locale -a" returns country codes
    // in ISO 3166-1 alpha-2 code (two letter) format.
    // https://en.wikipedia.org/wiki/List_of_ISO_3166_country_codes
    // To be sure, remove anything not looking as expected
    // (any number of lowercase letters, underscore, two uppercase letters).
    locales.erase(std::remove_if(locales.begin(),
                                 locales.end(),
                                 [](const std::string& s) {
                                     return ! std::regex_match(s,
                                         std::regex("^[a-z]+_[A-Z]{2}$"));
                                 }),
                   locales.end());

    if (system_language) {
        // Is there a candidate matching a country code of a system language? Move it to the end,
        // while maintaining the order of matches, so that the best match ends up at the very end.
        std::string system_country = "_" + into_u8(system_language->CanonicalName.AfterFirst('_')).substr(0, 2);
        int cnt = locales.size();
        for (int i = 0; i < cnt; ++i)
            if (locales[i].find(system_country) != std::string::npos) {
                locales.emplace_back(std::move(locales[i]));
                locales[i].clear();
            }
    }

    // Now try them one by one.
    for (auto it = locales.rbegin(); it != locales.rend(); ++ it)
        if (! it->empty()) {
            const std::string &locale = *it;
            const wxLanguageInfo* lang = wxLocale::FindLanguageInfo(from_u8(locale));
            if (wxLocale::IsAvailable(lang->Language))
                return lang;
        }
    return language;
}
#endif

int GUI_App::GetSingleChoiceIndex(const wxString& message,
                                const wxString& caption,
                                const wxArrayString& choices,
                                int initialSelection)
{
#ifdef _WIN32
    wxSingleChoiceDialog dialog(nullptr, message, caption, choices);
    wxGetApp().UpdateDlgDarkUI(&dialog);
    auto children = dialog.GetChildren();
    for (auto child : children)
        child->SetFont(normal_font());

    dialog.SetSelection(initialSelection);
    return dialog.ShowModal() == wxID_OK ? dialog.GetSelection() : -1;
#else
    return wxGetSingleChoiceIndex(message, caption, choices, initialSelection);
#endif
}

// select language from the list of installed languages
bool GUI_App::select_language()
{
	wxArrayString translations = wxTranslations::Get()->GetAvailableTranslations(SLIC3R_APP_KEY);
    std::vector<const wxLanguageInfo*> language_infos;
    language_infos.emplace_back(wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH));
    for (size_t i = 0; i < translations.GetCount(); ++ i) {
	    const wxLanguageInfo *langinfo = wxLocale::FindLanguageInfo(translations[i]);
        if (langinfo != nullptr)
            language_infos.emplace_back(langinfo);
    }
    sort_remove_duplicates(language_infos);
	std::sort(language_infos.begin(), language_infos.end(), [](const wxLanguageInfo* l, const wxLanguageInfo* r) { return l->Description < r->Description; });

    wxArrayString names;
    names.Alloc(language_infos.size());

    // Some valid language should be selected since the application start up.
    const wxLanguage current_language = wxLanguage(m_wxLocale->GetLanguage());
    int 		     init_selection   		= -1;
    int 			 init_selection_alt     = -1;
    int 			 init_selection_default = -1;
    for (size_t i = 0; i < language_infos.size(); ++ i) {
        if (wxLanguage(language_infos[i]->Language) == current_language)
        	// The dictionary matches the active language and country.
            init_selection = i;
        else if ((language_infos[i]->CanonicalName.BeforeFirst('_') == m_wxLocale->GetCanonicalName().BeforeFirst('_')) ||
        		 // if the active language is Slovak, mark the Czech language as active.
        	     (language_infos[i]->CanonicalName.BeforeFirst('_') == "cs" && m_wxLocale->GetCanonicalName().BeforeFirst('_') == "sk"))
        	// The dictionary matches the active language, it does not necessarily match the country.
        	init_selection_alt = i;
        if (language_infos[i]->CanonicalName.BeforeFirst('_') == "en")
        	// This will be the default selection if the active language does not match any dictionary.
        	init_selection_default = i;
        names.Add(language_infos[i]->Description);
    }
    if (init_selection == -1)
    	// This is the dictionary matching the active language.
    	init_selection = init_selection_alt;
    if (init_selection != -1)
    	// This is the language to highlight in the choice dialog initially.
    	init_selection_default = init_selection;

    const long index = GetSingleChoiceIndex(_L("Select the language"), _L("Language"), names, init_selection_default);
	// Try to load a new language.
    if (index != -1 && (init_selection == -1 || init_selection != index)) {
    	const wxLanguageInfo *new_language_info = language_infos[index];
    	if (this->load_language(new_language_info->CanonicalName, false)) {
			// Save language at application config.
            // Which language to save as the selected dictionary language?
            // 1) Hopefully the language set to wxTranslations by this->load_language(), but that API is weird and we don't want to rely on its
            //    stability in the future:
            //    wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
            // 2) Current locale language may not match the dictionary name, see GH issue #3901
            //    m_wxLocale->GetCanonicalName()
            // 3) new_language_info->CanonicalName is a safe bet. It points to a valid dictionary name.
			app_config->set("translation_language", new_language_info->CanonicalName.ToUTF8().data());
    		return true;
    	}
    }

    return false;
}

PrinterTechnology GUI_App::get_current_printer_technology() const {
    if (preset_bundle) {
        return preset_bundle->printers.get_edited_preset().printer_technology();
    }
    // still initialisating
    return ptUnknown;
}

// Load gettext translation files and activate them at the start of the application,
// based on the "translation_language" key stored in the application config.
bool GUI_App::load_language(wxString language, bool initial)
{
    if (initial) {
    	// There is a static list of lookup path prefixes in wxWidgets. Add ours.
	    wxFileTranslationsLoader::AddCatalogLookupPathPrefix(from_u8(localization_dir()));
    	// Get the active language from PrusaSlicer.ini, or empty string if the key does not exist.
        language = app_config->get("translation_language");
        if (! language.empty())
            BOOST_LOG_TRIVIAL(trace) << boost::format("translation_language provided by " SLIC3R_APP_NAME ".ini: %1%") % language;

        // Get the system language.
        {
	        const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
	        if (lang_system != wxLANGUAGE_UNKNOWN) {
				m_language_info_system = wxLocale::GetLanguageInfo(lang_system);
	        	BOOST_LOG_TRIVIAL(trace) << boost::format("System language detected (user locales and such): %1%") % m_language_info_system->CanonicalName.ToUTF8().data();
	        }
		}
        {
	    	// Allocating a temporary locale will switch the default wxTranslations to its internal wxTranslations instance.
	    	wxLocale temp_locale;
#ifdef __WXOSX__
            // ysFIXME - temporary workaround till it isn't fixed in wxWidgets:
            // Use English as an initial language, because of under OSX it try to load "inappropriate" language for wxLANGUAGE_DEFAULT.
            // For example in our case it's trying to load "en_CZ" and as a result PrusaSlicer catch warning message.
            // But wxWidgets guys work on it.
            temp_locale.Init(wxLANGUAGE_ENGLISH);
#else
            temp_locale.Init();
#endif // __WXOSX__
	    	// Set the current translation's language to default, otherwise GetBestTranslation() may not work (see the wxWidgets source code).
	    	wxTranslations::Get()->SetLanguage(wxLANGUAGE_DEFAULT);
	    	// Let the wxFileTranslationsLoader enumerate all translation dictionaries for PrusaSlicer
	    	// and try to match them with the system specific "preferred languages".
	    	// There seems to be a support for that on Windows and OSX, while on Linuxes the code just returns wxLocale::GetSystemLanguage().
	    	// The last parameter gets added to the list of detected dictionaries. This is a workaround
	    	// for not having the English dictionary. Let's hope wxWidgets of various versions process this call the same way.
			wxString best_language = wxTranslations::Get()->GetBestTranslation(SLIC3R_APP_KEY, wxLANGUAGE_ENGLISH);
			if (! best_language.IsEmpty()) {
				m_language_info_best = wxLocale::FindLanguageInfo(best_language);
	        	BOOST_LOG_TRIVIAL(trace) << boost::format("Best translation language detected (may be different from user locales): %1%") % m_language_info_best->CanonicalName.ToUTF8().data();
			}
            #ifdef __linux__
            wxString lc_all;
            if (wxGetEnv("LC_ALL", &lc_all) && ! lc_all.IsEmpty()) {
                // Best language returned by wxWidgets on Linux apparently does not respect LC_ALL.
                // Disregard the "best" suggestion in case LC_ALL is provided.
                m_language_info_best = nullptr;
            }
            #endif
		}
    }

	const wxLanguageInfo *language_info = language.empty() ? nullptr : wxLocale::FindLanguageInfo(language);
	if (! language.empty() && (language_info == nullptr || language_info->CanonicalName.empty())) {
		// Fix for wxWidgets issue, where the FindLanguageInfo() returns locales with undefined ANSII code (wxLANGUAGE_KONKANI or wxLANGUAGE_MANIPURI).
		language_info = nullptr;
    	BOOST_LOG_TRIVIAL(error) << boost::format("Language code \"%1%\" is not supported") % language.ToUTF8().data();
	}

    if (language_info != nullptr && language_info->LayoutDirection == wxLayout_RightToLeft) {
        BOOST_LOG_TRIVIAL(trace) << boost::format("The following language code requires right to left layout, which is not supported by %1%: %2%") % SLIC3R_APP_NAME % language_info->CanonicalName.ToUTF8().data();
        language_info = nullptr;
    }

    if (language_info == nullptr) {
        // PrusaSlicer does not support the Right to Left languages yet.
        if (m_language_info_system != nullptr && m_language_info_system->LayoutDirection != wxLayout_RightToLeft)
            language_info = m_language_info_system;
        if (m_language_info_best != nullptr && m_language_info_best->LayoutDirection != wxLayout_RightToLeft)
        	language_info = m_language_info_best;
	    if (language_info == nullptr)
			language_info = wxLocale::GetLanguageInfo(wxLANGUAGE_ENGLISH_US);
    }

	BOOST_LOG_TRIVIAL(trace) << boost::format("Switching wxLocales to %1%") % language_info->CanonicalName.ToUTF8().data();

    // Alternate language code.
    wxLanguage language_dict = wxLanguage(language_info->Language);
    if (language_info->CanonicalName.BeforeFirst('_') == "sk") {
    	// Slovaks understand Czech well. Give them the Czech translation.
    	language_dict = wxLANGUAGE_CZECH;
		BOOST_LOG_TRIVIAL(trace) << "Using Czech dictionaries for Slovak language";
    }

    // Select language for locales. This language may be different from the language of the dictionary.
    if (language_info == m_language_info_best || language_info == m_language_info_system) {
        // The current language matches user's default profile exactly. That's great.
    } else if (m_language_info_best != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_best->CanonicalName.BeforeFirst('_')) {
        // Use whatever the operating system recommends, if it the language code of the dictionary matches the recommended language.
        // This allows a Swiss guy to use a German dictionary without forcing him to German locales.
        language_info = m_language_info_best;
    } else if (m_language_info_system != nullptr && language_info->CanonicalName.BeforeFirst('_') == m_language_info_system->CanonicalName.BeforeFirst('_'))
        language_info = m_language_info_system;

#ifdef __linux__
    // If we can't find this locale , try to use different one for the language
    // instead of just reporting that it is impossible to switch.
    if (! wxLocale::IsAvailable(language_info->Language)) {
        std::string original_lang = into_u8(language_info->CanonicalName);
        language_info = linux_get_existing_locale_language(language_info, m_language_info_system);
        BOOST_LOG_TRIVIAL(trace) << boost::format("Can't switch language to %1% (missing locales). Using %2% instead.")
                                    % original_lang % language_info->CanonicalName.ToUTF8().data();
    }
#endif

    if (! wxLocale::IsAvailable(language_info->Language)) {
        // Loading the language dictionary failed.
        wxString message = "Switching " SLIC3R_APP_NAME " to language " + language_info->CanonicalName + " failed.";
#if !defined(_WIN32) && !defined(__APPLE__)
        // likely some linux system
        message += "\nYou may need to reconfigure the missing locales, likely by running the \"locale-gen\" and "
                   "\"dpkg-reconfigure locales\" commands.\n";
#endif
        if (initial) {
            message + "\n\nApplication will close.";
        }
        wxMessageBox(message, SLIC3R_APP_NAME " - Switching language failed", wxOK | wxICON_ERROR);
        if (initial) {
            std::exit(EXIT_FAILURE);
        } else {
            return false;
        }
    }

    // Release the old locales, create new locales.
    //FIXME wxWidgets cause havoc if the current locale is deleted. We just forget it causing memory leaks for now.
    m_wxLocale.release();
    m_wxLocale = Slic3r::make_unique<wxLocale>();
    m_wxLocale->Init(language_info->Language);
    // Override language at the active wxTranslations class (which is stored in the active m_wxLocale)
    // to load possibly different dictionary, for example, load Czech dictionary for Slovak language.
    wxTranslations::Get()->SetLanguage(language_dict);
    m_wxLocale->AddCatalog(SLIC3R_APP_KEY);
    m_imgui->set_language(into_u8(language_info->CanonicalName));
    //FIXME This is a temporary workaround, the correct solution is to switch to "C" locale during file import / export only.
    //wxSetlocale(LC_NUMERIC, "C");
    Preset::update_suffix_modified(format(" (%1%)", _L("modified")));
	return true;
}

Tab* GUI_App::get_tab(Preset::Type type, bool only_completed)
{
    for (Tab* tab: tabs_list)
        if (tab->type() == type)
            return tab->completed() || !only_completed ? tab : nullptr; // To avoid actions with no-completed Tab
    return nullptr;
}

ConfigOptionMode GUI_App::get_mode()
{
    if (!app_config->has("view_mode"))
        return comSimple;

    ConfigOptionMode mode = comNone;
    const std::string modes = app_config->get("view_mode");
    std::vector<std::string> tags;
    boost::algorithm::split(tags, modes, boost::is_any_of("|"));
    for (const auto& item : ConfigOptionDef::names_2_tag_mode) {
        for (std::string& tag : tags) {
            if (boost::iequals(item.first, tag)) {
                mode |= item.second;
                continue;
            }
        }
    }
    return mode;
}

bool GUI_App::save_mode(const ConfigOptionMode mode)
{

    auto can_switch_to_simple = [](Model& model) {
        for (const ModelObject* model_object : model.objects)
            if (model_object->volumes.size() > 1) {
                for (size_t i = 1; i < model_object->volumes.size(); ++i)
                    if (!model_object->volumes[i]->is_support_modifier())
                        return false;
            }
        return true;
    };

    if ( !get_app_config()->get_bool("objects_always_expert")
        && (((mode & comAdvanced) == comAdvanced) || ((mode & comExpert) == comExpert))
        && !can_switch_to_simple(model())) {
        show_info(nullptr,
            _L("Simple mode supports manipulation with single-part object(s)\n"
            "or object(s) with support modifiers only.") + "\n\n" +
            _L("Please check your object list before mode changing."),
            _L("Change application mode"));
        return false;
    }
    std::string str_serialized;
    for (const auto& item : ConfigOptionDef::names_2_tag_mode) {
        if ((item.second & mode) != 0) {
            str_serialized += str_serialized.empty() ? item.first : ("|" + item.first);
        }
    }
    app_config->set("view_mode", str_serialized);
    update_mode();
    return true;
}

// Update view mode according to selected menu
void GUI_App::update_mode()
{
    sidebar().update_mode();

#ifdef _USE_CUSTOM_NOTEBOOK
    if (!wxGetApp().tabs_as_menu())
        dynamic_cast<Notebook*>(mainframe->m_tabpanel)->UpdateMode();
#endif

    for (auto tab : tabs_list)
        tab->update_mode();

    plater()->update_menus();
    plater()->canvas3D()->update_gizmos_on_off_state();
}

void GUI_App::add_config_menu(wxMenuBar *menu)
{
    auto local_menu = new wxMenu();
    wxWindowID config_id_base = wxWindow::NewControlId(int(ConfigMenuCnt + Slic3r::GUI::get_app_config()->tags().size()*2));

    const wxString config_wizard_name = _(ConfigWizard::name(true));
    const wxString config_wizard_tooltip = from_u8((boost::format(_u8L("Run %s")) % config_wizard_name).str());
    // Cmd+, is standard on OS X - what about other operating systems?
    if (is_editor()) {
        local_menu->Append(config_id_base + ConfigMenuWizard, config_wizard_name + dots, config_wizard_tooltip);
        local_menu->Append(config_id_base + ConfigMenuSnapshots, _L("&Configuration Snapshots") + dots, _L("Inspect / activate configuration snapshots"));
        local_menu->Append(config_id_base + ConfigMenuTakeSnapshot, _L("Take Configuration &Snapshot"), _L("Capture a configuration snapshot"));
        local_menu->Append(config_id_base + ConfigMenuUpdateConf, _L("Check for Configuration Updates"), _L("Check for configuration updates"));
        local_menu->Append(config_id_base + ConfigMenuUpdateApp, _L("Check for Application Updates"), _L("Check for new version of application"));
#if defined(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
        //if (DesktopIntegrationDialog::integration_possible())
        local_menu->Append(config_id_base + ConfigMenuDesktopIntegration, _L("Desktop Integration"), _L("Desktop Integration"));
#endif //(__linux__) && defined(SLIC3R_DESKTOP_INTEGRATION)
        local_menu->AppendSeparator();
    }
    local_menu->Append(config_id_base + ConfigMenuPreferences, _L("&Preferences") + dots +
#ifdef __APPLE__
        "\tCtrl+,",
#else
        "\tCtrl+P",
#endif
        _L("Application preferences"));
    wxMenu* mode_menu = nullptr;
    wxMenu* tag_menu = nullptr;
    if (is_editor()) {
        local_menu->AppendSeparator();
        mode_menu = new wxMenu();
        int config_menu_idx = 0;
        for (const AppConfig::Tag& tag : Slic3r::GUI::get_app_config()->tags()) {
            mode_menu->AppendCheckItem(config_id_base + ConfigMenuCnt + config_menu_idx, _(tag.name), _(tag.description));
            Bind(wxEVT_UPDATE_UI, [this, tag](wxUpdateUIEvent& evt) { evt.Check((get_mode() & tag.tag) == tag.tag); }, config_id_base + ConfigMenuCnt + config_menu_idx);
            config_menu_idx++;
        }

        local_menu->AppendSubMenu(mode_menu, _L("Mode"), wxString::Format(_L("%s View Mode"), SLIC3R_APP_NAME));

        tag_menu = new wxMenu();
        for (const AppConfig::Tag& tag : Slic3r::GUI::get_app_config()->tags()) {
            tag_menu->AppendCheckItem(config_id_base + ConfigMenuCnt + config_menu_idx, _(tag.name), _(tag.description));
            Bind(wxEVT_UPDATE_UI, [this, tag](wxUpdateUIEvent& evt) { evt.Check((get_mode() & tag.tag) == tag.tag); }, config_id_base + ConfigMenuCnt + config_menu_idx);
            config_menu_idx++;
        }

        local_menu->AppendSubMenu(tag_menu, _L("Tags"), wxString::Format(_L("%s View Mode"), SLIC3R_APP_NAME));
    }
    local_menu->AppendSeparator();
    local_menu->Append(config_id_base + ConfigMenuLanguage, _L("&Language"));
    if (is_editor()) {
        local_menu->AppendSeparator();
        local_menu->Append(config_id_base + ConfigMenuFlashFirmware, _L("Flash Printer &Firmware"), _L("Upload a firmware image into an Arduino based printer"));
        // TODO: for when we're able to flash dictionaries
        // local_menu->Append(config_id_base + FirmwareMenuDict,  _L("Flash Language File"),    _L("Upload a language dictionary file into a Prusa printer"));
    }
    local_menu->Append(config_id_base + ConfigMenuWifiConfigFile, _L("Prusa Wi-Fi Configuration File"), _L("Generate a file to be loaded by a Prusa printer to configure its Wi-Fi connection."));

    local_menu->Bind(wxEVT_MENU, [this, config_id_base](wxEvent &event) {
        switch (event.GetId() - config_id_base) {
        case ConfigMenuWizard:
            run_wizard(ConfigWizard::RR_USER);
            break;
		case ConfigMenuUpdateConf:
			check_updates(true);
			break;
        case ConfigMenuUpdateApp:
            app_version_check(true);
            break;
#ifdef __linux__
        case ConfigMenuDesktopIntegration:
            show_desktop_integration_dialog();
            break;
#endif
        case ConfigMenuTakeSnapshot:
            // Take a configuration snapshot.
            if (wxString action_name = _L("Taking a configuration snapshot");
                check_and_save_current_preset_changes(action_name, _L("Some presets are modified and the unsaved changes will not be captured by the configuration snapshot."), false, true)) {
                wxTextEntryDialog dlg(nullptr, action_name, _L("Snapshot name"));
                UpdateDlgDarkUI(&dlg);

                // set current normal font for dialog children,
                // because of just dlg.SetFont(normal_font()) has no result;
                for (auto child : dlg.GetChildren())
                    child->SetFont(normal_font());

                if (dlg.ShowModal() == wxID_OK)
                    if (const Config::Snapshot *snapshot = Config::take_config_snapshot_report_error(
                            *app_config, Config::Snapshot::SNAPSHOT_USER, dlg.GetValue().ToUTF8().data());
                        snapshot != nullptr)
                        app_config->set("on_snapshot", snapshot->id);
            }
            break;
        case ConfigMenuSnapshots:
            if (check_and_save_current_preset_changes(_L("Loading a configuration snapshot"), "", false)) {
                std::string on_snapshot;
                if (Config::SnapshotDB::singleton().is_on_snapshot(*app_config))
                    on_snapshot = app_config->get("on_snapshot");
                ConfigSnapshotDialog dlg(Slic3r::GUI::Config::SnapshotDB::singleton(), on_snapshot);
                dlg.ShowModal();
                if (!dlg.snapshot_to_activate().empty()) {
                    if (! Config::SnapshotDB::singleton().is_on_snapshot(*app_config) &&
                        ! Config::take_config_snapshot_cancel_on_error(*app_config, Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK, "",
                                GUI::format(_L("Continue to activate a configuration snapshot %1%?"), dlg.snapshot_to_activate())))
                        break;
                    try {
                        app_config->set("on_snapshot", Config::SnapshotDB::singleton().restore_snapshot(dlg.snapshot_to_activate(), *app_config).id);
                        // Enable substitutions, log both user and system substitutions. There should not be any substitutions performed when loading system
                        // presets because compatibility of profiles shall be verified using the min_slic3r_version keys in config index, but users
                        // are known to be creative and mess with the config files in various ways.
                        if (PresetsConfigSubstitutions all_substitutions = preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::Enable);
                            ! all_substitutions.empty())
                            show_substitutions_info(all_substitutions);

                        // Load the currently selected preset into the GUI, update the preset selection box.
                        load_current_presets();
                    } catch (std::exception &ex) {
                        GUI::show_error(nullptr, _L("Failed to activate configuration snapshot.") + "\n" + into_u8(ex.what()));
                    }
                }
            }
            break;
        case ConfigMenuPreferences:
        {
            open_preferences();
            break;
        }
        case ConfigMenuLanguage:
        {
            /* Before change application language, let's check unsaved changes on 3D-Scene
             * and draw user's attention to the application restarting after a language change
             */
            {
                // the dialog needs to be destroyed before the call to switch_language()
                // or sometimes the application crashes into wxDialogBase() destructor
                // so we put it into an inner scope
                wxString title = is_editor() ? wxString(SLIC3R_APP_NAME) : wxString(GCODEVIEWER_APP_NAME);
                title += " - " + _L("Language selection");
                //wxMessageDialog dialog(nullptr,
                MessageDialog dialog(nullptr,
                    _L("Switching the language will trigger application restart.\n"
                        "You will lose content of the platter.") + "\n\n" +
                    _L("Do you want to proceed?"),
                    title,
                    wxICON_QUESTION | wxOK | wxCANCEL);
                if (dialog.ShowModal() == wxID_CANCEL)
                    return;
            }

            switch_language();
            break;
        }
        case ConfigMenuFlashFirmware:
            FirmwareDialog::run(mainframe);
            break;
        case ConfigMenuWifiConfigFile:
        {
            open_wifi_config_dialog(true);
            /*
            std::string file_path;
            WifiConfigDialog dialog(mainframe, file_path, removable_drive_manager());
            if (dialog.ShowModal() == wxID_OK)
            {
                plater_->get_notification_manager()->push_exporting_finished_notification(file_path, boost::filesystem::path(file_path).parent_path().string(), true);
            }
            */
        }
        break;
        default:
            break;
        }
    });

    using std::placeholders::_1;

    if (mode_menu != nullptr) {
        auto modefn = [this](ConfigOptionMode mode, wxCommandEvent&) { if (get_mode() != mode) save_mode(mode); };
        int config_menu_idx = 0;
        for (const AppConfig::Tag& tag : Slic3r::GUI::get_app_config()->tags()) {
            mode_menu->Bind(wxEVT_MENU, std::bind(modefn, tag.tag, _1), config_id_base + ConfigMenuCnt + config_menu_idx);
            config_menu_idx++;
        }
        if (tag_menu != nullptr) {
            auto tagfn = [this](ConfigOptionMode mode, wxCommandEvent&) { if (get_mode() != mode) save_mode(get_mode() ^ mode); };
            for (const AppConfig::Tag& tag : Slic3r::GUI::get_app_config()->tags()) {
                tag_menu->Bind(wxEVT_MENU, std::bind(tagfn, tag.tag, _1), config_id_base + ConfigMenuCnt + config_menu_idx);
                config_menu_idx++;
            }
        }
        //mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comSimple, _1), config_id_base + ConfigMenuModeSimple);
        //mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comAdvanced, _1), config_id_base + ConfigMenuModeAdvanced);
        //mode_menu->Bind(wxEVT_MENU, std::bind(modfn, comExpert, _1), config_id_base + ConfigMenuModeExpert);
    }

    menu->Append(local_menu, _L("&Configuration"));
}

void GUI_App::open_preferences(const std::string& highlight_option /*= std::string()*/, const std::string& group_name/*= std::string()*/)
{
    mainframe->preferences_dialog->show(highlight_option, group_name);

    if (mainframe->preferences_dialog->recreate_GUI())
        recreate_GUI(_L("Restart application") + dots);

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    if (dlg.seq_top_layer_only_changed() || dlg.seq_seq_top_gcode_indices_changed())
#else
    if (mainframe->preferences_dialog->seq_top_layer_only_changed())
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        this->plater_->refresh_print();

#ifdef _WIN32
    if (is_editor()) {
        if (app_config->get_bool("associate_3mf"))
            associate_3mf_files();
        if (app_config->get_bool("associate_stl"))
            associate_stl_files();
    }
    else {
        if (app_config->get_bool("associate_gcode"))
            associate_gcode_files();
        if (app_config->get_bool("associate_bgcode"))
            associate_bgcode_files();
    }
#endif // _WIN32

    if (mainframe->preferences_dialog->settings_layout_changed()) {
        // hide full main_sizer for mainFrame
        mainframe->GetSizer()->Show(false);
        mainframe->update_layout();
        mainframe->select_tab(MainFrame::ETabType::LastPlater);
    }
}

bool GUI_App::has_unsaved_preset_changes() const
{
    PrinterTechnology printer_technology = get_current_printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->completed() && tab->saved_preset_is_dirty())
            return true;
    }
    return false;
}

bool GUI_App::has_current_preset_changes() const
{
    PrinterTechnology printer_technology = get_current_printer_technology();
    for (const Tab* const tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->completed() && tab->current_preset_is_dirty())
            return true;
    }
    return false;
}

void GUI_App::update_saved_preset_from_current_preset()
{
    PrinterTechnology printer_technology = get_current_printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology) && tab->completed())
            tab->update_saved_preset_from_current_preset();
    }
}

std::vector<const PresetCollection*> GUI_App::get_active_preset_collections() const
{
    std::vector<const PresetCollection*> ret;
    PrinterTechnology printer_technology = get_current_printer_technology();
    for (const Tab* tab : tabs_list)
        if (tab->supports_printer_technology(printer_technology) && tab->completed()) {
            assert(tab->get_presets());
            ret.push_back(tab->get_presets());
        }
    return ret;
}

// To notify the user whether he is aware that some preset changes will be lost,
// UnsavedChangesDialog: "Discard / Save / Cancel"
// This is called when:
// - Close Application & Current project isn't saved
// - Load Project      & Current project isn't saved
// - Undo / Redo with change of print technologie
// - Loading snapshot
// - Loading config_file/bundle
// UnsavedChangesDialog: "Don't save / Save / Cancel"
// This is called when:
// - Exporting config_bundle
// - Taking snapshot
// ================================== superslicer behavior:
// an empty platter equals to an already saved project.
// if empty platter and unsaved perest changes, ask if want to savepreset or create a project
// if modified project but no preset change (from the last save project), then ask if project need to be saved
// if modified project and preset change (from the last save project), then ask if want to save preset, project.
bool GUI_App::check_and_save_current_preset_changes(const wxString& caption, const wxString& header, bool remember_choice/* = true*/, bool dont_save_insted_of_discard/* = false*/)
{
    if (has_current_preset_changes()) {
        const std::string app_config_key = remember_choice ? "default_action_on_close_application" : "";
        int act_buttons = ActionButtons::SAVE;
        if (dont_save_insted_of_discard)
            act_buttons |= ActionButtons::DONT_SAVE;
        UnsavedChangesDialog dlg(caption, header, app_config_key, act_buttons);
        std::string act = app_config_key.empty() ? "none" : wxGetApp().app_config->get(app_config_key);
        if (act == "none" && dlg.ShowModal() == wxID_CANCEL)
            return false;

        if (dlg.save_preset())  // save selected changes
        {
            for (const std::pair<std::string, Preset::Type>& nt : dlg.get_names_and_types())
                preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

            load_current_presets(false);

            // if we saved changes to the new presets, we should to
            // synchronize config.ini with the current selections.
            preset_bundle->export_selections(*app_config);

            MessageDialog(nullptr, dlg.msg_success_saved_modifications(dlg.get_names_and_types().size())).ShowModal();
        }
    }

    return true;
}

void GUI_App::apply_keeped_preset_modifications()
{
    PrinterTechnology printer_technology = get_current_printer_technology();
    for (Tab* tab : tabs_list) {
        if (tab->supports_printer_technology(printer_technology))
            tab->apply_config_from_cache();
    }
    load_current_presets(false);
}

// This is called when creating new project or load another project
// OR close ConfigWizard
// to ask the user what should we do with unsaved changes for presets.
// New Project          => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Cancel"
//                      => Current project isn't saved => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Close ConfigWizard   => Current project is saved    => UnsavedChangesDialog: "Keep / Discard / Save / Cancel"
// Note: no_nullptr postponed_apply_of_keeped_changes indicates that thie function is called after ConfigWizard is closed
bool GUI_App::check_and_keep_current_preset_changes(const wxString& caption, const wxString& header, int action_buttons, bool* postponed_apply_of_keeped_changes/* = nullptr*/)
{
    if (has_current_preset_changes()) {
        bool is_called_from_configwizard = postponed_apply_of_keeped_changes != nullptr;

        const std::string app_config_key = is_called_from_configwizard ? "" : "default_action_on_new_project";
        UnsavedChangesDialog dlg(caption, header, app_config_key, action_buttons);
        std::string act = app_config_key.empty() ? "none" : wxGetApp().app_config->get(app_config_key);
        if (act == "none" && dlg.ShowModal() == wxID_CANCEL)
            return false;

        auto reset_modifications = [this, is_called_from_configwizard]() {
            if (is_called_from_configwizard)
                return; // no need to discared changes. It will be done fromConfigWizard closing

            PrinterTechnology printer_technology = get_current_printer_technology();
            for (const Tab* const tab : tabs_list) {
                if (tab->supports_printer_technology(printer_technology) && tab->completed() && tab->current_preset_is_dirty())
                    tab->m_presets->discard_current_changes();
            }
            load_current_presets(false);
        };

        if (dlg.discard())
            reset_modifications();
        else  // save selected changes
        {
            const auto& preset_names_and_types = dlg.get_names_and_types();
            if (dlg.save_preset()) {
                for (const std::pair<std::string, Preset::Type>& nt : preset_names_and_types)
                    preset_bundle->save_changes_for_preset(nt.first, nt.second, dlg.get_unselected_options(nt.second));

                // if we saved changes to the new presets, we should to
                // synchronize config.ini with the current selections.
                preset_bundle->export_selections(*app_config);

                wxString text = dlg.msg_success_saved_modifications(preset_names_and_types.size());
                if (!is_called_from_configwizard)
                    text += "\n\n" + _L("For new project all modifications will be reseted");

                MessageDialog(nullptr, text).ShowModal();
                reset_modifications();
            }
            else if (dlg.transfer_changes() && (dlg.has_unselected_options() || is_called_from_configwizard)) {
                // execute this part of code only if not all modifications are keeping to the new project
                // OR this function is called when ConfigWizard is closed and "Keep modifications" is selected
                for (const std::pair<std::string, Preset::Type>& nt : preset_names_and_types) {
                    Preset::Type type = nt.second;
                    Tab* tab = get_tab(type);
                    std::vector<std::string> selected_options = dlg.get_selected_options(type);
                    if (type == Preset::TYPE_PRINTER) {
                        auto it = std::find(selected_options.begin(), selected_options.end(), "extruders_count");
                        if (it != selected_options.end()) {
                            // erase "extruders_count" option from the list
                            selected_options.erase(it);
                            // cache the extruders count
                            static_cast<TabPrinter*>(tab)->cache_extruder_cnt();
                        }
                    }
                    tab->cache_config_diff(selected_options);
                    if (!is_called_from_configwizard)
                        tab->m_presets->discard_current_changes();
                }
                if (is_called_from_configwizard)
                    *postponed_apply_of_keeped_changes = true;
                else
                    apply_keeped_preset_modifications();
            }
        }
    }

    return true;
}

bool GUI_App::can_load_project()
{
    int saved_project = plater()->save_project_if_dirty(_L("Loading a new project while the current project is modified."));
    if (saved_project == wxID_CANCEL ||
        (plater()->is_project_dirty() && saved_project == wxID_NO &&
         !check_and_save_current_preset_changes(_L("Project is loading"), _L("Opening new project while some presets are unsaved."))))
        return false;
    return true;
}

bool GUI_App::check_print_host_queue()
{
    wxString dirty;
    std::vector<std::pair<std::string, std::string>> jobs;
    // Get ongoing jobs from dialog
    mainframe->m_printhost_queue_dlg->get_active_jobs(jobs);
    if (jobs.empty())
        return true;
    // Show dialog
    wxString job_string = wxString();
    for (const auto& job : jobs) {
        job_string += format_wxstr("   %1% : %2% \n", job.first, job.second);
    }
    wxString message;
    message += _(L("The uploads are still ongoing")) + ":\n\n" + job_string +"\n" + _(L("Stop them and continue anyway?"));
    //wxMessageDialog dialog(mainframe,
    MessageDialog dialog(mainframe,
        message,
        wxString(SLIC3R_APP_NAME) + " - " + _(L("Ongoing uploads")),
        wxICON_QUESTION | wxYES_NO | wxNO_DEFAULT);
    if (dialog.ShowModal() == wxID_YES)
        return true;

    // TODO: If already shown, bring forward
    mainframe->m_printhost_queue_dlg->Show();
    return false;
}

bool GUI_App::checked_tab(Tab* tab)
{
    bool ret = true;
    if (find(tabs_list.begin(), tabs_list.end(), tab) == tabs_list.end())
        ret = false;
    return ret;
}

// Update UI / Tabs to reflect changes in the currently loaded presets
void GUI_App::load_current_presets(bool check_printer_presets_ /*= true*/)
{
    // check printer_presets for the containing information about "Print Host upload"
    // and create physical printer from it, if any exists
    if (check_printer_presets_)
        check_printer_presets();

    PrinterTechnology printer_technology = get_current_printer_technology();
	this->plater()->set_printer_technology(printer_technology);
    for (Tab *tab : tabs_list)
		if (tab->supports_printer_technology(printer_technology) && tab->get_presets()) {
			if (tab->type() == Preset::TYPE_PRINTER) {
				static_cast<TabPrinter*>(tab)->update_pages();
				// Mark the plater to update print bed by tab->load_current_preset() from Plater::on_config_change().
				this->plater()->force_print_bed_update();
			}
            else if (tab->type() == Preset::TYPE_FFF_FILAMENT)
                // active extruder can be changed in a respect to the new loaded configurations, if some filament preset will be modified
                static_cast<TabFilament*>(tab)->invalidate_active_extruder();
			tab->load_current_preset();
		}
}

bool GUI_App::OnExceptionInMainLoop()
{
    generic_exception_handle();
    return false;
}

#ifdef __APPLE__
// This callback is called from wxEntry()->wxApp::CallOnInit()->NSApplication run
// that is, before GUI_App::OnInit(), so we have a chance to switch GUI_App
// to a G-code viewer.
void GUI_App::OSXStoreOpenFiles(const wxArrayString &fileNames)
{
    size_t num_gcodes = 0;
    for (const wxString &filename : fileNames)
        if (is_gcode_file(into_u8(filename)))
            ++ num_gcodes;
    if (fileNames.size() == num_gcodes) {
        // Opening PrusaSlicer by drag & dropping a G-Code onto PrusaSlicer icon in Finder,
        // just G-codes were passed. Switch to G-code viewer mode.
        m_app_mode = EAppMode::GCodeViewer;
        unlock_lockfile(get_instance_hash_string() + ".lock", data_dir() + "/cache/");
        if(app_config)
            app_config.reset();
        init_app_config();
    }
    wxApp::OSXStoreOpenFiles(fileNames);
}
// wxWidgets override to get an event on open files.
void GUI_App::MacOpenFiles(const wxArrayString &fileNames)
{
    std::vector<std::string> files;
    std::vector<wxString>    gcode_files;
    std::vector<wxString>    non_gcode_files;
    for (const auto& filename : fileNames) {
        if (is_gcode_file(into_u8(filename)))
            gcode_files.emplace_back(filename);
        else {
            files.emplace_back(into_u8(filename));
            non_gcode_files.emplace_back(filename);
        }
    }
    if (m_app_mode == EAppMode::GCodeViewer) {
        // Running in G-code viewer.
        // Load the first G-code into the G-code viewer.
        // Or if no G-codes, send other files to slicer.
        if (! gcode_files.empty()) {
            if (m_post_initialized)
                this->plater()->load_gcode(gcode_files.front());
            else
                this->init_params->input_files = { into_u8(gcode_files.front()) };
        }
        if (!non_gcode_files.empty())
            start_new_slicer(non_gcode_files, true);
    } else {
        if (! files.empty()) {
            if (m_post_initialized) {
                wxArrayString input_files;
                for (size_t i = 0; i < non_gcode_files.size(); ++i)
                    input_files.push_back(non_gcode_files[i]);
                this->plater()->load_files(input_files);
            } else {
                for (const auto &f : non_gcode_files)
                    this->init_params->input_files.emplace_back(into_u8(f));
            }
        }
        for (const wxString &filename : gcode_files)
            start_new_gcodeviewer(&filename);
    }
}

void GUI_App::MacOpenURL(const wxString& url)
{
    if (app_config && !app_config->get_bool("downloader_url_registered"))
    {
        notification_manager()->push_notification(NotificationType::URLNotRegistered);
        BOOST_LOG_TRIVIAL(error) << "Recieved command to open URL, but it is not allowed in app configuration. URL: " << url;
        return;
    }
    start_download(boost::nowide::narrow(url));
}

#endif /* __APPLE */

Sidebar& GUI_App::sidebar()
{
    return plater_->sidebar();
}

ObjectManipulation* GUI_App::obj_manipul()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return (plater_ != nullptr) ? sidebar().obj_manipul() : nullptr;
}

ObjectSettings* GUI_App::obj_settings()
{
    return sidebar().obj_settings();
}

ObjectList* GUI_App::obj_list()
{
    // If this method is called before plater_ has been initialized, return nullptr (to avoid a crash)
    return plater_ ? sidebar().obj_list() : nullptr;
}

ObjectLayers* GUI_App::obj_layers()
{
    return sidebar().obj_layers();
}

Plater* GUI_App::plater()
{
    return plater_;
}

const Plater* GUI_App::plater() const
{
    return plater_;
}

Model& GUI_App::model()
{
    return plater_->model();
}
wxBookCtrlBase* GUI_App::tab_panel() const
{
    return mainframe->m_tabpanel;
}

NotificationManager* GUI_App::notification_manager()
{
    return plater_->get_notification_manager();
}

GalleryDialog* GUI_App::gallery_dialog()
{
    return mainframe->gallery_dialog();
}

Downloader* GUI_App::downloader()
{
    return m_downloader.get();
}

// extruders count from selected printer preset
int GUI_App::extruders_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_selected_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->size();
}

// extruders count from edited printer preset
int GUI_App::extruders_edited_cnt() const
{
    const Preset& preset = preset_bundle->printers.get_edited_preset();
    return preset.printer_technology() == ptSLA ? 1 :
           preset.config.option<ConfigOptionFloats>("nozzle_diameter")->size();
}

wxString GUI_App::current_language_code_safe() const
{
	// Translate the language code to a code, for which Prusa Research maintains translations.
	const std::map<wxString, wxString> mapping {
		{ "cs", 	"cs_CZ", },
		{ "sk", 	"cs_CZ", },
		{ "de", 	"de_DE", },
		{ "es", 	"es_ES", },
		{ "fr", 	"fr_FR", },
		{ "it", 	"it_IT", },
		{ "ja", 	"ja_JP", },
		{ "ko", 	"ko_KR", },
		{ "pl", 	"pl_PL", },
		//{ "uk", 	"uk_UA", },
		//{ "zh", 	"zh_CN", },
		//{ "ru", 	"ru_RU", },
	};
	wxString language_code = this->current_language_code().BeforeFirst('_');
	auto it = mapping.find(language_code);
	if (it != mapping.end())
		language_code = it->second;
	else
		language_code = "en_US";
	return language_code;
}

void GUI_App::open_web_page_localized(const std::string &http_address)
{
    wxString lng_param = wxString("lng=") + this->current_language_code_safe();

    // Check if http_address already contains a query parameter
    size_t query_pos = http_address.find('?');
    open_browser_with_warning_dialog(wxString(http_address) + wxString(query_pos == std::string::npos ? "?" : "&") + lng_param,
                                     nullptr, false);
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool GUI_App::may_switch_to_SLA_preset(const wxString& caption)
{
    if (model_has_parameter_modifiers_in_objects(model())) {
        show_info(nullptr,
            _L("It's impossible to print object(s) which contains parameter modifiers with SLA technology.") + "\n\n" +
            _L("Please check your object list before preset changing."),
            caption);
        return false;
    }
    return true;
}

bool GUI_App::run_wizard(ConfigWizard::RunReason reason, ConfigWizard::StartPage start_page)
{
    wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");

    if (reason == ConfigWizard::RR_USER) {
        // Cancel sync before starting wizard to prevent two downloads at same time
        preset_updater->cancel_sync();
        preset_updater->update_index_db();
        if (preset_updater->config_update(app_config->orig_version(), PresetUpdater::UpdateParams::FORCED_BEFORE_WIZARD) == PresetUpdater::R_ALL_CANCELED)
            return false;
    }


    ConfigWizard *wizard = nullptr;
    {
        wxBusyCursor wait;
        wizard = new ConfigWizard(mainframe);
    }
    const bool res = wizard->run(reason, start_page);

    // !!! Deallocate memory after close ConfigWizard.
    // Note, that mainframe is a parent of ConfigWizard.
    // So, wizard will be destroyed only during destroying of mainframe
    // To avoid this state the wizard have to be disconnected from mainframe and Destroyed explicitly
    assert(wizard);
    mainframe->RemoveChild(wizard);
    wizard->Destroy();

    if (res) {
        load_current_presets();

        // #ysFIXME - delete after testing: This part of code looks redundant. All checks are inside ConfigWizard::priv::apply_config()
        if (get_current_printer_technology() == ptSLA)
            may_switch_to_SLA_preset(_L("Configuration is editing from ConfigWizard"));
    }

    return res;
}

void GUI_App::show_desktop_integration_dialog()
{
#ifdef __linux__
    //wxCHECK_MSG(mainframe != nullptr, false, "Internal error: Main frame not created / null");
    DesktopIntegrationDialog dialog(mainframe);
    dialog.ShowModal();
#endif //__linux__
}

void GUI_App::show_downloader_registration_dialog()
{
    InfoDialog msg(nullptr
        , format_wxstr(_L("Welcome to %1% version %2%."), SLIC3R_APP_NAME, SLIC3R_VERSION)
        , format_wxstr(_L(
            "Do you wish to register downloads from <b>Printables.com</b>"
            "\nfor this <b>%1% %2%</b> executable?"
            "\n\nDownloads can be registered for only 1 executable at time."
            ), SLIC3R_APP_NAME, SLIC3R_VERSION)
        , true, wxYES_NO);
    if (msg.ShowModal() == wxID_YES) {
        auto downloader_worker = new DownloaderUtils::Worker(nullptr);
        downloader_worker->perform_register(app_config->get("url_downloader_dest"));
#ifdef __linux__
        if (downloader_worker->get_perform_registration_linux())
            DesktopIntegrationDialog::perform_downloader_desktop_integration();
#endif // __linux__
    } else {
        app_config->set("downloader_url_registered", "0");
    }
}


#if ENABLE_THUMBNAIL_GENERATOR_DEBUG
void GUI_App::gcode_thumbnails_debug()
{
    const std::string BEGIN_MASK = "; thumbnail begin";
    const std::string END_MASK = "; thumbnail end";
    std::string gcode_line;
    bool reading_image = false;
    unsigned int width = 0;
    unsigned int height = 0;

    wxFileDialog dialog(GetTopWindow(), _L("Select a gcode file:"), "", "", "G-code files (*.gcode)|*.gcode;*.GCODE;", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    std::string in_filename = into_u8(dialog.GetPath());
    std::string out_path = boost::filesystem::path(in_filename).remove_filename().append(L"thumbnail").string();

    boost::nowide::ifstream in_file(in_filename.c_str());
    std::vector<std::string> rows;
    std::string row;
    if (in_file.good())
    {
        while (std::getline(in_file, gcode_line))
        {
            if (in_file.good())
            {
                if (boost::starts_with(gcode_line, BEGIN_MASK))
                {
                    reading_image = true;
                    gcode_line = gcode_line.substr(BEGIN_MASK.length() + 1);
                    std::string::size_type x_pos = gcode_line.find('x');
                    std::string width_str = gcode_line.substr(0, x_pos);
                    width = (unsigned int)::atoi(width_str.c_str());
                    std::string height_str = gcode_line.substr(x_pos + 1);
                    height = (unsigned int)::atoi(height_str.c_str());
                    row.clear();
                }
                else if (reading_image && boost::starts_with(gcode_line, END_MASK))
                {
                    std::string out_filename = out_path + std::to_string(width) + "x" + std::to_string(height) + ".png";
                    boost::nowide::ofstream out_file(out_filename.c_str(), std::ios::binary);
                    if (out_file.good())
                    {
                        std::string decoded;
                        decoded.resize(boost::beast::detail::base64::decoded_size(row.size()));
                        decoded.resize(boost::beast::detail::base64::decode((void*)&decoded[0], row.data(), row.size()).first);

                        out_file.write(decoded.c_str(), decoded.size());
                        out_file.close();
                    }

                    reading_image = false;
                    width = 0;
                    height = 0;
                    rows.clear();
                }
                else if (reading_image)
                    row += gcode_line.substr(2);
            }
        }

        in_file.close();
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR_DEBUG

void GUI_App::window_pos_save(wxTopLevelWindow* window, const std::string &name)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    WindowMetrics metrics = WindowMetrics::from_window(window);
    app_config->set(config_key, metrics.serialize());
    // save changed app_config here, before all action related to a close of application is processed
    if (app_config->dirty())
        app_config->save();
}

void GUI_App::window_pos_restore(wxTopLevelWindow* window, const std::string &name, bool default_maximized)
{
    if (name.empty()) { return; }
    const auto config_key = (boost::format("window_%1%") % name).str();

    if (! app_config->has(config_key)) {
        window->Maximize(default_maximized);
        return;
    }

    auto metrics = WindowMetrics::deserialize(app_config->get(config_key));
    if (! metrics) {
        window->Maximize(default_maximized);
        return;
    }

    const wxRect& rect = metrics->get_rect();

    if (app_config->get_bool("restore_win_position")) {
        // workaround for crash related to the positioning of the window on secondary monitor
        app_config->set("restore_win_position", (boost::format("crashed_at_%1%_pos") % name).str());
        app_config->save();
        window->SetPosition(rect.GetPosition());

        // workaround for crash related to the positioning of the window on secondary monitor
        app_config->set("restore_win_position", (boost::format("crashed_at_%1%_size") % name).str());
        app_config->save();
        window->SetSize(rect.GetSize());

        // revert "restore_win_position" value if application wasn't crashed
        app_config->set("restore_win_position", "1");
        app_config->save();
    }
    else
        window->CenterOnScreen();

    window->Maximize(metrics->get_maximized());
}

void GUI_App::window_pos_sanitize(wxTopLevelWindow* window)
{
    /*unsigned*/int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    } else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    auto metrics = WindowMetrics::from_window(window);
    metrics.sanitize_for_display(display);
    if (window->GetScreenRect() != metrics.get_rect()) {
        window->SetSize(metrics.get_rect());
    }
}

bool GUI_App::config_wizard_startup()
{
    if (!m_app_conf_exists || preset_bundle->printers.only_default_printers()) {
        run_wizard(ConfigWizard::RR_DATA_EMPTY);
        return true;
    } else if (get_app_config()->legacy_datadir()) {
        // Looks like user has legacy pre-vendorbundle data directory,
        // explain what this is and run the wizard

        MsgDataLegacy dlg;
        dlg.ShowModal();

        run_wizard(ConfigWizard::RR_DATA_LEGACY);
        return true;
    }
#ifndef __APPLE__
    else if (is_editor() && m_last_app_conf_lower_version && app_config->get_bool("downloader_url_registered")) {
        show_downloader_registration_dialog();
        return true;
    }
#endif
    return false;
}

bool GUI_App::check_updates(const bool verbose)
{
	PresetUpdater::UpdateResult updater_result;
	try {
        preset_updater->update_index_db();
		updater_result = preset_updater->config_update(app_config->orig_version(), verbose ? PresetUpdater::UpdateParams::SHOW_TEXT_BOX : PresetUpdater::UpdateParams::SHOW_NOTIFICATION);
		if (updater_result == PresetUpdater::R_INCOMPAT_EXIT) {
			mainframe->Close();
            // Applicaiton is closing.
            return false;
		}
		else if (updater_result == PresetUpdater::R_INCOMPAT_CONFIGURED) {
            m_app_conf_exists = true;
		}
		else if (verbose && updater_result == PresetUpdater::R_NOOP) {
			MsgNoUpdates dlg;
			dlg.ShowModal();
		}
	} catch (const std::exception &ex) {
		show_error(nullptr, ex.what());
	}
    // Applicaiton will continue.
    return true;
}

bool GUI_App::open_browser_with_warning_dialog(const wxString& url,  wxWindow* parent/* = nullptr*/, bool allow_remember_choice/* = true*/, int flags/* = 0*/)
{

    // warning dialog containes a "Remember my choice" checkbox
    std::string option_key = "suppress_hyperlinks";
    std::string option_value = app_config->get(option_key);
    bool launch = true;
    if (allow_remember_choice && option_value != "allow") {
        RichMessageDialog dialog(parent, _L("Open hyperlink in default browser?"), format(_L("%1%: Open hyperlink"), SLIC3R_APP_NAME), wxICON_QUESTION | wxYES_NO);
        dialog.ShowCheckBox(_L("Remember my choice"));
        auto answer = dialog.ShowModal();
        launch = answer == wxID_YES;
        if (dialog.IsCheckBoxChecked()) {
            this->open_preferences("suppress_hyperlinks", "General");
            //wxString preferences_item = _L("Suppress to open hyperlink in browser");
            //wxString msg =
            //    format(_L("%1% will remember your choice."), SLIC3R_APP_NAME) + "\n\n" +
            //    _L("You will not be asked about it again on hyperlinks hovering.") + "\n\n" +
            //    format_wxstr(_L("Visit \"Preferences\" and check \"%1%\"\nto changes your choice."), preferences_item);

            //MessageDialog msg_dlg(parent, msg, format(_L("%1%: Don't ask me again"), SLIC3R_APP_NAME), wxOK | wxCANCEL | wxICON_INFORMATION);
            //if (msg_dlg.ShowModal() == wxID_CANCEL)
            //    return false;
            //app_config->set(option_key, answer == wxID_NO ? "1" : "0");
        }
    }
    // warning dialog doesn't containe a "Remember my choice" checkbox
    // and will be shown only when "Suppress to open hyperlink in browser" is ON.
    else if (option_value != "allow") {
        MessageDialog dialog(parent, _L("Open hyperlink in default browser?"), format(_L("%1%: Open hyperlink"), SLIC3R_APP_NAME), wxICON_QUESTION | wxYES_NO);
        launch = dialog.ShowModal() == wxID_YES;
    }

    return  launch && wxLaunchDefaultBrowser(url, flags);
}

// static method accepting a wxWindow object as first parameter
// void warning_catcher{
//     my($self, $message_dialog) = @_;
//     return sub{
//         my $message = shift;
//         return if $message = ~/ GLUquadricObjPtr | Attempt to free unreferenced scalar / ;
//         my @params = ($message, 'Warning', wxOK | wxICON_WARNING);
//         $message_dialog
//             ? $message_dialog->(@params)
//             : Wx::MessageDialog->new($self, @params)->ShowModal;
//     };
// }

// Do we need this function???
// void GUI_App::notify(message) {
//     auto frame = GetTopWindow();
//     // try harder to attract user attention on OS X
//     if (!frame->IsActive())
//         frame->RequestUserAttention(defined(__WXOSX__/*&Wx::wxMAC */)? wxUSER_ATTENTION_ERROR : wxUSER_ATTENTION_INFO);
//
//     // There used to be notifier using a Growl application for OSX, but Growl is dead.
//     // The notifier also supported the Linux X D - bus notifications, but that support was broken.
//     //TODO use wxNotificationMessage ?
// }


#ifdef __WXMSW__
void GUI_App::associate_3mf_files()
{
#ifdef SLIC3R_APP_W_PROG_ID
    associate_file_type(L".3mf", SLIC3R_APP_W_PROG_ID, SLIC3R_APP_WNAME, true);
#endif
}

void GUI_App::associate_stl_files()
{
#ifdef SLIC3R_APP_W_PROG_ID
    associate_file_type(L".stl", SLIC3R_APP_W_PROG_ID, SLIC3R_APP_WNAME, true);
#endif
}

void GUI_App::associate_gcode_files()
{
#ifdef GCODEVIEWER_APP_W_PROG_ID
    associate_file_type(L".gcode", GCODEVIEWER_APP_W_PROG_ID, wxString(GCODEVIEWER_APP_NAME).ToStdWstring(), true);
#endif
}

void GUI_App::associate_bgcode_files()
{
#ifdef GCODEVIEWER_APP_W_PROG_ID
    associate_file_type(L".bgcode", GCODEVIEWER_APP_W_PROG_ID, wxString(GCODEVIEWER_APP_NAME).ToStdWstring(), true);
#endif
}
#endif // __WXMSW__

void GUI_App::on_version_read(wxCommandEvent& evt)
{
    app_config->set("version_online", into_u8(evt.GetString()));
    std::string opt = app_config->get("notify_release");
    if (this->plater_ == nullptr || (!m_app_updater->get_triggered_by_user() && opt != "all" && opt != "release")) {
        BOOST_LOG_TRIVIAL(info) << "Version online: " << evt.GetString() << ". User does not wish to be notified.";
        return;
    }
    if (*Semver::parse(SLIC3R_VERSION_FULL) >= *Semver::parse(into_u8(evt.GetString()))) {
        if (m_app_updater->get_triggered_by_user())
        {
            std::string text = (*Semver::parse(into_u8(evt.GetString())) == Semver())
                ? _u8L("Check for application update has failed.")
                : Slic3r::format(_u8L("You are currently running the latest released version %1%."), evt.GetString());

            if (*Semver::parse(SLIC3R_VERSION) > *Semver::parse(into_u8(evt.GetString())))
                text = Slic3r::format(_u8L("There are no new released versions online. The latest release version is %1%."), evt.GetString());

            this->plater_->get_notification_manager()->push_version_notification(NotificationType::NoNewReleaseAvailable
                , NotificationManager::NotificationLevel::RegularNotificationLevel
                , text
                , std::string()
                , std::function<bool(wxEvtHandler*)>()
            );
        }
        return;
    }
    // notification
    /*
    this->plater_->get_notification_manager()->push_notification(NotificationType::NewAppAvailable
        , NotificationManager::NotificationLevel::ImportantNotificationLevel
        , Slic3r::format(_u8L("New release version %1% is available."), evt.GetString())
        , _u8L("See Download page.")
        , [](wxEvtHandler* evnthndlr) {wxGetApp().open_web_page_localized(SLIC3R_DOWNLOAD); return true; }
    );
    */
    // updater
    // read triggered_by_user that was set when calling  GUI_App::app_version_check
    app_updater(m_app_updater->get_triggered_by_user());
}

void GUI_App::app_updater(bool from_user)
{
    DownloadAppData app_data = m_app_updater->get_app_data();

    if (from_user && (!app_data.version || *app_data.version <= *Semver::parse(SLIC3R_VERSION)))
    {
        BOOST_LOG_TRIVIAL(info) << "There is no newer version online.";
        MsgNoAppUpdates no_update_dialog;
        no_update_dialog.ShowModal();
        return;

    }

    assert(!app_data.url.empty());
    assert(!app_data.target_path.empty());

    // dialog with new version info
    AppUpdateAvailableDialog dialog(*Semver::parse(SLIC3R_VERSION), *app_data.version, from_user);
    auto dialog_result = dialog.ShowModal();
    // checkbox "do not show again"
    if (dialog.disable_version_check()) {
        app_config->set("notify_release", "none");
    }
    // Doesn't wish to update
    if (dialog_result != wxID_OK) {
        return;
    }
    // dialog with new version download (installer or app dependent on system) including path selection
    AppUpdateDownloadDialog dwnld_dlg(*app_data.version, app_data.target_path);
    dialog_result = dwnld_dlg.ShowModal();
    //  Doesn't wish to download
    if (dialog_result != wxID_OK) {
        return;
    }
    app_data.target_path =dwnld_dlg.get_download_path();
    // start download
    this->plater_->get_notification_manager()->push_download_progress_notification(GUI::format(_L("Downloading %1%"), app_data.target_path.filename().string()), std::bind(&AppUpdater::cancel_callback, this->m_app_updater.get()));
    app_data.start_after = dwnld_dlg.run_after_download();
    m_app_updater->set_app_data(std::move(app_data));
    m_app_updater->sync_download();
}

void GUI_App::app_version_check(bool from_user)
{
    if (from_user) {
        if (m_app_updater->get_download_ongoing()) {
            MessageDialog msgdlg(nullptr, _L("Downloading of the new version is in progress. Do you want to continue?"), _L("Notice"), wxYES_NO);
            if (msgdlg.ShowModal() != wxID_YES)
                return;
        }
    }
    std::string version_check_url = app_config->version_check_url();
    m_app_updater->sync_version(version_check_url, from_user);
}

void GUI_App::start_download(std::string url)
{
    if (!plater_) {
        BOOST_LOG_TRIVIAL(error) << "Could not start URL download: plater is nullptr.";
        return;
    }
    //lets always init so if the download dest folder was changed, new dest is used
        boost::filesystem::path dest_folder(app_config->get("url_downloader_dest"));
        if (dest_folder.empty() || !boost::filesystem::is_directory(dest_folder)) {
            std::string msg = _u8L("Could not start URL download. Destination folder is not set. Please choose destination folder in Configuration Wizard.");
            BOOST_LOG_TRIVIAL(error) << msg;
            show_error(nullptr, msg);
            return;
        }
    m_downloader->init(dest_folder);
    m_downloader->start_download(url);
}

void GUI_App::open_wifi_config_dialog(bool forced, const wxString& drive_path/* = {}*/)
{
    if(m_wifi_config_dialog_shown)
        return;

    bool dialog_was_declined = app_config->get_bool("wifi_config_dialog_declined");

    if (!forced && dialog_was_declined) {

        // dialog was already declined this run, show only notification
        notification_manager()->push_notification(NotificationType::WifiConfigFileDetected
            , NotificationManager::NotificationLevel::ImportantNotificationLevel
            // TRN Text of notification when Slicer starts and usb stick with printer settings ini file is present
            , _u8L("Printer configuration file detected on removable media.")
            // TRN Text of hypertext of notification when Slicer starts and usb stick with printer settings ini file is present
            , _u8L("Write Wi-Fi credentials."), [drive_path](wxEvtHandler* evt_hndlr) {
                wxGetApp().open_wifi_config_dialog(true, drive_path);
                return true; });
        return;
    }

    m_wifi_config_dialog_shown = true;
    std::string file_path;
    WifiConfigDialog dialog(mainframe, file_path, removable_drive_manager(), drive_path);
    if (dialog.ShowModal() == wxID_OK) {
        plater_->get_notification_manager()->push_exporting_finished_notification(file_path, boost::filesystem::path(file_path).parent_path().string(), true);
        app_config->set("wifi_config_dialog_declined", "0");
    } else {
        app_config->set("wifi_config_dialog_declined", "1");
    }
    m_wifi_config_dialog_shown = false;
}

} // GUI
} //Slic3r
