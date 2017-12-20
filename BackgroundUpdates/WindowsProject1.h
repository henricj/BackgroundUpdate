#pragma once

#include "resource.h"

#include "MainWorker.h"

class HelloWorldApp : public wxApp
{
    MainWorker worker_;
public:
    HelloWorldApp();
    ~HelloWorldApp();
    virtual bool OnInit() override;
    virtual int OnExit() override;
};

DECLARE_APP(HelloWorldApp)

