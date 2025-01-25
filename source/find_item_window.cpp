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
#include "find_item_window.h"
#include "common_windows.h"
#include "gui.h"
#include "items.h"
#include "brush.h"
#include "raw_brush.h"
#include <algorithm>    // For std::all_of
#include <cctype>       // For std::isdigit
#include <sstream>      // For std::istringstream
#include <vector>
#include <string>
#include "string_utils.h"
#include "result_window.h"

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
 * 
 * Implementation Notes:
 * - Need to handle removal within the map's action system
 * - Should update statistics after removal
 * - Consider adding confirmation dialog for large numbers of items
 */


BEGIN_EVENT_TABLE(FindItemDialog, wxDialog)
EVT_TIMER(wxID_ANY, FindItemDialog::OnInputTimer)
EVT_BUTTON(wxID_OK, FindItemDialog::OnClickOK)
EVT_BUTTON(wxID_CANCEL, FindItemDialog::OnClickCancel)
END_EVENT_TABLE()

FindItemDialog::FindItemDialog(wxWindow* parent, const wxString& title, bool onlyPickupables /* = false*/) :
	wxDialog(parent, wxID_ANY, title, wxDefaultPosition, wxSize(800, 800), wxDEFAULT_DIALOG_STYLE),
	input_timer(this),
	result_brush(nullptr),
	result_id(0),
	only_pickupables(onlyPickupables) {
	this->SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* box_sizer = newd wxBoxSizer(wxHORIZONTAL);
	wxBoxSizer* options_box_sizer = newd wxBoxSizer(wxVERTICAL);

	// Radio box choices
	wxString radio_boxChoices[] = { "Find by Server ID",
									"Find by Client ID",
									"Find by Name",
									"Find by Types",
									"Find by Properties" };
	int radio_boxNChoices = sizeof(radio_boxChoices) / sizeof(wxString);
	options_radio_box = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, radio_boxNChoices, radio_boxChoices, 1, wxRA_SPECIFY_COLS);
	options_radio_box->SetSelection(SearchMode::ServerIDs);
	options_box_sizer->Add(options_radio_box, 0, wxALL | wxEXPAND, 5);

	// Server ID controls
	wxStaticBoxSizer* server_id_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Server ID"), wxVERTICAL);
	server_id_spin = newd wxSpinCtrl(server_id_box_sizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 100, g_items.getMaxID(), 100);
	server_id_box_sizer->Add(server_id_spin, 0, wxALL | wxEXPAND, 5);
	
	invalid_item = newd wxCheckBox(server_id_box_sizer->GetStaticBox(), wxID_ANY, "Invalid Item", wxDefaultPosition, wxDefaultSize, 0);
	server_id_box_sizer->Add(invalid_item, 0, wxALL, 5);
	
	options_box_sizer->Add(server_id_box_sizer, 0, wxALL | wxEXPAND, 5);

	// Client ID controls
	wxStaticBoxSizer* client_id_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Client ID"), wxVERTICAL);
	client_id_spin = newd wxSpinCtrl(client_id_box_sizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 100, g_gui.gfx.getItemSpriteMaxID(), 100);
	client_id_spin->Enable(false);
	client_id_box_sizer->Add(client_id_spin, 0, wxALL | wxEXPAND, 5);
	options_box_sizer->Add(client_id_box_sizer, 0, wxALL | wxEXPAND, 5);

	// Name controls
	wxStaticBoxSizer* name_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Name"), wxVERTICAL);
	name_text_input = newd wxTextCtrl(name_box_sizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
	name_text_input->Enable(false);
	name_box_sizer->Add(name_text_input, 0, wxALL | wxEXPAND, 5);
	options_box_sizer->Add(name_box_sizer, 0, wxALL | wxEXPAND, 5);

	// Range controls
	wxStaticBoxSizer* range_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "ID Range"), wxVERTICAL);

	// Checkbox to enable range search
	use_range = newd wxCheckBox(range_box_sizer->GetStaticBox(), wxID_ANY, "Search by Range", wxDefaultPosition, wxDefaultSize, 0);
	range_box_sizer->Add(use_range, 0, wxALL, 5);

	// Single range input for both Server and Client IDs
	wxStaticBoxSizer* range_input_box = newd wxStaticBoxSizer(newd wxStaticBox(range_box_sizer->GetStaticBox(), wxID_ANY, "ID Ranges"), wxVERTICAL);
	range_input = newd wxTextCtrl(range_input_box->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
	range_input->SetToolTip("Enter IDs or ranges separated by commas (e.g., 2222,2244-2266,5219)");
	range_input_box->Add(range_input, 0, wxALL | wxEXPAND, 5);
	range_box_sizer->Add(range_input_box, 0, wxALL | wxEXPAND, 5);

	options_box_sizer->Add(range_box_sizer, 0, wxALL | wxEXPAND, 5);

	// Ignored IDs Controls
	wxStaticBoxSizer* ignored_ids_box_sizer = newd wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, "Ignored IDs"), wxVERTICAL);
	
	// Checkbox to enable ignoring IDs
	ignore_ids_checkbox = newd wxCheckBox(ignored_ids_box_sizer->GetStaticBox(), wxID_ANY, "Enable Ignored IDs");
	ignored_ids_box_sizer->Add(ignore_ids_checkbox, 0, wxALL, 5);
	
	// Text input for entering IDs to ignore
	ignore_ids_text = newd wxTextCtrl(ignored_ids_box_sizer->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
	ignore_ids_text->SetToolTip("Enter IDs to ignore, separated by commas. Use '-' for ranges (e.g., 1212,1241,1256-1261,3199-4222,5993,5959)");
	ignored_ids_box_sizer->Add(ignore_ids_text, 0, wxALL | wxEXPAND, 5);
	
	options_box_sizer->Add(ignored_ids_box_sizer, 0, wxALL | wxEXPAND, 5);

	// Add after ignored IDs section
	wxStaticBoxSizer* remove_box = newd wxStaticBoxSizer(new wxStaticBox(this, wxID_ANY, "Item Removal"), wxVERTICAL);
	remove_found_items = newd wxCheckBox(remove_box->GetStaticBox(), wxID_ANY, "Remove found items");
	remove_found_items->SetToolTip("When checked, found items will be removed from their positions");
	remove_box->Add(remove_found_items, 0, wxALL, 5);
	options_box_sizer->Add(remove_box, 0, wxALL | wxEXPAND, 5);

	// Add spacer
	options_box_sizer->Add(0, 0, 1, wxEXPAND, 5);

	// Add buttons at the bottom
	buttons_box_sizer = newd wxStdDialogButtonSizer();
	ok_button = newd wxButton(this, wxID_OK);
	buttons_box_sizer->AddButton(ok_button);
	cancel_button = newd wxButton(this, wxID_CANCEL);
	buttons_box_sizer->AddButton(cancel_button);
	buttons_box_sizer->Realize();
	options_box_sizer->Add(buttons_box_sizer, 0, wxALIGN_CENTER | wxALL, 5);

	box_sizer->Add(options_box_sizer, 1, wxALL | wxEXPAND, 5);

	// --------------- Types ---------------

	wxStaticBoxSizer* type_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Types"), wxVERTICAL);

	wxString types_choices[] = { "Depot",
								 "Mailbox",
								 "Trash Holder",
								 "Container",
								 "Door",
								 "Magic Field",
								 "Teleport",
								 "Bed",
								 "Key",
								 "Podium" };

	int types_choices_count = sizeof(types_choices) / sizeof(wxString);
	types_radio_box = newd wxRadioBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, types_choices_count, types_choices, 1, wxRA_SPECIFY_COLS);
	types_radio_box->SetSelection(0);
	types_radio_box->Enable(false);
	type_box_sizer->Add(types_radio_box, 0, wxALL | wxEXPAND, 5);

	box_sizer->Add(type_box_sizer, 1, wxALL | wxEXPAND, 5);

	// --------------- Properties ---------------

	wxStaticBoxSizer* properties_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Properties"), wxVERTICAL);

	// Create property checkboxes with 3-state support
	unpassable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Unpassable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(unpassable, 0, wxALL, 5);

	unmovable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Unmovable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(unmovable, 0, wxALL, 5);

	block_missiles = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Block Missiles", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(block_missiles, 0, wxALL, 5);

	block_pathfinder = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Block Pathfinder", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(block_pathfinder, 0, wxALL, 5);

	readable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Readable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(readable, 0, wxALL, 5);

	writeable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Writeable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(writeable, 0, wxALL, 5);

	pickupable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Pickupable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	pickupable->Set3StateValue(only_pickupables ? wxCHK_CHECKED : wxCHK_UNCHECKED);
	pickupable->Enable(!only_pickupables);
	properties_box_sizer->Add(pickupable, 0, wxALL, 5);

	stackable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Stackable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(stackable, 0, wxALL, 5);

	rotatable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Rotatable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(rotatable, 0, wxALL, 5);

	hangable = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hangable", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(hangable, 0, wxALL, 5);

	hook_east = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hook East", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(hook_east, 0, wxALL, 5);

	hook_south = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Hook South", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(hook_south, 0, wxALL, 5);

	has_elevation = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Has Elevation", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(has_elevation, 0, wxALL, 5);

	ignore_look = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Ignore Look", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(ignore_look, 0, wxALL, 5);

	floor_change = newd wxCheckBox(properties_box_sizer->GetStaticBox(), wxID_ANY, "Floor Change", wxDefaultPosition, wxDefaultSize, wxCHK_3STATE | wxCHK_ALLOW_3RD_STATE_FOR_USER);
	properties_box_sizer->Add(floor_change, 0, wxALL, 5);

	// Add a tooltip explaining the states
	wxString tooltip = "Click to cycle through states:\n[ ] Ignore this property\n[V] Must have this property\n[-] Must NOT have this property";
	for(wxCheckBox* checkbox : {unpassable, unmovable, block_missiles, block_pathfinder, readable, writeable, 
		pickupable, stackable, rotatable, hangable, hook_east, hook_south, has_elevation, ignore_look, floor_change}) {
		checkbox->SetToolTip(tooltip);
	}

	box_sizer->Add(properties_box_sizer, 1, wxALL | wxEXPAND, 5);

	// --------------- Items list ---------------

	wxStaticBoxSizer* result_box_sizer = newd wxStaticBoxSizer(newd wxStaticBox(this, wxID_ANY, "Result"), wxVERTICAL);

	// Add a horizontal sizer for the refresh button and replace size
	wxBoxSizer* result_controls_sizer = newd wxBoxSizer(wxHORIZONTAL);

	// Add refresh button
	refresh_button = newd wxButton(result_box_sizer->GetStaticBox(), wxID_ANY, "Refresh", wxDefaultPosition, wxDefaultSize);
	result_controls_sizer->Add(refresh_button, 0, wxALL, 5);

	// Add replace size control
	result_controls_sizer->Add(newd wxStaticText(result_box_sizer->GetStaticBox(), wxID_ANY, "Max Results:"), 0, wxALL | wxALIGN_CENTER_VERTICAL, 5);
	replace_size_spin = newd wxSpinCtrl(result_box_sizer->GetStaticBox(), wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(80, -1), wxSP_ARROW_KEYS, 100, 10000, g_settings.getInteger(Config::REPLACE_SIZE));
	result_controls_sizer->Add(replace_size_spin, 0, wxALL, 5);

	result_box_sizer->Add(result_controls_sizer, 0, wxEXPAND, 5);
	items_list = newd FindDialogListBox(result_box_sizer->GetStaticBox(), wxID_ANY);
	items_list->SetMinSize(wxSize(230, 512));
	result_box_sizer->Add(items_list, 0, wxALL, 5);

	box_sizer->Add(result_box_sizer, 1, wxALL | wxEXPAND, 5);

	this->SetSizer(box_sizer);
	this->Layout();
	this->Centre(wxBOTH);
	this->EnableProperties(false);
	this->RefreshContentsInternal();

	// Connect Events
	options_radio_box->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), NULL, this);
	server_id_spin->Connect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), NULL, this);
	server_id_spin->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), NULL, this);
	client_id_spin->Connect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), NULL, this);
	client_id_spin->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), NULL, this);
	name_text_input->Connect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), NULL, this);

	types_radio_box->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), NULL, this);

	unpassable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	unmovable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	block_missiles->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	block_pathfinder->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	readable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	writeable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	pickupable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	stackable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	rotatable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hangable->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hook_east->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hook_south->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	has_elevation->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	ignore_look->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	floor_change->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	invalid_item->Connect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);

	// Connect the refresh button event
	refresh_button->Connect(wxEVT_BUTTON, wxCommandEventHandler(FindItemDialog::OnRefreshClick), NULL, this);
	replace_size_spin->Connect(wxEVT_SPINCTRL, wxCommandEventHandler(FindItemDialog::OnReplaceSizeChange), NULL, this);
}

