#include "window.hpp"
#include "ui_theme.hpp"
#include "providers.hpp"
#include "number_format.hpp"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <shlobj.h>
#include <QLineEdit>
#include <QListWidget>
#include <QTest>
#include <QImage>
#include <QElapsedTimer>
#include <QDialog>
#include <QTimer>
#include <QToolButton>
#include <QLabel>
#include <QLocale>
#include <algorithm>
#include <vector>
#include <iostream>

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    application.setQuitOnLastWindowClosed(false);
    QApplication::setStyle("Fusion");palette::apply_ui_theme();
    int failed=0;
    const auto check=[&](bool value,const char* name){if(!value){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    palette::Settings test_settings;test_settings.search_everything=true;test_settings.automatic_updates=false;
    test_settings.aliases={{L"worktest",L"%appdata%",L""}};
    palette::PaletteWindow window(test_settings);
    check(window.create(GetModuleHandleW(nullptr),true),"framework window created");
    auto* input=window.findChild<QLineEdit*>("queryInput");
    auto* results=window.findChild<QListWidget*>("results");
    if(!input || !results)return 1;
    input->setText("2+3*4");
    check(results->count()>0 && results->item(0)->data(Qt::UserRole).toString()=="14","calculator result in framework list");
    check(results->item(0)->toolTip()=="14\nEnter to copy","full result tooltip belongs to hit-tested list item");
    input->setText("180cm to ftin");
    check(results->item(0)->data(Qt::UserRole).toString()==palette::localized_number_result("5 ft 10.87 in"),"conversion appears in palette");
    input->setText("conv 2MBps to Mbps");
    check(results->count()==1 && results->item(0)->data(Qt::UserRole).toString()=="16 Mbps","conversion prefix isolates rate conversion");
    input->setText("usd to won");
    check(results->item(0)->data(Qt::UserRole).toString()=="Fetching currency rates...","fiat query works without prefix");
    input->setText("fx usd to yen");
    check(results->count()==1 && results->item(0)->data(Qt::UserRole).toString()=="Fetching currency rates...","currency query shows pending result without blocking");
    input->setText("@ worktest");
    check(results->count()==1 && results->item(0)->data(Qt::UserRole).toString()=="worktest","alias prefix resolves alias");
    input->setText("win bluetooth");
    check(results->item(0)->toolTip().contains("Windows Settings"),"Windows Settings prefix routes stripped query");
    input->setText("=2pi");
    check(results->count()==1 && results->item(0)->data(Qt::UserRole).toString().startsWith("6"+QLocale::system().decimalPoint()+"283"),"implicit multiplication reaches palette");
    input->setText("100,000 + 2");
    check(results->item(0)->data(Qt::UserRole).toString()==palette::localized_number_result("100002"),"calculator accepts grouped input");
    input->setText("conv 100,000cm to m");
    check(results->item(0)->data(Qt::UserRole).toString()==palette::localized_number_result("1000 m"),"unit conversion accepts grouped input");
    input->setText("1e6");
    check(results->item(0)->data(Qt::UserRole).toString()==QLocale::system().toString(1000000),"scientific input displays expanded localized integer");
    input->setText("1e-6");
    check(results->item(0)->data(Qt::UserRole).toString()=="0"+QLocale::system().decimalPoint()+"000001","negative exponent displays plain decimal");
    input->setText("hello world");input->setCursorPosition(11);
    QTest::keyClick(input,Qt::Key_Backspace,Qt::ControlModifier);
    check(input->text()=="hello ","framework Ctrl+Backspace deletes word");
    QTest::keyClick(input,Qt::Key_Z,Qt::ControlModifier);
    check(input->text()=="hello world","framework word-delete Undo");
    input->setText("=sqrt(-1)");
    check(results->count()==1 && !results->item(0)->data(Qt::UserRole).toString().isEmpty(),"calculation domain error displayed");
    check(window.testAttribute(Qt::WA_TranslucentBackground),"atomic translucent backing store enabled");
    window.show();application.processEvents();
    check(input->text().isEmpty(),"opening clears previous query");
    const auto first=window.grab().toImage();
    const auto expected=application.palette().color(QPalette::Window);
    const auto actual=first.pixelColor(80,20);
    check(actual.alpha()==255 && std::abs(actual.red()-expected.red())<3 &&
        std::abs(actual.green()-expected.green())<3,"first rendered surface uses theme background");
    QTest::keyClick(input,Qt::Key_Escape);
    check(!window.isVisible(),"Escape hides framework window");
    window.show();application.processEvents();
    const auto reopened=window.grab().toImage();
    check(reopened.pixelColor(80,20)==actual,"reopening retains themed surface");
    input->setText("notepad");
    QElapsedTimer indexing;indexing.start();
    while(indexing.elapsed()<10000 && results->count() &&
        results->item(0)->data(Qt::UserRole).toString()=="Finding applications...")QTest::qWait(30);
    application.processEvents();
    auto* footer=window.findChild<QLabel*>("paletteFooter");
    check(footer && !footer->isVisible(),"routine keyboard hints are hidden");
    check(results->mapTo(&window,results->rect().bottomRight()).y()<window.height(),"expanded results fit inside palette");
    QTest::qWait(250);
    window.grab().save(QCoreApplication::applicationDirPath()+"/../palette-preview.png");
    std::vector<double> samples;
    for(int i=0;i<40;++i) {
        QElapsedTimer typing;typing.start();
        input->setText(i%2?"notepad":"voicemeeter potato x64");
        application.processEvents();
        if(i>=10)samples.push_back(typing.nsecsElapsed()/1000000.0);
    }
    std::sort(samples.begin(),samples.end());
    std::cout<<"Query + widget update milliseconds: p50="<<samples[14]<<" p95="<<samples[28]<<'\n';
    if(application.arguments().contains("--currency-live")){
        for(const auto* query:{"100,000 usd to yen","100 usd to won","fx 5 btc to xmr"}){
            input->setText(query);QElapsedTimer wait;wait.start();
            while(wait.elapsed()<18000 && !results->item(0)->toolTip().contains("rates ·"))QTest::qWait(20);
            check(results->item(0)->toolTip().contains("rates ·"),"live currency result includes rate date");
        }
        window.grab().save(QCoreApplication::applicationDirPath()+"/../currency-preview.png");
    }
    if(application.arguments().contains("--catalog-live")){
        PWSTR programs=nullptr;
        check(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs,0,nullptr,&programs)),"Start menu path available");
        if(programs){
            QTemporaryDir folder(QString::fromWCharArray(programs)+"/FastPalette-test-XXXXXX");CoTaskMemFree(programs);
            check(folder.isValid(),"isolated Start menu fixture created");
            if(folder.isValid()){
                const QString name="FastPaletteCatalogProbe";
                const auto path=folder.filePath(name+".url");QFile shortcut(path);
                check(shortcut.open(QIODevice::WriteOnly),"new application shortcut created");
                shortcut.write("[InternetShortcut]\r\nURL=https://example.invalid/\r\n");shortcut.close();
                input->setText(name);QElapsedTimer wait;wait.start();
                auto found=[&]{for(int i=0;i<results->count();++i)if(results->item(i)->data(Qt::UserRole).toString()==name)return true;return false;};
                while(wait.elapsed()<20000 && !found())QTest::qWait(50);
                check(found(),"new nested Start menu app discovered without manual refresh");
                check(QFile::remove(path),"fixture shortcut removed");wait.restart();
                while(wait.elapsed()<20000 && found())QTest::qWait(50);
                check(!found(),"removed app disappears without manual refresh");
            }
        }
    }
    bool settings_opened=false;
    auto* settings=window.findChild<QToolButton*>("settingsButton");
    input->setText("bluetooth");
    check(results->count()>0 && results->item(0)->toolTip().contains("Windows Settings"),"Windows settings appear in live search");
    input->setText("%FAST_PALETTE_MISSING_VARIABLE%");QTest::qWait(80);
    input->setText("%appdata%");
    QElapsedTimer path_wait;path_wait.start();
    const auto appdata=QString::fromStdWString(palette::expand_path_query(L"%appdata%"));
    while(path_wait.elapsed()<2000 && !results->item(0)->toolTip().contains(appdata))QTest::qWait(20);
    check(results->item(0)->toolTip().contains(appdata),"current environment path wins over superseded query");
    if(argc>1 && !application.arguments().contains("--currency-live") && !application.arguments().contains("--catalog-live") && !application.arguments().contains("--taskbar-live")){input->setText("? Everything.exe");QElapsedTimer files_wait;files_wait.start();
        while(files_wait.elapsed()<2000 && !results->item(0)->toolTip().contains("Everything.exe",Qt::CaseInsensitive))QTest::qWait(20);
        check(results->item(0)->toolTip().contains("Everything.exe",Qt::CaseInsensitive),"Everything results reach the palette");
        window.grab().save(QCoreApplication::applicationDirPath()+"/../everything-preview.png");}
    QTimer::singleShot(50,[&]{
        if(auto* dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            settings_opened=true;dialog->reject();
        }
    });
    QTest::keyClick(settings,Qt::Key_Return);
    check(settings_opened,"Enter activates focused Settings button");
    window.hide();
    if(application.arguments().contains("--taskbar-live")){
        HWND host=nullptr;
        EnumThreadWindows(GetCurrentThreadId(),[](HWND candidate,LPARAM context)->BOOL{
            wchar_t name[128]{};GetClassNameW(candidate,name,128);
            if(std::wstring_view(name)==palette::window_class){*reinterpret_cast<HWND*>(context)=candidate;return FALSE;}return TRUE;
        },reinterpret_cast<LPARAM>(&host));
        check(host!=nullptr,"own message host found");
        QWidget fullscreen(nullptr,Qt::Window|Qt::FramelessWindowHint);fullscreen.showFullScreen();
        fullscreen.activateWindow();SetForegroundWindow(reinterpret_cast<HWND>(fullscreen.winId()));QTest::qWait(500);
        const auto taskbar=FindWindowW(L"Shell_TrayWnd",nullptr);
        const auto initial=GetWindowLongPtrW(taskbar,GWL_EXSTYLE)&WS_EX_TOPMOST;
        for(auto key:{VK_LWIN,VK_RWIN}){
            SendMessageW(host,palette::msg_invoke,key,0);QTest::qWait(300);
            check(window.isVisible() && input->hasFocus(),"bare Win keeps palette typing focus");
            check((GetWindowLongPtrW(taskbar,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"bare Win raises taskbar over fullscreen window");
            QTest::keyClick(input,Qt::Key_Escape);QTest::qWait(300);
            check((GetWindowLongPtrW(taskbar,GWL_EXSTYLE)&WS_EX_TOPMOST)==initial,"dismiss restores taskbar topmost state");
        }
        fullscreen.close();
    }
    palette::Settings disabled;disabled.search_apps=false;disabled.search_calculator=false;
    disabled.search_settings=false;disabled.search_paths=false;disabled.search_everything=false;disabled.search_conversions=false;disabled.search_aliases=false;disabled.search_currency=false;disabled.automatic_updates=false;
    palette::PaletteWindow disabled_window(disabled);
    auto* disabled_input=disabled_window.findChild<QLineEdit*>("queryInput");
    auto* disabled_results=disabled_window.findChild<QListWidget*>("results");
    for(const auto* query:{"2+2","bluetooth","%appdata%","? Everything.exe","conv 180cm to ftin","@ worktest","fx usd to yen"}){
        disabled_input->setText(query);QTest::qWait(90);
        check(disabled_results->count()==1 && disabled_results->item(0)->data(Qt::UserRole).toString()=="No results","disabled sources produce no results");}
    std::cout<<"Qt palette checks: "<<(failed?"FAILED":"passed")<<'\n';
    return failed?1:0;
}
