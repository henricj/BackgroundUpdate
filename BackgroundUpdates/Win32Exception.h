#pragma once

// From https://docs.microsoft.com/en-us/cpp/cpp/how-to-interface-between-exceptional-and-non-exceptional-code

class Win32Exception : public std::runtime_error
{
private:
    DWORD m_error = 0;
public:
    Win32Exception(const DWORD error, const std::string& msg);

    DWORD GetErrorCode() const noexcept { return m_error; }

    static void ThrowLastErrorIf(bool expression, const std::string& msg)
    {
        if (expression)
            throw Win32Exception(::GetLastError(), msg);
    }
private:
    static std::string FormatErrorMessage(DWORD error, const std::string& msg);
};


