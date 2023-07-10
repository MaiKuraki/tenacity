/**********************************************************************

  Audacity: A Digital Audio Editor

  FreqWindow.cpp

  Dominic Mazzoni

*******************************************************************//**

\class FrequencyPlotDialog
\brief Displays a spectrum plot of the waveform.  Has options for
selecting parameters of the plot.

Has a feature that finds peaks and reports their value as you move
the mouse around.

*//****************************************************************//**

\class FreqPlot
\brief Works with FrequencyPlotDialog to dsplay a spectrum plot of the waveform.
This class actually does the graph display.

Has a feature that finds peaks and reports their value as you move
the mouse around.

*//*******************************************************************/

/*
  Salvo Ventura - November 2006
  Extended range check for additional FFT windows
*/



#include "FreqWindow.h"

#include <algorithm>

#include <wx/setup.h> // for wxUSE_* macros

#include <wx/brush.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/font.h>
#include <wx/image.h>
#include <wx/file.h>
#include <wx/intl.h>
#include <wx/scrolbar.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/statusbr.h>

#include <wx/textctrl.h>
#include <wx/textfile.h>

#include <wx/wfstream.h>
#include <wx/txtstrm.h>

#include <cmath>

// Tenacity
#include <lib-files/FileNames.h>
#include <lib-math/FFT.h>
#include <lib-preferences/Prefs.h>

#include "SelectFile.h"
#include "shuttle/ShuttleGui.h"
#include "AColor.h"
#include "CommonCommandFlags.h"
#include "PitchName.h"
#include "Decibels.h"
#include "Project.h"
#include "ProjectWindow.h"
#include "theme/Theme.h"
#include "ViewInfo.h"
#include "theme/AllThemeResources.h"

#include "WaveTrack.h"

#include "./widgets/HelpSystem.h"
#include "widgets/AudacityMessageBox.h"
#include "widgets/Ruler.h"

#if wxUSE_ACCESSIBILITY
#include "widgets/WindowAccessible.h"
#endif

#define FrequencyAnalysisTitle XO("Frequency Analysis")

DEFINE_EVENT_TYPE(EVT_FREQWINDOW_RECALC);

enum {
   FirstID = 7000,

   FreqVZoomSliderID,
   FreqVPanScrollerID,
   FreqHZoomSliderID,
   FreqHPanScrollerID,
   FreqExportButtonID,
   FreqAlgChoiceID,
   FreqSizeChoiceID,
   FreqFuncChoiceID,
   FreqAxisChoiceID,
   ReplotButtonID,
   GridOnOffID
};

// These specify the minimum plot window width

#define FREQ_WINDOW_WIDTH 480
#define FREQ_WINDOW_HEIGHT 330


static const char * ZoomIn[] = {
"16 16 3 1",
" 	c None",
"+	c #000000",
"#	c #CCCCCC",
"                ",
"                ",
"                ",
"                ",
"      ++++      ",
"      +##+      ",
"      +##+      ",
"   ++++##++++   ",
"   +########+   ",
"   +########+   ",
"   ++++##++++   ",
"      +##+      ",
"      +##+      ",
"      ++++      ",
"                ",
"                "};

static const char * ZoomOut[] = {
"16 16 3 1",
" 	c None",
"+	c #000000",
"#	c #CCCCCC",
"                ",
"                ",
"                ",
"                ",
"                ",
"                ",
"                ",
"   ++++++++++   ",
"   +########+   ",
"   +########+   ",
"   ++++++++++   ",
"                ",
"                ",
"                ",
"                ",
"                "};

// FrequencyPlotDialog

BEGIN_EVENT_TABLE(FrequencyPlotDialog, wxDialogWrapper)
   EVT_CLOSE(FrequencyPlotDialog::OnCloseWindow)
   EVT_SIZE(FrequencyPlotDialog::OnSize)
   EVT_SLIDER(FreqVZoomSliderID, FrequencyPlotDialog::OnZoomSlider)
   EVT_COMMAND_SCROLL(FreqVPanScrollerID, FrequencyPlotDialog::OnPanScroller)
   EVT_SLIDER(FreqHZoomSliderID, FrequencyPlotDialog::OnZoomSlider)
   EVT_COMMAND_SCROLL(FreqHPanScrollerID, FrequencyPlotDialog::OnPanScroller)
   EVT_CHOICE(FreqAlgChoiceID, FrequencyPlotDialog::OnAlgChoice)
   EVT_CHOICE(FreqSizeChoiceID, FrequencyPlotDialog::OnSizeChoice)
   EVT_CHOICE(FreqFuncChoiceID, FrequencyPlotDialog::OnFuncChoice)
   EVT_CHOICE(FreqAxisChoiceID, FrequencyPlotDialog::OnAxisChoice)
   EVT_BUTTON(FreqExportButtonID, FrequencyPlotDialog::OnExport)
   EVT_BUTTON(ReplotButtonID, FrequencyPlotDialog::OnReplot)
   EVT_BUTTON(wxID_CANCEL, FrequencyPlotDialog::OnCloseButton)
   EVT_BUTTON(wxID_HELP, FrequencyPlotDialog::OnGetURL)
   EVT_CHECKBOX(GridOnOffID, FrequencyPlotDialog::OnGridOnOff)
   EVT_COMMAND(wxID_ANY, EVT_FREQWINDOW_RECALC, FrequencyPlotDialog::OnRecalc)
END_EVENT_TABLE()

FrequencyPlotDialog::FrequencyPlotDialog(wxWindow * parent, wxWindowID id,
                           TenacityProject &project,
                           const TranslatableString & title,
                           const wxPoint & pos)
:  wxDialogWrapper(parent, id, title, pos, wxDefaultSize,
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX),
   mAnalyst(std::make_unique<SpectrumAnalyst>())
