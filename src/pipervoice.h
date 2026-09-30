#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

namespace Ort {
struct Session;
}

// A Piper voice (VITS .onnx model + .onnx.json config) executed in-process
// with ONNX Runtime.
class PiperVoice {
public:
    PiperVoice();
    ~PiperVoice();

    bool load(const QString &onnxPath, QString *error);
    bool isLoaded() const { return bool(m_session); }

    QString modelPath() const { return m_modelPath; }
    int sampleRate() const { return m_sampleRate; }
    QString espeakVoice() const { return m_espeakVoice; }
    QString phonemeType() const { return m_phonemeType; }
    int speakerCount() const { return m_numSpeakers; }
    QStringList speakerNames() const { return m_speakerNames; }
    float defaultLengthScale() const { return m_lengthScale; }
    const QHash<char32_t, std::vector<int64_t>> &phonemeIdMap() const { return m_idMap; }

    // Runs the model. Returns float samples in [-1, 1] (not normalised).
    std::vector<float> synthesize(const std::vector<int64_t> &ids, float lengthScale, int speaker, QString *error);

    static int threadCount();

private:
    QString m_modelPath;
    int m_sampleRate = 22050;
    QString m_espeakVoice = QStringLiteral("en-us");
    QString m_phonemeType = QStringLiteral("espeak");
    int m_numSpeakers = 1;
    QStringList m_speakerNames;
    float m_noiseScale = 0.667f;
    float m_lengthScale = 1.0f;
    float m_noiseW = 0.8f;
    bool m_hasSid = false;
    QHash<char32_t, std::vector<int64_t>> m_idMap;
    std::unique_ptr<Ort::Session> m_session;
};
