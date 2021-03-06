#pragma once

class WaterfallBitmap;
class MainWorker;

class TestFrame : public wxFrame
{
public:
    TestFrame(MainWorker& main_worker, const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~TestFrame();
private:
    void OnDoStuff(wxCommandEvent& event);

    void Init();

    static void RunMe();

    std::unique_ptr<WaterfallBitmap> waterfall_;

    MainWorker& main_worker_;

    wxDECLARE_EVENT_TABLE();
};

