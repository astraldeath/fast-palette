#pragma once
#include <QIcon>
namespace palette {
enum class UiIcon { search, settings, application, calculator, logo, close };
void apply_ui_theme();
QIcon ui_icon(UiIcon icon);
}
