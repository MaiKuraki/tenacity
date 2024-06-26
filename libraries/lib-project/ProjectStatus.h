/**********************************************************************

Audacity: A Digital Audio Editor

ProjectStatus.h

Paul Licameli

**********************************************************************/

#ifndef __AUDACITY_PROJECT_STATUS__
#define __AUDACITY_PROJECT_STATUS__

#include <utility>
#include <vector>
#include "ClientData.h" // to inherit

// Tenacity libraries
#include "Prefs.h"
#include "Observer.h"

class TenacityProject;
class wxWindow;

enum StatusBarField : int {
   stateStatusBarField = 1,
   mainStatusBarField = 2,
   rateStatusBarField = 3,
   
   nStatusBarFields = 3
};

class PROJECT_API ProjectStatus final
   : public ClientData::Base
   , public PrefsListener
   , public Observer::Publisher<StatusBarField>
{
public:
   static ProjectStatus &Get( TenacityProject &project );
   static const ProjectStatus &Get( const TenacityProject &project );

   explicit ProjectStatus( TenacityProject &project );
   ProjectStatus( const ProjectStatus & ) = delete;
   ProjectStatus &operator= ( const ProjectStatus & ) = delete;
   ~ProjectStatus() override;

   // Type of a function to report translatable strings, and also report an extra
   // margin, to request that the corresponding field of the status bar should
   // be wide enough to contain any of those strings plus the margin.
   using StatusWidthResult = std::pair< std::vector<TranslatableString>, unsigned >;
   using StatusWidthFunction = std::function<
      StatusWidthResult( const TenacityProject &, StatusBarField )
   >;
   using StatusWidthFunctions = std::vector< StatusWidthFunction >;

   // Typically a static instance of this struct is used.
   struct PROJECT_API RegisteredStatusWidthFunction
   {
      explicit
      RegisteredStatusWidthFunction( const StatusWidthFunction &function );
   };

   static const StatusWidthFunctions &GetStatusWidthFunctions();

   const TranslatableString &Get( StatusBarField field = mainStatusBarField ) const;
   void Set(const TranslatableString &msg,
      StatusBarField field = mainStatusBarField);

   // PrefsListener implementation
   void UpdatePrefs() override;

private:
   TenacityProject &mProject;
   TranslatableString mLastStatusMessages[ nStatusBarFields ];
};

#endif
