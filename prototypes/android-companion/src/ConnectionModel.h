#pragma once

// Minimal SmartSDR TCP command-channel client for the spike.
// Wire format facts mirror src/core/CommandParser.{h,cpp} and the init
// sequence in src/models/RadioModel.cpp:
//   out:  C<seq>|<command>\n
//   in:   V<version> | H<hex handle> | R<seq>|<hexcode>|<body>
//         | S<hex handle>|<topic ...> | M<...>
// The spike registers as a NON-GUI client ("client program" only, no
// "client gui") so it never claims a GUI slot on a real radio.

#include <QHash>
#include <QObject>
#include <QTcpSocket>

#include <functional>

#include "RxAudioStream.h"
#include "SliceListModel.h"

class ConnectionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString radioLabel READ radioLabel NOTIFY stateChanged)
    Q_PROPERTY(SliceListModel* slices READ slices CONSTANT)
    Q_PROPERTY(RxAudioStream* rxAudio READ rxAudio CONSTANT)

public:
    explicit ConnectionModel(QObject* parent = nullptr);

    QString state() const { return m_state; }
    QString radioLabel() const { return m_radioLabel; }
    SliceListModel* slices() { return &m_slices; }
    RxAudioStream* rxAudio() { return &m_rxAudio; }

    Q_INVOKABLE void connectToRadio(const QString& host, int port,
                                    const QString& label);
    Q_INVOKABLE void disconnectFromRadio();

    Q_INVOKABLE void tune(int sliceId, double freqMhz);
    Q_INVOKABLE void setMode(int sliceId, const QString& mode);

    Q_INVOKABLE void startRxAudio();
    Q_INVOKABLE void stopRxAudio();

signals:
    void stateChanged();

private:
    using ReplyHandler = std::function<void(int code, const QString& body)>;

    void setState(const QString& state);
    void sendCommand(const QString& command, ReplyHandler onReply = {});
    void onReadyRead();
    void handleLine(const QString& line);

    QTcpSocket m_socket;
    SliceListModel m_slices;
    RxAudioStream m_rxAudio;
    QHash<quint32, ReplyHandler> m_pendingReplies;
    quint32 m_rxAudioStreamId{0};
    QString m_state{"disconnected"};
    QString m_radioLabel;
    QByteArray m_rxBuffer;
    quint32 m_seq{1};
};
