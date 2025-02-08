#include "main.h"
#include "hotkey_manager.h"
#include "settings.h"
#include "gui.h"
#include <wx/listctrl.h>
#include <wx/slider.h>
#include <pugixml.hpp>
#include <set>
#include <algorithm>

/*
Task: Fix Hotkey Manager Update Issue

Current Behavior:
- Hotkeys are saved to menubar.xml successfully
- Changes appear in the XML file
- Old hotkeys remain active in the current session
- ApplyHotkeys() creates a save/load loop
- Multiple saves occur during a single update

Expected Behavior:
- Hotkeys should be saved to menubar.xml
- New hotkeys should become active immediately
- Old hotkeys should be deactivated
- Single save operation per update
- Clean reload of hotkey configuration

Affected Files:
1. source/hotkey_manager.cpp
   - ShowHotkeyDialog() [lines: 163-406]
   - ApplyHotkeys() [lines: 409-453]
   - LoadHotkeys() [lines: 34-70]

2. source/main_menubar.cpp
   - Update() [lines: 468-478]
   - MainMenuBar class implementation

3. source/application.cpp
   - MainFrame::UpdateMenubar() [lines: 455-458]
   - MSWTranslateMessage() [lines: 441-452]

4. source/hotkey_manager.h
   - HotkeyManager class definition [lines: 11-41]

Key Functions to Modify:
1. HotkeyManager::ApplyHotkeys()
   - Remove redundant save operations
   - Implement clean reload mechanism
   - Update UI without triggering loops

2. HotkeyManager::LoadHotkeys()
   - Ensure proper loading from XML
   - Maintain callbacks during reload

3. MainFrame::UpdateMenubar()
   - Ensure proper UI update after hotkey changes

Dependencies:
- wxWidgets menu system
- pugixml for XML handling
- Settings management system
- GUI update mechanisms
*/


HotkeyManager g_hotkey_manager;

wxString ModifierKeyToString(int keyCode) {
    if (keyCode == WXK_SHIFT) return "Shift+";
    if (keyCode == WXK_CONTROL) return "Ctrl+";
    if (keyCode == WXK_ALT) return "Alt+";
    return "";
}

HotkeyManager::HotkeyManager() {
    // Constructor is called before g_settings is initialized
    // We'll load hotkeys later when needed
}

void HotkeyManager::RegisterHotkey(const std::string& name, const std::string& defaultKey,
                                 const std::string& description, std::function<void()> callback) {
    // Store the complete information
    hotkeys[name] = {defaultKey, description, callback};
    
    // No need to sort - map maintains order by key
    // Priority handling is done during hotkey processing
}

void HotkeyManager::LoadHotkeys() {
    // Clear existing hotkeys before loading
    hotkeys.clear();
    
    // First load default hotkeys from menubar.xml
    wxString path = g_gui.GetDataDirectory() + "\\menubar.xml";
    pugi::xml_document doc;
    
    OutputDebugStringA(wxString::Format("Loading menubar.xml from: %s\n", path).c_str());
    
    if (doc.load_file(path.mb_str())) {
        pugi::xml_node menubar = doc.child("menubar");
        if (menubar) {
            LoadHotkeysFromNode(menubar);
            OutputDebugStringA(wxString::Format("Loaded %zu hotkeys from menubar.xml\n", hotkeys.size()).c_str());
        } else {
            OutputDebugStringA("Failed to find menubar node in XML\n");
        }
    } else {
        OutputDebugStringA(wxString::Format("Failed to load menubar.xml from: %s\n", path).c_str());
    }

    // Then override with any saved custom hotkeys if settings are available
    if (&g_settings != nullptr) {
        size_t index = 0;
        for (auto& [name, info] : hotkeys) {
            uint32_t settingKey = Config::HOTKEY_BASE + index;
            std::string savedKey = g_settings.getString(settingKey);
            if (!savedKey.empty()) {
                info.key = savedKey;
            }
            index++;
        }
    }
    
    ApplyHotkeys();
}

void HotkeyManager::LoadHotkeysFromNode(pugi::xml_node& node) {
    // Process this node's items
    for (pugi::xml_node item = node.child("item"); item; item = item.next_sibling("item")) {
        std::string name = item.attribute("name").as_string();
        std::string hotkey = item.attribute("hotkey").as_string();
        std::string action = item.attribute("action").as_string();
        std::string help = item.attribute("help").as_string();
        
        if (!action.empty() && !hotkey.empty()) {
            OutputDebugStringA(wxString::Format("Loading hotkey: %s -> %s (%s)\n", 
                action.c_str(), hotkey.c_str(), help.c_str()).c_str());
            RegisterHotkey(action, hotkey, help, nullptr);
        }
    }

    // Recursively process all menu nodes
    for (pugi::xml_node menu = node.child("menu"); menu; menu = menu.next_sibling("menu")) {
        std::string menuName = menu.attribute("name").as_string();
        OutputDebugStringA(wxString::Format("Processing menu: %s\n", menuName.c_str()).c_str());
        LoadHotkeysFromNode(menu);
    }
}

