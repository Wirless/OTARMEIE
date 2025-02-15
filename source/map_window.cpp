//////////////////////////////////////////////////////////////////////
// This file is part of Remere's Map Editor
//////////////////////////////////////////////////////////////////////
// Remere's Map Editor is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Remere's Map Editor is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//////////////////////////////////////////////////////////////////////

#include "main.h"

#include "map_window.h"
#include "gui.h"
#include "sprites.h"
#include "editor.h"
#include "render/debug_renderer.h"

BEGIN_EVENT_TABLE(MapWindow, wxPanel)
	EVT_SIZE(MapWindow::OnSize)
	EVT_SCROLL(MapWindow::OnScroll)
END_EVENT_TABLE()

MapWindow::MapWindow(wxWindow* parent, Editor& editor) :
	wxPanel(parent, PANE_MAIN),
	editor(editor),
	replaceItemsDialog(nullptr),
	render_pool(std::make_unique<MapRenderPool>()) {
	int GL_settings[3];
	GL_settings[0] = WX_GL_RGBA;
	GL_settings[1] = WX_GL_DOUBLEBUFFER;
	GL_settings[2] = 0;
	canvas = newd MapCanvas(this, editor, GL_settings);

	vScroll = newd MapScrollBar(this, MAP_WINDOW_VSCROLL, wxVERTICAL, canvas);
	hScroll = newd MapScrollBar(this, MAP_WINDOW_HSCROLL, wxHORIZONTAL, canvas);

	gem = newd DCButton(this, MAP_WINDOW_GEM, wxDefaultPosition, DC_BTN_NORMAL, RENDER_SIZE_16x16, EDITOR_SPRITE_SELECTION_GEM);

	wxFlexGridSizer* topsizer = newd wxFlexGridSizer(2, 0, 0);

	topsizer->AddGrowableCol(0);
	topsizer->AddGrowableRow(0);

	topsizer->Add(canvas, wxSizerFlags(1).Expand());
	topsizer->Add(vScroll, wxSizerFlags(1).Expand());
	topsizer->Add(hScroll, wxSizerFlags(1).Expand());
	topsizer->Add(gem, wxSizerFlags(1));

	SetSizerAndFit(topsizer);

	// Initialize viewport
	UpdateViewport();
	
	// Initial segment creation
	UpdateVisibleSegments();
}

MapWindow::~MapWindow() {
	////
}

void MapWindow::ShowReplaceItemsDialog(bool selectionOnly) {
	if (replaceItemsDialog) {
		return;
	}

	replaceItemsDialog = new ReplaceItemsDialog(this, selectionOnly);
	replaceItemsDialog->Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(MapWindow::OnReplaceItemsDialogClose), NULL, this);
	replaceItemsDialog->Show();
}

void MapWindow::CloseReplaceItemsDialog() {
	if (replaceItemsDialog) {
		replaceItemsDialog->Close();
	}
}

void MapWindow::OnReplaceItemsDialogClose(wxCloseEvent& event) {
	if (replaceItemsDialog) {
		replaceItemsDialog->Disconnect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(MapWindow::OnReplaceItemsDialogClose), NULL, this);
		replaceItemsDialog->Destroy();
		replaceItemsDialog = nullptr;
	}
}

void MapWindow::SetSize(int x, int y, bool center) {
	if (x == 0 || y == 0) {
		return;
	}

	int windowSizeX;
	int windowSizeY;

	canvas->GetSize(&windowSizeX, &windowSizeY);

	hScroll->SetScrollbar(center ? (x - windowSizeX) / 2 : hScroll->GetThumbPosition(), windowSizeX / x, x, windowSizeX / x);
	vScroll->SetScrollbar(center ? (y - windowSizeY) / 2 : vScroll->GetThumbPosition(), windowSizeY / y, y, windowSizeX / y);
	// wxPanel::SetSize(x, y);
}

void MapWindow::UpdateScrollbars(int nx, int ny) {
	// nx and ny are size of this window
	hScroll->SetScrollbar(hScroll->GetThumbPosition(), nx / max(1, hScroll->GetRange()), max(1, hScroll->GetRange()), 96);
	vScroll->SetScrollbar(vScroll->GetThumbPosition(), ny / max(1, vScroll->GetRange()), max(1, vScroll->GetRange()), 96);
}

void MapWindow::UpdateDialogs(bool show) {
	if (replaceItemsDialog) {
		replaceItemsDialog->Show(show);
	}
}

void MapWindow::GetViewStart(int* x, int* y) {
	*x = hScroll->GetThumbPosition();
	*y = vScroll->GetThumbPosition();
}

void MapWindow::GetViewSize(int* x, int* y) {
	canvas->GetSize(x, y);
	*x *= canvas->GetContentScaleFactor();
	*y *= canvas->GetContentScaleFactor();
}

void MapWindow::FitToMap() {
	SetSize(editor.map.getWidth() * TileSize, editor.map.getHeight() * TileSize, true);
}

Position MapWindow::GetScreenCenterPosition() {
	int x, y;
	canvas->GetScreenCenter(&x, &y);
	return Position(x, y, canvas->GetFloor());
}

