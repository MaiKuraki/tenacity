/**********************************************************************

Audacity: A Digital Audio Editor

ProjectStatus.h

Paul Licameli

**********************************************************************/

#include "ProjectStatus.h"

#include "Project.h"

static const TenacityProject::AttachedObjects::RegisteredFactory key{
  []( TenacityProject &parent ){
     return std::make_shared< ProjectStatus >( parent );
   }
};

ProjectStatus &ProjectStatus::Get( TenacityProject &project )
{
   return project.AttachedObjects::Get< ProjectStatus >( key );
}

const ProjectStatus &ProjectStatus::Get( const TenacityProject &project )
{
   return Get( const_cast< TenacityProject & >( project ) );
}

ProjectStatus::ProjectStatus( TenacityProject &project )
   : mProject{ project }
{
}

ProjectStatus::~ProjectStatus() = default;

namespace
{
   ProjectStatus::StatusWidthFunctions &statusWidthFunctions()
   {
      static ProjectStatus::StatusWidthFunctions theFunctions;
      return theFunctions;
   }
}

ProjectStatus::RegisteredStatusWidthFunction::RegisteredStatusWidthFunction(
   const StatusWidthFunction &function )
{
   statusWidthFunctions().emplace_back( function );
}

auto ProjectStatus::GetStatusWidthFunctions() -> const StatusWidthFunctions &
{
   return statusWidthFunctions();
}

const TranslatableString &ProjectStatus::Get( StatusBarField field ) const
{
   return mLastStatusMessages[ field - 1 ];
}

void ProjectStatus::Set(const TranslatableString &msg, StatusBarField field )
{
   auto &project = mProject;
   auto &lastMessage = mLastStatusMessages[ field - 1 ];
   // compare full translations not msgids!
   if ( msg.Translation() != lastMessage.Translation() ) {
      lastMessage = msg;
      Publish(field);
   }
}

void ProjectStatus::UpdatePrefs()
{
   auto &project = mProject;
   for (auto field = 1; field <= nStatusBarFields; ++field)
      Publish(static_cast<StatusBarField>(field));
}
