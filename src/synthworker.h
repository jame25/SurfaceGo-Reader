#pragma once

#include "segment.h"

#include <QObject>
#include <QStringList>
#include <atomic>
#include <memory>

class Phonemizer;
class PiperVoice;

struct SynthJob {
    quint64 generation = 0;
    int sentence = -1;
    QString text;
    QStringList words;      // word tokens of the sentence, in order
    bool paragraphEnd = false;
    bool heading = false;
    float lengthScale = 1.0f;
    int speaker = 0;
};

// Lives in its own QThread. Runs phonemization + Piper inference and emits
// ready-to-play segments. Jobs belonging to an old playback "generation"
// (after a seek/stop/voice change) are skipped without being synthesized.
class SynthWorker : public QObject {
    Q_OBJECT
public:
    explicit SynthWorker(std::shared_ptr<std::atomic<quint64>> generation, QObject *parent = nullptr);
    ~SynthWorker() override;

    void loadVoice(const QString &onnxPath);
    void synthesize(const SynthJob &job);

Q_SIGNALS:
    void voiceLoaded(const QString &path, bool ok, const QString &error, int sampleRate, const QStringList &speakers);
    void segmentReady(SegmentPtr segment);
    void synthFailed(int sentence, quint64 generation, const QString &error);

private:
    std::shared_ptr<std::atomic<quint64>> m_generation;
    std::unique_ptr<Phonemizer> m_phonemizer;
    std::unique_ptr<PiperVoice> m_voice;
};
