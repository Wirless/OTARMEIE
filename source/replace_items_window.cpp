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
#include "replace_items_window.h"
#include "find_item_window.h"
#include "graphics.h"
#include "gui.h"
#include "artprovider.h"
#include "items.h"
#include "brush.h"
#include "ground_brush.h"
#include "wall_brush.h"
#include "doodad_brush.h"
#include <wx/dir.h>
#include <wx/tokenzr.h>
/*
 * ! CURRENT TASK:
 * Add Remove Items Checkbox to Find Items Dialog
 * --------------------------------------------
 * Add functionality to remove found items after finding them
 * 
 * Current Operation:
 * - Dialog finds items based on search criteria and displays results that allow to go to the item in the map
 * - Items are highlighted/selected when found
 * 
 * Desired Changes:
 * - Add checkbox under IgnoredIds section
 * - Label: "Remove found items"
 * - When checked, found items will be removed from their positions and the dialog will be refreshed
 * - Should work with both single and range searches
 * 
 * Technical Requirements:
 * - Add checkbox member to FindItemDialog class
 * - Modify search logic to handle item removal
 * - Ensure proper undo/redo support for removals
 * - Update item counts and display after removal
 * 
 * Visual Layout:
 * [Existing IgnoredIds section]
 * [x] Remove found items
 * [Rest of dialog...]
 * expand the main window to 800x800 to accomodate new checkbox
 * 
 * Implementation Notes:
 * - Need to handle removal within the map's action system
 * - Should update statistics after removal
 * - Consider adding confirmation dialog for large numbers of items
 * To note there seems to be potential error and some items are not being removed only the first item in list? 
 */
// ============================================================================
// ReplaceItemsButton

ReplaceItemsButton::ReplaceItemsButton(wxWindow* parent) :
	DCButton(parent, wxID_ANY, wxDefaultPosition, DC_BTN_TOGGLE, RENDER_SIZE_32x32, 0),
	m_id(0) {
	////
}

ItemGroup_t ReplaceItemsButton::GetGroup() const {
	if (m_id != 0) {
		const ItemType& it = g_items.getItemType(m_id);
		if (it.id != 0) {
			return it.group;
		}
	}
	return ITEM_GROUP_NONE;
}

void ReplaceItemsButton::SetItemId(uint16_t id) {
	if (m_id == id) {
		return;
	}

	m_id = id;

	if (m_id != 0) {
		const ItemType& it = g_items.getItemType(m_id);
		if (it.id != 0) {
			SetSprite(it.clientID);
			return;
		}
	}

	SetSprite(0);
}

// ============================================================================
// ReplaceItemsListBox

ReplaceItemsListBox::ReplaceItemsListBox(wxWindow* parent) :
	wxVListBox(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_SINGLE) {
	m_arrow_bitmap = wxArtProvider::GetBitmap(ART_POSITION_GO, wxART_TOOLBAR, wxSize(16, 16));
	m_flag_bitmap = wxArtProvider::GetBitmap(ART_PZ_BRUSH, wxART_TOOLBAR, wxSize(16, 16));
}

bool ReplaceItemsListBox::AddItem(const ReplacingItem& item) {
	if (item.replaceId == 0 || item.withId == 0 || item.replaceId == item.withId) {
		return false;
	}

	m_items.push_back(item);
	SetItemCount(m_items.size());
	Refresh();

	return true;
}

void ReplaceItemsListBox::MarkAsComplete(const ReplacingItem& item, uint32_t total) {
	auto it = std::find(m_items.begin(), m_items.end(), item);
	if (it != m_items.end()) {
		it->total = total;
		it->complete = true;
		Refresh();
	}
}

void ReplaceItemsListBox::RemoveSelected() {
	if (m_items.empty()) {
		return;
	}

	const int index = GetSelection();
	if (index == wxNOT_FOUND) {
		return;
	}

	m_items.erase(m_items.begin() + index);
	SetItemCount(GetItemCount() - 1);
	Refresh();
}

bool ReplaceItemsListBox::CanAdd(uint16_t replaceId, uint16_t withId) const {
	if (replaceId == 0 || withId == 0 || replaceId == withId) {
		return false;
	}

	for (const ReplacingItem& item : m_items) {
		if (replaceId == item.replaceId) {
			return false;
		}
	}
	return true;
}

