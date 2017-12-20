#include "stdafx.h"
#include "Win32Exception.h"

Win32Exception::Win32Exception(const DWORD error, const std::string& msg)
    : std::runtime_error(FormatErrorMessage(error, msg)),
      m_error(error)
{ }

std::string Win32Exception::FormatErrorMessage(const DWORD error, const std::string& msg)
{
    static const auto BUFFERLENGTH = 1024;

    std::vector<char> buf(BUFFERLENGTH);

    const auto ret = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, error,
                                    0, buf.data(), BUFFERLENGTH - 1, nullptr);

    if (!ret)
        return "Error code " + std::to_string(error) + "   (" + msg + ")";

    return std::string(buf.data()) + "   (" + msg + ")";
}
