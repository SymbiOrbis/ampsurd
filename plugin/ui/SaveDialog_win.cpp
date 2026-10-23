// Windows: the system save dialog (IFileSaveDialog) with one "Save as type" entry per file type.
#include "SaveDialog.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

using Microsoft::WRL::ComPtr;

bool hasTypedSaveDialog() { return true; }

juce::File showTypedSaveDialog(juce::Component* parent, const juce::String& title, const juce::File& suggestion,
                               const juce::StringArray& descriptions, const juce::StringArray& extensions, int& chosenType)
{
    chosenType = -1;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // already initialised by JUCE: S_FALSE / RPC_E_CHANGED_MODE
    struct Uninit { HRESULT hr; ~Uninit() { if (SUCCEEDED(hr)) CoUninitialize(); } } uninit { init };

    ComPtr<IFileSaveDialog> dlg;
    if (FAILED(CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileSaveDialog),
                                reinterpret_cast<void**>(dlg.GetAddressOf()))))
        return {};

    // "AMPSURD preset (*.ampsurd)" | "*.ampsurd" ...
    std::vector<std::wstring> names, specs;
    for (int i = 0; i < extensions.size(); ++i)
    {
        names.push_back((descriptions[i] + " (*." + extensions[i] + ")").toWideCharPointer());
        specs.push_back(("*." + extensions[i]).toWideCharPointer());
    }
    std::vector<COMDLG_FILTERSPEC> filter;
    for (size_t i = 0; i < names.size(); ++i) filter.push_back({ names[i].c_str(), specs[i].c_str() });
    dlg->SetFileTypes((UINT) filter.size(), filter.data());
    dlg->SetFileTypeIndex(1); // 1-based
    if (!extensions.isEmpty()) dlg->SetDefaultExtension(extensions[0].toWideCharPointer()); // follows the chosen type
    dlg->SetTitle(title.toWideCharPointer());
    DWORD opts = 0;
    dlg->GetOptions(&opts);
    dlg->SetOptions(opts | FOS_OVERWRITEPROMPT | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    {
        ComPtr<IShellItem> folder;
        if (SUCCEEDED(SHCreateItemFromParsingName(suggestion.getParentDirectory().getFullPathName().toWideCharPointer(), nullptr,
                                                  __uuidof(IShellItem), reinterpret_cast<void**>(folder.GetAddressOf()))))
            dlg->SetFolder(folder.Get());
    }
    dlg->SetFileName(suggestion.getFileNameWithoutExtension().toWideCharPointer());

    HWND owner = nullptr;
    if (parent != nullptr)
        if (auto* top = parent->getTopLevelComponent())
            if (auto* peer = top->getPeer()) owner = (HWND) peer->getNativeHandle();
    if (FAILED(dlg->Show(owner))) return {}; // cancelled (or failed)

    UINT index = 1;
    dlg->GetFileTypeIndex(&index);
    ComPtr<IShellItem> item;
    if (FAILED(dlg->GetResult(item.GetAddressOf()))) return {};
    PWSTR path = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) || path == nullptr) return {};
    const juce::File f(juce::String(static_cast<const wchar_t*>(path)));
    CoTaskMemFree(path);
    chosenType = juce::jlimit(0, extensions.size() - 1, (int) index - 1);
    return f.withFileExtension("." + extensions[chosenType]); // the chosen type decides
}
