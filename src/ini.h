#ifndef INI_H
#define INI_H

#include <memory>
#include <QString>
#include "logger.h"
#include "timelistener.h"
namespace ini {

extern QVector<QSharedPointer<TimeListener>> listeners;
void init(QStringList &params);
}

#endif // INI_H