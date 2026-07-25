#include "ConnectionModel.h"

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#endif

namespace {

// Foreground service keeps RX audio + the VITA socket alive while the
// screen is off (phase 6). No-op off Android.
void setForegroundService(bool on)
{
#ifdef Q_OS_ANDROID
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject::callStaticMethod<void>(
        "org/aethersdr/companion/RxForegroundService",
        on ? "start" : "stop",
        "(Landroid/content/Context;)V",
        context.object());
#else
    Q_UNUSED(on);
#endif
}

} // namespace

ConnectionModel::ConnectionModel(QObject* parent)
    : QObject(parent)
{
    const auto onLinkUp = [this] {
        setState("connected");
        // Non-GUI registration + subscriptions (RadioModel.cpp order,
        // minus "client gui" — see header comment). One VITA socket for
        // audio + FFT, opened for the connection's lifetime.
        m_vita.openSocket(QHostAddress(m_socket.peerAddress()));
        // WAN sessions authenticate first (WanConnection: "wan validate
        // handle=<h>" is the very first command on the wire).
        if (m_wanMode)
            sendCommand(QStringLiteral("wan validate handle=%1").arg(m_wanHandle));
        sendCommand("client program AetherCompanion");
        sendCommand("sub slice all");
        sendCommand("sub pan all");
        sendCommand("sub meter all");
    };
    // Plain LAN sockets signal connected; TLS WAN sockets are usable
    // only once encrypted.
    connect(&m_socket, &QSslSocket::connected, this, [this, onLinkUp] {
        if (!m_wanMode)
            onLinkUp();
    });
    connect(&m_socket, &QSslSocket::encrypted, this, [this, onLinkUp] {
        if (m_wanMode)
            onLinkUp();
    });
    connect(&m_socket, &QSslSocket::sslErrors, this,
            [this](const QList<QSslError>& errors) {
                // Spike-grade trust-on-connect for the radio's self-signed
                // cert. Desktop pins fingerprints (GHSA-wfx7-w6p8-4jr2);
                // required before any graduation from prototypes/.
                if (m_wanMode)
                    m_socket.ignoreSslErrors(errors);
            });
    connect(&m_vita, &VitaStream::meterData, this,
            [this](const QVector<quint16>& ids, const QVector<qint16>& values) {
                for (int i = 0; i < ids.size(); ++i) {
                    const auto it = m_sMeterSliceByIndex.constFind(ids[i]);
                    if (it != m_sMeterSliceByIndex.constEnd())
                        m_slices.setSMeter(*it, values[i] / 128.0); // dBm
                }
            });
    const auto teardown = [this] {
        setForegroundService(false);
        m_vita.closeSocket();
        m_rxAudioStreamId = 0;
        m_panId = 0;
        m_pendingReplies.clear();
        m_sMeterSliceByIndex.clear();
        m_slices.clear();
        emit panChanged();
    };
    connect(&m_socket, &QSslSocket::disconnected, this, [this, teardown] {
        teardown();
        setState("disconnected");
    });
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this, teardown](auto) {
        teardown();
        setState("error: " + m_socket.errorString());
    });
    connect(&m_socket, &QSslSocket::readyRead, this, &ConnectionModel::onReadyRead);
}

void ConnectionModel::setState(const QString& state)
{
    m_state = state;
    emit stateChanged();
}

void ConnectionModel::connectToRadio(const QString& host, int port,
                                     const QString& label)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
    m_wanMode = false;
    m_wanHandle.clear();
    m_radioLabel = label;
    m_rxBuffer.clear();
    m_seq = 1;
    setState("connecting");
    m_socket.connectToHost(host, static_cast<quint16>(port));
}

void ConnectionModel::connectWan(const QString& host, int tlsPort,
                                 const QString& wanHandle, const QString& label)
{
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
    m_wanMode = true;
    m_wanHandle = wanHandle;
    m_radioLabel = label + " (WAN)";
    m_rxBuffer.clear();
    m_seq = 1;
    setState("connecting (WAN TLS)");
    m_socket.connectToHostEncrypted(host, static_cast<quint16>(tlsPort));
}

