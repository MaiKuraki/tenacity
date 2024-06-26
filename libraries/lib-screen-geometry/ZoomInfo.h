/**********************************************************************

  Audacity: A Digital Audio Editor

  ZoomInfo.h

  Paul Licameli split from ViewInfo.h

**********************************************************************/

#ifndef __AUDACITY_ZOOM_INFO__
#define __AUDACITY_ZOOM_INFO__

// Tenacity libraries
#include "Prefs.h" // to inherit
#include "ClientData.h" // to inherit

#ifdef __GNUC__
#define CONST
#else
#define CONST const
#endif

class TenacityProject;

// See big pictorial comment in TrackPanel.cpp for explanation of these numbers
enum : int {
   // Constants related to x coordinates in the track panel
   kBorderThickness = 1,
   kShadowThickness = 0,

   kLeftInset = 4,
   kRightInset = kLeftInset,
   kLeftMargin = kLeftInset + kBorderThickness,
   kRightMargin = kRightInset + kShadowThickness + kBorderThickness,

   kTrackInfoWidth = 100 - kLeftMargin,
};

// The subset of ViewInfo information (other than selection)
// that is sufficient for purposes of TrackArtist,
// and for computing conversions between track times and pixel positions.
class SCREEN_GEOMETRY_API ZoomInfo /* not final */
   // Note that ViewInfo inherits from ZoomInfo but there are no virtual functions.
   // That's okay if we pass always by reference and never copy, suffering "slicing."
   : public ClientData::Base
   , protected PrefsListener
{
public:
   ZoomInfo(double start, double pixelsPerSecond);
   ~ZoomInfo();

   // Be sure we don't slice
   ZoomInfo(const ZoomInfo&) = delete;
   ZoomInfo& operator= (const ZoomInfo&) = delete;

   void UpdatePrefs() override;

   int vpos;                    // vertical scroll pos

   double h;                    // h pos in secs

protected:
   double zoom;                 // pixels per second

public:
   float dBr;                   // decibel scale range

   // do NOT use this once to convert a pixel width to a duration!
   // Instead, call twice to convert start and end times,
   // and take the difference.
   // origin specifies the pixel corresponding to time h
   double PositionToTime(long long position, long long origin = 0 ) const;

   // do NOT use this once to convert a duration to a pixel width!
   // Instead, call twice to convert start and end positions,
   // and take the difference.
   // origin specifies the pixel corresponding to time h
   long long TimeToPosition(double time, long long origin = 0) const;

   // You should prefer to call TimeToPosition twice, for endpoints, and take the difference!
   double TimeRangeToPixelWidth(double timeRange) const;

   double OffsetTimeByPixels(double time, long long offset) const
   {
      return PositionToTime(offset + TimeToPosition(time));
   }

   int GetWidth() const { return mWidth; }
   void SetWidth( int width ) { mWidth = width; }

   int GetVRulerWidth() const { return mVRulerWidth; }
   void SetVRulerWidth( int width ) { mVRulerWidth = width; }
   int GetVRulerOffset() const { return kTrackInfoWidth + kLeftMargin; }
   int GetLabelWidth() const { return GetVRulerOffset() + GetVRulerWidth(); }
   int GetLeftOffset() const { return GetLabelWidth() + 1;}

   int GetTracksUsableWidth() const
   {
      return
         std::max( 0, GetWidth() - ( GetLeftOffset() + kRightMargin ) );
   }

   // Returns the time corresponding to the pixel column one past the track area
   double GetScreenEndTime() const
   {
      auto width = GetTracksUsableWidth();
      return PositionToTime(width, 0);
   }

   bool ZoomInAvailable() const;
   bool ZoomOutAvailable() const;

   static double GetDefaultZoom()
   { return 44100.0 / 512.0; }


   // Limits zoom to certain bounds
   void SetZoom(double pixelsPerSecond);

   // This function should not be used to convert positions to times and back
   // Use TimeToPosition and PositionToTime and OffsetTimeByPixels instead
   double GetZoom() const;

   static double GetMaxZoom( );
   static double GetMinZoom( );

   // Limits zoom to certain bounds
   // multipliers above 1.0 zoom in, below out
   void ZoomBy(double multiplier);

   struct Interval {
      CONST long long position; CONST double averageZoom;
      Interval(long long p, double z, bool i)
         : position(p), averageZoom(z) {}
   };
   typedef std::vector<Interval> Intervals;

   // Find an increasing sequence of pixel positions.  Each is the start
   // of an interval, or is the end position.
   // Each of the disjoint intervals should be drawn
   // separately.
   // It is guaranteed that there is at least one entry and the position of the
   // first entry equals origin.
   // @param origin specifies the pixel position corresponding to time ViewInfo::h.
   void FindIntervals
      (double rate, Intervals &results, long long width, long long origin = 0) const;

   int mWidth{ 0 };
   int mVRulerWidth{ 36 };
};

#endif
