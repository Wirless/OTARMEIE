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

#include "graphics.h"
#include "editor.h"
#include "map.h"

#include "gui.h"
#include "map_display.h"
#include "minimap_window.h"

#include <thread>
#include <mutex>
#include <atomic>

BEGIN_EVENT_TABLE(MinimapWindow, wxPanel)
EVT_LEFT_DOWN(MinimapWindow::OnMouseClick)
EVT_SIZE(MinimapWindow::OnSize)
EVT_PAINT(MinimapWindow::OnPaint)
EVT_ERASE_BACKGROUND(MinimapWindow::OnEraseBackground)
EVT_CLOSE(MinimapWindow::OnClose)
EVT_TIMER(wxID_ANY, MinimapWindow::OnDelayedUpdate)
EVT_KEY_DOWN(MinimapWindow::OnKey)
END_EVENT_TABLE()

MinimapWindow::MinimapWindow(wxWindow* parent) :
	wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(205, 130)),
	update_timer(this) ,
	thread_running(false),
	needs_update(true),
	last_center_x(0),
	last_center_y(0),
	last_floor(0),
	last_start_x(0),
	last_start_y(0)
	{
	for (int i = 0; i < 256; ++i) {
		pens[i] = newd wxPen(wxColor(minimap_color[i].red, minimap_color[i].green, minimap_color[i].blue));
	}
	
	// Bind the timer event
	Bind(wxEVT_TIMER, &MinimapWindow::OnDelayedUpdate, this);
	
	StartRenderThread();
}

MinimapWindow::~MinimapWindow() {
	StopRenderThread();
	for (int i = 0; i < 256; ++i) {
		delete pens[i];
	}
}

void MinimapWindow::StartRenderThread() {
	thread_running = true;
	render_thread = std::thread(&MinimapWindow::RenderThreadFunction, this);
}

void MinimapWindow::StopRenderThread() {
	thread_running = false;
	if(render_thread.joinable()) {
		render_thread.join();
	}
}

