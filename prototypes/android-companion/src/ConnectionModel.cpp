#include "ConnectionModel.h"

ConnectionModel::ConnectionModel(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QTcpSocket::connected, this, [this] {
        setState("connected");
        // Non-GUI registration + slice subscription (RadioModel.cpp order,
        // minus "client gui" — see header comment).
        sendCommand("client program AetherCompanion");
        sendCommand("sub slice all");
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, [this] {
        m_rxAudio.stop();
        m_rxAudioStreamId = 0;
        m_pendingReplies.clear();
        m_slices.clear();
        setState("disconnected");
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](auto) {
        m_rxAudio.stop();
        m_rxAudioStreamId = 0;
        m_pendingReplies.clear();
        m_slices.clear();
        setState("error: " + m_socket.errorString());
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, &ConnectionModel::onReadyRead);
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
    m_radioLabel = label;
    m_rxBuffer.clear();
    m_seq = 1;
    setState("connecting");
    m_socket.connectToHost(host, static_cast<quint16>(port));
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

void ConnectionModel::startRxAudio()
{
    if (m_rxAudio.active())
        return;
    // Uncompressed stream (spike scope; desktop uses Opus on WAN only).
    sendCommand(QStringLiteral("stream create type=remote_audio_rx compression=none"),
                [this](int code, const QString& body) {
                    if (code != 0)
                        return;
                    // Reply body is the stream id (hex, with or without 0x).
                    bool ok = false;
                    const quint32 id = body.trimmed().toUInt(&ok, 16);
                    if (!ok || id == 0)
                        return;
                    m_rxAudioStreamId = id;
                    m_rxAudio.start(id, QHostAddress(m_socket.peerAddress()));
                });
}

void ConnectionModel::stopRxAudio()
{
    m_rxAudio.stop();
    if (m_rxAudioStreamId != 0) {
        sendCommand(QStringLiteral("stream remove 0x%1")
                        .arg(m_rxAudioStreamId, 8, 16, QChar('0')));
        m_rxAudioStreamId = 0;
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

    // S (status) lines drive the slice model; V/H/M are accepted and
    // dropped. Status body: "<topic> ..." after "S<handle>|".
    if (line[0] != 'S')
        return;
    const int bar = line.indexOf('|');
    if (bar < 0)
        return;
    const QString body = line.mid(bar + 1);

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