void MapWindow::SetScreenCenterPosition(const Position& position) {
	if (position == Position()) {
		return;
	}

	int x = position.x * TileSize;
	int y = position.y * TileSize;
	if (position.z <= GROUND_LAYER) {
		// Compensate for floor offset above ground
		x -= (GROUND_LAYER - position.z) * TileSize;
		y -= (GROUND_LAYER - position.z) * TileSize;
	}

	const Position& center = GetScreenCenterPosition();
	if (previous_position != center) {
		previous_position.x = center.x;
		previous_position.y = center.y;
		previous_position.z = center.z;
	}

	Scroll(x, y, true);
	canvas->ChangeFloor(position.z);
}

void MapWindow::GoToPreviousCenterPosition() {
	SetScreenCenterPosition(previous_position);
}

void MapWindow::Scroll(int x, int y, bool center) {
	if (center) {
		int windowSizeX, windowSizeY;

		canvas->GetSize(&windowSizeX, &windowSizeY);
		x -= int((windowSizeX * g_gui.GetCurrentZoom()) / 2.0);
		y -= int((windowSizeY * g_gui.GetCurrentZoom()) / 2.0);
	}

	hScroll->SetThumbPosition(x);
	vScroll->SetThumbPosition(y);
	g_gui.UpdateMinimap();
}

void MapWindow::ScrollRelative(int x, int y) {
	hScroll->SetThumbPosition(hScroll->GetThumbPosition() + x);
	vScroll->SetThumbPosition(vScroll->GetThumbPosition() + y);
	g_gui.UpdateMinimap();
}

void MapWindow::OnGem(wxCommandEvent& WXUNUSED(event)) {
	g_gui.SwitchMode();
}

void MapWindow::OnSize(wxSizeEvent& event) {
	UpdateScrollbars(event.GetSize().GetWidth(), event.GetSize().GetHeight());
	event.Skip();
}

void MapWindow::OnScroll(wxScrollEvent& event) {
	Refresh();
}

void MapWindow::OnScrollLineDown(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(96, 0);
	} else {
		ScrollRelative(0, 96);
	}
	Refresh();
}

void MapWindow::OnScrollLineUp(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(-96, 0);
	} else {
		ScrollRelative(0, -96);
	}
	Refresh();
}

void MapWindow::OnScrollPageDown(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(5 * 96, 0);
	} else {
		ScrollRelative(0, 5 * 96);
	}
	Refresh();
}

void MapWindow::OnScrollPageUp(wxScrollEvent& event) {
	if (event.GetOrientation() == wxHORIZONTAL) {
		ScrollRelative(-5 * 96, 0);
	} else {
		ScrollRelative(0, -5 * 96);
	}
	Refresh();
}

void MapWindow::UpdateViewport() {
	// Get current view information from editor
	int center_x, center_y;
	g_gui.GetCurrentMapTab()->GetCanvas()->GetScreenCenter(&center_x, &center_y);
	
	current_viewport.floor = g_gui.GetCurrentFloor();
	current_viewport.start_x = (center_x - GetSize().GetWidth() / 2) / MapRenderPool::SEGMENT_SIZE;
	current_viewport.start_y = (center_y - GetSize().GetHeight() / 2) / MapRenderPool::SEGMENT_SIZE;
	current_viewport.end_x = (center_x + GetSize().GetWidth() / 2) / MapRenderPool::SEGMENT_SIZE + 1;
	current_viewport.end_y = (center_y + GetSize().GetHeight() / 2) / MapRenderPool::SEGMENT_SIZE + 1;
}

void MapWindow::UpdateVisibleSegments() {
	visible_segments.clear();
	
	for(int y = current_viewport.start_y; y <= current_viewport.end_y; ++y) {
		for(int x = current_viewport.start_x; x <= current_viewport.end_x; ++x) {
			auto segment = render_pool->CreateSegment(x, y, current_viewport.floor);
			segment->is_visible = true;
			visible_segments.push_back(segment);
		}
	}
	
	QueueVisibleSegments();
}

void MapWindow::QueueVisibleSegments() {
	for(auto& segment : visible_segments) {
		if(segment->is_dirty) {
			RenderTask task;
			task.segment = segment;
			task.priority = segment->is_visible ? 1 : 0;
			task.render_func = [this](RenderSegment& seg) {
				// Render the segment using the editor's tile data
				RenderSegmentTiles(seg);
			};
			render_pool->QueueRenderTask(task);
		}
	}
}

void MapWindow::RenderSegmentTiles(RenderSegment& segment) {
	// Create a bitmap for this segment
	wxBitmap bitmap(MapRenderPool::SEGMENT_SIZE, MapRenderPool::SEGMENT_SIZE);
	wxMemoryDC dc(bitmap);
	
	// Render all tiles in this segment
	for(int y = segment.start_y; y < segment.end_y; ++y) {
		for(int x = segment.start_x; x < segment.end_x; ++x) {
			Tile* tile = editor.map.getTile(x, y, segment.floor);
			if(tile) {
				g_gui.GetCurrentMapTab()->GetCanvas()->DrawTile(&dc, tile);
			}
		}
	}
	
	// Add debug visualization
	DebugRenderer::DrawSegmentDebug(dc, segment, segment.in_cache);
	
	segment.buffer = std::make_shared<wxBitmap>(bitmap);
	segment.is_dirty = false;
}

void MapWindow::OnPaint(wxPaintEvent& event) {
	wxPaintDC dc(this);
	
	// Draw performance metrics in top-left corner
	DebugRenderer::DrawPerformanceMetrics(dc, wxPoint(10, 10));
}
