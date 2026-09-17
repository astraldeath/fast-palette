#include <windows.h>
#include <shellapi.h>
#include <string>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    int count=0;auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args||count!=3){if(args)LocalFree(args);return 1;}
    wchar_t directory[32768]{};GetCurrentDirectoryW(32768,directory);
    const auto text=std::wstring(args[2])+L"\n"+directory;
    const auto file=CreateFileW(args[1],GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    LocalFree(args);if(file==INVALID_HANDLE_VALUE)return 2;
    DWORD written=0;const bool ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()*sizeof(wchar_t)),&written,nullptr)!=FALSE;
    CloseHandle(file);return ok?0:3;
}