void ReplaceItemsListBox::OnDrawItem(wxDC& dc, const wxRect& rect, size_t index) const {
	ASSERT(index < m_items.size());

	const ReplacingItem& item = m_items.at(index);
	const ItemType& type1 = g_items.getItemType(item.replaceId);
	Sprite* sprite1 = g_gui.gfx.getSprite(type1.clientID);
	const ItemType& type2 = g_items.getItemType(item.withId);
	Sprite* sprite2 = g_gui.gfx.getSprite(type2.clientID);

	if (sprite1 && sprite2) {
		int x = rect.GetX();
		int y = rect.GetY();
		sprite1->DrawTo(&dc, SPRITE_SIZE_32x32, x + 4, y + 4, rect.GetWidth(), rect.GetHeight());
		dc.DrawBitmap(m_arrow_bitmap, x + 38, y + 10, true);
		sprite2->DrawTo(&dc, SPRITE_SIZE_32x32, x + 56, y + 4, rect.GetWidth(), rect.GetHeight());
		dc.DrawText(wxString::Format("Replace: %d With: %d", item.replaceId, item.withId), x + 104, y + 10);

		if (item.complete) {
			x = rect.GetWidth() - 100;
			dc.DrawBitmap(m_flag_bitmap, x + 70, y + 10, true);
			dc.DrawText(wxString::Format("Total: %d", item.total), x, y + 10);
		}
	}

	if (IsSelected(index)) {
		if (HasFocus()) {
			dc.SetTextForeground(wxColor(0xFF, 0xFF, 0xFF));
		} else {
			dc.SetTextForeground(wxColor(0x00, 0x00, 0xFF));
		}
	} else {
		dc.SetTextForeground(wxColor(0x00, 0x00, 0x00));
	}
}

wxCoord ReplaceItemsListBox::OnMeasureItem(size_t WXUNUSED(index)) const {
	return 40;
}

void ReplaceItemsListBox::Clear() {
	m_items.clear();  // Clear the vector
	SetItemCount(0);  // Reset the list count
	Refresh();        // Force a visual refresh
	Update();         // Force immediate update
}

// ============================================================================
// ReplaceItemsDialog

