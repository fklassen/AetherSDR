#include "SmartLinkModel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

SmartLinkModel::SmartLinkModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(&m_socket, &QSslSocket::encrypted, this, [this] {
        m_brokerConnected = true;
        // Register (SmartLinkClient::onSslConnected order).
        m_socket.write(QStringLiteral(
                           "application register name=AetherCompanion "
                           "platform=Android token=%1\n")
                           .arg(m_idToken)
                           .toUtf8());
        m_pingTimer.start();
        setAuthState("connected");
    });
    connect(&m_socket, &QSslSocket::disconnected, this, [this] {
        m_pingTimer.stop();
        m_brokerConnected = false;
        beginResetModel();
        m_radios.clear();
        endResetModel();
        if (m_authState != QStringLiteral("logged out"))
            setAuthState("broker disconnected");
    });
    connect(&m_socket, &QSslSocket::sslErrors, this,
            [this](const QList<QSslError>& errors) {
                // Harness broker uses a self-signed cert; ONLY that
                // override may bypass verification. The real SmartLink
                // host always verifies.
                if (m_harnessMode)
                    m_socket.ignoreSslErrors(errors);
            });
    connect(&m_socket, &QSslSocket::errorOccurred, this, [this](auto) {
        if (!m_brokerConnected)
            setAuthState("broker error: " + m_socket.errorString());
    });
    connect(&m_socket, &QSslSocket::readyRead, this, [this] {
        m_rxBuffer += m_socket.readAll();
        int nl = -1;
        while ((nl = m_rxBuffer.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(m_rxBuffer.left(nl)).trimmed();
            m_rxBuffer.remove(0, nl + 1);
            if (!line.isEmpty())
                onBrokerLine(line);
        }
    });

    m_pingTimer.setInterval(10000);
    connect(&m_pingTimer, &QTimer::timeout, this, [this] {
        m_socket.write("ping\n");
    });
}

int SmartLinkModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_radios.size());
}

QVariant SmartLinkModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_radios.size()))
        return {};
    const WanRadio& r = m_radios[static_cast<size_t>(index.row())];
    switch (role) {
    case SerialRole:   return r.serial;
    case ModelRole2:   return r.model;
    case NicknameRole: return r.nickname;
    case CallsignRole: return r.callsign;
    case StatusRole:   return r.status;
    }
    return {};
}

QHash<int, QByteArray> SmartLinkModel::roleNames() const
{
    return {
        {SerialRole, "serial"},
        {ModelRole2, "radioModel"},
        {NicknameRole, "nickname"},
        {CallsignRole, "callsign"},
        {StatusRole, "status"},
    };
}

void SmartLinkModel::setAuthState(const QString& s)
{
    m_authState = s;
    emit authStateChanged();
}

void SmartLinkModel::login(const QString& email, const QString& password,
                           const QString& brokerOverride)
{
    if (!brokerOverride.isEmpty()) {
        // Harness mode: skip Auth0, register with a demo token.
        m_harnessMode = true;
        m_brokerHost = brokerOverride.section(':', 0, 0);
        m_brokerPort = brokerOverride.section(':', 1, 1).toInt();
        if (m_brokerPort <= 0)
            m_brokerPort = kBrokerPort;
        m_idToken = QStringLiteral("demo");
        setAuthState("connecting (harness)");
        connectBroker();
        return;
    }

    m_harnessMode = false;
    m_brokerHost = QString::fromLatin1(kBrokerHost);
    m_brokerPort = kBrokerPort;
    setAuthState("authenticating");

    // Auth0 Resource Owner Password Grant (SmartLinkClient::login).
    QNetworkRequest req(
        QUrl(QStringLiteral("https://%1/oauth/token").arg(kAuth0Domain)));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    QJsonObject body;
    body["grant_type"] = "http://auth0.com/oauth/grant-type/password-realm";
    body["realm"] = "Username-Password-Authentication";
    body["username"] = email;
    body["password"] = password;
    body["client_id"] = kAuth0ClientId;
    body["scope"] =
        "openid email given_name family_name profile picture offline_access";

    auto* reply = m_nam.post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const QJsonObject err =
                QJsonDocument::fromJson(reply->readAll()).object();
            setAuthState("auth failed: "
                         + err.value(QStringLiteral("error_description"))
                               .toString(reply->errorString()));
            return;
        }
        const QJsonObject resp =
            QJsonDocument::fromJson(reply->readAll()).object();
        m_idToken = resp.value(QStringLiteral("id_token")).toString();
        if (m_idToken.isEmpty()) {
            setAuthState("auth failed: no id_token");
            return;
        }
        setAuthState("authenticated — connecting broker");
        connectBroker();
    });
}

