// WindowsProject1.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "TestFrame.h"
#include "WindowsProject1.h"

wxIMPLEMENT_APP_CONSOLE(HelloWorldApp);


HelloWorldApp::HelloWorldApp()
{ }

HelloWorldApp::~HelloWorldApp()
{ }


// This is executed upon startup, like 'main()' in non-wxWidgets programs.
bool HelloWorldApp::OnInit()
{
    std::unique_ptr<wxFrame> frame{ new TestFrame(worker_, L"Waterfall", wxDefaultPosition, wxDefaultSize) };
    
    frame->Show(true);
    
    SetTopWindow(frame.release());

    worker_.Start();

    return true;
}

int HelloWorldApp::OnExit()
{
    worker_.Stop();

    return true;
}