ReplaceItemsDialog::ReplaceItemsDialog(wxWindow* parent, bool selectionOnly) :
	wxDialog(parent, wxID_ANY, (selectionOnly ? "Replace Items on Selection" : "Replace Items"), 
		wxDefaultPosition, wxSize(500, 800), wxDEFAULT_DIALOG_STYLE),
	selectionOnly(selectionOnly) {
	SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	wxFlexGridSizer* list_sizer = new wxFlexGridSizer(0, 2, 0, 0);
	list_sizer->SetFlexibleDirection(wxBOTH);
	list_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
	list_sizer->SetMinSize(wxSize(25, 300));

	list = new ReplaceItemsListBox(this);
	list->SetMinSize(wxSize(480, 320));

	list_sizer->Add(list, 0, wxALL | wxEXPAND, 5);
	sizer->Add(list_sizer, 1, wxALL | wxEXPAND, 5);

	wxBoxSizer* items_sizer = new wxBoxSizer(wxHORIZONTAL);
	items_sizer->SetMinSize(wxSize(-1, 40));

	replace_button = new ReplaceItemsButton(this);
	items_sizer->Add(replace_button, 0, wxALL, 5);

	// After replace_button initialization, add range input
	wxBoxSizer* range_sizer = new wxBoxSizer(wxHORIZONTAL);
	replace_range_input = new wxTextCtrl(this, wxID_ANY, "", 
		wxDefaultPosition, wxDefaultSize);
	replace_range_input->SetToolTip("Enter range (e.g., 100-105,200)");
	range_sizer->Add(new wxStaticText(this, wxID_ANY, "Replace Range:"), 
		0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
	range_sizer->Add(replace_range_input, 1, wxEXPAND);
	items_sizer->Add(range_sizer, 1, wxALL | wxEXPAND, 5);

	wxBitmap bitmap = wxArtProvider::GetBitmap(ART_POSITION_GO, wxART_TOOLBAR, wxSize(16, 16));
	arrow_bitmap = new wxStaticBitmap(this, wxID_ANY, bitmap);
	items_sizer->Add(arrow_bitmap, 0, wxTOP, 15);

	with_button = new ReplaceItemsButton(this);
	items_sizer->Add(with_button, 0, wxALL, 5);

	items_sizer->Add(0, 0, 1, wxEXPAND, 5);

	progress = new wxGauge(this, wxID_ANY, 100);
	progress->SetValue(0);
	items_sizer->Add(progress, 0, wxALL, 5);

	sizer->Add(items_sizer, 1, wxALL | wxEXPAND, 5);

	// Add border controls after preset controls
	wxBoxSizer* border_sizer = new wxBoxSizer(wxVERTICAL);
	
	// Add border label
	wxStaticText* border_label = new wxStaticText(this, wxID_ANY, "Replace Borders:");
	border_sizer->Add(border_label, 0, wxALL | wxALIGN_LEFT, 5);
	
	// Create horizontal sizer for border selection
	wxBoxSizer* border_selection_sizer = new wxBoxSizer(wxHORIZONTAL);
	
	border_from_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(200, 30));
	border_to_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(200, 30));
	
	border_selection_sizer->Add(border_from_choice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	border_selection_sizer->Add(border_to_choice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	
	border_sizer->Add(border_selection_sizer, 0, wxALL | wxCENTER, 5);
	
	// Add border replace button in its own row
	add_border_button = new wxButton(this, wxID_ANY, "Add Border Items", wxDefaultPosition, wxSize(150, 30));
	border_sizer->Add(add_border_button, 0, wxALL | wxCENTER, 5);
	
	sizer->Add(border_sizer, 0, wxALL | wxCENTER, 5);

	// Create main buttons row (Add, Remove, Execute, Close)
	wxBoxSizer* buttons_sizer = new wxBoxSizer(wxHORIZONTAL);

	add_button = new wxButton(this, wxID_ANY, wxT("Add"));
	add_button->Enable(false);
	buttons_sizer->Add(add_button, 0, wxALL, 5);
	add_button->SetMinSize(wxSize(60, 30));

	remove_button = new wxButton(this, wxID_ANY, wxT("Remove"));
	remove_button->Enable(false);
	buttons_sizer->Add(remove_button, 0, wxALL, 5);
	remove_button->SetMinSize(wxSize(60, 30));

	buttons_sizer->Add(0, 0, 1, wxEXPAND, 5);

	// Create a static box for the swap checkbox with increased size
	wxStaticBoxSizer* swap_box = new wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, "Swap <-->"), wxVERTICAL);
	swap_box->GetStaticBox()->SetMinSize(wxSize(140, 60)); // Increased box size

	// Add checkbox with better spacing and positioning, but no text
	swap_checkbox = new wxCheckBox(swap_box->GetStaticBox(), wxID_ANY, "");
	swap_box->Add(swap_checkbox, 0, wxALL | wxALIGN_CENTER, 10); // Increased padding

	// Add it to the buttons_sizer before the execute button
	buttons_sizer->Add(swap_box, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);

	execute_button = new wxButton(this, wxID_ANY, wxT("Execute"));
	execute_button->Enable(false);
	buttons_sizer->Add(execute_button, 0, wxALL, 5);
	execute_button->SetMinSize(wxSize(60, 30));

	close_button = new wxButton(this, wxID_ANY, wxT("Close"));
	buttons_sizer->Add(close_button, 0, wxALL, 5);
	close_button->SetMinSize(wxSize(60, 30));

	sizer->Add(buttons_sizer, 1, wxALL | wxLEFT | wxRIGHT | wxSHAPED, 5);

	// Add spacing between button rows
	sizer->AddSpacer(10);

	// Create preset controls row at the bottom
	wxBoxSizer* preset_sizer = new wxBoxSizer(wxHORIZONTAL);
	
	// Create preset dropdown with increased height
	preset_choice = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(150, 30));
	preset_sizer->Add(preset_choice, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	
	// Add load button
	load_preset_button = new wxButton(this, wxID_ANY, wxT("Load"), wxDefaultPosition, wxSize(60, 30));
	preset_sizer->Add(load_preset_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	
	// Add preset management buttons with proper size
	add_preset_button = new wxButton(this, wxID_ANY, wxT("Add Preset"), wxDefaultPosition, wxSize(100, 30));
	preset_sizer->Add(add_preset_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	
	remove_preset_button = new wxButton(this, wxID_ANY, wxT("Remove Preset"), wxDefaultPosition, wxSize(100, 30));
	preset_sizer->Add(remove_preset_button, 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	
	sizer->Add(preset_sizer, 0, wxALL | wxCENTER, 5);

	SetSizer(sizer);
	Layout();
	Centre(wxBOTH);

	// Connect Events
	list->Connect(wxEVT_COMMAND_LISTBOX_SELECTED, wxCommandEventHandler(ReplaceItemsDialog::OnListSelected), NULL, this);
	replace_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnReplaceItemClicked), NULL, this);
	with_button->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnWithItemClicked), NULL, this);
	add_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnAddButtonClicked), NULL, this);
	remove_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnRemoveButtonClicked), NULL, this);
	execute_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnExecuteButtonClicked), NULL, this);
	close_button->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnCancelButtonClicked), NULL, this);
	preset_choice->Connect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnPresetSelect), NULL, this);
	add_preset_button->Connect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnAddPreset), NULL, this);
	remove_preset_button->Connect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnRemovePreset), NULL, this);
	load_preset_button->Connect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnLoadPreset), NULL, this);
	swap_checkbox->Connect(wxEVT_CHECKBOX, wxCommandEventHandler(ReplaceItemsDialog::OnSwapCheckboxClicked), NULL, this);
	border_from_choice->Connect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnBorderFromSelect), NULL, this);
	border_to_choice->Connect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnBorderToSelect), NULL, this);
	add_border_button->Connect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnAddBorderItems), NULL, this);

	// Load initial preset list
	RefreshPresetList();

	// Load initial border lists
	LoadBorderChoices();
}