FindItemDialog::~FindItemDialog() {
	// Disconnect Events
	options_radio_box->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnOptionChange), NULL, this);
	server_id_spin->Disconnect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), NULL, this);
	server_id_spin->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnServerIdChange), NULL, this);
	client_id_spin->Disconnect(wxEVT_COMMAND_SPINCTRL_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), NULL, this);
	client_id_spin->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnClientIdChange), NULL, this);
	name_text_input->Disconnect(wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler(FindItemDialog::OnText), NULL, this);

	types_radio_box->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(FindItemDialog::OnTypeChange), NULL, this);

	unpassable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	unmovable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	block_missiles->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	block_pathfinder->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	readable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	writeable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	pickupable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	stackable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	rotatable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hangable->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hook_east->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	hook_south->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	has_elevation->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	ignore_look->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	floor_change->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);
	invalid_item->Disconnect(wxEVT_COMMAND_CHECKBOX_CLICKED, wxCommandEventHandler(FindItemDialog::OnPropertyChange), NULL, this);

	// Disconnect the refresh button event
	refresh_button->Disconnect(wxEVT_BUTTON, wxCommandEventHandler(FindItemDialog::OnRefreshClick), NULL, this);
	replace_size_spin->Disconnect(wxEVT_SPINCTRL, wxCommandEventHandler(FindItemDialog::OnReplaceSizeChange), NULL, this);
}

