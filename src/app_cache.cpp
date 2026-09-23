#include "app_cache.hpp"
#include "catalog.hpp"
#include "core.hpp"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <unordered_set>

namespace palette {
namespace {
constexpr qint64 max_index_bytes=8*1024*1024, max_icon_bytes=1024*1024;
constexpr int max_entries=10000, max_string=32768, max_dimension=256;
QString q(const std::wstring& value){return QString::fromStdWString(value);}
QString hash(const QByteArray& data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
bool valid_string(const QString& text,bool required=false){return (!required || !text.trimmed().isEmpty()) && text.size()<=max_string && !text.contains(QChar(0));}
bool valid_app(const AppEntry& app) {
    return valid_string(q(app.id),true) && valid_string(q(app.name),true) && valid_string(q(app.target),true)
        && valid_string(q(app.detail)) && valid_string(q(app.arguments))
        && static_cast<int>(app.kind)>=0 && static_cast<int>(app.kind)<=2
        && static_cast<int>(app.source)>=0 && static_cast<int>(app.source)<=4;
}
QString expanded(const QString& path) {
    wchar_t buffer[32768]{};const auto value=path.toStdWString();
    const auto count=ExpandEnvironmentStringsW(value.c_str(),buffer,32768);
    return count && count<=32768?QString::fromWCharArray(buffer):path;
}
void append_stamp(QJsonArray& stamps,const QString& path) {
    QFileInfo file(expanded(path));
    stamps.append(QJsonArray{file.absoluteFilePath(),file.exists(),QString::number(file.size()),QString::number(file.lastModified().toMSecsSinceEpoch())});
}
QString fingerprint(const AppEntry& app) {
    QJsonArray stamps;
    if(app.kind==LaunchKind::shell) {
        // Shell application resources can change without a filesystem target.
        stamps.append(QString::number(QDateTime::currentSecsSinceEpoch()/86400));
    } else {
        append_stamp(stamps,q(app.target));
        const auto resolved=resolved_app_path(app);
        if(!resolved.empty())append_stamp(stamps,q(resolved));
        if(q(app.target).endsWith(".lnk",Qt::CaseInsensitive)) {
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            Microsoft::WRL::ComPtr<IPersistFile> file;
            if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))) &&
                SUCCEEDED(link.As(&file)) && SUCCEEDED(file->Load(expanded(q(app.target)).toStdWString().c_str(),STGM_READ))) {
                wchar_t location[32768]{};int index=0;
                if(SUCCEEDED(link->GetIconLocation(location,32768,&index)) && *location) {
                    append_stamp(stamps,QString::fromWCharArray(location));stamps.append(index);
                }
            }
        }
    }
    return hash(QJsonDocument(stamps).toJson(QJsonDocument::Compact));
}
void prune(const QString& directory) {
    // Reserve space for up to eight bounded PNG writes between sweeps.
    static std::atomic<unsigned> writes{0};
    if(writes.fetch_add(1)%8!=0)return;
    QDir dir(directory);
    const auto files=dir.entryInfoList({"*.png"},QDir::Files,QDir::Time);
    qint64 bytes=0;int count=0;
    for(const auto& file:files) {
        bytes+=file.size();++count;
        // Leave room for the next maintenance interval's writes.
        if(count>504 || bytes>24*1024*1024)QFile::remove(file.absoluteFilePath());
    }
}
}

QString app_cache_directory() {
    auto base=QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation);
    if(base.isEmpty())base=QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base.isEmpty()?QString{}:QDir(base).filePath("FastPalette/cache");
}

