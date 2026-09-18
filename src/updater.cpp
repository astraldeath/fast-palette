#include "updater.hpp"
#include "win_util.hpp"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>
#include <winhttp.h>
#include <shellapi.h>
#include <array>
#include <stdexcept>

namespace palette {
namespace {
constexpr auto release_api="https://api.github.com/repos/astraldeath/fast-palette/releases/latest";
QString executable_path(){std::wstring path(32768,L'\0');const auto size=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));return QString::fromWCharArray(path.data(),static_cast<int>(size));}
QString update_root(){return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)+"/FastPalette/Updates";}
[[noreturn]] void fail(const QString& text){throw std::runtime_error(text.toStdString());}
struct InternetHandle{HINTERNET value=nullptr;~InternetHandle(){if(value)WinHttpCloseHandle(value);}};
struct NativeHandle{HANDLE value=nullptr;~NativeHandle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
std::optional<std::array<unsigned,3>> version_parts(const QString& text){
    static const QRegularExpression format("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
    const auto match=format.match(text);if(!match.hasMatch())return {};
    std::array<unsigned,3> result{};
    for(int i=0;i<3;++i){bool ok=false;result[i]=match.captured(i+1).toUInt(&ok);if(!ok || result[i]>65535)return {};}
    return result;
}
bool allowed_url(const QUrl& url){
    return url.isValid() && url.scheme()=="https" && url.port(443)==443 && url.userInfo().isEmpty() &&
        (url.host()=="api.github.com" || url.host()=="github.com" || url.host()=="release-assets.githubusercontent.com" || url.host()=="objects.githubusercontent.com");
}
QString quote(const QString& value){
    QString result="\"";int slashes=0;
    for(const auto ch:value){if(ch=='\\'){++slashes;continue;}
        result+=QString(slashes*(ch=='"'?2:1),'\\');slashes=0;if(ch=='"')result+='\\';result+=ch;}
    result+=QString(slashes*2,'\\');return result+'"';
}
bool launch(const QString& exe,const QStringList& args,HANDLE* child=nullptr){
    QString command=quote(QDir::toNativeSeparators(exe));for(const auto& arg:args)command+=' '+quote(arg);
    auto buffer=command.toStdWString();const auto path=QDir::toNativeSeparators(exe).toStdWString();
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
    if(!CreateProcessW(path.c_str(),buffer.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))return false;
    CloseHandle(process.hThread);if(child)*child=process.hProcess;else CloseHandle(process.hProcess);return true;
}
QByteArray read_file(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))fail("Could not read the update file.");return file.readAll();}
void write_file(const QString& path,const QByteArray& bytes){QFile file(path);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly) || file.write(bytes)!=bytes.size() || !file.flush())fail("Could not save the update.");}
bool matches_binary_version(const QString& path,const QString& version){
    const auto parts=version_parts(version);if(!parts)return false;
    const auto native=QDir::toNativeSeparators(path).toStdWString();DWORD ignored=0;
    const auto size=GetFileVersionInfoSizeW(native.c_str(),&ignored);if(!size || size>1024*1024)return false;
    std::vector<std::byte> data(size);if(!GetFileVersionInfoW(native.c_str(),0,size,data.data()))return false;
    VS_FIXEDFILEINFO* info=nullptr;UINT length=0;
    return VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&length) && length>=sizeof(*info) &&
        info->dwSignature==0xfeef04bd && HIWORD(info->dwFileVersionMS)==(*parts)[0] && LOWORD(info->dwFileVersionMS)==(*parts)[1] &&
        HIWORD(info->dwFileVersionLS)==(*parts)[2] && LOWORD(info->dwFileVersionLS)==0;
}
bool managed_directory(const QString& directory){
    const QFileInfo info(directory);const QDir root(update_root());
    return info.dir().canonicalPath()==QFileInfo(root.absolutePath()).canonicalFilePath() &&
        !QUuid(info.fileName()).isNull() && !info.isSymLink();
}
}
bool newer_version(const QString& candidate,const QString& current){const auto next=version_parts(candidate),old=version_parts(current);return next && old && *next>*old;}
std::optional<UpdateRelease> parse_update_release(const QByteArray& json,const QString& current){
    const auto document=QJsonDocument::fromJson(json);if(!document.isObject())return {};
    const auto release=document.object();const auto tag=release["tag_name"].toString();
    if(release["draft"].toBool(true) || release["prerelease"].toBool(true) || !tag.startsWith('v') || !newer_version(tag.mid(1),current))return {};
    UpdateRelease result;result.version=tag.mid(1);
    const auto base="https://github.com/astraldeath/fast-palette/releases/download/"+tag+"/";
    for(const auto asset:release["assets"].toArray()){
        const auto item=asset.toObject();const auto name=item["name"].toString(),url=item["browser_download_url"].toString();
        if(url!=base+name)continue;
        if(name=="FastPalette.exe")result.executable=url;
        if(name=="FastPalette.exe.sha256")result.checksum=url;
    }
    if(result.executable.isEmpty() || result.checksum.isEmpty())return {};
    return result;
}
bool verify_update(const QByteArray& binary,const QByteArray& checksum){
    static const QRegularExpression format("^([a-fA-F0-9]{64}) [ *]FastPalette\\.exe$");
    const auto match=format.match(QString::fromLatin1(checksum).trimmed());
    return match.hasMatch() && QCryptographicHash::hash(binary,QCryptographicHash::Sha256).toHex()==match.captured(1).toLatin1().toLower();
}
QByteArray download_update_url(const QString& address,qsizetype limit,std::stop_token stop){
    InternetHandle session{WinHttpOpen(L"FastPalette-Updater/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    if(!session.value)fail("Could not initialize the update connection.");
    WinHttpSetTimeouts(session.value,5000,5000,5000,5000);
    QUrl url(address);const auto deadline=GetTickCount64()+120000;
    for(int redirect=0;redirect<6;++redirect){
        if(stop.stop_requested())fail("Update cancelled.");if(!allowed_url(url))fail("The update download address is not trusted.");
        const auto host=url.host().toStdWString();
        InternetHandle connection{WinHttpConnect(session.value,host.c_str(),INTERNET_DEFAULT_HTTPS_PORT,0)};
        if(!connection.value)fail("Could not connect to GitHub.");
        auto resource=url.path(QUrl::FullyEncoded);if(url.hasQuery())resource+='?'+url.query(QUrl::FullyEncoded);
        const auto path=resource.toStdWString();
        InternetHandle request{WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
        if(!request.value)fail("Could not create the update request.");
        DWORD disabled=WINHTTP_DISABLE_REDIRECTS|WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_AUTHENTICATION;
        if(!WinHttpSetOption(request.value,WINHTTP_OPTION_DISABLE_FEATURE,&disabled,sizeof(disabled)))fail("Could not secure the update request.");
        if(!WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0) || !WinHttpReceiveResponse(request.value,nullptr))fail("Could not reach GitHub. Check your connection and try again.");
        DWORD status=0,size=sizeof(status);
        if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX))fail("Invalid update response.");
        if(status>=300 && status<400){
            std::wstring location(16384,L'\0');size=static_cast<DWORD>(location.size()*sizeof(wchar_t));
            if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_LOCATION,WINHTTP_HEADER_NAME_BY_INDEX,location.data(),&size,WINHTTP_NO_HEADER_INDEX))fail("Invalid download redirect.");
            url=url.resolved(QUrl(QString::fromWCharArray(location.c_str())));continue;
        }
        if(status!=200)fail(QString("GitHub returned HTTP %1. Try again later.").arg(status));
        QByteArray bytes;std::array<char,65536> buffer{};
        for(;;){if(stop.stop_requested() || GetTickCount64()>deadline)fail("Update download cancelled or timed out.");
            DWORD count=0;if(!WinHttpReadData(request.value,buffer.data(),static_cast<DWORD>(buffer.size()),&count))fail("The update download was interrupted.");
            if(!count)return bytes;if(bytes.size()+count>limit)fail("The update download is larger than expected.");bytes.append(buffer.data(),count);}
    }
    fail("Too many update redirects.");
}