void HotkeyManager::SaveHotkeys() {
    size_t index = 0;
    for (const auto& [name, info] : hotkeys) {
        uint32_t settingKey = Config::HOTKEY_BASE + index;
        g_settings.setString(settingKey, info.key);
        index++;
    }
}

bool IsValidModifier(const wxString& modifier) {
    return modifier == "Ctrl" || modifier == "Alt" || modifier == "Shift";
}

bool IsValidKey(const wxString& key) {
    // Single character A-Z
    if (key.length() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
        return true;
    }
    
    // Function keys F1-F12
    if (key.StartsWith("F") && key.length() <= 3) {
        long num;
        wxString numStr = key.Mid(1);
        if (numStr.ToLong(&num)) {
            return num >= 1 && num <= 12;
        }
    }
    
    // Add other valid special keys as needed
    static const wxString validSpecialKeys[] = {
        "Space", "Tab", "Enter", "Esc",
        "Left", "Right", "Up", "Down",
        "Home", "End", "PgUp", "PgDn",
        "Insert", "Delete", "Plus", "Minus"
    };
    
    for (const auto& specialKey : validSpecialKeys) {
        if (key == specialKey) return true;
    }
    
    return false;
}

bool ValidateHotkeyString(const wxString& hotkey, wxString& error) {
    if (hotkey.empty()) {
        return true; // Empty hotkey is valid (removes hotkey)
    }
    
    wxArrayString parts = wxSplit(hotkey, '+');
    
    // Check if last part is a valid key
    if (parts.IsEmpty() || !IsValidKey(parts.Last())) {
        error = "Invalid key. Must be A-Z, F1-F12, or a special key";
        return false;
    }
    
    // Check modifiers
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        if (!IsValidModifier(parts[i].Trim())) {
            error = "Invalid modifier. Must be Ctrl, Alt, or Shift";
            return false;
        }
    }
    
    return true;
}

