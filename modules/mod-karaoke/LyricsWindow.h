/**********************************************************************

  Audacity: A Digital Audio Editor

  LyricsWindow.h

  Vaughan Johnson
  Dominic Mazzoni

**********************************************************************/

#ifndef __AUDACITY_LYRICS_WINDOW__
#define __AUDACITY_LYRICS_WINDOW__

#include <wx/frame.h> // to inherit
#include <memory>

// Tenacity libraries
#include <lib-preferences/Prefs.h>
#include <lib-utility/Observer.h>

class TenacityProject;
class LyricsPanel;

class LyricsWindow final : public wxFrame,
                           public PrefsListener
{

public:
  LyricsWindow(TenacityProject *parent);

  LyricsPanel *GetLyricsPanel() { return mLyricsPanel; };

private:
  void OnCloseWindow(wxCloseEvent & /* event */);

  void OnStyle_BouncingBall(wxCommandEvent &evt);
  void OnStyle_Highlight(wxCommandEvent &evt);
  void OnTimer(Observer::Message);

  void SetWindowTitle();

  // PrefsListener implementation
  void UpdatePrefs() override;

  std::weak_ptr<TenacityProject> mProject;
  LyricsPanel *mLyricsPanel;
  Observer::Subscription mSubscription;
};

#endif
