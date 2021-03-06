// WindowsProject1.cpp : Defines the entry point for the application.
//

#include "stdafx.h"

#include "TestFrame.h"

#include "WaterfallBitmap.h"


wxBEGIN_EVENT_TABLE(TestFrame, wxFrame)
wxEND_EVENT_TABLE()

TestFrame::TestFrame(MainWorker& main_worker, const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size), main_worker_(main_worker)
{
    Init();
}

TestFrame::~TestFrame()
{
}

extern "C" void MakeTrouble();

void TestFrame::Init()
{
    waterfall_.reset(new WaterfallBitmap(this, static_cast<wxWindowID>(EventIds::BUTTON_Hello), wxPoint(0, 0), wxSize(400, 250)));

    //worker_.add_signal(&TestFrame::RunMe);

    //auto x = worker_.enqueue_work(&TestFrame::RunMe);
    //auto x = worker_.queue_work<void>(&TestFrame::RunMe);

    //auto status = x.wait_for(10ms);

    //std::cout << (int)status << '\n';

    //auto y = worker_.enqueue_work(&TestFrame::RunMe);

    //auto status_y = y.wait_for(11ms);

    //std::cout << (int)status_y << '\n';

    MakeTrouble();
}

void TestFrame::OnDoStuff(wxCommandEvent& event)
{

}

void TestFrame::RunMe()
{

}

