#include "updater.hpp"
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <windows.h>
#include <iostream>
using namespace palette;
int main(int argc,char** argv) {
    QCoreApplication application(argc,argv);
    if(const auto helper=run_update_helper())return *helper;
    if(application.arguments().contains("--update-parent")){Sleep(60000);return 0;}
    if(application.arguments().contains("--background")){
        QFile marker(QCoreApplication::applicationFilePath()+".started");if(!marker.open(QIODevice::WriteOnly))return 1;marker.write("started");return 0;
    }
    int failed=0;const auto check=[&](bool value,const char* name){if(!value){++failed;std::cerr<<"FAIL: "<<name<<'\n';}};
    check(newer_version("1.10.0","1.9.9"),"numeric version ordering");
    check(!newer_version("1.1.0","1.1.0") && !newer_version("1.0.0","1.1.0"),"no reinstall or downgrade");
    check(!newer_version("1.2.0-beta","1.1.0") && !newer_version("999999.0.0","1.0.0"),"invalid release versions rejected");
    const QByteArray metadata=R"({"tag_name":"v1.2.0","draft":false,"prerelease":false,"assets":[{"name":"FastPalette.exe","browser_download_url":"https://github.com/astraldeath/fast-palette/releases/download/v1.2.0/FastPalette.exe"},{"name":"FastPalette.exe.sha256","browser_download_url":"https://github.com/astraldeath/fast-palette/releases/download/v1.2.0/FastPalette.exe.sha256"}]})";
    check(parse_update_release(metadata,"1.1.0").has_value(),"complete newer release accepted");
    check(!parse_update_release(metadata,"1.2.0"),"current release ignored");
    auto changed=metadata;changed.replace("github.com/astraldeath","github.com/other");
    check(!parse_update_release(changed,"1.1.0"),"foreign release assets rejected");
    changed=metadata;changed.replace("\"prerelease\":false","\"prerelease\":true");
    check(!parse_update_release(changed,"1.1.0"),"prereleases ignored");
    const QByteArray binary="test payload";
    const auto digest=QCryptographicHash::hash(binary,QCryptographicHash::Sha256).toHex();
    check(verify_update(binary,digest+" *FastPalette.exe\r\n"),"published checksum format accepted");
    check(!verify_update(binary+"corrupt",digest+" *FastPalette.exe"),"corrupt download rejected");
    check(!verify_update(binary,digest+" *Other.exe"),"wrong checksum filename rejected");
    if(application.arguments().contains("--live")){try {
        const auto json=download_update_url("https://api.github.com/repos/astraldeath/fast-palette/releases/latest",1024*1024);
        const auto release=parse_update_release(json,"0.0.0");check(release.has_value(),"live GitHub release metadata parsed");
        if(release){const auto checksum=download_update_url(release->checksum,1024);const auto downloaded=download_update_url(release->executable,64*1024*1024);
            check(verify_update(downloaded,checksum),"live release executable passes checksum verification");std::cout<<"Verified live release "<<release->version.toStdString()<<'\n';}
        }catch(const std::exception& error){check(false,error.what());}}
    QTemporaryDir sandbox(QDir::tempPath()+"/Fast Palette update test-XXXXXX");
    const auto target=sandbox.path()+"/Fast Palette.exe";
    const auto directory=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/FastPalette/Updates/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    check(QDir().mkpath(directory),"isolated update staging directory");
    check(QFile::copy(application.applicationFilePath(),target),"copy test parent executable");
    check(QFile::copy(application.applicationFilePath(),directory+"/updater.exe"),"copy native update helper");
    QFile input(application.applicationFilePath());check(input.open(QIODevice::ReadOnly),"read fixture binary");auto payload=input.readAll();payload.append("update-test-overlay");input.close();
    QFile output(directory+"/payload.exe");check(output.open(QIODevice::WriteOnly),"write staged fixture binary");output.write(payload);output.close();
    QProcess parent;parent.start(target,{"--update-parent"});check(parent.waitForStarted(),"test parent starts");
    const auto event="Local\\FastPalette.Update."+QUuid::createUuid().toString(QUuid::WithoutBraces);
    const HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,event.toStdWString().c_str());
    QProcess rejected;rejected.start(directory+"/updater.exe",{"--apply-update",QString::number(parent.processId()),QDir::toNativeSeparators(target),QString(64,'0'),FAST_PALETTE_VERSION,event});
    check(rejected.waitForStarted() && rejected.waitForFinished(5000) && rejected.exitCode()!=0,"helper rejects a tampered payload before parent exits");
    check(parent.state()==QProcess::Running && WaitForSingleObject(ready,0)==WAIT_TIMEOUT,"failed validation leaves running app alone");
    QProcess helper;helper.start(directory+"/updater.exe",{"--apply-update",QString::number(parent.processId()),QDir::toNativeSeparators(target),QString::fromLatin1(QCryptographicHash::hash(payload,QCryptographicHash::Sha256).toHex()),FAST_PALETTE_VERSION,event});
    check(helper.waitForStarted(),"update helper starts before parent exit");
    const bool prepared=WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;check(prepared,"helper validates and stages before signaling ready");
    parent.kill();parent.waitForFinished(5000);
    check(helper.waitForFinished(10000) && helper.exitCode()==0,"helper replaces executable and restarts it");
    for(int i=0;i<50 && !QFile::exists(target+".started");++i)Sleep(50);
    check(QFile::exists(target+".started"),"replacement executable actually starts");
    QFile installed(target);check(installed.open(QIODevice::ReadOnly),"read installed replacement");check(installed.readAll()==payload,"replacement bytes match verified payload");installed.close();
    CloseHandle(ready);clean_update_directory(directory);
    return failed?1:0;
}
