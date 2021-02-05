/**********************************************************************

Audacity: A Digital Audio Editor

ProjectSettings.cpp

Paul Licameli split from TenacityProject.cpp

**********************************************************************/

#include "ProjectSettings.h"

// Tenacity libraries
#include <lib-xml/XMLWriter.h>

#include "AudioIOBase.h"
#include "Project.h"
#include "prefs/QualitySettings.h"
#include "prefs/TracksBehaviorsPrefs.h"
#include "widgets/NumericTextCtrl.h"

wxDEFINE_EVENT(EVT_PROJECT_SETTINGS_CHANGE, wxCommandEvent);

namespace {
   void Notify( TenacityProject &project, ProjectSettings::EventCode code )
   {
      wxCommandEvent e{ EVT_PROJECT_SETTINGS_CHANGE };
      e.SetInt( static_cast<int>( code ) );
      project.ProcessEvent( e );
   }
}

static const TenacityProject::AttachedObjects::RegisteredFactory
sProjectSettingsKey{
  []( TenacityProject &project ){
     auto result = std::make_shared< ProjectSettings >( project );
     return result;
   }
};

ProjectSettings &ProjectSettings::Get( TenacityProject &project )
{
   return project.AttachedObjects::Get< ProjectSettings >(
      sProjectSettingsKey );
}

const ProjectSettings &ProjectSettings::Get( const TenacityProject &project )
{
   return Get( const_cast< TenacityProject & >( project ) );
}

ProjectSettings::ProjectSettings(TenacityProject &project)
   : mProject{ project }
   , mSelectionFormat{ NumericTextCtrl::LookupFormat(
      NumericConverter::TIME,
      gPrefs->Read(wxT("/SelectionFormat"), wxT("")))
}
, mAudioTimeFormat{ NumericTextCtrl::LookupFormat(
   NumericConverter::TIME,
   gPrefs->Read(wxT("/AudioTimeFormat"), wxT("hh:mm:ss")))
}
, mFrequencySelectionFormatName{ NumericTextCtrl::LookupFormat(
   NumericConverter::FREQUENCY,
   gPrefs->Read(wxT("/FrequencySelectionFormatName"), wxT("")) )
}
, mBandwidthSelectionFormatName{ NumericTextCtrl::LookupFormat(
   NumericConverter::BANDWIDTH,
   gPrefs->Read(wxT("/BandwidthSelectionFormatName"), wxT("")) )
}
, mSnapTo( gPrefs->Read(wxT("/SnapTo"), SNAP_OFF) )
{
   gPrefs->Read(wxT("/GUI/SyncLockTracks"), &mIsSyncLocked, false);

   bool multiToolActive = false;
   gPrefs->Read(wxT("/GUI/ToolBars/Tools/MultiToolActive"), &multiToolActive);

   if (multiToolActive)
      mCurrentTool = ToolCodes::multiTool;
   else
      mCurrentTool = ToolCodes::selectTool;

   UpdatePrefs();
}

void ProjectSettings::UpdatePrefs()
{
   gPrefs->Read(wxT("/AudioFiles/ShowId3Dialog"), &mShowId3Dialog, true);
   gPrefs->Read(wxT("/GUI/EmptyCanBeDirty"), &mEmptyCanBeDirty, true);
   gPrefs->Read(wxT("/GUI/ShowSplashScreen"), &mShowSplashScreen, true);
   mSoloPref = TracksBehaviorsSolo.Read();
   // Update the old default to the NEW default.
   if (mSoloPref == wxT("Standard"))
      mSoloPref = wxT("Simple");
   gPrefs->Read(wxT("/GUI/TracksFitVerticallyZoomed"),
      &mTracksFitVerticallyZoomed, false);
   //   gPrefs->Read(wxT("/GUI/UpdateSpectrogram"),
   //     &mViewInfo.bUpdateSpectrogram, true);

   // This code to change an empty projects rate is currently disabled, after
   // discussion.  The rule 'Default sample rate' only affects newly created
   // projects was felt to be simpler and better.
#if 0
   // The DefaultProjectSample rate is the rate for new projects.
   // Do not change this project's rate, unless there are no tracks.
   if( TrackList::Get( *this ).size() == 0){
      mRate = QualityDefaultSampleRate.Read();
      // If necessary, we change this rate in the selection toolbar too.
      auto bar = SelectionBar::Get( *this );
      bar.SetRate( mRate );
   }
#endif
}