void ConnectionModel::disconnectFromRadio()
{
    m_socket.disconnectFromHost();
    if (m_socket.state() != QAbstractSocket::UnconnectedState)
        m_socket.abort();
}

void ConnectionModel::sendCommand(const QString& command, ReplyHandler onReply)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    const quint32 seq = m_seq++;
    if (onReply)
        m_pendingReplies.insert(seq, std::move(onReply));
    m_socket.write(QStringLiteral("C%1|%2\n").arg(seq).arg(command).toUtf8());
}

void ConnectionModel::setOpusEnabled(bool on)
{
    if (m_opusEnabled == on)
        return;
    m_opusEnabled = on;
    emit opusEnabledChanged();
}

void ConnectionModel::startRxAudio()
{
    if (m_vita.audioActive())
        return;
    // Compression per toggle (desktop: Opus on WAN, none on LAN).
    const bool opus = m_opusEnabled && VitaStream::opusCapable();
    sendCommand(QStringLiteral("stream create type=remote_audio_rx compression=%1")
                    .arg(opus ? QStringLiteral("opus") : QStringLiteral("none")),
                [this](int code, const QString& body) {
                    if (code != 0)
                        return;
                    // Reply body is the stream id (hex, with or without 0x).
                    bool ok = false;
                    const quint32 id = body.trimmed().toUInt(&ok, 16);
                    if (!ok || id == 0)
                        return;
                    m_rxAudioStreamId = id;
                    m_vita.setAudioStream(id);
                    setForegroundService(true);
                });
}

void ConnectionModel::stopRxAudio()
{
    setForegroundService(false);
    m_vita.clearAudioStream();
    if (m_rxAudioStreamId != 0) {
        sendCommand(QStringLiteral("stream remove 0x%1")
                        .arg(m_rxAudioStreamId, 8, 16, QChar('0')));
        m_rxAudioStreamId = 0;
    }
}

void ConnectionModel::startSpectrum()
{
    if (m_panId != 0)
        return;
    // Desktop flow (RadioModel::createPanadapter): panafall create, then
    // size + dBm range. Reply body's first comma field is the pan id.
    sendCommand(QStringLiteral("display panafall create x=100 y=100"),
                [this](int code, const QString& body) {
                    if (code != 0)
                        return;
                    const QString first = body.split(',').first().trimmed();
                    bool ok = false;
                    const quint32 id =
                        first.startsWith("0x")
                            ? first.mid(2).toUInt(&ok, 16)
                            : first.toUInt(&ok, 16);
                    if (!ok || id == 0)
                        return;
                    m_panId = id;
                    const QString hexId =
                        QStringLiteral("0x%1").arg(id, 8, 16, QChar('0'));
                    sendCommand(QStringLiteral(
                                    "display pan set %1 xpixels=512 ypixels=200")
                                    .arg(hexId));
                    sendCommand(QStringLiteral(
                                    "display pan set %1 min_dbm=-130 max_dbm=-40")
                                    .arg(hexId));
                    m_vita.setFftStream(id);
                    emit panChanged();
                });
}

void ConnectionModel::stopSpectrum()
{
    m_vita.clearFftStream();
    if (m_panId != 0) {
        sendCommand(QStringLiteral("display pan remove 0x%1")
                        .arg(m_panId, 8, 16, QChar('0')));
        m_panId = 0;
        emit panChanged();
    }
}

void ConnectionModel::tune(int sliceId, double freqMhz)
{
    // Scroll-wheel-style tune; matches src/models/SliceModel.cpp.
    sendCommand(QStringLiteral("slice tune %1 %2 autopan=0")
                    .arg(sliceId)
                    .arg(freqMhz, 0, 'f', 6));
}

void ConnectionModel::setMode(int sliceId, const QString& mode)
{
    sendCommand(QStringLiteral("slice set %1 mode=%2").arg(sliceId).arg(mode));
}