ReplaceItemsDialog::~ReplaceItemsDialog() {
	// Disconnect Events
	list->Disconnect(wxEVT_COMMAND_LISTBOX_SELECTED, wxCommandEventHandler(ReplaceItemsDialog::OnListSelected), NULL, this);
	replace_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnReplaceItemClicked), NULL, this);
	with_button->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(ReplaceItemsDialog::OnWithItemClicked), NULL, this);
	add_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnAddButtonClicked), NULL, this);
	remove_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnRemoveButtonClicked), NULL, this);
	execute_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnExecuteButtonClicked), NULL, this);
	close_button->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(ReplaceItemsDialog::OnCancelButtonClicked), NULL, this);
	preset_choice->Disconnect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnPresetSelect), NULL, this);
	add_preset_button->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnAddPreset), NULL, this);
	remove_preset_button->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnRemovePreset), NULL, this);
	load_preset_button->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnLoadPreset), NULL, this);
	swap_checkbox->Disconnect(wxEVT_CHECKBOX, wxCommandEventHandler(ReplaceItemsDialog::OnSwapCheckboxClicked), NULL, this);
	border_from_choice->Disconnect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnBorderFromSelect), NULL, this);
	border_to_choice->Disconnect(wxEVT_CHOICE, wxCommandEventHandler(ReplaceItemsDialog::OnBorderToSelect), NULL, this);
	add_border_button->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(ReplaceItemsDialog::OnAddBorderItems), NULL, this);
}

void ReplaceItemsDialog::UpdateWidgets() {
	// Always enable add button, we'll handle validation with error messages
	add_button->Enable(true);
	
	// Only these buttons need conditional enabling
	remove_button->Enable(list->GetCount() != 0 && list->GetSelection() != wxNOT_FOUND);
	execute_button->Enable(list->GetCount() != 0);
}

void ReplaceItemsDialog::OnListSelected(wxCommandEvent& WXUNUSED(event)) {
	remove_button->Enable(list->GetCount() != 0 && list->GetSelection() != wxNOT_FOUND);
}

void ReplaceItemsDialog::OnReplaceItemClicked(wxMouseEvent& WXUNUSED(event)) {
	OutputDebugStringA("ReplaceItemsDialog::OnReplaceItemClicked called\n");
	
	const Brush* brush = g_gui.GetCurrentBrush();
	uint16_t id = getActualItemIdFromBrush(brush);

	if (id != 0) {
		replace_button->SetItemId(id);
		UpdateWidgets();
		OutputDebugStringA(wxString::Format("Final Replace Item ID set: %d\n", id).c_str());
	} else {
		OutputDebugStringA("ReplaceItemsDialog::OnReplaceItemClicked: Could not resolve item ID from brush\n");
	}
}

void ReplaceItemsDialog::OnWithItemClicked(wxMouseEvent& WXUNUSED(event)) {
	OutputDebugStringA("ReplaceItemsDialog::OnWithItemClicked called\n");

	if (replace_button->GetItemId() == 0) {
		OutputDebugStringA("ReplaceItemsDialog::OnWithItemClicked: Replace button has no item selected\n");
		return;
	}

	const Brush* brush = g_gui.GetCurrentBrush();
	uint16_t id = getActualItemIdFromBrush(brush);

	if (id != 0) {
		with_button->SetItemId(id);
		UpdateWidgets();
		OutputDebugStringA(wxString::Format("Final With Item ID set: %d\n", id).c_str());
	} else {
		OutputDebugStringA("ReplaceItemsDialog::OnWithItemClicked: Could not resolve item ID from brush\n");
	}
}

