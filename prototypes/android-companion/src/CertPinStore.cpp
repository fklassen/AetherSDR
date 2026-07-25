#include "CertPinStore.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

constexpr auto kKey = "wanCertPins";

QSettings settings()
{
    return QSettings(QStringLiteral("AetherSDR"),
                     QStringLiteral("CompanionSpike"));
}

QJsonObject loadCache()
{
    QSettings s = settings();
    return QJsonDocument::fromJson(s.value(QLatin1String(kKey)).toByteArray())
        .object();
}

void saveCache(const QJsonObject& obj)
{
    QSettings s = settings();
    s.setValue(QLatin1String(kKey),
               QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace

namespace CertPinStore {

QString load(const QString& host)
{
    return loadCache()
        .value(host)
        .toObject()
        .value(QStringLiteral("fp"))
        .toString();
}

void store(const QString& host, const QString& fingerprintHex)
{
    QJsonObject obj = loadCache();
    QJsonObject entry;
    entry[QStringLiteral("fp")] = fingerprintHex;
    entry[QStringLiteral("pinnedAt")] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    obj[host] = entry;
    saveCache(obj);
}

QVector<PinnedCert> list()
{
    QVector<PinnedCert> out;
    const QJsonObject obj = loadCache();
    out.reserve(obj.size());
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString fp = entry.value(QStringLiteral("fp")).toString();
        if (!fp.isEmpty())
            out.append({it.key(), fp,
                        entry.value(QStringLiteral("pinnedAt")).toString()});
    }
    return out;
}

void forget(const QString& host)
{
    QJsonObject obj = loadCache();
    obj.remove(host);
    saveCache(obj);
}

void forgetAll()
{
    saveCache({});
}

} // namespace CertPinStore