void ConnectionModel::onReadyRead()
{
    m_rxBuffer += m_socket.readAll();
    int nl = -1;
    while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_rxBuffer.left(nl)).trimmed();
        m_rxBuffer.remove(0, nl + 1);
        if (!line.isEmpty())
            handleLine(line);
    }
}

void ConnectionModel::handleLine(const QString& line)
{
    // R<seq>|<hexcode>|<body> — dispatch to the pending-reply handler.
    if (line[0] == 'R') {
        const QStringList parts = line.mid(1).split('|');
        if (parts.size() < 2)
            return;
        const quint32 seq = parts[0].toUInt();
        const auto it = m_pendingReplies.find(seq);
        if (it == m_pendingReplies.end())
            return;
        const ReplyHandler handler = std::move(it.value());
        m_pendingReplies.erase(it);
        handler(parts[1].toInt(nullptr, 16),
                parts.size() >= 3 ? parts[2] : QString());
        return;
    }

    // S (status) lines drive the slice model and pan state; V/H/M are
    // accepted and dropped. Status body: "<topic> ..." after "S<handle>|".
    if (line[0] != 'S')
        return;
    const int bar = line.indexOf('|');
    if (bar < 0)
        return;
    const QString body = line.mid(bar + 1);

    // Meter definitions: "meter 7.src=SLC#7.num=0#7.nam=LEVEL#…"
    // ('#'-separated "index.key=value" tokens — FlexBackend::decodeMeterStatus).
    // The spike only tracks per-slice LEVEL meters (the S-meter).
    if (body.startsWith(QStringLiteral("meter "))) {
        const QString meterBody = body.mid(6);
        if (meterBody.contains(QStringLiteral("removed")))
            return;
        QHash<int, QHash<QString, QString>> grouped;
        const QStringList meterTokens = meterBody.split('#', Qt::SkipEmptyParts);
        for (const QString& token : meterTokens) {
            const int dot = token.indexOf('.');
            if (dot <= 0)
                continue;
            const int eq = token.indexOf('=', dot);
            if (eq < 0)
                continue;
            bool ok = false;
            const int idx = token.left(dot).toInt(&ok);
            if (!ok)
                continue;
            grouped[idx][token.mid(dot + 1, eq - dot - 1)] = token.mid(eq + 1);
        }
        for (auto it = grouped.constBegin(); it != grouped.constEnd(); ++it) {
            const auto& fields = it.value();
            if (fields.value(QStringLiteral("src")) == QStringLiteral("SLC")
                && fields.value(QStringLiteral("nam")) == QStringLiteral("LEVEL")) {
                m_sMeterSliceByIndex[static_cast<quint16>(it.key())] =
                    fields.value(QStringLiteral("num")).toInt();
            }
        }
        return;
    }

    // "display pan 0x<id> center=… bandwidth=…"
    if (body.startsWith(QStringLiteral("display pan "))) {
        bool changed = false;
        const QStringList panTokens = body.split(' ', Qt::SkipEmptyParts);
        for (const QString& token : panTokens) {
            const int eq = token.indexOf('=');
            if (eq <= 0)
                continue;
            const QString key = token.left(eq);
            const QString value = token.mid(eq + 1);
            if (key == QStringLiteral("center")) {
                m_panCenterMhz = value.toDouble();
                changed = true;
            } else if (key == QStringLiteral("bandwidth")) {
                m_panBandwidthMhz = value.toDouble();
                changed = true;
            }
        }
        if (changed)
            emit panChanged();
        return;
    }

    const QStringList tokens = body.split(' ', Qt::SkipEmptyParts);
    if (tokens.size() < 2 || tokens[0] != QStringLiteral("slice"))
        return;
    bool ok = false;
    const int sliceId = tokens[1].toInt(&ok);
    if (!ok)
        return;

    QHash<QString, QString> kvs;
    for (int i = 2; i < tokens.size(); ++i) {
        const int eq = tokens[i].indexOf('=');
        if (eq > 0)
            kvs.insert(tokens[i].left(eq), tokens[i].mid(eq + 1));
    }
    m_slices.applyStatus(sliceId, kvs);
}