void ReplaceItemsDialog::OnAddButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	const uint16_t withId = with_button->GetItemId();
	if (withId == 0) {
		wxMessageBox("Please select an item to replace with!", "Error", wxOK | wxICON_ERROR);
		return;
	}

	// Check if range input is provided
	wxString rangeStr = replace_range_input->GetValue().Trim();
	if (!rangeStr.IsEmpty()) {
		AddItemsFromRange(rangeStr, withId);
	} else {
		// Original single item add logic
		const uint16_t replaceId = replace_button->GetItemId();
		if (replaceId == 0) {
			wxMessageBox("Please select an item to replace!", "Error", wxOK | wxICON_ERROR);
			return;
		}
		
		// Only check CanAdd for single items, not for range inputs
		if (!list->CanAdd(replaceId, withId)) {
			wxMessageBox("This item is already in the list or cannot be replaced with itself!", "Error", wxOK | wxICON_ERROR);
			return;
		}
		
		ReplacingItem item;
		item.replaceId = replaceId;
		item.withId = withId;
		list->AddItem(item);
	}

	// Reset controls
	replace_button->SetItemId(0);
	with_button->SetItemId(0);
	replace_range_input->SetValue("");
	UpdateWidgets();
}

void ReplaceItemsDialog::AddItemsFromRange(const wxString& rangeStr, uint16_t withId) {
	wxString input = rangeStr;
	wxStringTokenizer tokenizer(input, ",");
	bool addedAny = false;
	
	while (tokenizer.HasMoreTokens()) {
		wxString token = tokenizer.GetNextToken().Trim();
		
		if (token.Contains("-")) {
			// Handle range (e.g., "100-105")
			long start, end;
			wxString startStr = token.Before('-').Trim();
			wxString endStr = token.After('-').Trim();
			
			if (startStr.ToLong(&start) && endStr.ToLong(&end)) {
				for (long i = start; i <= end; ++i) {
					if (i > 0 && i <= 65535) {  // Valid uint16_t range
						ReplacingItem item;
						item.replaceId = static_cast<uint16_t>(i);
						item.withId = withId;
						// Skip CanAdd check for range inputs
						list->AddItem(item);
						addedAny = true;
					}
				}
			}
		} else {
			// Handle single number
			long id;
			if (token.ToLong(&id) && id > 0 && id <= 65535) {
				ReplacingItem item;
				item.replaceId = static_cast<uint16_t>(id);
				item.withId = withId;
				// Skip CanAdd check for range inputs
				list->AddItem(item);
				addedAny = true;
			}
		}
	}

	if (!addedAny) {
		wxMessageBox("No valid values in range!", "Error", wxOK | wxICON_ERROR);
	}
}

void ReplaceItemsDialog::OnRemoveButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	list->RemoveSelected();
	UpdateWidgets();
}

void ReplaceItemsDialog::OnExecuteButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	if (!g_gui.IsEditorOpen()) {
		return;
	}

	const auto& items = list->GetItems();
	if (items.empty()) {
		return;
	}

	replace_button->Enable(false);
	with_button->Enable(false);
	add_button->Enable(false);
	remove_button->Enable(false);
	execute_button->Enable(false);
	close_button->Enable(false);
	progress->SetValue(0);

	MapTab* tab = dynamic_cast<MapTab*>(GetParent());
	if (!tab) {
		return;
	}

	Editor* editor = tab->GetEditor();
	bool isReversed = swap_checkbox->GetValue();

	int done = 0;
	for (const ReplacingItem& info : items) {
		// If reversed, swap the IDs for the search
		uint16_t searchId = isReversed ? info.withId : info.replaceId;
		uint16_t replaceWithId = isReversed ? info.replaceId : info.withId;
		
		ItemFinder finder(searchId, (uint32_t)g_settings.getInteger(Config::REPLACE_SIZE));

		// search on map
		foreach_ItemOnMap(editor->map, finder, selectionOnly);

		uint32_t total = 0;
		std::vector<std::pair<Tile*, Item*>>& result = finder.result;

		if (!result.empty()) {
			Action* action = editor->actionQueue->createAction(ACTION_REPLACE_ITEMS);
			for (std::vector<std::pair<Tile*, Item*>>::const_iterator rit = result.begin(); rit != result.end(); ++rit) {
				Tile* new_tile = rit->first->deepCopy(editor->map);
				int index = rit->first->getIndexOf(rit->second);
				ASSERT(index != wxNOT_FOUND);
				Item* item = new_tile->getItemAt(index);
				ASSERT(item && item->getID() == rit->second->getID());
				transformItem(item, replaceWithId, new_tile);
				action->addChange(new Change(new_tile));
				total++;
			}
			editor->actionQueue->addAction(action);
		}

		done++;
		const int value = static_cast<int>((done / items.size()) * 100);
		progress->SetValue(std::clamp<int>(value, 0, 100));
		list->MarkAsComplete(info, total);
	}

	// Re-enable all buttons
	replace_button->Enable(true);
	with_button->Enable(true);
	add_button->Enable(false); // Stays disabled until valid items are selected
	remove_button->Enable(false); // Stays disabled until an item is selected in list
	execute_button->Enable(list->GetCount() != 0);
	close_button->Enable(true);
	UpdateWidgets();

	tab->Refresh();
}

