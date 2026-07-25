#pragma once

// SmartLink client for the spike: Auth0 login + broker connection +
// WAN radio list. Facts mirror src/core/SmartLinkClient.{h,cpp}:
//   - Auth0 ROPC ("password-realm" grant) at frtest.auth0.com with the
//     public SmartSDR client id → id_token (JWT) + refresh_token
//   - Broker: TLS smartlink.flexradio.com:443, newline commands:
//       application register name=<n> platform=<p> token=<jwt>
//       application connect serial=<s> hole_punch_port=<port>
//       ping (10 s keepalive)
//     replies include "radio list ..." and
//     "radio connect_ready handle=<h> serial=<s>"
//
// Spike security posture (throwaway): passwords are never persisted;
// tokens live in memory only (no keychain). TLS errors are only
// ignorable when the broker host has been overridden to a test
// harness — the real smartlink.flexradio.com always verifies.

#include <QAbstractListModel>
#include <QNetworkAccessManager>
#include <QSslSocket>
#include <QTimer>

#include <vector>

struct WanRadio {
    QString serial;
    QString model;
    QString nickname;
    QString callsign;
    QString status;
    QString publicIp;
    int publicTlsPort{-1};
};

class SmartLinkModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString authState READ authState NOTIFY authStateChanged)
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY authStateChanged)

public:
    enum Role {
        SerialRole = Qt::UserRole + 1,
        ModelRole2,
        NicknameRole,
        CallsignRole,
        StatusRole,
    };

    explicit SmartLinkModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString authState() const { return m_authState; }
    bool loggedIn() const { return m_brokerConnected; }

    // Auth0 login, then broker connect. If brokerOverride is non-empty
    // ("host:port"), Auth0 is skipped and a demo token registers against
    // that harness broker instead (TLS errors ignored for it only).
    Q_INVOKABLE void login(const QString& email, const QString& password,
                           const QString& brokerOverride = QString());
    Q_INVOKABLE void logout();

    // Ask broker for a WAN connection to a listed radio.
    // holePunchPort = local VITA UDP port (VitaStream::kLocalPort).
    Q_INVOKABLE void requestConnect(int row, int holePunchPort);

signals:
    void authStateChanged();
    // Broker says the radio accepted: TLS-connect to host:port and send
    // "wan validate handle=<handle>".
    void wanConnectReady(const QString& host, int tlsPort,
                         const QString& handle, const QString& label);

private:
    void connectBroker();
    void onBrokerLine(const QString& line);
    void parseRadioList(const QString& msg);
    void setAuthState(const QString& s);

    static constexpr const char* kAuth0Domain = "frtest.auth0.com";
    static constexpr const char* kAuth0ClientId =
        "4Y9fEIIsVYyQo5u6jr7yBWc4lV5ugC2m";
    static constexpr const char* kBrokerHost = "smartlink.flexradio.com";
    static constexpr int kBrokerPort = 443;

    QNetworkAccessManager m_nam;
    QSslSocket m_socket;
    QTimer m_pingTimer;
    QByteArray m_rxBuffer;
    std::vector<WanRadio> m_radios;
    QString m_authState{"logged out"};
    QString m_idToken;
    QString m_brokerHost;
    int m_brokerPort{kBrokerPort};
    bool m_harnessMode{false};
    bool m_brokerConnected{false};
    QString m_pendingConnectSerial;
};
