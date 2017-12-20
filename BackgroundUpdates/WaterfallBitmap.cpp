#include "stdafx.h"

#include "WaterfallBitmap.h"


wxIMPLEMENT_DYNAMIC_CLASS(WaterfallBitmap, wxControl)

wxBEGIN_EVENT_TABLE(WaterfallBitmap, wxControl)
EVT_PAINT(WaterfallBitmap::OnPaintOffset)
//EVT_ERASE_BACKGROUND(WaterfallBitmap::OnEraseBackground)
EVT_TIMER(static_cast<wxWindowID>(EventIds::TIMER_Hello), WaterfallBitmap::OnTimerBump)
wxEND_EVENT_TABLE()

void WaterfallBitmap::OnPaintOffset(wxPaintEvent& event)
{
    if (!bitmap_.IsOk())
        return;

    wxPaintDC dc(this);
    wxMemoryDC memdc;

    memdc.SelectObject(bitmap_);

    const auto y = bitmap_.GetHeight() - offset_;

    // Blit upper part of control from lower part of bitmap
    dc.Blit(0, 0, bitmap_.GetWidth(), y, &memdc, 0, offset_);
    
    // Blit lower part of control from upper part of bitmap
    if (offset_ > 0)
        dc.Blit(0, y, bitmap_.GetWidth(), offset_, &memdc, 0, 0);

    memdc.SelectObject(wxNullBitmap);
}

void WaterfallBitmap::OnEraseBackground(wxEraseEvent&)
{ }

void WaterfallBitmap::OnTimerBump(wxTimerEvent&)
{
    if (++offset_ >= bitmap_.GetHeight())
        offset_ = 0;

    Refresh();
}

bool WaterfallBitmap::Create(wxWindow *parent, wxWindowID id,
    const wxPoint& pos, const wxSize& size, long style,
    const wxValidator& validator, const wxString& name)
{
    if (!wxControl::Create(parent, id, pos, size, style, validator, name))
        return false;

    timer_.reset(new wxTimer(this, static_cast<wxWindowID>(EventIds::TIMER_Hello)));

    SetBackgroundStyle(wxBG_STYLE_PAINT);

    bitmap_.Create(size);

    wxMemoryDC dc;

    dc.SelectObject(bitmap_);

    dc.SetBackground(*wxWHITE_BRUSH);
    dc.Clear();

    dc.SetPen(*wxRED_PEN);
    dc.DrawLine(0, 0, size.GetX(), size.GetY());
    dc.DrawLine(0, size.GetY(), size.GetX(), 0);

    dc.SelectObject(wxNullBitmap);

    timer_->Start(33);

    return true;
}

void WaterfallBitmap::Init()
{
}

WaterfallBitmap::~WaterfallBitmap()
{ }