void clean_update_directory(const QString& directory){
    if(!managed_directory(directory))return;
    // Only our two named update files are removed; never recurse through user files.
    QFile::remove(directory+"/payload.exe");QFile::remove(directory+"/updater.exe");QDir().rmdir(directory);
}

std::optional<int> run_update_helper(){
    int count=0;auto** values=CommandLineToArgvW(GetCommandLineW(),&count);if(!values)return {};
    QStringList args;for(int i=0;i<count;++i)args.push_back(QString::fromWCharArray(values[i]));LocalFree(values);
    if(args.size()<2 || args[1]!="--apply-update")return {};
    if(args.size()!=7)return 1;
    const auto directory=QFileInfo(executable_path()).absolutePath();
    if(!managed_directory(directory))return 1;
    const auto target=QDir::toNativeSeparators(args[3]),hash=args[4],version=args[5];
    bool valid_pid=false;const auto pid=args[2].toUInt(&valid_pid);
    if(!valid_pid || !args[6].startsWith("Local\\FastPalette.Update."))return 1;
    NativeHandle parent{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};
    NativeHandle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,args[6].toStdWString().c_str())};
    if(!parent.value || !ready.value)return 1;
    std::wstring parent_path(32768,L'\0');DWORD length=static_cast<DWORD>(parent_path.size());
    if(!QueryFullProcessImageNameW(parent.value,0,parent_path.data(),&length) || QString::compare(QString::fromWCharArray(parent_path.data(),static_cast<int>(length)),target,Qt::CaseInsensitive)!=0)return 1;
    const auto next=target+".update-"+QFileInfo(directory).fileName();
    const auto backup=next+".backup";
    try {
        const auto payload=directory+"/payload.exe";
        if(!verify_update(read_file(payload),hash.toLatin1()+" *FastPalette.exe") || !matches_binary_version(payload,version))return 1;
        if(QCryptographicHash::hash(read_file(target),QCryptographicHash::Sha256)!=QCryptographicHash::hash(read_file(executable_path()),QCryptographicHash::Sha256))return 1;
        if(!QFile::copy(payload,next))return 1;
        SetEvent(ready.value);
        if(WaitForSingleObject(parent.value,60000)!=WAIT_OBJECT_0){QFile::remove(next);return 1;}
        bool replaced=false;
        for(int i=0;i<30 && !replaced;++i){replaced=ReplaceFileW(target.toStdWString().c_str(),next.toStdWString().c_str(),backup.toStdWString().c_str(),0,nullptr,nullptr)!=FALSE;if(!replaced)Sleep(100);}
        if(!replaced){
            if(!QFile::exists(target) && QFile::exists(backup))QFile::rename(backup,target);
            QFile::remove(next);launch(target,{"--background"});return 1;
        }
        if(!launch(target,{"--background","--cleanup-update",directory})){
            ReplaceFileW(target.toStdWString().c_str(),backup.toStdWString().c_str(),nullptr,0,nullptr,nullptr);
            launch(target,{"--background"});return 1;
        }
        QFile::remove(backup);return 0;
    }catch(...){QFile::remove(next);return 1;}
}