void ReplaceItemsDialog::OnCancelButtonClicked(wxCommandEvent& WXUNUSED(event)) {
	Close();
}

void ReplaceItemsDialog::OnSwapCheckboxClicked(wxCommandEvent& WXUNUSED(event)) {
	if (arrow_bitmap) {
		// Get the original bitmap from the art provider
		wxBitmap original = wxArtProvider::GetBitmap(ART_POSITION_GO, wxART_TOOLBAR, wxSize(16, 16));
		
		// Create a rotated version using wxImage for the 180-degree rotation
		wxImage img = original.ConvertToImage();
		img = swap_checkbox->GetValue() ? 
			img.Rotate180() :  // Flip it completely when checked
			original.ConvertToImage();  // Use original when unchecked
			
		// Update the bitmap
		arrow_bitmap->SetBitmap(wxBitmap(img));
	}
}

void ReplaceItemsDialog::RefreshPresetList() {
	wxString path = g_gui.GetDataDirectory() + "\\replace_presets\\";
	if (!wxDirExists(path)) {
		wxMkdir(path);
	}
	
	preset_choice->Clear();
	wxDir dir(path);
	if (!dir.IsOpened()) return;
	
	wxString filename;
	bool cont = dir.GetFirst(&filename, "*.xml", wxDIR_FILES);
	while (cont) {
		preset_choice->Append(filename.BeforeLast('.'));
		cont = dir.GetNext(&filename);
	}
	
	remove_preset_button->Enable(preset_choice->GetCount() > 0);
}

void ReplaceItemsDialog::OnPresetSelect(wxCommandEvent& WXUNUSED(event)) {
	int sel = preset_choice->GetSelection();
	if (sel != wxNOT_FOUND) {
		LoadPresetFromXML(preset_choice->GetString(sel));
	}
}

void ReplaceItemsDialog::OnAddPreset(wxCommandEvent& WXUNUSED(event)) {
	wxString name = wxGetTextFromUser("Enter preset name:", "Save Replace Items Preset");
	if (!name.empty()) {
		SavePresetToXML(name);
		RefreshPresetList();
		// Select the newly added preset
		int idx = preset_choice->FindString(name);
		if (idx != wxNOT_FOUND) {
			preset_choice->SetSelection(idx);
		}
	}
}

void ReplaceItemsDialog::OnRemovePreset(wxCommandEvent& WXUNUSED(event)) {
	int sel = preset_choice->GetSelection();
	if (sel != wxNOT_FOUND) {
		wxString name = preset_choice->GetString(sel);
		if (wxMessageBox(wxString::Format("Are you sure you want to delete the preset '%s'?", name), 
						"Confirm Delete", wxYES_NO | wxNO_DEFAULT) == wxYES) {
			wxString path = g_gui.GetDataDirectory() + "\\replace_presets\\" + name + ".xml";
			wxRemoveFile(path);
			RefreshPresetList();
		}
	}
}

void ReplaceItemsDialog::SavePresetToXML(const wxString& name) {
	wxString path = g_gui.GetDataDirectory() + "\\replace_presets\\";
	if (!wxDirExists(path)) {
		wxMkdir(path);
	}

	pugi::xml_document doc;
	pugi::xml_node root = doc.append_child("replace_items");
	
	const auto& items = list->GetItems();
	for (const ReplacingItem& item : items) {
		pugi::xml_node replace_node = root.append_child("replace");
		replace_node.append_attribute("replaceId") = item.replaceId;
		replace_node.append_attribute("withId") = item.withId;
	}
	
	doc.save_file((path + name + ".xml").mb_str());
}

