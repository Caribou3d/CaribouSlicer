#ifndef slic3r_GUI_CalibrationExtruderDialog_hpp_
#define slic3r_GUI_CalibrationExtruderDialog_hpp_

#include "CalibrationAbstractDialog.hpp"

namespace Slic3r {
namespace GUI {

class CalibrationExtruderDialog : public CalibrationAbstractDialog
{

public:
    CalibrationExtruderDialog(GUI_App* app, MainFrame* mainframe) : CalibrationAbstractDialog(app, mainframe, "Calibration walls") { create(boost::filesystem::path("calibration") / "extrusionmultiplier", "extrusionmultiplier.html"); }
    virtual ~CalibrationExtruderDialog(){ }

protected:
    void create_buttons(wxStdDialogButtonSizer* sizer) override;
    void create_geometry();
    void create_geometry_v(wxCommandEvent& event_args) { create_geometry(); }

    wxComboBox* dimension;
    wxComboBox* calibrate;

};

} // namespace GUI
} // namespace Slic3r

#endif
