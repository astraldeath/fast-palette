#pragma once
#include "model.hpp"
#include <QImage>
#include <QString>
#include <functional>

namespace palette {
QString app_cache_directory();
std::vector<AppEntry> read_app_cache(const QString& directory);
void write_app_cache(const QString& directory, const std::vector<AppEntry>& apps);
// Worker-thread only; the caller initializes COM. The optional loader supports tests.
QImage cached_app_icon(const QString& directory, const AppEntry& app,
    const std::function<QImage(const AppEntry&)>& loader = {});
}