void ReplaceItemsDialog::LoadPresetFromXML(const wxString& name) {
	wxString path = g_gui.GetDataDirectory() + "\\replace_presets\\" + name + ".xml";
	
	pugi::xml_document doc;
	if (!doc.load_file(path.mb_str())) {
		return;
	}

	// Reset everything first
	list->Clear();    // Clear the list (which will clear internal m_items)
	replace_button->SetItemId(0);
	with_button->SetItemId(0);
	progress->SetValue(0);
	
	pugi::xml_node root = doc.child("replace_items");
	for (pugi::xml_node replace_node = root.child("replace"); replace_node; replace_node = replace_node.next_sibling("replace")) {
		ReplacingItem item;
		item.replaceId = replace_node.attribute("replaceId").as_uint();
		item.withId = replace_node.attribute("withId").as_uint();
		item.complete = false;
		item.total = 0;
		if (item.replaceId != 0 && item.withId != 0) {  // Validate items before adding
			list->AddItem(item);
		}
	}
	
	UpdateWidgets();
	list->Refresh();
	list->Update();
}

void ReplaceItemsDialog::OnLoadPreset(wxCommandEvent& WXUNUSED(event)) {
	int sel = preset_choice->GetSelection();
	if (sel != wxNOT_FOUND) {
		LoadPresetFromXML(preset_choice->GetString(sel));
	}
}

uint16_t ReplaceItemsDialog::getActualItemIdFromBrush(const Brush* brush) const {
	if (!brush) {
		OutputDebugStringA("getActualItemIdFromBrush: No brush provided\n");
		return 0;
	}

	uint16_t id = 0;

	if (brush->isRaw()) {
		RAWBrush* raw = static_cast<RAWBrush*>(const_cast<Brush*>(brush));
		id = raw->getItemID();
		OutputDebugStringA(wxString::Format("RAW brush item ID: %d\n", id).c_str());
	} else if (brush->isGround()) {
		GroundBrush* gb = const_cast<Brush*>(brush)->asGround();
		if (gb) {
			// Try to get the actual item ID through the ground brush's properties
			if (gb->getID() != 0) {
				// First try to find a matching RAW brush
				for (const auto& brushPair : g_brushes.getMap()) {
					if (brushPair.second && brushPair.second->isRaw()) {
						RAWBrush* raw = static_cast<RAWBrush*>(brushPair.second);
						const ItemType& rawType = g_items.getItemType(raw->getItemID());
						if (rawType.brush && rawType.brush->getID() == gb->getID()) {
							id = raw->getItemID();
							OutputDebugStringA(wxString::Format("Found matching RAW brush ID: %d for ground brush\n", id).c_str());
							break;
						}
					}
				}

				// If no RAW brush found, try to get through item database
				if (id == 0) {
					const ItemType& type = g_items.getItemType(gb->getID());
					if (type.id != 0) {
						id = type.id;
						OutputDebugStringA(wxString::Format("Found item type ID: %d for ground brush\n", id).c_str());
					}
				}
			}
		}
	} else {
		// For other brush types, try to find the corresponding item ID
		const ItemType& type = g_items.getItemType(brush->getID());
		if (type.id != 0) {
			id = type.id;
			OutputDebugStringA(wxString::Format("Found item type ID: %d for brush\n", id).c_str());
		}
	}

	if (id == 0) {
		OutputDebugStringA("Could not resolve actual item ID from brush\n");
	}

	return id;
}

