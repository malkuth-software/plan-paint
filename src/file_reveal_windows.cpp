#include "file_reveal.hpp"
#include "paths.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <shlobj.h>
#include <windows.h>
namespace paint {
namespace {
struct RevealComSession {
    RevealComSession() = default;
    RevealComSession(const RevealComSession&) = delete;
    RevealComSession& operator=(const RevealComSession&) = delete;
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~RevealComSession() {
        if (SUCCEEDED(result)) {
            CoUninitialize();
        }
    }
};
struct ShellItem {
    ShellItem() = default;
    ShellItem(const ShellItem&) = delete;
    ShellItem& operator=(const ShellItem&) = delete;
    PIDLIST_ABSOLUTE value = nullptr;
    ~ShellItem() {
        CoTaskMemFree(value);
    }
};
} // namespace
RevealOutcome reveal_in_file_manager(const std::string& path, const std::atomic<bool>* cancel) {
    const RevealTarget target = prepare_file_reveal(path);
    if (cancel && (*cancel).load()) {
        return RevealOutcome::Cancelled;
    }
    RevealComSession session;
    if (FAILED(session.result) && session.result != RPC_E_CHANGED_MODE) {
        throw std::runtime_error("Could not start the Windows file-manager service.");
    }
    ShellItem item;
    const std::wstring native = path_from_utf8(target.path).wstring();
    const HRESULT parsed = SHParseDisplayName(native.c_str(), nullptr, &item.value, 0, nullptr);
    if (cancel && (*cancel).load()) {
        return RevealOutcome::Cancelled;
    }
    if (FAILED(parsed) || FAILED(SHOpenFolderAndSelectItems(item.value, 0, nullptr, 0))) {
        throw std::runtime_error("Windows could not reveal the selected file or folder.");
    }
    return RevealOutcome::SelectionRequested;
}
} // namespace paint
