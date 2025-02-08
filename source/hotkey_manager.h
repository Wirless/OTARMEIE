#ifndef RME_HOTKEY_MANAGER_H
#define RME_HOTKEY_MANAGER_H

#include <map>
#include <string>
#include <functional>
#include <wx/accel.h>
#include <pugixml.hpp>
#include <set>

class HotkeyManager {
public:
    HotkeyManager();
    struct HotkeyInfo {
        std::string key;
        std::string description;
        std::function<void()> callback;
    };

    void RegisterHotkey(const std::string& name, const std::string& defaultKey, 
                       const std::string& description, std::function<void()> callback);
    void LoadHotkeys();
    void SaveHotkeys();
    void ShowHotkeyDialog(wxWindow* parent);
    std::map<std::string, HotkeyInfo> GetAllHotkeys() const;
    
    // Convert between wxKeyCode and string representation
    static wxString KeyCodeToString(int keyCode);
    static int StringToKeyCode(const wxString& keyString);

private:
    std::map<std::string, HotkeyInfo> hotkeys;
    std::set<int> currentModifiers;
    void ApplyHotkeys();
    void LoadHotkeysFromNode(pugi::xml_node& node);
    void UpdateHotkeyString(wxTextCtrl* hotkeyEdit);
    
    // Helper methods for hotkey dialog
    void UpdateHotkeyList();
    bool ValidateHotkeys();
};

extern HotkeyManager g_hotkey_manager;

#endif // RME_HOTKEY_MANAGER_H 