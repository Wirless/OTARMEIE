#include "debug_renderer.h"
#include "performance_monitor.h"
#include <sstream>

bool DebugRenderer::show_segment_bounds = false;
bool DebugRenderer::show_segment_stats = false;
bool DebugRenderer::show_performance_metrics = false;

void DebugRenderer::DrawSegmentDebug(wxDC& dc, const RenderSegment& segment, bool is_cached) {
    if (!show_segment_bounds) return;
    
    wxPen pen(GetSegmentColor(segment, is_cached), 1, wxPENSTYLE_DOT);
    dc.SetPen(pen);
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    
    // Draw segment bounds
    dc.DrawRectangle(
        segment.start_x,
        segment.start_y,
        segment.end_x - segment.start_x,
        segment.end_y - segment.start_y
    );
    
    if (show_segment_stats) {
        DrawSegmentStats(dc, segment, wxPoint(segment.start_x + 5, segment.start_y + 5));
    }
}

wxColour DebugRenderer::GetSegmentColor(const RenderSegment& segment, bool is_cached) {
    if (segment.is_dirty) {
        return wxColour(255, 0, 0, 128);  // Red for dirty segments
    } else if (is_cached) {
        return wxColour(0, 255, 0, 128);  // Green for cached segments
    } else {
        return wxColour(0, 0, 255, 128);  // Blue for normal segments
    }
}

void DebugRenderer::DrawSegmentStats(wxDC& dc, const RenderSegment& segment, const wxPoint& position) {
    std::stringstream ss;
    ss << "Segment " << segment.start_x / MapRenderPool::SEGMENT_SIZE 
       << "," << segment.start_y / MapRenderPool::SEGMENT_SIZE
       << "\nFloor: " << segment.floor
       << "\nDirty: " << (segment.is_dirty ? "Yes" : "No");
    
    dc.SetTextForeground(wxColour(255, 255, 255));
    dc.DrawText(ss.str(), position.x, position.y);
}

void DebugRenderer::DrawPerformanceMetrics(wxDC& dc, const wxPoint& position) {
    if (!show_performance_metrics) return;
    
    auto& monitor = PerformanceMonitor::getInstance();
    std::stringstream ss;
    ss << "Avg Render Time: " << monitor.getAverageRenderTime() << "µs";
    
    dc.SetTextForeground(wxColour(255, 255, 255));
    dc.DrawText(ss.str(), position.x, position.y);
} 