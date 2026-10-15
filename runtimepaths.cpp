#include "runtimepaths.h"

#include <QDir>
#include <QStandardPaths>

QString RuntimePaths::pythonExecutable()
{
    const QString configured = qEnvironmentVariable("PYTHON_EXECUTABLE");
    if (!configured.isEmpty()) {
        return QStandardPaths::findExecutable(QDir::fromNativeSeparators(configured));
    }

    for (const QString& name : {QStringLiteral("python"), QStringLiteral("python3")}) {
        const QString executable = QStandardPaths::findExecutable(name);
        if (!executable.isEmpty()) return executable;
    }
    return {};
}