FindItemDialog::SearchMode FindItemDialog::getSearchMode() const {
	return (SearchMode)options_radio_box->GetSelection();
}

void FindItemDialog::setSearchMode(FindItemDialog::SearchMode mode) {
	if ((SearchMode)options_radio_box->GetSelection() != mode) {
		options_radio_box->SetSelection(mode);
	}

	server_id_spin->Enable(mode == SearchMode::ServerIDs);
	invalid_item->Enable(mode == SearchMode::ServerIDs);
	client_id_spin->Enable(mode == SearchMode::ClientIDs);
	name_text_input->Enable(mode == SearchMode::Names);
	types_radio_box->Enable(mode == SearchMode::Types);
	EnableProperties(mode == SearchMode::Properties);
	RefreshContentsInternal();

	if (mode == SearchMode::ServerIDs) {
		server_id_spin->SetFocus();
		server_id_spin->SetSelection(-1, -1);
	} else if (mode == SearchMode::ClientIDs) {
		client_id_spin->SetFocus();
		client_id_spin->SetSelection(-1, -1);
	} else if (mode == SearchMode::Names) {
		name_text_input->SetFocus();
	}

	range_input->Enable(mode == SearchMode::ServerIDs || mode == SearchMode::ClientIDs);
	use_range->Enable(mode == SearchMode::ServerIDs || mode == SearchMode::ClientIDs);

	// Update tooltip based on mode
	if (mode == SearchMode::ServerIDs) {
		range_input->SetToolTip("Enter Server IDs or ranges separated by commas (e.g., 2222,2244-2266,5219)");
	} else if (mode == SearchMode::ClientIDs) {
		range_input->SetToolTip("Enter Client IDs or ranges separated by commas (e.g., 2222,2244-2266,5219)");
	}
}

