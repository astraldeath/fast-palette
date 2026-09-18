#pragma once
#include <QIcon>
namespace palette {
enum class UiIcon { search, settings, application, calculator, logo, close, minimize, maximize, restore, folder, file };
void apply_ui_theme();
QIcon ui_icon(UiIcon icon);
}
