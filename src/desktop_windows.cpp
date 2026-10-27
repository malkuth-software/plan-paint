#include "desktop.hpp"
#include "paths.hpp"
#if !RAINSTAR_FORMS_NATIVE_PRINT
#include <SDL3/SDL.h>
#endif
#include <array>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
// The base SDK must precede COM and mail declarations.
// clang-format off
#include <windows.h>
#include <oleauto.h>
#include <mapi.h>
// clang-format on
namespace paint {
class ComSession {
  public:
    ComSession() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE) {
            throw std::runtime_error("The Windows image acquisition service could not start.");
        }
    }
    ComSession(const ComSession&) = delete;
    ComSession& operator=(const ComSession&) = delete;
    ~ComSession() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

  private:
    HRESULT result_;
};
struct AutomationValue {
    AutomationValue() = default;
    AutomationValue(const AutomationValue&) = delete;
    AutomationValue& operator=(const AutomationValue&) = delete;
    VARIANT value{};
    ~AutomationValue() {
        VariantClear(&value);
    }
};
static void invoke_method(IDispatch& object, wchar_t* name, VARIANTARG* arguments, UINT count,
                          VARIANT* result) {
    DISPID identifier = 0;
    if (FAILED(object.GetIDsOfNames(IID_NULL, &name, 1, LOCALE_USER_DEFAULT, &identifier))) {
        throw std::runtime_error("The acquisition service does not provide the requested command.");
    }
    DISPPARAMS parameters{arguments, nullptr, count, 0};
    EXCEPINFO exception{};
    UINT bad_argument = 0;
    HRESULT status = object.Invoke(identifier, IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &parameters,
                                   result, &exception, &bad_argument);
    SysFreeString(exception.bstrSource);
    SysFreeString(exception.bstrDescription);
    SysFreeString(exception.bstrHelpFile);
    if (FAILED(status)) {
        throw std::runtime_error(
            "Windows could not acquire this picture. Check the scanner or camera and its driver.");
    }
}
bool acquire_picture(std::string& acquired_path) {
    ComSession session;
    CLSID identifier{};
    if (FAILED(CLSIDFromProgID(L"WIA.CommonDialog", &identifier))) {
        throw std::runtime_error("Windows Image Acquisition is not installed.");
    }
    AutomationValue dialog;
    dialog.value.vt = VT_DISPATCH;
    if (FAILED(CoCreateInstance(identifier, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER,
                                IID_IDispatch, reinterpret_cast<void**>(&dialog.value.pdispVal)))) {
        throw std::runtime_error(
            "Windows Image Acquisition could not open. Check that its service is enabled.");
    }
    std::array<VARIANTARG, 7> arguments{};
    arguments[0].vt = VT_BOOL;
    arguments[0].boolVal = VARIANT_FALSE;
    arguments[1].vt = VT_BOOL;
    arguments[1].boolVal = VARIANT_TRUE;
    arguments[2].vt = VT_BOOL;
    arguments[2].boolVal = VARIANT_TRUE;
    AutomationValue format;
    format.value.vt = VT_BSTR;
    format.value.bstrVal = SysAllocString(L"{B96B3CAB-0728-11D3-9D7B-0000F81EF32E}");
    arguments[3] = format.value;
    for (int index = 4; index < 7; ++index) {
        arguments[index].vt = VT_I4;
        arguments[index].lVal = 0;
    }
    AutomationValue picture;
    wchar_t acquire[] = L"ShowAcquireImage";
    invoke_method(*dialog.value.pdispVal, acquire, arguments.data(), 7, &picture.value);
    if (picture.value.vt != VT_DISPATCH || !picture.value.pdispVal) {
        return false;
    }
    acquired_path = preference_directory() + "Capture-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bmp";
    std::wstring native_path = path_from_utf8(acquired_path).wstring();
    AutomationValue destination;
    destination.value.vt = VT_BSTR;
    destination.value.bstrVal = SysAllocString(native_path.c_str());
    wchar_t save[] = L"SaveFile";
    invoke_method(*picture.value.pdispVal, save, &destination.value, 1, nullptr);
    return true;
}

void set_wallpaper(const std::string& path) {
    std::wstring native_path = path_from_utf8(path).wstring();
    if (!SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, native_path.data(),
                               SPIF_UPDATEINIFILE | SPIF_SENDCHANGE)) {
        throw std::runtime_error("Windows could not change the desktop background.");
    }
}
} // namespace paint
