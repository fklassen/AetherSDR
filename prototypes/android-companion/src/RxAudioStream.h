#pragma once

// Remote RX audio for the spike: VITA-49 UDP in → QAudioSink out.
// Wire facts mirror src/core/PanadapterStream.{h,cpp}:
//   - ExtDataWithStream packets, 28-byte header, stream id in word 1
//   - PCC 0x03E3 (SL_VITA_IF_NARROW_CLASS): float32 stereo big-endian,
//     24 kHz (see AudioEngine's 24000→output resamplers)
//   - LAN prime: 1-byte 0x00 datagram to radio:4992 from the audio
//     socket until the first packet arrives (routedPrimeTimer pattern)
// Uncompressed only (compression=none) — Opus is out of spike scope.

#include <QAudioSink>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

class RxAudioStream : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY statsChanged)
    Q_PROPERTY(int packetsReceived READ packetsReceived NOTIFY statsChanged)
    Q_PROPERTY(qint64 bytesPlayed READ bytesPlayed NOTIFY statsChanged)

public:
    // Fixed local port so the emulator harness can redir into it.
    static constexpr quint16 kLocalPort = 14993;

    explicit RxAudioStream(QObject* parent = nullptr);

    bool active() const { return m_active; }
    int packetsReceived() const { return m_packetsReceived; }
    qint64 bytesPlayed() const { return m_bytesPlayed; }

    void start(quint32 streamId, const QHostAddress& radioAddress);
    void stop();

signals:
    void statsChanged();

private:
    void onReadyRead();
    void sendPrime();

    QUdpSocket m_socket;
    QTimer m_primeTimer;
    QTimer m_statsTimer;
    QAudioSink* m_sink{nullptr};
    QIODevice* m_sinkIo{nullptr};
    QHostAddress m_radioAddress;
    quint32 m_streamId{0};
    bool m_active{false};
    int m_packetsReceived{0};
    qint64 m_bytesPlayed{0};
};