void ReplaceItemsDialog::LoadBorderChoices() {
	// Clear existing choices
	border_from_choice->Clear();
	border_to_choice->Clear();
	
	// Get version string and convert to directory format
	std::string versionStr = g_gui.GetCurrentVersion().getName();
	wxString formattedVersion;
	
	// Parse version string (e.g., "7.60" -> "760")
	wxString versionWx(versionStr);
	versionWx.Replace(".", ""); // Remove dots
	
	OutputDebugStringA(wxString::Format("Original version string: %s\n", versionStr).c_str());
	OutputDebugStringA(wxString::Format("Formatted version: %s\n", versionWx).c_str());
	
	wxString bordersPath = g_gui.GetDataDirectory() + "/" + versionWx + "/borders.xml";
	
	OutputDebugStringA(wxString::Format("Attempting to load borders from: %s\n", bordersPath).c_str());
	
	// Add default text to choices
	border_from_choice->Append("Select border to replace...");
	border_to_choice->Append("Select border to replace with...");
	border_from_choice->SetSelection(0);
	border_to_choice->SetSelection(0);
	
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(bordersPath.mb_str());
	
	if(result) {
		OutputDebugStringA("Successfully opened borders.xml\n");
		for(pugi::xml_node borderNode = doc.child("materials").child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			int borderId = borderNode.attribute("id").as_int();
			wxString comment;
			
			// Count border items
			int itemCount = 0;
			for(pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
				itemCount++;
			}
			
			// Try to extract comment
			for(pugi::xml_node node = borderNode.next_sibling(); node; node = node.next_sibling()) {
				if(node.type() == pugi::node_comment) {
					comment = wxString(node.value()).Trim();
					break;
				}
			}
			
			wxString displayText;
			if(!comment.IsEmpty()) {
				displayText = wxString::Format("Border %d [%d] (%s)", borderId, itemCount, comment);
			} else {
				displayText = wxString::Format("Border %d [%d]", borderId, itemCount);
			}
			
			border_from_choice->Append(displayText);
			border_to_choice->Append(displayText);
			
			OutputDebugStringA(wxString::Format("Added border ID %d with %d items %s\n", 
				borderId,
				itemCount,
				comment.IsEmpty() ? "" : wxString::Format("(%s)", comment).c_str()
			).c_str());
		}
	} else {
		OutputDebugStringA(wxString::Format("Failed to load borders.xml: %s\n", result.description()).c_str());
		OutputDebugStringA(wxString::Format("Error at offset: %zu\n", result.offset).c_str());
		
		// Check file access
		if(wxFileExists(bordersPath)) {
			OutputDebugStringA("File exists but could not be loaded - possible permission or format issue\n");
		} else {
			OutputDebugStringA("File does not exist!\n");
		}
	}
}

void ReplaceItemsDialog::OnAddBorderItems(wxCommandEvent& WXUNUSED(event)) {
	int fromIdx = border_from_choice->GetSelection();
	int toIdx = border_to_choice->GetSelection();
	
	OutputDebugStringA(wxString::Format("OnAddBorderItems - From: %d, To: %d\n", fromIdx, toIdx).c_str());
	
	if(fromIdx <= 0 || toIdx <= 0) { // Account for the "Select border..." entry
		wxMessageBox("Please select both border types!", "Error", wxOK | wxICON_ERROR);
		return;
	}

	// Adjust indices to account for the "Select border..." entry
	fromIdx--;
	toIdx--;

	// Get border IDs from the actual XML file
	wxString versionStr(g_gui.GetCurrentVersion().getName());
	versionStr.Replace(".", "");
	wxString bordersPath = g_gui.GetDataDirectory() + "/" + versionStr + "/borders.xml";
	
	OutputDebugStringA(wxString::Format("Loading borders from: %s\n", bordersPath).c_str());
	
	pugi::xml_document doc;
	if(doc.load_file(bordersPath.mb_str())) {
		std::map<std::string, uint16_t> fromItems;
		std::map<std::string, uint16_t> toItems;
		
		int currentBorder = 0;
		for(pugi::xml_node borderNode = doc.child("materials").child("border"); borderNode; borderNode = borderNode.next_sibling("border")) {
			if(currentBorder == fromIdx || currentBorder == toIdx) {
				for(pugi::xml_node itemNode = borderNode.child("borderitem"); itemNode; itemNode = itemNode.next_sibling("borderitem")) {
					std::string edge = itemNode.attribute("edge").value();
					uint16_t itemId = itemNode.attribute("item").as_uint();
					
					if(currentBorder == fromIdx) {
						fromItems[edge] = itemId;
						OutputDebugStringA(wxString::Format("From Border - Edge: %s, Item: %d\n", edge, itemId).c_str());
					} else {
						toItems[edge] = itemId;
						OutputDebugStringA(wxString::Format("To Border - Edge: %s, Item: %d\n", edge, itemId).c_str());
					}
				}
			}
			currentBorder++;
		}
		
		// Add items to the replace list
		for(const auto& pair : fromItems) {
			if(toItems.count(pair.first)) {
				ReplacingItem item;
				item.replaceId = pair.second;
				item.withId = toItems[pair.first];
				item.complete = false;
				item.total = 0;
				
				OutputDebugStringA(wxString::Format("Adding replacement: %d -> %d\n", item.replaceId, item.withId).c_str());
				list->AddItem(item);
			}
		}
		
		UpdateWidgets();
		list->Refresh();
	} else {
		OutputDebugStringA("Failed to load borders.xml\n");
		wxMessageBox("Failed to load borders configuration!", "Error", wxOK | wxICON_ERROR);
	}
}
