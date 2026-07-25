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

struct OpusDecoder;

class VitaStream : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool audioActive READ audioActive NOTIFY statsChanged)
    Q_PROPERTY(int packetsReceived READ packetsReceived NOTIFY statsChanged)
    Q_PROPERTY(qint64 bytesPlayed READ bytesPlayed NOTIFY statsChanged)
    Q_PROPERTY(bool opusCapable READ opusCapable CONSTANT)

public:
    // Fixed local port so the emulator harness can redir into it.
    static constexpr quint16 kLocalPort = 14993;

    explicit VitaStream(QObject* parent = nullptr);

    bool audioActive() const { return m_audioSink != nullptr; }
    int packetsReceived() const { return m_packetsReceived; }
    qint64 bytesPlayed() const { return m_bytesPlayed; }
    static bool opusCapable();

    void openSocket(const QHostAddress& radioAddress);
    void closeSocket();

    void setAudioStream(quint32 streamId);
    void clearAudioStream();

    void setFftStream(quint32 streamId);
    void clearFftStream();

    void setWaterfallStream(quint32 streamId);
    void clearWaterfallStream();

signals:
    void statsChanged();
    // One complete FFT frame; values are y-pixels from the top at the
    // pan's ypixels scale (smaller = stronger signal).
    void fftFrame(QVector<int> bins);
    // Meter data packet (PCC 0x8002): parallel id/raw-value arrays.
    // Value semantics depend on the meter's unit (dBm = raw/128).
    void meterData(QVector<quint16> ids, QVector<qint16> values);
    // One assembled waterfall row (PCC 0x8004): bin values in dBm.
    void waterfallRow(QVector<float> dbmBins);

private:
    void onReadyRead();
    void sendPrime();
    void handleAudio(const uchar* raw, int size, bool hasTrailer);
    void handleOpusAudio(const uchar* raw, int size, bool hasTrailer);
    void handleFft(const uchar* raw, int size, bool hasTrailer);
    void handleWaterfall(const uchar* raw, int size, bool hasTrailer);
    void handleMeter(const uchar* raw, int size, bool hasTrailer);
    void writePcmFloats(const float* samples, int count);

    QUdpSocket m_socket;
    QTimer m_primeTimer;
    QTimer m_statsTimer;
    QAudioSink* m_audioSink{nullptr};
    QIODevice* m_sinkIo{nullptr};
    OpusDecoder* m_opusDecoder{nullptr}; // lazy on first 0x8005 packet
    QHostAddress m_radioAddress;
    quint32 m_audioStreamId{0};
    quint32 m_fftStreamId{0};
    int m_packetsReceived{0};
    qint64 m_bytesPlayed{0};

    // FFT frame assembly (single stream)
    QVector<quint16> m_frameBuf;
    quint32 m_frameIndex{0};
    int m_frameBinsReceived{0};

    // Waterfall frame assembly (single stream, timecode-keyed)
    quint32 m_wfStreamId{0};
    QVector<float> m_wfBuf;
    quint32 m_wfTimecode{0};
    int m_wfBinsReceived{0};
};
