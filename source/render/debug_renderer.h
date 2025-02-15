#ifndef RME_DEBUG_RENDERER_H_
#define RME_DEBUG_RENDERER_H_

#include <wx/dc.h>
#include "map_render_pool.h"

class DebugRenderer {
public:
    static void DrawSegmentDebug(wxDC& dc, const RenderSegment& segment, bool is_cached);
    static void DrawPerformanceMetrics(wxDC& dc, const wxPoint& position);
    
    // Debug visualization flags
    static bool show_segment_bounds;
    static bool show_segment_stats;
    static bool show_performance_metrics;
    
private:
    static wxColour GetSegmentColor(const RenderSegment& segment, bool is_cached);
    static void DrawSegmentStats(wxDC& dc, const RenderSegment& segment, const wxPoint& position);
};

#endif 