,  mProject{ &project }
{
   SetName();

   mMouseX = 0;
   mMouseY = 0;
   mRate = 0;
   mDataLen = 0;

   gPrefs->Read(wxT("/FrequencyPlotDialog/DrawGrid"), &mDrawGrid, true);
   gPrefs->Read(wxT("/FrequencyPlotDialog/SizeChoice"), &mSize, 3);

   int alg;
   gPrefs->Read(wxT("/FrequencyPlotDialog/AlgChoice"), &alg, 0);
   mAlg = static_cast<SpectrumAnalyst::Algorithm>(alg);

   gPrefs->Read(wxT("/FrequencyPlotDialog/FuncChoice"), &mFunc, 3);
   gPrefs->Read(wxT("/FrequencyPlotDialog/AxisChoice"), &mAxis, 1);

   Populate();
}

FrequencyPlotDialog::~FrequencyPlotDialog()
{
}

/// The vertical zoom slider stores a value between 1 and MAX_ZOOMED_OUT_V inclusive.
/// The value is converted to a zoom level by computing
/// value / MAX_ZOOMED_OUT_V and showing that amount of the total area.
///
/// This is confusing because "min zoom is max slider value".
/// On Linux, the blue fill is *above* the slider, which is ugly.
/// The blue fill can be moved to the bottom (and the math made consistent with
/// horizontal zoom sliders) using wxSL_INVERSE.
/// However, it causes Home to move the slider down
/// and End to move the slider up. This is inconsistent with prior behavior,
/// my intuition, scrollable documents, default GTK3 vertical sliders,
/// and Windows volume control, but consistent with HTML range inputs.
///
/// Because I dislike the new behavior and don't want to change behavior,
/// I chose to not use wxSL_INVERSE and retain "min zoom is max slider value".
constexpr int MAX_ZOOMED_OUT_V = 100;

/// The horizontal zoom slider ranges from [0..HZOOM_COUNT] inclusive.
constexpr int HZOOM_COUNT = 128;
/// Horizontal zoom is exponential. When you increase the slider position by
/// HZOOM_STEPS_PER_DOUBLE steps, the zoom doubles and the viewport size halves.
constexpr int HZOOM_STEPS_PER_DOUBLE = 16;

