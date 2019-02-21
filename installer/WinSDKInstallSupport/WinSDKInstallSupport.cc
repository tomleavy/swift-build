//
// Copyright © 2019 Saleem Abdulrasool <compnerd@compnerd.org>
//

// $(WIX)sdk\$(WixPlatformToolset)\inc

#define WIN32_LEAN_AND_MEAN
#define VC_EXTRA_LEAN
#define NOMINMAX
#include <Windows.h>

#include <msiquery.h>

#include <wcautil.h>

#include <string>
#include <utility>
#include <vector>
#include <experimental/filesystem>

using filesystem = std::experimental::filesystem;

namespace {
filesystem::path GetWindows10SDKRoot() {
  DWORD size = 0;
  std::unique_ptr<char[]> buffer;

  RegGetValueA(HKEY_LOCAL_MACHINE,
               "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
               "KitsRoot10",
               RRF_RT_REG_SZ /* | RRF_SUBKEY_WOW6432KEY */,
               nullptr, nullptr, &size);

  buffer.reset(new char[++size]);
  if (RegGetValueA(HKEY_LOCAL_MACHINE,
                   "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots",
                   "KitsRoot10",
                   RRF_RT_REG_SZ /* | RRF_SUBKEY_WOW6432KEY */,
                   nullptr, buffer.get(), &size) == ERROR_SUCCESS)
    return buffer.get();

  WcaLog(LOGMSG_STANDARD, "unable to get Windows SDK root: [%#08x]",
         GetLastError());
  return {};
}

std::vector<std::string> GetWindows10SDKVersions() {
  DWORD index = 0, size = 0;
  std::unique_ptr<char[]> buffer{nullptr};
  std::vector<std::string> versions;
  HKEY hKey;

  RegOpenKeyA(HKEY_LOCAL_MACHINE,
              "SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots", &hKey);
  RegQueryInfoKeyA(hKey, nullptr, nullptr, nullptr, nullptr, &size, nullptr,
                   nullptr, nullptr, nullptr, nullptr, nullptr);

  buffer.reset(new char[++size]);
  while (RegEnumKeyA(hKey, index++, buffer.get(), size) == ERROR_SUCCESS)
    versions.emplace_back(buffer.get());

  RegCloseKey(hKey);

  return versions;
}
}


__declspec(dllexport)
UINT __stdcall InstallUniversalSDKSymbolicLinks(MSIHANDLE hInstall) {
  HRESULT hr;
  LPWSTR wszInstallDirectory;
  filesystem::path sdk_root;

  hr = WcaInitialize(hInstall, "InstallUniversalSDKSymbolicLinks");
  ExitOnFailure(hr, "failed to initialise");

  WcaLog(LOGMSG_STANDARD, "initialise");

  hr = WcaGetProperty(L"INSTALL_ROOT", &wszInstallDirectory);
  ExitOnFailure(hr, "unable to get INSTALL_ROOT");

  sdk_root = GetWindows10SDKRoot();
  if (sdk_root.empty())
    ExitOnFailure((hr = E_FAIL), "unable to query Windows 10 SDK root");

  const filesystem::path platforms =
    filesystem::path(wszInstallDirectory) / "Library" / "Developer" / "Platforms";
  const filesystem::path windows_sdk =
     platforms / "Windows.platform" / "SDKs" / "Windows.sdk";

  for (const auto &version : GetWindows10SDKVersions()) {
    enum { name, target };
    const std::tuple<filesystem::path, filesystem::path> symlinks[] = {
      {sdk_root / "Include" / version / "um" / "module.modulemap",
        windows_sdk / "usr" / "share" / "winsdk.modulemap"},
      {sdk_root / "Include" / version / "ucrt" / "module.modulemap",
        windows_sdk / "usr" / "share" / "ucrt.modulemap"},
    };

    for (const auto &link : symlinks)
      if (CreateSymbolicLinkW((L"\\\\?\\" + std::get<name>(link).wstring()).c_str(),
                              (L"\\\\?\\" + std::get<target>(link).wstring()).c_str(),
                               SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == FALSE)
        ExitOnFailure((hr = E_FAIL), "unable to create symbolic link (%s) [%#08x]",
                      std::get<name>(link).string().c_str(), GetLastError());
  }

  WcaLog(LOGMSG_STANDARD, "finalise")

LExit:
  return WcaFinalize(SUCCEEDED(hr) ? ERROR_SUCCESS : ERROR_INSTALL_FAILURE);
}

extern "C" BOOL WINAPI
DllMain(HINSTANCE hInstance, ULONG ulReason, LPVOID) {
  switch (ulReason) {
  case DLL_PROCESS_ATTACH:
    WcaGlobalInitialize(hInstance);
    break;
  case DLL_PROCESS_DETACH:
    WcaGlobalFinalize();
    break;
  }
  return TRUE;
}

