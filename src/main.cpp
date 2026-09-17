#include "window.hpp"
#include "ui_theme.hpp"
#include "win_util.hpp"
#include <QApplication>
#include <QMessageBox>

int main(int argc,char** argv) {
    const HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FastPalette.Instance.1");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        HWND window=nullptr;
        for(int attempt=0;attempt<100 && !window;++attempt){window=FindWindowW(palette::window_class,nullptr);if(!window)Sleep(25);}
        if(window){DWORD pid=0;GetWindowThreadProcessId(window,&pid);AllowSetForegroundWindow(pid);PostMessageW(window,RegisterWindowMessageW(palette::invoke_message),0,0);}
        CloseHandle(mutex);return window?0:1;
    }
    palette::ComApartment apartment;
    QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);
    application.setApplicationName("Fast Palette");application.setApplicationVersion("0.2.0");
    QApplication::setStyle("Fusion");palette::apply_ui_theme();application.setWindowIcon(palette::ui_icon(palette::UiIcon::logo));
    int result=0;
    {
        palette::PaletteWindow window;
        if(!window.create(GetModuleHandleW(nullptr),application.arguments().contains("--background"))){
            QMessageBox::critical(nullptr,"Fast Palette","Fast Palette could not initialize its Windows integration.");result=1;
        }else result=application.exec();
    }
    CloseHandle(mutex);return result;
}