void FrequencyPlotDialog::Populate()
{
   SetTitle(FrequencyAnalysisTitle);

   TranslatableStrings algChoices{
      XO("Spectrum") ,
      XO("Standard Autocorrelation") ,
      XO("Cuberoot Autocorrelation") ,
      XO("Enhanced Autocorrelation") ,
        /* i18n-hint: This is a technical term, derived from the word
         * "spectrum".  Do not translate it unless you are sure you
         * know the correct technical word in your language. */
      XO("Cepstrum") ,
   };

   TranslatableStrings sizeChoices{
      Verbatim( "128" ) ,
      Verbatim( "256" ) ,
      Verbatim( "512" ) ,
      Verbatim( "1024" ) ,
      Verbatim( "2048" ) ,
      Verbatim( "4096" ) ,
      Verbatim( "8192" ) ,
      Verbatim( "16384" ) ,
      Verbatim( "32768" ) ,
      Verbatim( "65536" ) ,
   };

   TranslatableStrings funcChoices;
   for (int i = 0, cnt = NumWindowFuncs(); i < cnt; i++)
   {
      funcChoices.push_back(
         /* i18n-hint: This refers to a "window function",
          * such as Hann or Rectangular, used in the
          * Frequency analyze dialog box. */
         XO("%s window").Format( WindowFuncName(i) ) );
   }

   TranslatableStrings axisChoices{
      XO("Linear frequency") ,
      XO("Log frequency") ,
   };

   mFreqFont = wxFont(fontSize, wxFONTFAMILY_SWISS, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
   mArrowCursor = std::make_unique<wxCursor>(wxCURSOR_ARROW);
   mCrossCursor = std::make_unique<wxCursor>(wxCURSOR_CROSS);

   long size;
   // reinterpret one of the verbatim strings above as a number
   sizeChoices[mSize].MSGID().GET().ToLong(&size);
   mWindowSize = size;

   dBRange = DecibelScaleCutoff.Read();
   if(dBRange < 90.)
      dBRange = 90.;

   ShuttleGui S(this, eIsCreating);

   S.SetBorder(0);

   S.AddSpace(5);

   S.SetSizerProportion(1);
   S.StartMultiColumn(3, wxEXPAND);
   {
      S.SetStretchyCol(1);
      S.SetStretchyRow(0);

      // -------------------------------------------------------------------
      // ROW 1: Freq response panel and sliders for vertical scale
      // -------------------------------------------------------------------

      S.StartVerticalLay(2);
      {
         vRuler = safenew RulerPanel(
            S.GetParent(), wxID_ANY, wxVERTICAL,
            wxSize{ 100, 100 }, // Ruler can't handle small sizes
            RulerPanel::Range{ 0.0, -dBRange },
            Ruler::LinearDBFormat,
            XO("dB"),
            RulerPanel::Options{}
               .LabelEdges(true)
               .TickColour( theTheme.Colour( clrGraphLabels ) )
         );

         S.AddSpace(wxDefaultCoord, 1);
         S.Prop(1)
            .Position(wxALIGN_RIGHT | wxALIGN_TOP)
            .AddWindow(vRuler);
         S.AddSpace(wxDefaultCoord, 1);
      }
      S.EndVerticalLay();

      mFreqPlot = safenew FreqPlot(S.GetParent(), wxID_ANY);
      S.Prop(1)
         .Position(wxEXPAND)
         .MinSize( { wxDefaultCoord, FREQ_WINDOW_HEIGHT } )
         .AddWindow(mFreqPlot);

      S.StartHorizontalLay(wxEXPAND, 0);
      {
         S.StartVerticalLay();
         {
            vPanScroller = safenew wxScrollBar(S.GetParent(), FreqVPanScrollerID,
               wxDefaultPosition, wxDefaultSize, wxSB_VERTICAL);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            vPanScroller->SetAccessible(safenew WindowAccessible(vPanScroller));
#endif
            S.Prop(1);
            S
               .Name(XO("Scroll"))
               .Position( wxALIGN_LEFT | wxTOP)
               .AddWindow(vPanScroller);
         }
         S.EndVerticalLay();

         S.StartVerticalLay();
         {
            wxStaticBitmap *zi = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomIn));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zi);

            S.AddSpace(5);

            vZoomSlider = safenew wxSliderWrapper(S.GetParent(), FreqVZoomSliderID,
               MAX_ZOOMED_OUT_V, 1, MAX_ZOOMED_OUT_V,
               wxDefaultPosition, wxDefaultSize, wxSL_VERTICAL);
            S.Prop(1);
            S
               .Name(XO("Zoom"))
               .Position(wxALIGN_CENTER_HORIZONTAL)
               .AddWindow(vZoomSlider);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            vZoomSlider->SetAccessible(safenew WindowAccessible(vZoomSlider));
#endif

            S.AddSpace(5);

            wxStaticBitmap *zo = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomOut));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zo);
         }
         S.EndVerticalLay();

         S.AddSpace(5, wxDefaultCoord);
      }
      S.EndHorizontalLay();

      // -------------------------------------------------------------------
      // ROW 2: Frequency ruler, pan, and zoom
      // -------------------------------------------------------------------

      // col0
      S.AddSpace(1);

      // col1
      S.StartVerticalLay(0);  // do not expand vertically
      {
         S.StartHorizontalLay(wxEXPAND, 0);  // expand horizontally, not vertically
         {
            hRuler  = safenew RulerPanel(
               S.GetParent(), wxID_ANY, wxHORIZONTAL,
               wxSize{ 100, 100 }, // Ruler can't handle small sizes
               RulerPanel::Range{ 10, 20000 },
               Ruler::RealFormat,
               XO("Hz"),
               RulerPanel::Options{}
                  .Log(true)
                  .Flip(true)
                  .LabelEdges(true)
                  .TickColour( theTheme.Colour( clrGraphLabels ) )
            );

            S.AddSpace(1, wxDefaultCoord);
            S.Prop(1)
               .Position(wxALIGN_LEFT | wxALIGN_TOP)
               .AddWindow(hRuler);
            S.AddSpace(1, wxDefaultCoord);
         }
         S.EndHorizontalLay();

         S.StartHorizontalLay(wxEXPAND, 0);
         {
            hPanScroller = safenew wxScrollBar(S.GetParent(), FreqHPanScrollerID,
               wxDefaultPosition, wxDefaultSize, wxSB_HORIZONTAL);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            hPanScroller->SetAccessible(safenew WindowAccessible(hPanScroller));
#endif
            S.Prop(1);
            S
               .Name(XO("Scroll Horizontal"))
               .Position( wxALIGN_LEFT | wxTOP)
               .AddWindow(hPanScroller);
         }
         S.EndHorizontalLay();

         S.StartHorizontalLay(wxEXPAND, 0);
         {
            wxStaticBitmap *zi = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomOut));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zi);

            S.AddSpace(5);

            hZoomSlider = safenew wxSliderWrapper(S.GetParent(), FreqHZoomSliderID,
               0, 0, HZOOM_COUNT,
               wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
            S.Prop(1);
            S
               .Name(XO("Zoom Horizontal"))
               .Position(wxALIGN_CENTER_VERTICAL)
               .AddWindow(hZoomSlider);
#if wxUSE_ACCESSIBILITY
            // so that name can be set on a standard control
            hZoomSlider->SetAccessible(safenew WindowAccessible(hZoomSlider));
#endif

            S.AddSpace(5);

            wxStaticBitmap *zo = safenew wxStaticBitmap(S.GetParent(), wxID_ANY, wxBitmap(ZoomIn));
            S.Position(wxALIGN_CENTER)
               .AddWindow(zo);
         }
         S.EndHorizontalLay();
      }
      S.EndVerticalLay();

      // col2
      S.AddSpace(1);
      // next row

      // -------------------------------------------------------------------
      // ROW 3: Spacer
      // -------------------------------------------------------------------
      S.AddSpace(5);
      S.AddSpace(5);
      S.AddSpace(5);

      // -------------------------------------------------------------------
      // ROW 4: Info
      // -------------------------------------------------------------------

      S.AddSpace(1);

      S.StartHorizontalLay(wxEXPAND);
      {
         S.SetSizerProportion(1);
         S.StartMultiColumn(6);
         S.SetStretchyCol(1);
         S.SetStretchyCol(3);
         {
            S.AddPrompt(XXO("Cursor:"));

            mCursorText = S.Style(wxTE_READONLY)
               .AddTextBox( {}, wxT(""), 10);

            S.AddPrompt(XXO("Peak:"));

            mPeakText = S.Style(wxTE_READONLY)
               .AddTextBox( {}, wxT(""), 10);
            S.AddSpace(5);

            mGridOnOff = S.Id(GridOnOffID).AddCheckBox(XXO("&Grids"), mDrawGrid);
         }
         S.EndMultiColumn();
      }
      S.EndHorizontalLay();

      S.AddSpace(1);
   }
   S.EndMultiColumn();

   // -------------------------------------------------------------------
   // ROW 5: Spacer
   // -------------------------------------------------------------------
   
   S.AddSpace(5);

   S.SetBorder(2);
   S.SetSizerProportion(0);
   S.StartMultiColumn(9, wxALIGN_CENTER);
   {
      // ----------------------------------------------------------------
      // ROW 6: Algorithm, Size, Export, Replot
      // ----------------------------------------------------------------

      S.AddSpace(5);

      mAlgChoice = S.Id(FreqAlgChoiceID).Focus()
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(XXO("&Algorithm:"), algChoices, mAlg);

      S.AddSpace(5);

      mSizeChoice = S.Id(FreqSizeChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(XXO("&Size:"), sizeChoices, mSize);

      S.AddSpace(5);

      mExportButton = S.Id(FreqExportButtonID).AddButton(XXO("&Export..."));

      S.AddSpace(5);


      // ----------------------------------------------------------------
      // ROW 7: Function, Axix, Grids, Close
      // ----------------------------------------------------------------

      S.AddSpace(5);

      mFuncChoice = S.Id(FreqFuncChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(XXO("&Function:"), funcChoices, mFunc);
      mFuncChoice->MoveAfterInTabOrder(mSizeChoice);

      S.AddSpace(5);

      mAxisChoice = S.Id(FreqAxisChoiceID)
         .MinSize( { wxDefaultCoord, wxDefaultCoord } )
         .AddChoice(XXO("&Axis:"), axisChoices, mAxis);
      mAxisChoice->MoveAfterInTabOrder(mFuncChoice);

      S.AddSpace(5);

      mReplotButton = S.Id(ReplotButtonID).AddButton(XXO("&Replot..."));

      S.AddSpace(5);

      //mCloseButton = S.Id(wxID_CANCEL).AddButton(XO("&Close"));

      //S.AddSpace(5);
   }
   S.EndMultiColumn();
   S.AddStandardButtons( eHelpButton | eCloseButton );

   // -------------------------------------------------------------------
   // ROW 8: Spacer
   // -------------------------------------------------------------------

   S.AddSpace(5);

   mProgress = safenew FreqGauge(S.GetParent(), wxID_ANY); //, wxST_SIZEGRIP);
   S.Position(wxEXPAND)
      .AddWindow(mProgress);

   // Log-frequency axis works for spectrum plots only.
   if (mAlg != SpectrumAnalyst::Spectrum)
   {
      mAxis = 0;
      mAxisChoice->Disable();
   }
   mLogAxis = mAxis != 0;

   mCloseButton = static_cast<wxButton*>(FindWindowById( wxID_CANCEL ));
   mCloseButton->SetDefault();

   Layout();
   Fit();
   // Bug 1607:
   Center();

   SetMinSize(GetSize());

#if defined(__WXGTK__)
   // This should be rechecked with wx3.
   //
   // The scrollbar (focus some reason) doesn't allow tabbing past it
   // because it can't receive focus.  So, convince it otherwise.
   //
   // Unfortunately, this still doesn't let you adjust the scrollbar
   // from the keyboard.  Near as I can tell, wxWGTK is capturing the
   // keyboard input, so the GTK widget doesn't see it, preventing
   // the normal scroll events from being generated.
   //
   // I guess the only way round it would be to handle key actions
   // ourselves, but we'll leave that for a future date.
//   gtk_widget_set_can_focus(vPanScroller->m_widget, true);
#endif
}

void FrequencyPlotDialog::OnGetURL(wxCommandEvent & WXUNUSED(event))
{
   // Original help page is back on-line (March 2016), but the manual should be more reliable.
   // http://www.eramp.com/WCAG_2_audio_contrast_tool_help.htm
   HelpSystem::ShowHelp(this, L"Plot Spectrum");
}

bool FrequencyPlotDialog::Show(bool show)
{
   if (!show)
   {
      mFreqPlot->SetCursor(*mArrowCursor);
   }

   bool shown = IsShown();

   if (show && !shown)
   {
      dBRange = DecibelScaleCutoff.Read();
      if(dBRange < 90.)
         dBRange = 90.;
      GetAudio();
      // Don't send an event.  We need the recalc right away.
      // so that mAnalyst is valid when we paint.
      //SendRecalcEvent();
      Recalc();
   }

   bool res = wxDialogWrapper::Show(show);

   return res;
}

void FrequencyPlotDialog::GetAudio()
{
   mData.reset();
   mDataLen = 0;

   int selcount = 0;
   bool warning = false;
   for (auto track : TrackList::Get( *mProject ).Selected< const WaveTrack >()) {
      auto &selectedRegion = ViewInfo::Get( *mProject ).selectedRegion;
      if (selcount==0) {
         mRate = track->GetRate();
         auto start = track->TimeToLongSamples(selectedRegion.t0());
         auto end = track->TimeToLongSamples(selectedRegion.t1());
         auto dataLen = end - start;
         if (dataLen > 10485760) {
            warning = true;
            mDataLen = 10485760;
         }
         else
            // dataLen is not more than 10 * 2 ^ 20
            mDataLen = dataLen.as_size_t();
         mData = Floats{ mDataLen };
         // Don't allow throw for bad reads
         track->GetFloats(mData.get(), start, mDataLen,
                    fillZero, false);
      }
      else {
         if (track->GetRate() != mRate) {
            AudacityMessageBox(
               XO(
"To plot the spectrum, all selected tracks must be the same sample rate.") );
            mData.reset();
            mDataLen = 0;
            return;
         }
         auto start = track->TimeToLongSamples(selectedRegion.t0());
         Floats buffer2{ mDataLen };
         // Again, stop exceptions
         track->GetFloats(buffer2.get(), start, mDataLen,
                    fillZero, false);
         for (size_t i = 0; i < mDataLen; i++)
            mData[i] += buffer2[i];
      }
      selcount++;
   }

   if (selcount == 0)
      return;

   if (warning) {
      auto msg = XO(
"Too much audio was selected. Only the first %.1f seconds of audio will be analyzed.")
         .Format(mDataLen / mRate);
      AudacityMessageBox( msg );
   }
}

void FrequencyPlotDialog::OnSize(wxSizeEvent & WXUNUSED(event))
{
   Layout();

   DrawPlot();

   Refresh(true);
}

void FrequencyPlotDialog::DrawBackground(wxMemoryDC & dc)
{
   Layout();

   mBitmap.reset();

   mPlotRect = mFreqPlot->GetClientRect();

   mBitmap = std::make_unique<wxBitmap>(mPlotRect.width, mPlotRect.height,24);

   dc.SelectObject(*mBitmap);

   dc.SetBackground(wxBrush(wxColour(254, 254, 254)));// DONT-THEME Mask colour.
   dc.Clear();

   dc.SetPen(*wxBLACK_PEN);
   dc.SetBrush(*wxWHITE_BRUSH);
   dc.DrawRectangle(mPlotRect);

   dc.SetFont(mFreqFont);
}

void FrequencyPlotDialog::DrawPlot()
{
   if (!mData || mDataLen < mWindowSize || mAnalyst->GetProcessedSize() == 0) {
      wxMemoryDC memDC;

      vRuler->ruler.SetLog(false);
      vRuler->ruler.SetRange(0.0, -dBRange);

      hRuler->ruler.SetLog(false);
      hRuler->ruler.SetRange(0, 1);
      hNumberScale = NumberScale();
      hRuler->ruler.SetNumberScale(hNumberScale);

      DrawBackground(memDC);

      if (mDataLen < mWindowSize) {
         wxString msg = _("Not enough data selected.");
         wxSize sz = memDC.GetTextExtent(msg);
         memDC.DrawText(msg,
                        (mPlotRect.GetWidth() - sz.GetWidth()) / 2,
                        (mPlotRect.GetHeight() - sz.GetHeight()) / 2);
      }

      memDC.SelectObject(wxNullBitmap);
      
      mFreqPlot->Refresh();

      Refresh();

      return;
   }

   // Compute y axis ruler range, given current zoom level
   float yTotal, yMax, yMin;
   {
      float yRange = mYMax - mYMin;

      // How much of the entire graph to show vertically.
      float showAmount = float(vZoomSlider->GetValue()) / float(MAX_ZOOMED_OUT_V);
      yTotal = yRange * showAmount;

      int sTotal = yTotal * 100;
      int sRange = yRange * 100;
      int sPos = vPanScroller->GetThumbPosition() + ((vPanScroller->GetThumbSize() - sTotal) / 2);

      // Set scrollbar size and position
      vPanScroller->SetScrollbar(sPos, sTotal, sRange, sTotal);

      // Recompute sPos, taking into account SetScrollbar() clamping the position.
      sPos = vPanScroller->GetThumbPosition();
      yMax = mYMax - ((float)sPos / 100);
      yMin = yMax - yTotal;
   }

   // Set up y axis ruler

   if (mAlg == SpectrumAnalyst::Spectrum) {
      vRuler->ruler.SetUnits(XO("dB"));
      vRuler->ruler.SetFormat(Ruler::LinearDBFormat);
   } else {
      vRuler->ruler.SetUnits({});
      vRuler->ruler.SetFormat(Ruler::RealFormat);
   }
   int w1, w2, h;
   vRuler->ruler.GetMaxSize(&w1, &h);
   vRuler->ruler.SetRange(yMax, yMin); // Note inversion for vertical.
   vRuler->ruler.GetMaxSize(&w2, &h);
   if( w1 != w2 )   // Reduces flicker
   {
      vRuler->SetMinSize(wxSize(w2,h));
      Layout();
   }
   vRuler->Refresh(false);

   wxMemoryDC memDC;
   DrawBackground(memDC);

   // Get the plot dimensions
   //
   // Must be done after setting the vertical ruler above since the
   // the width could change.
   wxRect r = mPlotRect;
   int width = r.width - 2;

   // Compute x axis ruler range, given current zoom level
   // (range is between 0 and 1 inclusive.)
   float xViewportRelMin, xViewportRelMax;
   {
      constexpr int H_SCROLLBAR_RANGE = 10000;
      // How much of the entire graph to show horizontally.

      float showAmount = pow(0.5f, float(hZoomSlider->GetValue()) / float(HZOOM_STEPS_PER_DOUBLE));

      int oldThumbSize = hPanScroller->GetThumbSize();
      int newThumbSize = showAmount * H_SCROLLBAR_RANGE;

      int oldThumbPosition = hPanScroller->GetThumbPosition();
      int newThumbPosition = oldThumbPosition - (newThumbSize - oldThumbSize) / 2;

      // Set scrollbar size and position
      hPanScroller->SetScrollbar(newThumbPosition, newThumbSize, H_SCROLLBAR_RANGE, newThumbSize);
      newThumbPosition = hPanScroller->GetThumbPosition();

      xViewportRelMin = float(newThumbPosition) / float(H_SCROLLBAR_RANGE);
      xViewportRelMax = float(newThumbPosition + newThumbSize) / float(H_SCROLLBAR_RANGE);
   }

   // Set up x axis ruler

   // TODO store NumberScaleType as a member variable
   NumberScaleType nst;

   // Frequencies/periods passed into SpectrumAnalyst::GetProcessedValue().
   // The entire range of computed values.
   float fullXMin, fullXMax;

   if (mAlg == SpectrumAnalyst::Spectrum) {
      nst = mLogAxis ? nstLogarithmic : nstLinear;
      fullXMin = mRate / mWindowSize;
      fullXMax = mRate / 2;
      // TODO how does track spectrogram create mel/bark/erb/period rulers?
      hRuler->ruler.SetLog(mLogAxis);
      hRuler->ruler.SetUnits(XO("Hz"));
   } else {
      nst = nstLinear;
      fullXMin = 0;
      fullXMax = mAnalyst->GetProcessedSize() / mRate;
      hRuler->ruler.SetLog(false);
      /* i18n-hint: short form of 'seconds'.*/
      hRuler->ruler.SetUnits(XO("s"));
   }

   // Frequencies/periods passed into SpectrumAnalyst::GetProcessedValue().
   // The currently visible region given our current zoom and scroll settings.
   float viewportXMin, viewportXMax;
   {
      // The entire region, ignoring the zoom viewport.
      auto fullScale = NumberScale(nst, fullXMin, fullXMax);

      // Compute the viewport size, in frequencies passed into GetProcessedValue().
      viewportXMin = fullScale.PositionToValue(xViewportRelMin);
      viewportXMax = fullScale.PositionToValue(xViewportRelMax);
   }

   hRuler->ruler.SetRange(viewportXMin, viewportXMax);

   // hNumberScale.PositionToValue() is used to map normalized x-coordinates [0, 1)
   // to frequencies/periods (within the viewport's limits)
   // passed into SpectrumAnalyst::GetProcessedValue().
   hNumberScale = NumberScale(nst, viewportXMin, viewportXMax);
   hRuler->ruler.SetNumberScale(hNumberScale);  // this allows it to eventually draw nonlinear scales.

   hRuler->Refresh(false);

   // Draw the plot
   if (mAlg == SpectrumAnalyst::Spectrum)
      memDC.SetPen(wxPen(theTheme.Colour( clrHzPlot ), 1, wxPENSTYLE_SOLID));
   else
      memDC.SetPen(wxPen(theTheme.Colour( clrWavelengthPlot), 1, wxPENSTYLE_SOLID));

   float xPos;
   float xPosNext = hNumberScale.PositionToValue(0.f);

   for (int i = 0; i < width; i++) {
      xPos = xPosNext;
      xPosNext = hNumberScale.PositionToValue(float(i + 1) / float(width));

      float y = mAnalyst->GetProcessedValue(xPos, xPosNext);
      float ynorm = (y - yMin) / yTotal;

      int lineheight = (int)(ynorm * (r.height - 1));

      if (lineheight > r.height - 2)
         lineheight = r.height - 2;

      if (ynorm > 0.0)
         AColor::Line(memDC, r.x + 1 + i, r.y + r.height - 1 - lineheight,
                        r.x + 1 + i, r.y + r.height - 1);
   }

   // Outline the graph
   memDC.SetPen(*wxBLACK_PEN);
   memDC.SetBrush(*wxTRANSPARENT_BRUSH);
   memDC.DrawRectangle(r);

   if(mDrawGrid)
   {
      hRuler->ruler.DrawGrid(memDC, r.height, true, true, 1, 1);
      vRuler->ruler.DrawGrid(memDC, r.width, true, true, 1, 1);
   }

   memDC.SelectObject( wxNullBitmap );

   mFreqPlot->Refresh();
}


void FrequencyPlotDialog::PlotMouseEvent(wxMouseEvent & event)
{
   if (event.Moving() && (event.m_x != mMouseX || event.m_y != mMouseY)) {
      mMouseX = event.m_x;
      mMouseY = event.m_y;

      if (mPlotRect.Contains(mMouseX, mMouseY))
         mFreqPlot->SetCursor(*mCrossCursor);
      else
         mFreqPlot->SetCursor(*mArrowCursor);

      wxRect r = mPlotRect;
      int width = r.width - 2;
      if (
         hNumberScale != NumberScale() &&
         r.Contains(mMouseX, mMouseY) &&
         (mMouseX!=0) &&
         (mMouseX!=r.width-1)
      ) {
         auto calcXPosFromMouseX = [&r, &width, &hNumberScale = this->hNumberScale](
            int mouseX
         ) -> float {
            float relativeMouseX = float(mouseX - (r.x + 1)) / float(width);
            return hNumberScale.PositionToValue(relativeMouseX);
         };

         /// Frequency of the mouse pixel
         mCursorXLeft = calcXPosFromMouseX(mMouseX);
         /// Frequency at 1 pixel to the right
         mCursorXRight = calcXPosFromMouseX(mMouseX + 1);
      } else {
         mCursorXLeft = NO_CURSOR;
         mCursorXRight = NO_CURSOR;
      }

      mFreqPlot->Refresh(false);
   }
}

void FrequencyPlotDialog::OnPanScroller(wxScrollEvent & WXUNUSED(event))
{
   DrawPlot();
}

void FrequencyPlotDialog::OnZoomSlider(wxCommandEvent & WXUNUSED(event))
{
   DrawPlot();
}

void FrequencyPlotDialog::OnAlgChoice(wxCommandEvent & WXUNUSED(event))
{
   mAlg = SpectrumAnalyst::Algorithm(mAlgChoice->GetSelection());

   // Log-frequency axis works for spectrum plots only.
   if (mAlg == SpectrumAnalyst::Spectrum) {
      mAxisChoice->Enable(true);
      mLogAxis = mAxisChoice->GetSelection() ? true : false;
   }
   else {
      mAxisChoice->Disable();
      mLogAxis = false;
   }

   SendRecalcEvent();
}

void FrequencyPlotDialog::OnSizeChoice(wxCommandEvent & WXUNUSED(event))
{
   long windowSize = 0;
   mSizeChoice->GetStringSelection().ToLong(&windowSize);
   mWindowSize = windowSize;

   SendRecalcEvent();
}

void FrequencyPlotDialog::OnFuncChoice(wxCommandEvent & WXUNUSED(event))
{
   SendRecalcEvent();
}

void FrequencyPlotDialog::OnAxisChoice(wxCommandEvent & WXUNUSED(event))
{
   mLogAxis = mAxisChoice->GetSelection() ? true : false;
   DrawPlot();
}

void FrequencyPlotDialog::PlotPaint(wxPaintEvent & event)
{
   wxPaintDC dc( (wxWindow *) event.GetEventObject() );

   dc.DrawBitmap( *mBitmap, 0, 0, true );
   // Fix for Bug 1226 "Plot Spectrum freezes... if insufficient samples selected"
   if (!mData || mDataLen < mWindowSize)
      return;

   dc.SetFont(mFreqFont);

   wxRect r = mPlotRect;

   int width = r.width - 2;

   // Find the peak nearest the cursor and plot it
   if ( mCursorXLeft != NO_CURSOR ) {
      /// Frequency of the mouse pixel
      float xPos = mCursorXLeft;
      /// Frequency at 1 pixel to the right
      float xPosNext = mCursorXRight;

      float peakAmplitude = 0;
      float peakPos = mAnalyst->FindPeak(xPos, &peakAmplitude);

      float relativePeakX = hNumberScale.ValueToPosition(peakPos);
      int peakX = int(float(width) * relativePeakX);

      dc.SetPen(wxPen(wxColour(160,160,160), 1, wxPENSTYLE_SOLID));
      AColor::Line(dc, r.x + 1 + peakX, r.y, r.x + 1 + peakX, r.y + r.height);

       // print out info about the cursor location

      float value = mAnalyst->GetProcessedValue(xPos, xPosNext);

      TranslatableString cursor;
      TranslatableString peak;

      if (mAlg == SpectrumAnalyst::Spectrum) {
         auto xp = PitchName_Absolute(FreqToMIDInote(xPos));
         auto pp = PitchName_Absolute(FreqToMIDInote(peakPos));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#*/
         cursor = XO("%d Hz (%s) = %d dB")
            .Format( (int)(xPos + 0.5), xp, (int)(value + 0.5));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#*/
         peak = XO("%d Hz (%s) = %.1f dB")
            .Format( (int)(peakPos + 0.5), pp, peakAmplitude );
      } else if (xPos > 0.0 && peakPos > 0.0) {
         auto xp = PitchName_Absolute(FreqToMIDInote(1.0 / xPos));
         auto pp = PitchName_Absolute(FreqToMIDInote(1.0 / peakPos));
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#
          * the %.4f are numbers, and 'sec' should be an abbreviation for seconds */
         cursor = XO("%.4f sec (%d Hz) (%s) = %f")
            .Format( xPos, (int)(1.0 / xPos + 0.5), xp, value );
         /* i18n-hint: The %d's are replaced by numbers, the %s by musical notes, e.g. A#
          * the %.4f are numbers, and 'sec' should be an abbreviation for seconds */
         peak = XO("%.4f sec (%d Hz) (%s) = %.3f")
            .Format( peakPos, (int)(1.0 / peakPos + 0.5), pp, peakAmplitude );
      }
      mCursorText->SetValue( cursor.Translation() );
      mPeakText->SetValue( peak.Translation() );
   }
   else {
      mCursorText->SetValue(wxT(""));
      mPeakText->SetValue(wxT(""));
   }


   // Outline the graph
   dc.SetPen(*wxBLACK_PEN);
   dc.SetBrush(*wxTRANSPARENT_BRUSH);
   dc.DrawRectangle(r);
}

void FrequencyPlotDialog::OnCloseWindow(wxCloseEvent & WXUNUSED(event))
{
   Show(false);
}

void FrequencyPlotDialog::OnCloseButton(wxCommandEvent & WXUNUSED(event))
{
   gPrefs->Write(wxT("/FrequencyPlotDialog/DrawGrid"), mDrawGrid);
   gPrefs->Write(wxT("/FrequencyPlotDialog/SizeChoice"), mSizeChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/AlgChoice"), mAlgChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/FuncChoice"), mFuncChoice->GetSelection());
   gPrefs->Write(wxT("/FrequencyPlotDialog/AxisChoice"), mAxisChoice->GetSelection());
   gPrefs->Flush();
   Show(false);
}

