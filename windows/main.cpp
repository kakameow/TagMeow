#include <QGuiApplication>
#include <QQmlApplicationEngine>

#ifdef _WIN32
#include <windows.h>
#endif

#include "bridge.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    Bridge bridge;
    QQmlApplicationEngine engine;

    engine.loadFromModule("windows", "Main");
    if (engine.rootObjects().isEmpty())
    {
        return -1;
    }

    bridge.bindTo(engine.rootObjects().first());

    return QGuiApplication::exec();
}
