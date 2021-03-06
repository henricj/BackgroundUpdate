// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#define NOMINMAX
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <numeric>
#include <memory>
#include <queue>
#include <random>
#include <stack>
#include <stdexcept>  
#include <string>  
#include <type_traits>
#include <thread>
#include <vector>  



#include <wx/wx.h>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/event.h>
#include <wx/timer.h>

#include <Strsafe.h>

#include <wrl/client.h>
#include <AudioClient.h>

extern bool DisableMMCSS;

enum class EventIds : wxWindowID
{
    BUTTON_Hello = wxID_HIGHEST + 1, // declares an id which will be used to call our button
    TIMER_Hello
};