void FrequencyPlotDialog::SendRecalcEvent()
{
   wxCommandEvent e(EVT_FREQWINDOW_RECALC, wxID_ANY);
   GetEventHandler()->AddPendingEvent(e);
}

void FrequencyPlotDialog::Recalc()
{
   if (!mData || mDataLen < mWindowSize) {
      DrawPlot();
      return;
   }

   SpectrumAnalyst::Algorithm alg =
      SpectrumAnalyst::Algorithm(mAlgChoice->GetSelection());
   int windowFunc = mFuncChoice->GetSelection();

   wxWindow *hadFocus = FindFocus();
   // In wxMac, the skipped window MUST be a top level window.  I'd originally made it
   // just the mProgress window with the idea of preventing user interaction with the
   // controls while the plot was being recalculated.  This doesn't appear to be necessary
   // so just use the top level window instead.
   {
      std::optional<wxWindowDisabler> blocker;
      if (IsShown())
         blocker.emplace(this);
      wxYieldIfNeeded();

      mAnalyst->Calculate(alg, windowFunc, mWindowSize, mRate,
         mData.get(), mDataLen,
         &mYMin, &mYMax, mProgress);
   }
   if (hadFocus) {
      hadFocus->SetFocus();
   }

   if (alg == SpectrumAnalyst::Spectrum) {
      if(mYMin < -dBRange)
         mYMin = -dBRange;
      if(mYMax <= -dBRange)
         mYMax = -dBRange + 10.; // it's all out of range, but show a scale.
      else
         mYMax += .5;
   }

   // Prime the scrollbar
   vPanScroller->SetScrollbar(0, (mYMax - mYMin) * 100, (mYMax - mYMin) * 100, 1);

   DrawPlot();
}

