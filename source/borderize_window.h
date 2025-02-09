#ifndef RME_BORDERIZE_WINDOW_H
#define RME_BORDERIZE_WINDOW_H

#include <wx/dialog.h>
#include "editor.h"

class wxGauge;
class wxStaticText;
class wxSpinCtrl;

class BorderizeWindow : public wxDialog {
public:
    BorderizeWindow(wxWindow* parent, Editor& editor);
    ~BorderizeWindow();

    void Start();
    void UpdateProgress(int current, int total);
    
private:
    void OnClickNext(wxCommandEvent& event);
    void OnClickCancel(wxCommandEvent& event);
    void OnChunkSizeChange(wxSpinEvent& event);
    
    Editor& editor;
    wxGauge* progress;
    wxStaticText* status_text;
    wxButton* next_button;
    wxButton* cancel_button;
    wxSpinCtrl* chunk_size_spin;
    
    size_t current_chunk;
    size_t total_chunks;
    bool processing_whole_map;
    TileList remaining_tiles;
    
    DECLARE_EVENT_TABLE()
};

#endif 