std::vector<AppEntry> read_app_cache(const QString& directory) {
    if(directory.isEmpty())return {};
    QFile file(QDir(directory).filePath("apps-v1.json"));
    if(!file.open(QIODevice::ReadOnly) || file.size()>max_index_bytes)return {};
    const auto document=QJsonDocument::fromJson(file.read(max_index_bytes+1));
    if(!document.isObject())return {};
    const auto root=document.object();
    if(root.value("version").toDouble()!=1 || !root.value("apps").isArray())return {};
    const auto array=root.value("apps").toArray();
    if(array.size()>max_entries)return {};
    std::vector<AppEntry> result;result.reserve(array.size());std::unordered_set<std::wstring> seen;
    for(const auto& value:array) {
        if(!value.isObject())return {};
        const auto item=value.toObject();AppEntry app;
        for(const auto* field:{"id","name","target","detail","arguments"})if(!item.value(field).isString())return {};
        app.id=item.value("id").toString().toStdWString();app.name=item.value("name").toString().toStdWString();
        app.target=item.value("target").toString().toStdWString();app.detail=item.value("detail").toString().toStdWString();
        app.arguments=item.value("arguments").toString().toStdWString();
        const auto kind=item.value("kind").toInt(-1),source=item.value("source").toInt(-1);
        if(kind<0 || kind>2 || source<0 || source>4 || !item.value("folder").isBool())return {};
        app.kind=static_cast<LaunchKind>(kind);app.source=static_cast<SearchSource>(source);app.is_folder=item.value("folder").toBool();
        if(!valid_app(app))return {};
        if(app.source!=SearchSource::application || app.id.starts_with(L"portable:") || !seen.insert(app.id).second)continue;
        app.folded=fold(app.name);result.push_back(std::move(app));
    }
    return result;
}

void write_app_cache(const QString& directory,const std::vector<AppEntry>& apps) {
    if(directory.isEmpty())return;
    QJsonArray array;qint64 estimated_bytes=64;
    for(const auto& app:apps) {
        if(app.source!=SearchSource::application || app.id.starts_with(L"portable:") || !valid_app(app))continue;
        if(array.size()>=max_entries)break;
        const QJsonObject item{{"id",q(app.id)},{"name",q(app.name)},{"target",q(app.target)},
            {"detail",q(app.detail)},{"arguments",q(app.arguments)},{"kind",static_cast<int>(app.kind)},
            {"source",static_cast<int>(app.source)},{"folder",app.is_folder}};
        estimated_bytes+=QJsonDocument(item).toJson(QJsonDocument::Compact).size()+1;
        if(estimated_bytes>max_index_bytes)break;
        array.append(item);
    }
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"apps",array}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>max_index_bytes || !QDir().mkpath(directory))return;
    QSaveFile file(QDir(directory).filePath("apps-v1.json"));
    if(file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size())file.commit();
}

QImage cached_app_icon(const QString& directory,const AppEntry& app,const std::function<QImage(const AppEntry&)>& loader) {
    if(!valid_app(app))return {};
    const auto icon_dir=directory.isEmpty()?QString{}:QDir(directory).filePath("icons-v1");
    const auto key=hash(QJsonDocument(QJsonArray{q(app.id),q(app.target),static_cast<int>(app.source),static_cast<int>(app.kind)}).toJson(QJsonDocument::Compact));
    const auto stamp=fingerprint(app);
    const auto path=QDir(icon_dir).filePath(key+".png");
    if(!icon_dir.isEmpty()) {
        QFile file(path);
        if(file.open(QIODevice::ReadOnly) && file.size()<=max_icon_bytes) {
            QImageReader reader(&file,"PNG");const auto size=reader.size();
            if(size.width()>0 && size.height()>0 && size.width()<=max_dimension && size.height()<=max_dimension) {
                auto image=reader.read();
                if(!image.isNull() && image.text("fingerprint")==stamp)return image;
            }
        }
    }
    QImage image;
    if(loader)image=loader(app);
    else if(const auto icon=load_app_icon(app)) {image=QImage::fromHICON(icon);DestroyIcon(icon);}
    if(image.isNull())return {};
    if(image.width()>max_dimension || image.height()>max_dimension)image=image.scaled(max_dimension,max_dimension,Qt::KeepAspectRatio,Qt::SmoothTransformation);
    image.setText("fingerprint",stamp);
    if(!icon_dir.isEmpty() && QDir().mkpath(icon_dir)) {
        prune(icon_dir);
        QSaveFile file(path);
        if(file.open(QIODevice::WriteOnly) && image.save(&file,"PNG") && file.size()<=max_icon_bytes)file.commit();
    }
    return image;
}
}