void FrequencyPlotDialog::OnExport(wxCommandEvent & WXUNUSED(event))
{
   wxString fName = _("spectrum.txt");

   fName = SelectFile(FileNames::Operation::Export,
      XO("Export Spectral Data As:"),
      wxEmptyString,
      fName,
      wxT("txt"),
      { FileNames::TextFiles, FileNames::AllFiles },
      wxFD_SAVE | wxRESIZE_BORDER,
      this);

   if (fName.empty())
      return;

   wxFFileOutputStream ffStream{ fName };
   if (!ffStream.IsOk()) {
      AudacityMessageBox( XO("Couldn't write to file: %s").Format( fName ) );
      return;
   }

  wxTextOutputStream ss(ffStream);

   const int processedSize = mAnalyst->GetProcessedSize();
   const float *const processed = mAnalyst->GetProcessed();
   if (mAlgChoice->GetSelection() == 0) {
      ss
         << XO("Frequency (Hz)\tLevel (dB)") << '\n';
      for (int i = 1; i < processedSize; i++)
         ss
            << wxString::Format(wxT("%f\t%f\n"),
               i * mRate / mWindowSize, processed[i] );
   }
   else {
      ss
         << XO("Lag (seconds)\tFrequency (Hz)\tLevel") << '\n';
      for (int i = 1; i < processedSize; i++)
         ss
            << wxString::Format(wxT("%f\t%f\t%f\n"),
               i / mRate, mRate / i, processed[i] );
   }
}