void FindItemDialog::EnableProperties(bool enable) {
	unpassable->Enable(enable);
	unmovable->Enable(enable);
	block_missiles->Enable(enable);
	block_pathfinder->Enable(enable);
	readable->Enable(enable);
	writeable->Enable(enable);
	pickupable->Enable(!only_pickupables && enable);
	stackable->Enable(enable);
	rotatable->Enable(enable);
	hangable->Enable(enable);
	hook_east->Enable(enable);
	hook_south->Enable(enable);
	has_elevation->Enable(enable);
	ignore_look->Enable(enable);
	floor_change->Enable(enable);
}

void FindItemDialog::RefreshContentsInternal() {
	items_list->Clear();
	bool found_search_results = false;
	
	// Parse ignored IDs if the checkbox is checked
	if (ignore_ids_checkbox->GetValue()) {
		ParseIgnoredIDs();
	}
	
	SearchMode selection = (SearchMode)options_radio_box->GetSelection();
	if(selection == SearchMode::ServerIDs) {
		if(use_range->GetValue()) {
			// Parse the range string into pairs
			auto ranges = ParseRangeString(range_input->GetValue());
			
			for(uint16_t id = 100; id <= g_items.getMaxID(); ++id) {
				if(items_list->GetItemCount() >= size_t(replace_size_spin->GetValue())) {
					break;
				}
				
				// Check if ID is in any of the specified ranges
				if(!IsInRanges(id, ranges)) {
					continue;
				}
				
				ItemType& item = g_items[id];
				if(item.id == 0) continue;
				
				RAWBrush* raw_brush = item.raw_brush;
				if(!raw_brush) continue;
				
				if(only_pickupables && !item.pickupable) continue;
				
				found_search_results = true;
				items_list->AddBrush(raw_brush);
			}
		} else {
			result_id = std::min(server_id_spin->GetValue(), 0xFFFF);
			uint16_t serverID = static_cast<uint16_t>(result_id);
			if(serverID <= g_items.getMaxID()) {
				// Check if ID is ignored
				bool is_ignored = false;
				if (ignore_ids_checkbox->GetValue()) {
					if (std::find(ignored_ids.begin(), ignored_ids.end(), serverID) != ignored_ids.end()) {
						is_ignored = true;
					}
					for (const auto& range : ignored_ranges) {
						if (serverID >= range.first && serverID <= range.second) {
							is_ignored = true;
							break;
						}
					}
				}
				if (!is_ignored) {
					ItemType& item = g_items.getItemType(serverID);
					RAWBrush* raw_brush = item.raw_brush;
					if(raw_brush) {
						if(only_pickupables) {
							if(item.pickupable) {
								found_search_results = true;
								items_list->AddBrush(raw_brush);
							}
						} else {
							found_search_results = true;
							items_list->AddBrush(raw_brush);
						}
					}
				}
			}
			
			if (invalid_item->GetValue()) {
				found_search_results = true;
			}
		}
	 } else if (selection == SearchMode::ClientIDs) {
        if (use_range->GetValue()) {
            // Parse the range string into pairs
            auto ranges = ParseRangeString(range_input->GetValue());
            
            for (int id = 100; id <= g_items.getMaxID(); ++id) {
                if(items_list->GetItemCount() >= size_t(replace_size_spin->GetValue())) {
                    break;
                }

                ItemType& item = g_items.getItemType(id);
                if (item.id == 0) continue;

                // Check if clientID is in any of the specified ranges
                if (!IsInRanges(item.clientID, ranges)) {
                    continue;
                }

                // Check if clientID is ignored
                bool is_ignored = false;
                if (ignore_ids_checkbox->GetValue()) {
                    if (std::find(ignored_ids.begin(), ignored_ids.end(), item.clientID) != ignored_ids.end()) {
                        is_ignored = true;
                    }
                    for (const auto& range : ignored_ranges) {
                        if (item.clientID >= range.first && item.clientID <= range.second) {
                            is_ignored = true;
                            break;
                        }
                    }
                }
                if (is_ignored) continue;

                RAWBrush* raw_brush = item.raw_brush;
                if (!raw_brush) continue;

                if (only_pickupables && !item.pickupable) continue;

                found_search_results = true;
                items_list->AddBrush(raw_brush);
            }
        } else {
            uint16_t clientID = (uint16_t)client_id_spin->GetValue();
            
            // Check if clientID is ignored
            bool is_ignored = false;
            if (ignore_ids_checkbox->GetValue()) {
                if (std::find(ignored_ids.begin(), ignored_ids.end(), clientID) != ignored_ids.end()) {
                    is_ignored = true;
                }
                for (const auto& range : ignored_ranges) {
                    if (clientID >= range.first && clientID <= range.second) {
                        is_ignored = true;
                        break;
                    }
                }
            }
            if (!is_ignored) {
                for (int id = 100; id <= g_items.getMaxID(); ++id) {
                    ItemType& item = g_items.getItemType(id);
                    if (item.id == 0 || item.clientID != clientID) {
                        continue;
                    }

                    RAWBrush* raw_brush = item.raw_brush;
                    if (!raw_brush) {
                        continue;
                    }

                    if (only_pickupables && !item.pickupable) {
                        continue;
                    }

                    found_search_results = true;
                    items_list->AddBrush(raw_brush);
                }
            }
        }
	} else if (selection == SearchMode::Names) {
		std::string search_string = as_lower_str(nstr(name_text_input->GetValue()));
		if (search_string.size() >= 2) {
			for (int id = 100; id <= g_items.getMaxID(); ++id) {
				ItemType& item = g_items.getItemType(id);
				if (item.id == 0) {
					continue;
				}

				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) {
					continue;
				}

				if (only_pickupables && !item.pickupable) {
					continue;
				}

				if (as_lower_str(raw_brush->getName()).find(search_string) == std::string::npos) {
					continue;
				}

				found_search_results = true;
				items_list->AddBrush(raw_brush);
			}
		}
	} else if (selection == SearchMode::Types) {
		for (int id = 100; id <= g_items.getMaxID(); ++id) {
			ItemType& item = g_items.getItemType(id);
			if (item.id == 0) {
				continue;
			}

			RAWBrush* raw_brush = item.raw_brush;
			if (!raw_brush) {
				continue;
			}

			if (only_pickupables && !item.pickupable) {
				continue;
			}

			SearchItemType selection = (SearchItemType)types_radio_box->GetSelection();
			if ((selection == SearchItemType::Depot && !item.isDepot()) || (selection == SearchItemType::Mailbox && !item.isMailbox()) || (selection == SearchItemType::TrashHolder && !item.isTrashHolder()) || (selection == SearchItemType::Container && !item.isContainer()) || (selection == SearchItemType::Door && !item.isDoor()) || (selection == SearchItemType::MagicField && !item.isMagicField()) || (selection == SearchItemType::Teleport && !item.isTeleport()) || (selection == SearchItemType::Bed && !item.isBed()) || (selection == SearchItemType::Key && !item.isKey()) || (selection == SearchItemType::Podium && !item.isPodium())) {
				continue;
			}

			found_search_results = true;
			items_list->AddBrush(raw_brush);
		}
	} else if (selection == SearchMode::Properties) {
		bool has_selected = false;
		// Check if any checkbox is not in unchecked state
		for(wxCheckBox* checkbox : {unpassable, unmovable, block_missiles, block_pathfinder, readable, writeable, 
			pickupable, stackable, rotatable, hangable, hook_east, hook_south, has_elevation, ignore_look, floor_change}) {
			if(checkbox->Get3StateValue() != wxCHK_UNCHECKED) {
				has_selected = true;
				break;
			}
		}

		if (has_selected) {
			for (int id = 100; id <= g_items.getMaxID(); ++id) {
				ItemType& item = g_items.getItemType(id);
				if (item.id == 0) continue;

				RAWBrush* raw_brush = item.raw_brush;
				if (!raw_brush) continue;

				// Check each property considering all three states
				if ((unpassable->Get3StateValue() == wxCHK_CHECKED && !item.unpassable) ||
					(unpassable->Get3StateValue() == wxCHK_UNDETERMINED && item.unpassable) ||
					(unmovable->Get3StateValue() == wxCHK_CHECKED && item.moveable) ||
					(unmovable->Get3StateValue() == wxCHK_UNDETERMINED && !item.moveable) ||
					(block_missiles->Get3StateValue() == wxCHK_CHECKED && !item.blockMissiles) ||
					(block_missiles->Get3StateValue() == wxCHK_UNDETERMINED && item.blockMissiles) ||
					(block_pathfinder->Get3StateValue() == wxCHK_CHECKED && !item.blockPathfinder) ||
					(block_pathfinder->Get3StateValue() == wxCHK_UNDETERMINED && item.blockPathfinder) ||
					(readable->Get3StateValue() == wxCHK_CHECKED && !item.canReadText) ||
					(readable->Get3StateValue() == wxCHK_UNDETERMINED && item.canReadText) ||
					(writeable->Get3StateValue() == wxCHK_CHECKED && !item.canWriteText) ||
					(writeable->Get3StateValue() == wxCHK_UNDETERMINED && item.canWriteText) ||
					(pickupable->Get3StateValue() == wxCHK_CHECKED && !item.pickupable) ||
					(pickupable->Get3StateValue() == wxCHK_UNDETERMINED && item.pickupable) ||
					(stackable->Get3StateValue() == wxCHK_CHECKED && !item.stackable) ||
					(stackable->Get3StateValue() == wxCHK_UNDETERMINED && item.stackable) ||
					(rotatable->Get3StateValue() == wxCHK_CHECKED && !item.rotable) ||
					(rotatable->Get3StateValue() == wxCHK_UNDETERMINED && item.rotable) ||
					(hangable->Get3StateValue() == wxCHK_CHECKED && !item.isHangable) ||
					(hangable->Get3StateValue() == wxCHK_UNDETERMINED && item.isHangable) ||
					(hook_east->Get3StateValue() == wxCHK_CHECKED && !item.hookEast) ||
					(hook_east->Get3StateValue() == wxCHK_UNDETERMINED && item.hookEast) ||
					(hook_south->Get3StateValue() == wxCHK_CHECKED && !item.hookSouth) ||
					(hook_south->Get3StateValue() == wxCHK_UNDETERMINED && item.hookSouth) ||
					(has_elevation->Get3StateValue() == wxCHK_CHECKED && !item.hasElevation) ||
					(has_elevation->Get3StateValue() == wxCHK_UNDETERMINED && item.hasElevation) ||
					(ignore_look->Get3StateValue() == wxCHK_CHECKED && !item.ignoreLook) ||
					(ignore_look->Get3StateValue() == wxCHK_UNDETERMINED && item.ignoreLook) ||
					(floor_change->Get3StateValue() == wxCHK_CHECKED && !item.floorChangeDown && !item.floorChangeNorth && !item.floorChangeSouth && !item.floorChangeEast && !item.floorChangeWest) ||
					(floor_change->Get3StateValue() == wxCHK_UNDETERMINED && (item.floorChangeDown || item.floorChangeNorth || item.floorChangeSouth || item.floorChangeEast || item.floorChangeWest))) {
					continue;
				}

				found_search_results = true;
				items_list->AddBrush(raw_brush);
			}
		}
	}

	if (found_search_results) {
		items_list->SetSelection(0);
		ok_button->Enable(true);
	} else {
		items_list->SetNoMatches();
	}

	items_list->Refresh();
}

