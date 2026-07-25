#pragma once

// VITA-49 UDP ingest for the spike (one socket, matching the desktop's
// single PanadapterStream socket): remote RX audio + panadapter FFT.
// Wire facts mirror src/core/PanadapterStream.{h,cpp}:
//   - ExtDataWithStream packets, 28-byte header, stream id in word 1
//   - PCC 0x03E3: float32 stereo big-endian audio, 24 kHz
//   - PCC 0x8003: FFT — 12-byte subheader (startBin u16, numBins u16,
//     binSize u16, totalBins u16, frameIndex u32), bins u16 big-endian,
//     bin value = y-pixel from top at the pan's ypixels scale
//   - LAN prime: 1-byte 0x00 datagram to radio:4992 from this socket
//     until the first packet arrives (routedPrimeTimer pattern)
// Audio is uncompressed only (compression=none) — Opus out of scope.

#include <QAudioSink>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <QVector>

class VitaStream : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool audioActive READ audioActive NOTIFY statsChanged)
    Q_PROPERTY(int packetsReceived READ packetsReceived NOTIFY statsChanged)
    Q_PROPERTY(qint64 bytesPlayed READ bytesPlayed NOTIFY statsChanged)

public:
    // Fixed local port so the emulator harness can redir into it.
    static constexpr quint16 kLocalPort = 14993;

    explicit VitaStream(QObject* parent = nullptr);

    bool audioActive() const { return m_audioSink != nullptr; }
    int packetsReceived() const { return m_packetsReceived; }
    qint64 bytesPlayed() const { return m_bytesPlayed; }

    void openSocket(const QHostAddress& radioAddress);
    void closeSocket();

    void setAudioStream(quint32 streamId);
    void clearAudioStream();

    void setFftStream(quint32 streamId);
    void clearFftStream();

signals:
    void statsChanged();
    // One complete FFT frame; values are y-pixels from the top at the
    // pan's ypixels scale (smaller = stronger signal).
    void fftFrame(QVector<int> bins);

private:
    void onReadyRead();
    void sendPrime();
    void handleAudio(const uchar* raw, int size, bool hasTrailer);
    void handleFft(const uchar* raw, int size, bool hasTrailer);

    QUdpSocket m_socket;
    QTimer m_primeTimer;
    QTimer m_statsTimer;
    QAudioSink* m_audioSink{nullptr};
    QIODevice* m_sinkIo{nullptr};
    QHostAddress m_radioAddress;
    quint32 m_audioStreamId{0};
    quint32 m_fftStreamId{0};
    int m_packetsReceived{0};
    qint64 m_bytesPlayed{0};

    // FFT frame assembly (single stream)
    QVector<quint16> m_frameBuf;
    quint32 m_frameIndex{0};
    int m_frameBinsReceived{0};
};
