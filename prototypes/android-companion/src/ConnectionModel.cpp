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
        m_slices.clear();
        setState("disconnected");
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](auto) {
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

void ConnectionModel::sendCommand(const QString& command)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return;
    m_socket.write(QStringLiteral("C%1|%2\n").arg(m_seq++).arg(command).toUtf8());
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
    // Only S (status) lines matter to the spike; V/H/R/M are accepted and
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
