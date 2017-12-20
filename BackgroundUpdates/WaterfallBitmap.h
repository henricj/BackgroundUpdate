#pragma once

class WaterfallBitmap : public wxControl
{
public:
    WaterfallBitmap() { Init(); }
    WaterfallBitmap(wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0,
        const wxValidator& validator = wxDefaultValidator,
        const wxString& name = wxButtonNameStr)
    {
        Init();

        Create(parent, id, pos, size, style, validator, name);
    }

    virtual ~WaterfallBitmap();

    bool Create(wxWindow *parent, wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = 0,
        const wxValidator& validator = wxDefaultValidator,
        const wxString& name = wxControlNameStr);
        
private:
    void Init();

    void OnPaintOffset(wxPaintEvent&);
    void OnEraseBackground(wxEraseEvent&);
    void OnTimerBump(wxTimerEvent&);

    std::unique_ptr<wxTimer> timer_;
    wxBitmap bitmap_;
    int offset_ = 0;

    wxDECLARE_DYNAMIC_CLASS(WaterfallBitmap);
    wxDECLARE_EVENT_TABLE();
};

