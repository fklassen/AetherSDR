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

#include "SliceListModel.h"
#include "VitaStream.h"

class ConnectionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString radioLabel READ radioLabel NOTIFY stateChanged)
    Q_PROPERTY(SliceListModel* slices READ slices CONSTANT)
    Q_PROPERTY(VitaStream* vita READ vita CONSTANT)
    Q_PROPERTY(bool spectrumActive READ spectrumActive NOTIFY panChanged)
    Q_PROPERTY(double panCenterMhz READ panCenterMhz NOTIFY panChanged)
    Q_PROPERTY(double panBandwidthMhz READ panBandwidthMhz NOTIFY panChanged)

public:
    explicit ConnectionModel(QObject* parent = nullptr);

    QString state() const { return m_state; }
    QString radioLabel() const { return m_radioLabel; }
    SliceListModel* slices() { return &m_slices; }
    VitaStream* vita() { return &m_vita; }
    bool spectrumActive() const { return m_panId != 0; }
    double panCenterMhz() const { return m_panCenterMhz; }
    double panBandwidthMhz() const { return m_panBandwidthMhz; }

    Q_INVOKABLE void connectToRadio(const QString& host, int port,
                                    const QString& label);
    Q_INVOKABLE void disconnectFromRadio();

    Q_INVOKABLE void tune(int sliceId, double freqMhz);
    Q_INVOKABLE void setMode(int sliceId, const QString& mode);

    Q_INVOKABLE void startRxAudio();
    Q_INVOKABLE void stopRxAudio();

    Q_INVOKABLE void startSpectrum();
    Q_INVOKABLE void stopSpectrum();

signals:
    void stateChanged();
    void panChanged();

private:
    using ReplyHandler = std::function<void(int code, const QString& body)>;

    void setState(const QString& state);
    void sendCommand(const QString& command, ReplyHandler onReply = {});
    void onReadyRead();
    void handleLine(const QString& line);

    QTcpSocket m_socket;
    SliceListModel m_slices;
    VitaStream m_vita;
    QHash<quint32, ReplyHandler> m_pendingReplies;
    QHash<quint16, int> m_sMeterSliceByIndex; // meter index → slice id (SLC/LEVEL)
    quint32 m_rxAudioStreamId{0};
    quint32 m_panId{0};
    double m_panCenterMhz{14.1};
    double m_panBandwidthMhz{0.2};
    QString m_state{"disconnected"};
    QString m_radioLabel;
    QByteArray m_rxBuffer;
    quint32 m_seq{1};
};
