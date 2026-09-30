#include "synthworker.h"
#include "phonemizer.h"
#include "pipervoice.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

namespace {

bool hasSpeakableText(const QString &s)
{
    for (QChar c : s)
        if (c.isLetterOrNumber())
            return true;
    return false;
}

// Pause (in phoneme-weight units) the voice makes after this token.
double pauseUnitsAfter(const QString &token)
{
    int i = int(token.size()) - 1;
    while (i >= 0) {
        const char16_t c = token.at(i).unicode();
        if (c == '"' || c == '\'' || c == ')' || c == ']' || c == 0x201D || c == 0x2019 || c == 0xBB)
            --i;
        else
            break;
    }
    if (i < 0)
        return 0;
    switch (token.at(i).unicode()) {
    case ',':
        return 2.0;
    case ';': case ':': case 0x2014: case 0x2013:
        return 3.0;
    case '.': case '!': case '?': case 0x2026:
        return 4.0;
    default:
        return 0;
    }
}

} // namespace

SynthWorker::SynthWorker(std::shared_ptr<std::atomic<quint64>> generation, QObject *parent)
    : QObject(parent)
    , m_generation(std::move(generation))
{
}

SynthWorker::~SynthWorker() = default;

void SynthWorker::loadVoice(const QString &onnxPath)
{
    if (!m_phonemizer)
        m_phonemizer = std::make_unique<Phonemizer>();
    auto voice = std::make_unique<PiperVoice>();
    QString error;
    QElapsedTimer t;
    t.start();
    if (!voice->load(onnxPath, &error)) {
        Q_EMIT voiceLoaded(onnxPath, false, error, 0, {});
        return;
    }
    if (voice->phonemeType() == QLatin1String("espeak") && !m_phonemizer->setVoice(voice->espeakVoice())) {
        Q_EMIT voiceLoaded(onnxPath, false,
                           QStringLiteral("eSpeak NG has no voice '%1' (is espeak-ng installed?)").arg(voice->espeakVoice()),
                           0, {});
        return;
    }
    qInfo("Loaded voice %s in %lld ms (%d threads)", qPrintable(onnxPath), t.elapsed(), PiperVoice::threadCount());
    m_voice = std::move(voice);
    Q_EMIT voiceLoaded(onnxPath, true, {}, m_voice->sampleRate(), m_voice->speakerNames());
}

void SynthWorker::synthesize(const SynthJob &job)
{
    if (job.generation != m_generation->load())
        return; // stale request (user seeked / stopped)
    if (!m_voice || !m_voice->isLoaded()) {
        Q_EMIT synthFailed(job.sentence, job.generation, QStringLiteral("No voice model loaded"));
        return;
    }

    auto seg = std::make_shared<AudioSegment>();
    seg->sentence = job.sentence;
    seg->generation = job.generation;
    seg->sampleRate = m_voice->sampleRate();
    seg->words.resize(size_t(job.words.size()));

    const double ls = job.lengthScale > 0 ? job.lengthScale : 1.0;
    double pauseSec = 0.2 * ls;
    if (job.heading)
        pauseSec = 0.65 * ls;
    else if (job.paragraphEnd)
        pauseSec = 0.45 * ls;

    std::vector<float> audio;
    if (hasSpeakableText(job.text)) {
        QString phonemes;
        if (m_voice->phonemeType() == QLatin1String("espeak"))
            phonemes = m_phonemizer->phonemize(job.text);
        else
            phonemes = job.text; // "text" phoneme type: characters are the symbols
        const std::vector<int64_t> ids = phonemesToIds(phonemes, m_voice->phonemeIdMap());
        if (job.generation != m_generation->load())
            return;
        QString error;
        if (ids.size() > 3) {
            audio = m_voice->synthesize(ids, job.lengthScale, job.speaker, &error);
            if (audio.empty() && !error.isEmpty()) {
                Q_EMIT synthFailed(job.sentence, job.generation, error);
                return;
            }
        }
    }
    if (job.generation != m_generation->load())
        return;

    if (!audio.empty()) {
        std::vector<double> weights, pauses;
        weights.reserve(size_t(job.words.size()));
        pauses.reserve(size_t(job.words.size()));
        for (const QString &w : job.words) {
            weights.push_back(m_phonemizer ? m_phonemizer->wordWeight(w) : double(w.size()));
            pauses.push_back(pauseUnitsAfter(w));
        }
        seg->words = estimateWordTimes(audio.data(), audio.size(), seg->sampleRate, weights, pauses);

        // Peak-normalise like Piper does.
        float peak = 0.01f;
        for (float s : audio)
            peak = std::max(peak, std::abs(s));
        const float scale = 32767.f * 0.92f / peak;
        seg->samples.reserve(audio.size() + size_t(pauseSec * seg->sampleRate));
        for (float s : audio)
            seg->samples.push_back(int16_t(std::clamp(s * scale, -32767.f, 32767.f)));
    }
    seg->samples.resize(seg->samples.size() + size_t(pauseSec * seg->sampleRate), 0);
    Q_EMIT segmentReady(seg);
}
