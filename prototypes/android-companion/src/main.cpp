#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "ConnectionModel.h"
#include "DiscoveryModel.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    DiscoveryModel discovery;
    discovery.start();

    ConnectionModel connection;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("discoveryModel", &discovery);
    engine.rootContext()->setContextProperty("connection", &connection);
    engine.loadFromModule("AetherCompanion", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