void FindItemDialog::OnOptionChange(wxCommandEvent& WXUNUSED(event)) {
	setSearchMode((SearchMode)options_radio_box->GetSelection());
}

void FindItemDialog::OnServerIdChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnClientIdChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnText(wxCommandEvent& WXUNUSED(event)) {
	input_timer.Start(800, true);
}

void FindItemDialog::OnTypeChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnPropertyChange(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnInputTimer(wxTimerEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnClickOK(wxCommandEvent& event) {
	if (!g_gui.IsEditorOpen()) return;

	if (invalid_item->GetValue()) {
		result_brush = nullptr;
		result_id = 0;
		EndModal(wxID_OK);
		return;
	}

	if (items_list->GetItemCount() != 0) {
		Brush* brush = items_list->GetSelectedBrush();
		if (brush) {
			result_brush = brush;
			result_id = brush->asRaw()->getItemID();

			// If remove checkbox is checked, handle item removal
			if (remove_found_items->GetValue()) {
				Editor* editor = g_gui.GetCurrentEditor();
				if (editor) {
					editor->actionQueue->clear();  // Clear previous actions
					g_gui.CreateLoadBar("Searching items to remove...");

					int64_t count = 0;
					
					if (getUseRange()) {
						// Handle range-based removal
						auto ranges = ParseRangeString(GetRangeInput());
						if (!ranges.empty()) {
							struct RangeRemoveCondition {
								std::vector<std::pair<uint16_t, uint16_t>> ranges;
								
								RangeRemoveCondition(const std::vector<std::pair<uint16_t, uint16_t>>& r) : ranges(r) {}
								
								bool operator()(Map& map, Item* item, long long removed, long long done) {
									if (done % 0x800 == 0) {
										g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
									}
									
									for (const auto& range : ranges) {
										if (item->getID() >= range.first && item->getID() <= range.second) {
											return true;
										}
									}
									return false;
								}
							} condition(ranges);
							
							count = RemoveItemOnMap(editor->map, condition, editor->selection.size() > 0);
						}
					} else {
						// Handle single item removal
						struct SingleItemRemoveCondition {
							uint16_t itemId;
							SingleItemRemoveCondition(uint16_t id) : itemId(id) {}
							
							bool operator()(Map& map, Item* item, long long removed, long long done) {
								if (done % 0x800 == 0) {
									g_gui.SetLoadDone((unsigned int)(100 * done / map.getTileCount()));
								}
								return item->getID() == itemId;
							}
						} condition(result_id);
						
						count = RemoveItemOnMap(editor->map, condition, editor->selection.size() > 0);
					}
					
					g_gui.DestroyLoadBar();

					wxString msg;
					msg << count << " items removed.";
					g_gui.PopupDialog("Remove Items", msg, wxOK);
					
					editor->map.doChange();
					g_gui.RefreshView();
				}
			}

			EndModal(wxID_OK);
		}
	}
}

void FindItemDialog::OnClickCancel(wxCommandEvent& WXUNUSED(event)) {
	EndModal(wxID_CANCEL);
}

void FindItemDialog::OnRefreshClick(wxCommandEvent& WXUNUSED(event)) {
	RefreshContentsInternal();
}

void FindItemDialog::OnReplaceSizeChange(wxCommandEvent& WXUNUSED(event)) {
	g_settings.setInteger(Config::REPLACE_SIZE, replace_size_spin->GetValue());
}

void FindItemDialog::ParseIgnoredIDs() {
	OutputDebugStringA("ParseIgnoredIDs called\n");
	
	ignored_ids.clear();
	ignored_ranges.clear();
	
	std::string input = as_lower_str(nstr(ignore_ids_text->GetValue()));
	OutputDebugStringA(wxString::Format("Parsing ignored IDs input: %s\n", input).c_str());
	
	std::vector<std::string> parts = splitString(input, ',');
	
	for (const auto& part : parts) {
		if (part.find('-') != std::string::npos) {
			std::vector<std::string> range = splitString(part, '-');
			if (range.size() == 2 && isInteger(range[0]) && isInteger(range[1])) {
				uint16_t from = static_cast<uint16_t>(std::stoi(range[0]));
				uint16_t to = static_cast<uint16_t>(std::stoi(range[1]));
				if (from <= to) {
					ignored_ranges.emplace_back(from, to);
					OutputDebugStringA(wxString::Format("Added ignored range: %d-%d\n", from, to).c_str());
				}
			}
		} else {
			if (isInteger(part)) {
				uint16_t id = static_cast<uint16_t>(std::stoi(part));
				ignored_ids.push_back(id);
				OutputDebugStringA(wxString::Format("Added ignored ID: %d\n", id).c_str());
			}
		}
	}
	
	OutputDebugStringA(wxString::Format("Total ignored IDs: %zu, Total ranges: %zu\n", 
		ignored_ids.size(), ignored_ranges.size()).c_str());
}

namespace RME {

// Helper function to split a string by a delimiter
std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter)) {
        // Optionally trim whitespace from each token
        token.erase(token.find_last_not_of(" \n\r\t")+1);
        token.erase(0, token.find_first_not_of(" \n\r\t"));
        tokens.push_back(token);
    }
    return tokens;
}

