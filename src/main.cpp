#include "window.hpp"
#include "ui_theme.hpp"
#include "win_util.hpp"
#include "updater.hpp"
#include <QTimer>
#include <QApplication>
#include <QMessageBox>
#include <string_view>

int main(int argc,char** argv) {
    if(const auto result=palette::run_update_helper())return *result;
    bool background=false;for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--background")background=true;
    const HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\FastPalette.Instance.1");
    if(!mutex)return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS){
        if(background){CloseHandle(mutex);return 0;}
        HWND window=nullptr;
        for(int attempt=0;attempt<100 && !window;++attempt){window=FindWindowW(palette::window_class,nullptr);if(!window)Sleep(25);}
        if(window){DWORD pid=0;GetWindowThreadProcessId(window,&pid);AllowSetForegroundWindow(pid);PostMessageW(window,RegisterWindowMessageW(palette::invoke_message),0,0);}
        CloseHandle(mutex);return window?0:1;
    }
    palette::ComApartment apartment;
    QApplication application(argc,argv);application.setQuitOnLastWindowClosed(false);
    application.setApplicationName("Fast Palette");application.setApplicationVersion(FAST_PALETTE_VERSION);
    const auto cleanup=application.arguments().indexOf("--cleanup-update");
    if(cleanup>=0 && cleanup+1<application.arguments().size()){
        const auto directory=application.arguments()[cleanup+1];
        QTimer::singleShot(10000,&application,[directory]{palette::clean_update_directory(directory);});
    }
    QApplication::setStyle("Fusion");palette::apply_ui_theme();application.setWindowIcon(palette::ui_icon(palette::UiIcon::logo));
    int result=0;
    {
        auto settings=palette::load_settings();
        if(settings.start_at_login){
            std::wstring error;
            if(!palette::set_start_at_login(true,error))
                QMessageBox::warning(nullptr,"Launch at sign-in",QString::fromStdWString(error));
        }
        palette::PaletteWindow window(std::move(settings));
        if(!window.create(GetModuleHandleW(nullptr),background)){
            QMessageBox::critical(nullptr,"Fast Palette","Fast Palette could not initialize its Windows integration.");result=1;
        }else result=application.exec();
    }
    CloseHandle(mutex);return result;
}
