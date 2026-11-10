#pragma once
#include "model.hpp"
#include <QDateTime>
#include <QString>
namespace palette {
struct TimestampResult { CalcStatus status=CalcStatus::none; QString text,detail; };
TimestampResult convert_timestamp(QString query,QDateTime now=QDateTime::currentDateTimeUtc());
}
