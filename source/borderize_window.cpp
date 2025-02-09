#include "main.h"
#include "borderize_window.h"
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include "gui.h"

BEGIN_EVENT_TABLE(BorderizeWindow, wxDialog)
    EVT_BUTTON(wxID_ANY, BorderizeWindow::OnClickNext)
    
    EVT_SPINCTRL(wxID_ANY, BorderizeWindow::OnChunkSizeChange)
END_EVENT_TABLE()

BorderizeWindow::BorderizeWindow(wxWindow* parent, Editor& editor) :
    wxDialog(parent, wxID_ANY, "Borderize Progress", wxDefaultPosition, wxDefaultSize),
    editor(editor),
    current_chunk(0),
    total_chunks(0),
    processing_whole_map(false)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    
    // Add chunk size control
    wxBoxSizer* chunk_sizer = new wxBoxSizer(wxHORIZONTAL);
    chunk_sizer->Add(new wxStaticText(this, wxID_ANY, "Tiles per chunk:"), 0, wxALIGN_CENTER_VERTICAL | wxALL, 5);
    chunk_size_spin = new wxSpinCtrl(this, wxID_ANY, "500", wxDefaultPosition, wxDefaultSize,
        wxSP_ARROW_KEYS, 100, 10000, 500);
    chunk_sizer->Add(chunk_size_spin, 0, wxALL, 5);
    sizer->Add(chunk_sizer);
    
    status_text = new wxStaticText(this, wxID_ANY, "Preparing to borderize...");
    sizer->Add(status_text, 0, wxALL, 5);
    
    progress = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(300, 20));
    sizer->Add(progress, 0, wxEXPAND | wxALL, 5);
    
    wxBoxSizer* button_sizer = new wxBoxSizer(wxHORIZONTAL);
    next_button = new wxButton(this, wxID_ANY, "Process Next Chunk");

    
    button_sizer->Add(next_button, 1, wxALL, 5);
    button_sizer->Add(cancel_button, 1, wxALL, 5);
    sizer->Add(button_sizer, 0, wxEXPAND | wxALL, 5);
    
    SetSizer(sizer);
    Fit();
    Centre();

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
        // Clear remaining tiles and close without processing
        remaining_tiles.clear();
        EndModal(0);
    });
}

BorderizeWindow::~BorderizeWindow() {}

void BorderizeWindow::Start() {
    // Initialize the tile list based on whether we're processing selection or whole map
    if (editor.selection.size() > 0) {
        processing_whole_map = false;
        for (Tile* tile : editor.selection) {
            remaining_tiles.push_back(tile);
        }
    } else {
        processing_whole_map = true;
        for (TileLocation* tileLocation : editor.map) {
            if (Tile* tile = tileLocation->get()) {
                remaining_tiles.push_back(tile);
            }
        }
    }
    
    total_chunks = (remaining_tiles.size() + 499) / 500; // Process 500 tiles per chunk
    UpdateProgress(0, total_chunks);
    ShowModal();
}

void BorderizeWindow::UpdateProgress(int current, int total) {
    progress->SetValue((current * 100) / total);
    status_text->SetLabel(wxString::Format("Processing chunk %d of %d (%d tiles remaining)",
        current + 1, total, (int)remaining_tiles.size()));
}

void BorderizeWindow::OnChunkSizeChange(wxSpinEvent& event) {
    // Recalculate total chunks based on new chunk size
    if (!remaining_tiles.empty()) {
        total_chunks = (remaining_tiles.size() + chunk_size_spin->GetValue() - 1) / chunk_size_spin->GetValue();
        UpdateProgress(current_chunk, total_chunks);
    }
}

void BorderizeWindow::OnClickNext(wxCommandEvent& WXUNUSED(event)) {
    if (remaining_tiles.empty()) {
        EndModal(1);
        return;
    }
    
    // Process next chunk of tiles using the user-defined chunk size
    Action* action = editor.actionQueue->createAction(ACTION_BORDERIZE);
    
    size_t chunk_size = chunk_size_spin->GetValue();
    size_t tiles_to_process = std::min<size_t>(chunk_size, remaining_tiles.size());
    
    for (size_t i = 0; i < tiles_to_process; ++i) {
        Tile* tile = remaining_tiles.front();
        remaining_tiles.pop_front();
        
        Tile* newTile = tile->deepCopy(editor.map);
        newTile->borderize(&editor.map);
        if (!processing_whole_map) {
            newTile->select();
        }
        action->addChange(newd Change(newTile));
    }
    
    editor.addAction(action);
    current_chunk++;
    UpdateProgress(current_chunk, total_chunks);
    
    if (remaining_tiles.empty()) {
        next_button->SetLabel("Finish");
    }
    
    editor.map.doChange();
    // Refresh the view after each chunk
    g_gui.RefreshView();
}
