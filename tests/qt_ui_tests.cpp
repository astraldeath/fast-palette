#include "window.hpp"
#include "ui_theme.hpp"
#include "providers.hpp"
#include <QApplication>
#include <QLineEdit>
#include <QListWidget>
#include <QTest>
#include <QImage>
#include <QElapsedTimer>
#include <QDialog>
#include <QTimer>
#include <QToolButton>
#include <QLabel>
#include <algorithm>
#include <vector>
#include <iostream>

int main(int argc,char** argv) {
    QApplication application(argc,argv);
    application.setQuitOnLastWindowClosed(false);
    QApplication::setStyle("Fusion");palette::apply_ui_theme();
    int failed=0;
    const auto check=[&](bool value,const char* name){if(!value){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    palette::Settings test_settings;test_settings.search_everything=true;
    palette::PaletteWindow window(test_settings);
    check(window.create(GetModuleHandleW(nullptr),true),"framework window created");
    auto* input=window.findChild<QLineEdit*>("queryInput");
    auto* results=window.findChild<QListWidget*>("results");
    if(!input || !results)return 1;
    input->setText("2+3*4");
    check(results->count()>0 && results->item(0)->data(Qt::UserRole).toString()=="14","calculator result in framework list");
    check(results->item(0)->toolTip()=="14\nEnter to copy","full result tooltip belongs to hit-tested list item");
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
    if(argc>1){input->setText("? Everything.exe");QElapsedTimer files_wait;files_wait.start();
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
    palette::Settings disabled;disabled.search_apps=false;disabled.search_calculator=false;
    disabled.search_settings=false;disabled.search_paths=false;disabled.search_everything=false;
    palette::PaletteWindow disabled_window(disabled);
    auto* disabled_input=disabled_window.findChild<QLineEdit*>("queryInput");
    auto* disabled_results=disabled_window.findChild<QListWidget*>("results");
    for(const auto* query:{"2+2","bluetooth","%appdata%","? Everything.exe"}){
        disabled_input->setText(query);QTest::qWait(90);
        check(disabled_results->count()==1 && disabled_results->item(0)->data(Qt::UserRole).toString()=="No results","disabled sources produce no results");}
    std::cout<<"Qt palette checks: "<<(failed?"FAILED":"passed")<<'\n';
    return failed?1:0;
}
