#include "app_cache.hpp"
#include "worker.hpp"
#include <future>
#include "win_util.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <shobjidl.h>
#include <wrl/client.h>

int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    palette::ComApartment apartment;
    int failures=0;
    auto check=[&](bool ok,const char* message){if(!ok){std::cerr<<message<<'\n';++failures;}};
    QTemporaryDir temp;
    palette::AppEntry app;app.id=L"test";app.name=L"Example";app.target=temp.filePath("example.exe").toStdWString();
    app.kind=palette::LaunchKind::executable;app.arguments=L"--profile test";
    auto write=[&](const QString& name,const QByteArray& data){QFile file(name);check(file.open(QIODevice::WriteOnly),"open fixture");file.write(data);};
    write(QString::fromStdWString(app.target),"first");
    palette::write_app_cache(temp.path(),{app});
    auto restored=palette::read_app_cache(temp.path());
    check(restored.size()==1 && restored[0].id==app.id && restored[0].arguments==app.arguments,"round trip application");
    auto portable=app;portable.id=L"portable:removed";
    auto transient=app;transient.id=L"file-result";transient.source=palette::SearchSource::file;
    palette::write_app_cache(temp.path(),{app,portable,transient});
    check(palette::read_app_cache(temp.path()).size()==1,"portable and transient entries excluded");
    const auto index=temp.filePath("apps-v1.json");
    write(index,"not json");check(palette::read_app_cache(temp.path()).empty(),"malformed index rejected");
    write(index,"{\"version\":999,\"apps\":[]}");check(palette::read_app_cache(temp.path()).empty(),"unsupported version rejected");
    write(index,"{\"version\":1,\"apps\":[{\"id\":\"a\",\"name\":\"b\",\"target\":\"c\",\"detail\":\"\",\"arguments\":\"\",\"folder\":false,\"kind\":90,\"source\":0}]}");
    check(palette::read_app_cache(temp.path()).empty(),"invalid enum rejected");
    write(index,QByteArray(8*1024*1024+1,'x'));
    check(palette::read_app_cache(temp.path()).empty(),"oversized index rejected");
    int loads=0;
    auto loader=[&](const palette::AppEntry&){++loads;QImage image(32,32,QImage::Format_ARGB32);image.fill(Qt::red);return image;};
    check(!palette::cached_app_icon(temp.path(),app,loader).isNull() && loads==1,"extract first icon");
    check(!palette::cached_app_icon(temp.path(),app,loader).isNull() && loads==1,"reuse persistent icon");
    write(QString::fromStdWString(app.target),"updated executable bytes");
    check(!palette::cached_app_icon(temp.path(),app,loader).isNull() && loads==2,"invalidate changed executable");
    const auto icons=QDir(temp.filePath("icons-v1")).entryList({"*.png"},QDir::Files);
    check(icons.size()==1,"stable cache key replaces previous icon");
    if(!icons.isEmpty())write(temp.filePath("icons-v1/"+icons[0]),"broken PNG");
    check(!palette::cached_app_icon(temp.path(),app,loader).isNull() && loads==3,"corrupted icon regenerated");
    auto other=app;other.source=palette::SearchSource::path;
    check(!palette::cached_app_icon(temp.path(),other,loader).isNull() && loads==4,"source included in cache identity");
    auto missing=app;missing.id=L"temporarily unavailable icon";
    int attempts=0;
    auto retry_loader=[&](const palette::AppEntry& entry){++attempts;return attempts==1?QImage{}:loader(entry);};
    check(palette::cached_app_icon(temp.path(),missing,retry_loader).isNull(),"failed extraction stays empty");
    check(!palette::cached_app_icon(temp.path(),missing,retry_loader).isNull() && attempts==2,"failed extraction retried");
    const auto shortcut_path=temp.filePath("example.lnk"),resource_path=temp.filePath("separate.ico");
    write(resource_path,"first icon resource");
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    Microsoft::WRL::ComPtr<IPersistFile> persist;
    const bool shortcut_saved=SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))) &&
        SUCCEEDED(link->SetPath(app.target.c_str())) && SUCCEEDED(link->SetIconLocation(resource_path.toStdWString().c_str(),0)) &&
        SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Save(shortcut_path.toStdWString().c_str(),TRUE));
    check(shortcut_saved,"create shortcut with explicit icon resource");
    if(shortcut_saved) {
        auto shortcut=app;shortcut.id=L"shortcut";shortcut.target=shortcut_path.toStdWString();shortcut.kind=palette::LaunchKind::shortcut;
        int shortcut_loads=0;
        auto shortcut_loader=[&](const palette::AppEntry& entry){++shortcut_loads;return loader(entry);};
        check(!palette::cached_app_icon(temp.path(),shortcut,shortcut_loader).isNull(),"extract shortcut icon");
        check(!palette::cached_app_icon(temp.path(),shortcut,shortcut_loader).isNull() && shortcut_loads==1,"reuse shortcut icon");
        write(resource_path,"updated independent icon resource bytes");
        check(!palette::cached_app_icon(temp.path(),shortcut,shortcut_loader).isNull() && shortcut_loads==2,"invalidate explicit shortcut icon resource");
        write(QString::fromStdWString(app.target),"updated shortcut executable content again");
        check(!palette::cached_app_icon(temp.path(),shortcut,shortcut_loader).isNull() && shortcut_loads==3,"invalidate resolved shortcut executable");
    }
    {
        palette::Worker worker;std::promise<void> started,release,finished;auto gate=release.get_future();
        std::atomic<bool> obsolete=false;
        worker.enqueue([&](std::stop_token){started.set_value();gate.wait();});
        const bool running=started.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready;
        check(running,"worker begins active request");
        worker.enqueue([&](std::stop_token){obsolete=true;});
        worker.replace_pending([&](std::stop_token){finished.set_value();});
        release.set_value();
        check(finished.get_future().wait_for(std::chrono::seconds(2))==std::future_status::ready,"latest icon batch runs");
        check(!obsolete,"superseded queued icon batch discarded");
        worker.stop();
    }
    return failures?1:0;
}