void SmartLinkModel::logout()
{
    m_pingTimer.stop();
    m_socket.abort();
    m_idToken.clear();
    m_brokerConnected = false;
    beginResetModel();
    m_radios.clear();
    endResetModel();
    setAuthState("logged out");
}

void SmartLinkModel::connectBroker()
{
    m_rxBuffer.clear();
    m_socket.abort();
    m_socket.connectToHostEncrypted(m_brokerHost,
                                    static_cast<quint16>(m_brokerPort));
}

void SmartLinkModel::requestConnect(int row, int holePunchPort)
{
    if (!m_brokerConnected || row < 0
        || row >= static_cast<int>(m_radios.size()))
        return;
    m_pendingConnectSerial = m_radios[static_cast<size_t>(row)].serial;
    m_socket.write(QStringLiteral(
                       "application connect serial=%1 hole_punch_port=%2\n")
                       .arg(m_pendingConnectSerial)
                       .arg(holePunchPort)
                       .toUtf8());
}

void SmartLinkModel::onBrokerLine(const QString& line)
{
    if (line.startsWith(QStringLiteral("radio list "))) {
        parseRadioList(line.mid(11));
        return;
    }
    if (line.startsWith(QStringLiteral("radio connect_ready"))) {
        // "radio connect_ready handle=<h> serial=<s>"
        QString handle, serial;
        const QStringList words = line.split(' ', Qt::SkipEmptyParts);
        for (const QString& w : words) {
            const int eq = w.indexOf('=');
            if (eq <= 0)
                continue;
            if (w.left(eq) == QStringLiteral("handle"))
                handle = w.mid(eq + 1);
            else if (w.left(eq) == QStringLiteral("serial"))
                serial = w.mid(eq + 1);
        }
        for (const WanRadio& r : m_radios) {
            if (r.serial == serial && !handle.isEmpty()
                && r.publicTlsPort > 0) {
                emit wanConnectReady(
                    r.publicIp, r.publicTlsPort, handle,
                    r.model + (r.nickname.isEmpty() ? QString()
                                                    : " · " + r.nickname));
                return;
            }
        }
    }
}

void SmartLinkModel::parseRadioList(const QString& msg)
{
    // Serialized radios separated by '|', fields "key=value" separated
    // by '#' (SmartLinkClient::parseRadioList shape).
    beginResetModel();
    m_radios.clear();
    const QStringList entries = msg.split('|', Qt::SkipEmptyParts);
    for (const QString& entry : entries) {
        WanRadio radio;
        const QStringList fields = entry.split('#', Qt::SkipEmptyParts);
        for (const QString& f : fields) {
            const int eq = f.indexOf('=');
            if (eq <= 0)
                continue;
            const QString key = f.left(eq);
            const QString value = f.mid(eq + 1);
            if (key == QStringLiteral("serial")) radio.serial = value;
            else if (key == QStringLiteral("model")) radio.model = value;
            else if (key == QStringLiteral("nickname")) radio.nickname = value;
            else if (key == QStringLiteral("callsign")) radio.callsign = value;
            else if (key == QStringLiteral("status")) radio.status = value;
            else if (key == QStringLiteral("public_ip")) radio.publicIp = value;
            else if (key == QStringLiteral("public_tls_port"))
                radio.publicTlsPort = value.toInt();
        }
        if (!radio.serial.isEmpty())
            m_radios.push_back(std::move(radio));
    }
    endResetModel();
}
