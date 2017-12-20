#pragma once

class CoInitializeWrapper final
{
    HRESULT hr_;
public:
    explicit CoInitializeWrapper(DWORD flags)
    {
        hr_ = CoInitializeEx(nullptr, flags);
    }

    ~CoInitializeWrapper()
    {
        if (SUCCEEDED(hr_))
        {
            CoUninitialize();
        }
    }

    HRESULT get() const
    {
        return hr_;
    }
};

