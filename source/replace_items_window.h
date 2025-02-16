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

#ifndef RME_REPLACE_ITEMS_WINDOW_H_
#define RME_REPLACE_ITEMS_WINDOW_H_

#include "main.h"
#include "common_windows.h"
#include "editor.h"
#include <wx/tokenzr.h>

struct ReplacingItem {
	ReplacingItem() :
		replaceId(0), withId(0), total(0), complete(false) { }

	bool operator==(const ReplacingItem& other) const {
		return replaceId == other.replaceId && withId == other.withId;
	}

	uint16_t replaceId;
	uint16_t withId;
	uint32_t total;
	bool complete;
};

// ============================================================================
// ReplaceItemsButton

class ReplaceItemsButton : public DCButton {
public:
	ReplaceItemsButton(wxWindow* parent);
	~ReplaceItemsButton() { }

	ItemGroup_t GetGroup() const;
	uint16_t GetItemId() const {
		return m_id;
	}
	void SetItemId(uint16_t id);

private:
	uint16_t m_id;
};

// ============================================================================
// ReplaceItemsListBox

class ReplaceItemsListBox : public wxVListBox {
public:
	ReplaceItemsListBox(wxWindow* parent);

	bool AddItem(const ReplacingItem& item);
	void MarkAsComplete(const ReplacingItem& item, uint32_t total);
	void RemoveSelected();
	bool CanAdd(uint16_t replaceId, uint16_t withId) const;
	const std::vector<ReplacingItem>& GetItems() const { return m_items; }
	void Clear();

	void OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const override;
	wxCoord OnMeasureItem(size_t index) const override;

	size_t GetCount() const {
		return m_items.size();
	}

private:
	std::vector<ReplacingItem> m_items;
	wxBitmap m_arrow_bitmap;
	wxBitmap m_flag_bitmap;
};

// ============================================================================
// ReplaceItemsDialog

struct ItemFinder {
	ItemFinder(uint16_t itemid, int32_t limit = -1) :
		itemid(itemid), limit(limit), exceeded(false) { }

	void operator()(Map& map, Tile* tile, Item* item, long long done) {
		if (exceeded) {
			return;
		}

		if (item->getID() == itemid) {
			result.push_back(std::make_pair(tile, item));
			if (limit > 0 && result.size() >= size_t(limit)) {
				exceeded = true;
			}
		}
	}

	std::vector<std::pair<Tile*, Item*>> result;

private:
	uint16_t itemid;
	int32_t limit;
	bool exceeded;
};

class ReplaceItemsDialog : public wxDialog {
public:
	ReplaceItemsDialog(wxWindow* parent, bool selectionOnly);
	~ReplaceItemsDialog();

	void OnListSelected(wxCommandEvent& event);
	void OnReplaceItemClicked(wxMouseEvent& event);
	void OnWithItemClicked(wxMouseEvent& event);
	void OnAddButtonClicked(wxCommandEvent& event);
	void OnRemoveButtonClicked(wxCommandEvent& event);
	void OnExecuteButtonClicked(wxCommandEvent& event);
	void OnCancelButtonClicked(wxCommandEvent& event);
	void OnPresetButton(wxCommandEvent& event);
	void OnSavePreset(wxCommandEvent& event);
	void OnLoadPreset(wxCommandEvent& event);
	void OnManagePresets(wxCommandEvent& event);
	void OnSwapButtonClicked(wxCommandEvent& event);
	void OnSwapCheckboxClicked(wxCommandEvent& event);
	void AddItemsFromRange(const wxString& rangeStr, uint16_t withId);
	uint16_t getActualItemIdFromBrush(const Brush* brush) const;
	void OnBorderFromSelect(wxCommandEvent& event);
	void OnBorderToSelect(wxCommandEvent& event);
	wxString GetDataDirectoryForVersion(const wxString& versionStr);
	void AddWallVariations(uint16_t fromId, uint16_t toId);
	void LoadWallChoices();
	void OnWallFromSelect(wxCommandEvent& event);
	void OnWallToSelect(wxCommandEvent& event);
	void OnAddWallItems(wxCommandEvent& event);
		void AddReplacingItem(uint16_t fromId, uint16_t toId);

private:
	void UpdateWidgets();


	ReplaceItemsListBox* list;
	ReplaceItemsButton* replace_button;
	ReplaceItemsButton* with_button;
	wxGauge* progress;
	wxStaticBitmap* arrow_bitmap;
	wxButton* add_button;
	wxButton* remove_button;
	wxButton* execute_button;
	wxButton* close_button;
	wxButton* preset_button;
	wxMenu* preset_menu;
	bool selectionOnly;

	wxChoice* preset_choice;
	wxButton* add_preset_button;
	wxButton* remove_preset_button;
	wxButton* load_preset_button;
	wxButton* swap_button;
	wxCheckBox* swap_checkbox;

	// Add new range input control
	wxTextCtrl* replace_range_input;

	// Add after replace_range_input declaration
	wxTextCtrl* with_range_input;

	// Add after preset_choice declaration
	wxChoice* border_from_choice;
	wxChoice* border_to_choice;

	// Add to private members section around line 150
	wxButton* add_border_button;

	// Add after border choice declarations
	wxChoice* wall_from_choice;
	wxChoice* wall_to_choice;
	wxChoice* wall_orientation_choice;
	wxButton* add_wall_button;


	void OnPresetSelect(wxCommandEvent& event);
	void OnAddPreset(wxCommandEvent& event);
	void OnRemovePreset(wxCommandEvent& event);

	void SavePresetToXML(const wxString& name);
	void LoadPresetFromXML(const wxString& name);
	void RefreshPresetList();

	void LoadBorderChoices();
	void OnAddBorderItems(wxCommandEvent& event);

	static const int ID_SAVE_PRESET = 1001;
	static const int ID_LOAD_PRESET = 1002;
	static const int ID_MANAGE_PRESETS = 1003;

	wxTextCtrl* replace_id_input;
	wxTextCtrl* with_id_input;
	wxButton* add_manual_button;

	void OnIdInput(wxCommandEvent& event);
	void UpdateAddButtonState();

	std::vector<std::pair<uint16_t, uint16_t>> ParseRangeString(const wxString& input);
};

#endif
