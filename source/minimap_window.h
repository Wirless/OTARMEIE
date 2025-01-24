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

#ifndef RME_MINIMAP_WINDOW_H_
#define RME_MINIMAP_WINDOW_H_

#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>

class MinimapWindow : public wxPanel {
public:
	enum {
		ID_MINIMAP_UPDATE = 45000  // Choose a number that won't conflict with other IDs
	};

	MinimapWindow(wxWindow* parent);
	virtual ~MinimapWindow();

	void OnPaint(wxPaintEvent&);
	void OnEraseBackground(wxEraseEvent&) { }
	void OnMouseClick(wxMouseEvent&);
	void OnSize(wxSizeEvent&);
	void OnClose(wxCloseEvent&);

	void DelayedUpdate();
	void OnDelayedUpdate(wxTimerEvent& event);
	void OnKey(wxKeyEvent& event);

	static const int BLOCK_SIZE = 64;  // Same as OTClient
	
	struct MinimapBlock {
		wxBitmap bitmap;
		bool needsUpdate = true;
		bool wasSeen = false;
		int floor = -1;
		
		MinimapBlock() : needsUpdate(true), wasSeen(false) {}
	};
	
	using BlockPtr = std::shared_ptr<MinimapBlock>;
	using BlockMap = std::unordered_map<uint32_t, BlockPtr>;

private:
	BlockMap m_blocks;
	std::mutex m_mutex;
	
	// Helper methods
	uint32_t getBlockIndex(int x, int y) {
		return ((y / BLOCK_SIZE) * (65536 / BLOCK_SIZE)) + (x / BLOCK_SIZE);
	}
	
	wxPoint getBlockOffset(int x, int y) {
		return wxPoint(x - x % BLOCK_SIZE, y - y % BLOCK_SIZE);
	}
	
	BlockPtr getBlock(int x, int y);
	void updateBlock(BlockPtr block, int startX, int startY, int floor);

	wxBitmap buffer;
	std::mutex buffer_mutex;
	std::thread render_thread;
	std::atomic<bool> thread_running;
	std::atomic<bool> needs_update;
	
	void RenderThreadFunction();
	void StartRenderThread();
	void StopRenderThread();
	
	// Store last known state to detect changes
	int last_center_x;
	int last_center_y;
	int last_floor;

	wxPen* pens[256];
	wxTimer update_timer;
	int last_start_x;
	int last_start_y;

	DECLARE_EVENT_TABLE()
};

#endif