// Helper function to check if a string represents a valid integer
bool isInteger(const std::string& s) {
    if (s.empty()) return false;
    return std::all_of(s.begin(), s.end(), ::isdigit);
}

} // namespace RME

std::vector<std::pair<uint16_t, uint16_t>> FindItemDialog::ParseRangeString(const wxString& input) {
    std::vector<std::pair<uint16_t, uint16_t>> ranges;
    std::string str = as_lower_str(nstr(input));
    std::vector<std::string> parts = splitString(str, ',');
    
    for(const auto& part : parts) {
        if(part.find('-') != std::string::npos) {
            std::vector<std::string> range = splitString(part, '-');
            if(range.size() == 2 && isInteger(range[0]) && isInteger(range[1])) {
                uint16_t from = static_cast<uint16_t>(std::stoi(range[0]));
                uint16_t to = static_cast<uint16_t>(std::stoi(range[1]));
                if(from <= to) {
                    ranges.emplace_back(from, to);
                }
            }
        } else if(isInteger(part)) {
            uint16_t id = static_cast<uint16_t>(std::stoi(part));
            ranges.emplace_back(id, id);
        }
    }
    return ranges;
}

bool FindItemDialog::IsInRanges(uint16_t id, const std::vector<std::pair<uint16_t, uint16_t>>& ranges) {
    for(const auto& range : ranges) {
        if(id >= range.first && id <= range.second) {
            return true;
        }
    }
    return false;
}

