#pragma once
#include <QLocale>
#include <QString>
namespace palette {
QString normalize_number_input(const QString& input,const QLocale& locale=QLocale::system());
QString plain_number_result(const QString& result);
QString localized_number_result(const QString& result,const QLocale& locale=QLocale::system());
}