void FrequencyPlotDialog::OnReplot(wxCommandEvent & WXUNUSED(event))
{
   dBRange = DecibelScaleCutoff.Read();
   if(dBRange < 90.)
      dBRange = 90.;
   GetAudio();
   SendRecalcEvent();
}

void FrequencyPlotDialog::OnGridOnOff(wxCommandEvent & WXUNUSED(event))
{
   mDrawGrid = mGridOnOff->IsChecked();

   DrawPlot();
}

void FrequencyPlotDialog::OnRecalc(wxCommandEvent & WXUNUSED(event))
{
   Recalc();
}

void FrequencyPlotDialog::UpdatePrefs()
{
   bool shown = IsShown();
   if (shown) {
      Show(false);
   }

   auto zoomSlider = vZoomSlider->GetValue();
   auto drawGrid = mGridOnOff->GetValue();
   auto sizeChoice = mSizeChoice->GetStringSelection();
   auto algChoice = mAlgChoice->GetSelection();
   auto funcChoice = mFuncChoice->GetSelection();
   auto axisChoice = mAxisChoice->GetSelection();

   SetSizer(nullptr);
   DestroyChildren();

   Populate();

   vZoomSlider->SetValue(zoomSlider);

   mDrawGrid = drawGrid;
   mGridOnOff->SetValue(drawGrid);

   long windowSize = 0;
   sizeChoice.ToLong(&windowSize);
   mWindowSize = windowSize;
   mSizeChoice->SetStringSelection(sizeChoice);

   mAlg = static_cast<SpectrumAnalyst::Algorithm>(algChoice);
   mAlgChoice->SetSelection(algChoice);

   mFunc = funcChoice;
   mFuncChoice->SetSelection(funcChoice);

   mAxis = axisChoice;
   mAxisChoice->SetSelection(axisChoice);

   if (shown) {
      Show(true);
   }
}