void MinimapWindow::RenderThreadFunction() {
	while(thread_running) {
		if(needs_update && g_gui.IsEditorOpen()) {
			Editor& editor = *g_gui.GetCurrentEditor();
			MapCanvas* canvas = g_gui.GetCurrentMapTab()->GetCanvas();
			
			int center_x, center_y;
			canvas->GetScreenCenter(&center_x, &center_y);
			int floor = g_gui.GetCurrentFloor();
			
			// Force update if floor changed
			if(floor != last_floor) {
				// Clear the buffer when floor changes
				std::lock_guard<std::mutex> lock(buffer_mutex);
				buffer = wxBitmap(GetSize().GetWidth(), GetSize().GetHeight());
				
				// Clear block cache
				std::lock_guard<std::mutex> blockLock(m_mutex);
				m_blocks.clear();
			}
			
			// Always update if floor changed or position changed
			if(center_x != last_center_x || 
			   center_y != last_center_y || 
			   floor != last_floor) {
				
				int window_width = GetSize().GetWidth();
				int window_height = GetSize().GetHeight();
				
				// Create temporary bitmap
				wxBitmap temp_buffer(window_width, window_height);
				wxMemoryDC dc(temp_buffer);
				
				dc.SetBackground(*wxBLACK_BRUSH);
				dc.Clear();
				
				int start_x = center_x - window_width / 2;
				int start_y = center_y - window_height / 2;
				
				// Batch drawing by color
				std::vector<std::vector<wxPoint>> colorPoints(256);
				
				for(int y = 0; y < window_height; ++y) {
					for(int x = 0; x < window_width; ++x) {
						int map_x = start_x + x;
						int map_y = start_y + y;
						
						if(map_x >= 0 && map_y >= 0 && 
						   map_x < editor.map.getWidth() && 
						   map_y < editor.map.getHeight()) {
							
							Tile* tile = editor.map.getTile(map_x, map_y, floor);
							if(tile) {
								uint8_t color = tile->getMiniMapColor();
								if(color) {
									colorPoints[color].push_back(wxPoint(x, y));
								}
							}
						}
					}
				}
				
				// Draw points by color
				for(int color = 0; color < 256; ++color) {
					if(!colorPoints[color].empty()) {
						dc.SetPen(*pens[color]);
						for(const wxPoint& pt : colorPoints[color]) {
							dc.DrawPoint(pt.x, pt.y);
						}
					}
				}
				
				// Update buffer safely
				{
					std::lock_guard<std::mutex> lock(buffer_mutex);
					buffer = temp_buffer;
				}
				
				// Store current state
				last_center_x = center_x;
				last_center_y = center_y;
				last_floor = floor;
				
				// Request refresh of the window
				wxCommandEvent evt(wxEVT_COMMAND_BUTTON_CLICKED);
				evt.SetId(ID_MINIMAP_UPDATE);
				wxPostEvent(this, evt);
			}
			
			needs_update = false;
		}
		
		// Sleep to prevent excessive CPU usage
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

void MinimapWindow::OnSize(wxSizeEvent& event) {
	Refresh();
}

void MinimapWindow::OnClose(wxCloseEvent&) {
	g_gui.DestroyMinimap();
}

void MinimapWindow::OnDelayedUpdate(wxTimerEvent& event) {
	needs_update = true;
}

void MinimapWindow::DelayedUpdate() {
	update_timer.Start(100, true);  // 100ms single-shot timer
}

void MinimapWindow::OnPaint(wxPaintEvent& event) {
	wxBufferedPaintDC dc(this);
	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();
	
	if (!g_gui.IsEditorOpen()) return;
	
	Editor& editor = *g_gui.GetCurrentEditor();
	MapCanvas* canvas = g_gui.GetCurrentMapTab()->GetCanvas();
	
	int centerX, centerY;
	canvas->GetScreenCenter(&centerX, &centerY);
	int floor = g_gui.GetCurrentFloor();
	
	// Trigger update if floor changed
	if (floor != last_floor) {
		needs_update = true;
		last_floor = floor;
		
		// Clear block cache when floor changes
		std::lock_guard<std::mutex> lock(m_mutex);
		m_blocks.clear();
	}

	int windowWidth = GetSize().GetWidth();
	int windowHeight = GetSize().GetHeight();
	
	// Calculate visible blocks
	int startBlockX = (centerX - windowWidth/2) / BLOCK_SIZE;
	int startBlockY = (centerY - windowHeight/2) / BLOCK_SIZE;
	int endBlockX = (centerX + windowWidth/2) / BLOCK_SIZE + 1;
	int endBlockY = (centerY + windowHeight/2) / BLOCK_SIZE + 1;
	
	// Draw visible blocks
	for (int by = startBlockY; by <= endBlockY; ++by) {
		for (int bx = startBlockX; bx <= endBlockX; ++bx) {
			BlockPtr block = getBlock(bx * BLOCK_SIZE, by * BLOCK_SIZE);
			if (block->needsUpdate) {
				updateBlock(block, bx * BLOCK_SIZE, by * BLOCK_SIZE, floor);
			}
			
			if (block->wasSeen) {
				int drawX = bx * BLOCK_SIZE - (centerX - windowWidth/2);
				int drawY = by * BLOCK_SIZE - (centerY - windowHeight/2);
				dc.DrawBitmap(block->bitmap, drawX, drawY, false);
			}
		}
	}
}

void MinimapWindow::OnMouseClick(wxMouseEvent& event) {
	if (!g_gui.IsEditorOpen())
		return;

	Editor& editor = *g_gui.GetCurrentEditor();
	MapCanvas* canvas = g_gui.GetCurrentMapTab()->GetCanvas();
	
	int centerX, centerY;
	canvas->GetScreenCenter(&centerX, &centerY);
	
	int windowWidth = GetSize().GetWidth();
	int windowHeight = GetSize().GetHeight();
	
	// Calculate start positions like the original
	last_start_x = centerX - (windowWidth / 2);
	last_start_y = centerY - (windowHeight / 2);
	
	// Use the original click handling
	int new_map_x = last_start_x + event.GetX();
	int new_map_y = last_start_y + event.GetY();
	
	g_gui.SetScreenCenterPosition(Position(new_map_x, new_map_y, g_gui.GetCurrentFloor()));
	Refresh();
	g_gui.RefreshView();
}

void MinimapWindow::OnKey(wxKeyEvent& event) {
	if (g_gui.GetCurrentTab() != nullptr) {
		g_gui.GetCurrentMapTab()->GetEventHandler()->AddPendingEvent(event);
	}
}

MinimapWindow::BlockPtr MinimapWindow::getBlock(int x, int y) {
	std::lock_guard<std::mutex> lock(m_mutex);
	uint32_t index = getBlockIndex(x, y);
	
	auto it = m_blocks.find(index);
	if (it == m_blocks.end()) {
		auto block = std::make_shared<MinimapBlock>();
		m_blocks[index] = block;
		return block;
	}
	return it->second;
}

void MinimapWindow::updateBlock(BlockPtr block, int startX, int startY, int floor) {
	Editor& editor = *g_gui.GetCurrentEditor();
	
	// Always update if the block's floor doesn't match current floor
	if (!block->needsUpdate && block->floor != floor) {
		block->needsUpdate = true;
	}
	
	if (!block->needsUpdate) return;
	
	wxBitmap bitmap(BLOCK_SIZE, BLOCK_SIZE);
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxBLACK_BRUSH);
	dc.Clear();
	
	// Store the floor this block was rendered for
	block->floor = floor;
	
	// Batch drawing by color like OTClient
	std::vector<std::vector<wxPoint>> colorPoints(256);
	
	for (int y = 0; y < BLOCK_SIZE; ++y) {
		for (int x = 0; x < BLOCK_SIZE; ++x) {
			int mapX = startX + x;
			int mapY = startY + y;
			
			Tile* tile = editor.map.getTile(mapX, mapY, floor);
			if (tile) {
				uint8_t color = tile->getMiniMapColor();
				if (color != 255) {  // Not transparent
					colorPoints[color].push_back(wxPoint(x, y));
				}
			}
		}
	}
	
	// Draw all points of same color at once
	for (int color = 0; color < 256; ++color) {
		if (!colorPoints[color].empty()) {
			dc.SetPen(*pens[color]);
			for (const wxPoint& pt : colorPoints[color]) {
				dc.DrawPoint(pt);
			}
		}
	}
	
	block->bitmap = bitmap;
	block->needsUpdate = false;
	block->wasSeen = true;
}

void MinimapWindow::ClearCache() {
	std::lock_guard<std::mutex> lock(buffer_mutex);
	buffer = wxBitmap(wxPanel::GetSize().GetWidth(), wxPanel::GetSize().GetHeight());
	
	std::lock_guard<std::mutex> blockLock(m_mutex);
	m_blocks.clear();
	needs_update = true;
}