const NumericFormatSymbol &
ProjectSettings::GetFrequencySelectionFormatName() const
{
   return mFrequencySelectionFormatName;
}

void ProjectSettings::SetFrequencySelectionFormatName(
   const NumericFormatSymbol & formatName)
{
   mFrequencySelectionFormatName = formatName;
}

const NumericFormatSymbol &
ProjectSettings::GetBandwidthSelectionFormatName() const
{
   return mBandwidthSelectionFormatName;
}

void ProjectSettings::SetBandwidthSelectionFormatName(
   const NumericFormatSymbol & formatName)
{
   mBandwidthSelectionFormatName = formatName;
}

void ProjectSettings::SetSelectionFormat(const NumericFormatSymbol & format)
{
   mSelectionFormat = format;
}

const NumericFormatSymbol & ProjectSettings::GetSelectionFormat() const
{
   return mSelectionFormat;
}

void ProjectSettings::SetAudioTimeFormat(const NumericFormatSymbol & format)
{
   mAudioTimeFormat = format;
}

const NumericFormatSymbol & ProjectSettings::GetAudioTimeFormat() const
{
   return mAudioTimeFormat;
}

void ProjectSettings::SetSnapTo(int snap)
{
   mSnapTo = snap;
}
   
int ProjectSettings::GetSnapTo() const
{
   return mSnapTo;
}

bool ProjectSettings::IsSyncLocked() const
{
#ifdef EXPERIMENTAL_SYNC_LOCK
   return mIsSyncLocked;
#else
   return false;
#endif
}

void ProjectSettings::SetSyncLock(bool flag)
{
   auto &project = mProject;
   if (flag != mIsSyncLocked) {
      mIsSyncLocked = flag;
      Notify( project, ChangedSyncLock );
   }
}

static ProjectFileIORegistry::WriterEntry entry {
[](const TenacityProject &project, XMLWriter &xmlFile){
   auto &settings = ProjectSettings::Get(project);
   xmlFile.WriteAttr(wxT("snapto"), settings.GetSnapTo() ? wxT("on") : wxT("off"));
   xmlFile.WriteAttr(wxT("selectionformat"),
                     settings.GetSelectionFormat().Internal());
   xmlFile.WriteAttr(wxT("frequencyformat"),
                     settings.GetFrequencySelectionFormatName().Internal());
   xmlFile.WriteAttr(wxT("bandwidthformat"),
                     settings.GetBandwidthSelectionFormatName().Internal());
}
};

static ProjectFileIORegistry::AttributeReaderEntries entries {
// Just a pointer to function, but needing overload resolution as non-const:
(ProjectSettings& (*)(TenacityProject &)) &ProjectSettings::Get, {
   // PRL:  The following have persisted as per-project settings for long.
   // Maybe that should be abandoned.  Enough to save changes in the user
   // preference file.
   { L"snapto", [](auto &settings, auto value){
      settings.SetSnapTo(wxString(value) == wxT("on") ? true : false);
   } },
   { L"selectionformat", [](auto &settings, auto value){
      settings.SetSelectionFormat(
         NumericConverter::LookupFormat( NumericConverter::TIME, value) );
   } },
   { L"frequencyformat", [](auto &settings, auto value){
      settings.SetFrequencySelectionFormatName(
         NumericConverter::LookupFormat( NumericConverter::FREQUENCY, value ) );
   } },
   { L"bandwidthformat", [](auto &settings, auto value){
      settings.SetBandwidthSelectionFormatName(
         NumericConverter::LookupFormat( NumericConverter::BANDWIDTH, value ) );
   } },
} };