BEGIN_EVENT_TABLE(FreqPlot, wxWindow)
   EVT_PAINT(FreqPlot::OnPaint)
   EVT_MOUSE_EVENTS(FreqPlot::OnMouseEvent)
END_EVENT_TABLE()

FreqPlot::FreqPlot(wxWindow *parent, wxWindowID winid)
:  wxWindow(parent, winid)
{
   SetBackgroundStyle(wxBG_STYLE_SYSTEM);
   freqWindow = (FrequencyPlotDialog *) parent;
}

bool FreqPlot::AcceptsFocus() const
{
   return false;
}

void FreqPlot::OnPaint(wxPaintEvent & evt)
{
   freqWindow->PlotPaint(evt);
}

void FreqPlot::OnMouseEvent(wxMouseEvent & event)
{
   freqWindow->PlotMouseEvent(event);
}

// Remaining code hooks this add-on into the application
#include "commands/CommandContext.h"
#include "commands/CommandManager.h"
#include "ProjectWindows.h"

namespace {

AttachedWindows::RegisteredFactory sFrequencyWindowKey{
   []( TenacityProject &parent ) -> wxWeakRef< wxWindow > {
      auto &window = ProjectWindow::Get( parent );
      return safenew FrequencyPlotDialog(
         &window, -1, parent, FrequencyAnalysisTitle,
         wxPoint{ 150, 150 }
      );
   }
};

// Define our extra menu item that invokes that factory
struct Handler : CommandHandlerObject {
   void OnPlotSpectrum(const CommandContext &context)
   {
      auto &project = context.project;
      CommandManager::Get(project).RegisterLastAnalyzer(context);  //Register Plot Spectrum as Last Analyzer
      auto freqWindow = &GetAttachedWindows(project)
         .Get< FrequencyPlotDialog >( sFrequencyWindowKey );

      freqWindow->Show(true);
      freqWindow->Raise();
      freqWindow->SetFocus();
   }
};

CommandHandlerObject &findCommandHandler(TenacityProject &) {
   // Handler is not stateful.  Doesn't need a factory registered with
   // TenacityProject.
   static Handler instance;
   return instance;
}

// Register that menu item

using namespace MenuTable;
AttachedItem sAttachment{ wxT("Analyze/Analyzers/Windows"),
   ( FinderScope{ findCommandHandler },
      Command( wxT("PlotSpectrum"), XXO("Plot Spectrum..."),
         &Handler::OnPlotSpectrum,
         AudioIONotBusyFlag() | WaveTracksSelectedFlag() | TimeSelectedFlag() ) )
};

}