Updater::Updater(QWidget* owner,std::function<bool()> idle):QObject(owner),owner_(owner),idle_(std::move(idle)){
    check_timer_.setInterval(6*60*60*1000);install_timer_.setInterval(30000);
    connect(&check_timer_,&QTimer::timeout,this,[this]{if(automatic_)check(false);});
    connect(&install_timer_,&QTimer::timeout,this,[this]{if(automatic_ && idle_())install(false);});
}
Updater::~Updater(){worker_.stop();if(!directory_.isEmpty())clean_update_directory(directory_);}
void Updater::set_automatic(bool enabled){
    if(enabled==automatic_)return;automatic_=enabled;
    if(enabled){check_timer_.start();QTimer::singleShot(15000,this,[this]{if(automatic_)check(false);});if(!directory_.isEmpty())install_timer_.start();}
    else{check_timer_.stop();install_timer_.stop();}
}
void Updater::check(bool manual,std::function<void()> completed){
    auto* parent=QApplication::activeModalWidget();if(!parent)parent=owner_;
    if(busy_){if(completed)completed();if(manual)QMessageBox::information(parent,"Updates","An update check is already running.");return;}
    if(!directory_.isEmpty()){if(completed)completed();install(manual);return;}
    busy_=true;
    worker_.enqueue([this,manual,completed=std::move(completed)](std::stop_token stop){QString directory,version,hash,error;
        try{
            const auto json=download_update_url(release_api,1024*1024,stop);
            if(!QJsonDocument::fromJson(json).isObject())fail("GitHub returned invalid release information.");
            if(const auto release=parse_update_release(json,FAST_PALETTE_VERSION)){
                version=release->version;const auto checksum=download_update_url(release->checksum,1024,stop);
                const auto binary=download_update_url(release->executable,64*1024*1024,stop);
                if(!verify_update(binary,checksum))fail("The downloaded update failed its checksum check.");
                hash=QString::fromLatin1(QCryptographicHash::hash(binary,QCryptographicHash::Sha256).toHex());
                directory=update_root()+"/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                if(!QDir().mkpath(directory))fail("Could not create the update folder.");
                write_file(directory+"/payload.exe",binary);
                if(!matches_binary_version(directory+"/payload.exe",version))fail("The update version does not match its release.");
                if(!QFile::copy(executable_path(),directory+"/updater.exe"))fail("Could not prepare the updater.");
            }
        }catch(const std::exception& exception){error=QString::fromUtf8(exception.what());clean_update_directory(directory);directory.clear();}
        if(stop.stop_requested()){clean_update_directory(directory);return;}
        QMetaObject::invokeMethod(this,[this,manual,directory,version,hash,error,completed]{
            busy_=false;if(completed)completed();auto* parent=QApplication::activeModalWidget();if(!parent)parent=owner_;
            if(!error.isEmpty()){if(manual)QMessageBox::warning(parent,"Update unavailable",error);return;}
            if(directory.isEmpty()){if(manual)QMessageBox::information(parent,"Updates","Fast Palette is up to date.");return;}
            directory_=directory;version_=version;hash_=hash;
            if(automatic_)install_timer_.start();
            if(manual || (automatic_ && idle_()))install(manual);
        },Qt::QueuedConnection);
    });
}
void Updater::install(bool manual){
    if(directory_.isEmpty())return;
    if(manual){auto* parent=QApplication::activeModalWidget();if(!parent)parent=owner_;
        if(QMessageBox::question(parent,"Update ready","Install Fast Palette "+version_+" and restart? Unsaved settings will be discarded.",QMessageBox::Yes|QMessageBox::No,QMessageBox::Yes)!=QMessageBox::Yes)return;}
    else if(!automatic_ || !idle_())return;
    const auto event="Local\\FastPalette.Update."+QUuid::createUuid().toString(QUuid::WithoutBraces);
    NativeHandle ready{CreateEventW(nullptr,TRUE,FALSE,event.toStdWString().c_str())};
    NativeHandle child;
    const bool started=ready.value && launch(directory_+"/updater.exe",{"--apply-update",QString::number(GetCurrentProcessId()),QDir::toNativeSeparators(executable_path()),hash_,version_,event},&child.value);
    if(started && WaitForSingleObject(ready.value,5000)==WAIT_OBJECT_0){install_timer_.stop();qApp->quit();return;}
    if(child.value){TerminateProcess(child.value,1);WaitForSingleObject(child.value,2000);QFile::remove(executable_path()+".update-"+QFileInfo(directory_).fileName());}
    install_timer_.stop();
    if(manual)QMessageBox::warning(QApplication::activeModalWidget()?QApplication::activeModalWidget():owner_,"Could not install update","Fast Palette could not prepare to replace its executable. Make sure its folder is writable, then try again.");
}
}
