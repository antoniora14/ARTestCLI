#pragma once
#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace artest::extensions::private_path
{
// Only ordinary drive/UNC paths may enter the extended Win32 namespace. Keep
// device names, NT object paths and alternate streams out of this conversion.
inline std::filesystem::path Ordinary(const std::filesystem::path &input)
{
    auto text = input.wstring();
    std::replace(text.begin(), text.end(), L'/', L'\\');
    if (text.starts_with(L"\\\\?\\UNC\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\"))
    {
        const auto rest = text.substr(4);
        if (rest.size() < 3 || !((rest[0] >= L'A' && rest[0] <= L'Z') ||
            (rest[0] >= L'a' && rest[0] <= L'z')) || rest[1] != L':' || rest[2] != L'\\')
            throw std::runtime_error("Unsupported Windows device path.");
        text = rest;
    }
    if (text.empty() || text.starts_with(L"\\\\.\\") || text.starts_with(L"\\??\\"))
        throw std::runtime_error("Unsupported Windows device path.");
    const bool drive = text.size() >= 2 && text[1] == L':';
    if (drive && (text.size() < 3 || text[2] != L'\\' ||
        !((text[0] >= L'A' && text[0] <= L'Z') || (text[0] >= L'a' && text[0] <= L'z'))))
        throw std::runtime_error("Unsupported Windows device path.");
    std::size_t start = drive ? 3U : (text.starts_with(L"\\\\") ? 2U : 0U);
    std::size_t components = 0;
    while (start < text.size())
    {
        const auto end = text.find(L'\\', start);
        const auto part = text.substr(start, end == std::wstring::npos ? end : end - start);
        if (!part.empty())
        {
            ++components;
            if (text.starts_with(L"\\\\") && components <= 2 && (part == L"." || part == L".."))
                throw std::runtime_error("Unsupported UNC file path.");
            if (part.find_first_of(L":<>\"|?*") != std::wstring::npos ||
                std::any_of(part.begin(), part.end(), [](wchar_t ch) { return ch < 32; }) ||
                (part != L"." && part != L".." && (part.back() == L' ' || part.back() == L'.')))
                throw std::runtime_error("Unsupported Windows file path.");
            auto device = part.substr(0, part.find(L'.'));
            std::transform(device.begin(), device.end(), device.begin(),
                [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
            if (text.starts_with(L"\\\\") && components == 2 && (device == L"PIPE" || device == L"MAILSLOT"))
                throw std::runtime_error("Unsupported Windows device path.");
            if (device == L"CON" || device == L"PRN" || device == L"AUX" || device == L"NUL" ||
                device == L"CONIN$" || device == L"CONOUT$" ||
                (device.size() == 4 && (device.starts_with(L"COM") || device.starts_with(L"LPT")) &&
                    ((device[3] >= L'1' && device[3] <= L'9') || device[3] == L'\u00b9' ||
                     device[3] == L'\u00b2' || device[3] == L'\u00b3')))
                throw std::runtime_error("Unsupported Windows device path.");
        }
        if (end == std::wstring::npos) break;
        start = end + 1;
    }
    if (text.starts_with(L"\\\\") && components < 2)
        throw std::runtime_error("Incomplete UNC file path.");
    return std::filesystem::path{text};
}
inline std::filesystem::path Access(const std::filesystem::path &input)
{
    const auto absolute = Ordinary(std::filesystem::absolute(Ordinary(input)).lexically_normal());
    const auto text = absolute.wstring();
    return std::filesystem::path{text.starts_with(L"\\\\") ? L"\\\\?\\UNC\\" + text.substr(2) : L"\\\\?\\" + text};
}
inline std::filesystem::path Canonical(const std::filesystem::path &input)
{
    // MSVC weakly_canonical may remove the extended prefix. Restore it for every
    // query and for both sides of containment/inventory comparisons.
    return Access(std::filesystem::weakly_canonical(Access(input)));
}
}
