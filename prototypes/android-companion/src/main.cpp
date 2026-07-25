#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include <QObject>

#include "ConnectionModel.h"
#include "DiscoveryModel.h"
#include "SmartLinkModel.h"
#include "VitaStream.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);

    DiscoveryModel discovery;
    discovery.start();

    ConnectionModel connection;
    SmartLinkModel smartLink;
    QObject::connect(&smartLink, &SmartLinkModel::wanConnectReady,
                     &connection,
                     [&connection](const QString& host, int tlsPort,
                                   const QString& handle, const QString& label) {
                         connection.connectWan(host, tlsPort, handle, label);
                     });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("discoveryModel", &discovery);
    engine.rootContext()->setContextProperty("connection", &connection);
    engine.rootContext()->setContextProperty("smartLink", &smartLink);
    engine.loadFromModule("AetherCompanion", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