void HotkeyManager::ShowHotkeyDialog(wxWindow* parent) {
    wxDialog* dialog = new wxDialog(parent, wxID_ANY, "Hotkey Configuration", 
                                  wxDefaultPosition, wxSize(600, 500));
    
    wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
    
    // Create list control
    wxListCtrl* hotkeyList = new wxListCtrl(dialog, wxID_ANY, wxDefaultPosition, 
                                           wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
    hotkeyList->InsertColumn(0, "Menu", wxLIST_FORMAT_LEFT, 150);
    hotkeyList->InsertColumn(1, "Action", wxLIST_FORMAT_LEFT, 200);
    hotkeyList->InsertColumn(2, "Hotkey", wxLIST_FORMAT_LEFT, 150);
    
    // Load directly from menubar.xml
    wxString path = g_gui.GetDataDirectory() + "\\menubar.xml";
    pugi::xml_document doc;
    
    if (doc.load_file(path.mb_str())) {
        pugi::xml_node menubar = doc.child("menubar");
        if (menubar) {
            long idx = 0;
            for (pugi::xml_node menu = menubar.child("menu"); menu; menu = menu.next_sibling("menu")) {
                std::string menuName = menu.attribute("name").as_string();
                
                for (pugi::xml_node item = menu.child("item"); item; item = item.next_sibling("item")) {
                    std::string hotkey = item.attribute("hotkey").as_string();
                    std::string action = item.attribute("action").as_string();
                    
                    if (!action.empty()) {  // Show all actions, even without hotkeys
                        hotkeyList->InsertItem(idx, wxString(menuName));
                        hotkeyList->SetItem(idx, 1, wxString(action));
                        hotkeyList->SetItem(idx, 2, wxString(hotkey));
                        idx++;
                    }
                }
            }
        }
    }

    // Add edit box for hotkey input
    wxBoxSizer* editSizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* label = new wxStaticText(dialog, wxID_ANY, "Hotkey:");
    wxTextCtrl* hotkeyEdit = new wxTextCtrl(dialog, wxID_ANY, "", 
        wxDefaultPosition, wxSize(150, -1), wxTE_PROCESS_ENTER | wxTE_PROCESS_TAB);

    // Make the text control read-only to prevent direct typing
    hotkeyEdit->SetEditable(false);

    // Add key event handling
    hotkeyEdit->Bind(wxEVT_KEY_DOWN, [this, hotkeyEdit](wxKeyEvent& event) {
        int keyCode = event.GetKeyCode();
        
        // Handle modifier keys
        if (keyCode == WXK_SHIFT || keyCode == WXK_CONTROL || keyCode == WXK_ALT) {
            currentModifiers.insert(keyCode);
            UpdateHotkeyString(hotkeyEdit);
            return;
        }
        
        // Handle regular keys only if they're valid
        if ((keyCode >= 'A' && keyCode <= 'Z') || 
            (keyCode >= '0' && keyCode <= '9') ||
            (keyCode >= WXK_F1 && keyCode <= WXK_F12)) {
            
            // Convert to uppercase if it's a letter
            if (keyCode >= 'a' && keyCode <= 'z') {
                keyCode = keyCode - 'a' + 'A';
            }
            
            wxString finalKey;
            if (keyCode >= WXK_F1 && keyCode <= WXK_F12) {
                int fKeyNum = keyCode - WXK_F1 + 1;
                finalKey = wxString::Format("F%d", fKeyNum);
            } else {
                finalKey = wxString(static_cast<wxChar>(keyCode));
            }
            
            // Build the complete hotkey string
            wxString hotkeyStr;
            if (currentModifiers.count(WXK_CONTROL)) hotkeyStr += "Ctrl+";
            if (currentModifiers.count(WXK_SHIFT)) hotkeyStr += "Shift+";
            if (currentModifiers.count(WXK_ALT)) hotkeyStr += "Alt+";
            hotkeyStr += finalKey;
            
            hotkeyEdit->SetValue(hotkeyStr);
            currentModifiers.clear();
            event.Skip(false);
            return;
        }
        
        // Block all other keys except backspace
        if (keyCode != WXK_BACK) {
            event.Skip(false);
        }
    });

    hotkeyEdit->Bind(wxEVT_KEY_UP, [this, hotkeyEdit](wxKeyEvent& event) {
        int keyCode = event.GetKeyCode();
        
        if (keyCode == WXK_BACK) {
            hotkeyEdit->SetValue("");
            currentModifiers.clear();
        } else if (keyCode == WXK_SHIFT || keyCode == WXK_CONTROL || keyCode == WXK_ALT) {
            currentModifiers.erase(keyCode);
            UpdateHotkeyString(hotkeyEdit);
        }
        event.Skip();
    });

    wxButton* setButton = new wxButton(dialog, wxID_ANY, "Set");
    editSizer->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    editSizer->Add(hotkeyEdit, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    editSizer->Add(setButton, 0);

    // Handle list selection
    hotkeyList->Bind(wxEVT_LIST_ITEM_SELECTED, [hotkeyList, hotkeyEdit](wxListEvent& event) {
        wxListItem item;
        item.SetId(event.GetIndex());
        item.SetColumn(2);  // Hotkey column
        item.SetMask(wxLIST_MASK_TEXT);
        hotkeyList->GetItem(item);
        hotkeyEdit->SetValue(item.GetText());
    });

    // Handle set button
    setButton->Bind(wxEVT_BUTTON, [this, hotkeyList, hotkeyEdit, dialog](wxCommandEvent& event) {
        wxString newHotkey = hotkeyEdit->GetValue();
        wxString error;
        
        // Get the selected item's action
        long selectedIndex = hotkeyList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if (selectedIndex == -1) {
            wxMessageBox("Please select an action first", "Error", wxOK | wxICON_ERROR);
            return;
        }
        
        wxListItem item;
        item.SetId(selectedIndex);
        item.SetColumn(1);  // Action column
        item.SetMask(wxLIST_MASK_TEXT);
        hotkeyList->GetItem(item);
        std::string action = item.GetText().ToStdString();
        
        // Validate the hotkey
        if (!ValidateHotkeyString(newHotkey, error)) {
            wxMessageBox(error, "Invalid Hotkey", wxOK | wxICON_ERROR);
            return;
        }
        
        // Check for duplicates
        for (const auto& [existingAction, info] : hotkeys) {
            if (existingAction != action && info.key == newHotkey.ToStdString()) {
                wxMessageBox("This hotkey is already assigned to: " + wxString(existingAction), 
                           "Duplicate Hotkey", wxOK | wxICON_ERROR);
                return;
            }
        }
        
        // Update the hotkey
        hotkeys[action].key = newHotkey.ToStdString();
        hotkeyList->SetItem(selectedIndex, 2, newHotkey);
        
        // Save changes
        SaveHotkeys();
        ApplyHotkeys();
    });

    // Main dialog layout
    mainSizer->Add(hotkeyList, 1, wxEXPAND | wxALL, 5);
    mainSizer->Add(editSizer, 0, wxEXPAND | wxALL, 5);
    
    // Add OK/Cancel buttons
    wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
    wxButton* saveButton = new wxButton(dialog, wxID_OK, "Save");
    wxButton* cancelButton = new wxButton(dialog, wxID_CANCEL, "Cancel");
    buttonSizer->Add(saveButton, 0, wxRIGHT, 5);
    buttonSizer->Add(cancelButton);
    mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, 5);
    
    dialog->SetSizer(mainSizer);
    
    if (dialog->ShowModal() == wxID_OK) {
        bool hasChanges = false;
        
        // Save changes to settings
        for (long item = 0; item < hotkeyList->GetItemCount(); ++item) {
            wxListItem listItem;
            listItem.SetId(item);
            
            // Get action
            listItem.SetColumn(1);
            listItem.SetMask(wxLIST_MASK_TEXT);
            hotkeyList->GetItem(listItem);
            std::string action = listItem.GetText().ToStdString();
            
            // Get hotkey
            listItem.SetColumn(2);
            hotkeyList->GetItem(listItem);
            std::string newHotkey = listItem.GetText().ToStdString();
            
            if (!action.empty()) {
                if (hotkeys[action].key != newHotkey) {
                    hotkeys[action].key = newHotkey;
                    hasChanges = true;
                }
            }
        }
        
        if (hasChanges) {
            SaveHotkeys();
            
            // Save to menubar.xml
            wxString path = g_gui.GetDataDirectory() + "\\menubar.xml";
            pugi::xml_document doc;
            
            if (doc.load_file(path.mb_str())) {
                bool xmlModified = false;
                
                // Update hotkeys in XML
                for (pugi::xml_node menu = doc.child("menubar").child("menu"); menu; 
                     menu = menu.next_sibling("menu")) {
                    for (pugi::xml_node item = menu.child("item"); item; 
                         item = item.next_sibling("item")) {
                        std::string action = item.attribute("action").as_string();
                        if (!action.empty() && hotkeys.count(action) > 0) {
                            pugi::xml_attribute hotkeyAttr = item.attribute("hotkey");
                            if (hotkeyAttr && hotkeyAttr.as_string() != hotkeys[action].key) {
                                hotkeyAttr.set_value(hotkeys[action].key.c_str());
                                xmlModified = true;
                            }
                        }
                    }
                }
                
                if (xmlModified) {
                    doc.save_file(path.mb_str());
                }
            }
            
            ApplyHotkeys();
        }
    }
    
    dialog->Destroy();
}


void HotkeyManager::ApplyHotkeys() {
    // First save to settings
    SaveHotkeys();
    
    // Then save to menubar.xml
    wxString path = g_gui.GetDataDirectory() + "\\menubar.xml";
    pugi::xml_document doc;
    
    if (doc.load_file(path.mb_str())) {
        bool xmlModified = false;
        pugi::xml_node menubar = doc.child("menubar");
        
        if (menubar) {
            // Update hotkeys in XML
            for (pugi::xml_node menu = menubar.child("menu"); menu; menu = menu.next_sibling("menu")) {
                for (pugi::xml_node item = menu.child("item"); item; item = item.next_sibling("item")) {
                    std::string action = item.attribute("action").as_string();
                    if (!action.empty() && hotkeys.count(action) > 0) {
                        pugi::xml_attribute hotkeyAttr = item.attribute("hotkey");
                        if (hotkeyAttr) {
                            if (hotkeyAttr.as_string() != hotkeys[action].key) {
                                hotkeyAttr.set_value(hotkeys[action].key.c_str());
                                xmlModified = true;
                            }
                        } else if (!hotkeys[action].key.empty()) {
                            item.append_attribute("hotkey").set_value(hotkeys[action].key.c_str());
                            xmlModified = true;
                        }
                    }
                }
            }
            
            // Save XML if modified
            if (xmlModified) {
                doc.save_file(path.mb_str());
                // Force reload of hotkeys
                LoadHotkeys();
                // Update GUI
                if (g_gui.root) {
                    g_gui.root->UpdateMenubar();
                }
            }
        }
    }
}

std::map<std::string, HotkeyManager::HotkeyInfo> HotkeyManager::GetAllHotkeys() const {
    return hotkeys;
}

wxString HotkeyManager::KeyCodeToString(int keyCode) {
    // Convert wxWidgets key code to string representation
    return wxAcceleratorEntry(wxACCEL_NORMAL, keyCode, 0).ToString();
}

int HotkeyManager::StringToKeyCode(const wxString& keyString) {
    // Convert string representation to wxWidgets key code
    wxAcceleratorEntry entry;
    entry.FromString(keyString);
    return entry.GetKeyCode();
}

void HotkeyManager::UpdateHotkeyString(wxTextCtrl* hotkeyEdit) {
    wxString hotkeyStr;
    if (currentModifiers.count(WXK_CONTROL)) hotkeyStr += "Ctrl+";
    if (currentModifiers.count(WXK_SHIFT)) hotkeyStr += "Shift+";
    if (currentModifiers.count(WXK_ALT)) hotkeyStr += "Alt+";
    hotkeyEdit->SetValue(hotkeyStr);
}

// ... implement other methods ... 