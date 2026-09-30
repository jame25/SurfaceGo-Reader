#include "pipervoice.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <array>
#include <map>

#include <onnxruntime_cxx_api.h>

namespace {
Ort::Env &ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "surfacego-reader");
    return env;
}
}

PiperVoice::PiperVoice() = default;
PiperVoice::~PiperVoice() = default;

int PiperVoice::threadCount()
{
    bool ok = false;
    const int env = qEnvironmentVariableIntValue("SGREADER_TTS_THREADS", &ok);
    if (ok && env > 0)
        return env;
    // The Surface Go 3 i3 has 2 cores / 4 threads: two inference threads keep
    // synthesis well ahead of playback while leaving room for the UI.
    return qBound(1, QThread::idealThreadCount() / 2, 4);
}

bool PiperVoice::load(const QString &onnxPath, QString *error)
{
    m_session.reset();
    m_modelPath.clear();

    QString configPath = onnxPath + QStringLiteral(".json");
    if (!QFile::exists(configPath)) {
        QString alt = onnxPath;
        alt.chop(5); // ".onnx"
        alt += QStringLiteral(".json");
        configPath = alt;
    }
    QFile cf(configPath);
    if (!cf.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Voice config not found: %1").arg(onnxPath + QStringLiteral(".json"));
        return false;
    }
    QJsonParseError perr;
    const QJsonObject cfg = QJsonDocument::fromJson(cf.readAll(), &perr).object();
    if (perr.error != QJsonParseError::NoError) {
        *error = QStringLiteral("Invalid voice config: %1").arg(perr.errorString());
        return false;
    }

    m_sampleRate = cfg.value(QLatin1String("audio")).toObject().value(QLatin1String("sample_rate")).toInt(22050);
    m_espeakVoice = cfg.value(QLatin1String("espeak")).toObject().value(QLatin1String("voice")).toString(QStringLiteral("en-us"));
    m_phonemeType = cfg.value(QLatin1String("phoneme_type")).toString(QStringLiteral("espeak"));
    const QJsonObject inf = cfg.value(QLatin1String("inference")).toObject();
    m_noiseScale = float(inf.value(QLatin1String("noise_scale")).toDouble(0.667));
    m_lengthScale = float(inf.value(QLatin1String("length_scale")).toDouble(1.0));
    m_noiseW = float(inf.value(QLatin1String("noise_w")).toDouble(0.8));
    m_numSpeakers = qMax(1, cfg.value(QLatin1String("num_speakers")).toInt(1));

    m_speakerNames.clear();
    std::map<int, QString> speakers;
    const QJsonObject sidMap = cfg.value(QLatin1String("speaker_id_map")).toObject();
    for (auto it = sidMap.begin(); it != sidMap.end(); ++it)
        speakers[it.value().toInt()] = it.key();
    for (int i = 0; i < m_numSpeakers; ++i)
        m_speakerNames.append(speakers.count(i) ? speakers[i] : QStringLiteral("Speaker %1").arg(i));

    m_idMap.clear();
    const QJsonObject map = cfg.value(QLatin1String("phoneme_id_map")).toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QList<uint> cps = it.key().toUcs4();
        if (cps.size() != 1)
            continue;
        std::vector<int64_t> ids;
        for (const QJsonValue &v : it.value().toArray())
            ids.push_back(v.toInteger());
        m_idMap.insert(char32_t(cps.first()), ids);
    }
    if (m_idMap.isEmpty()) {
        *error = QStringLiteral("Voice config has no phoneme_id_map");
        return false;
    }

    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(threadCount());
        opts.SetInterOpNumThreads(1);
        opts.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        // Inputs vary in length every call; the memory arena mostly wastes RAM.
        opts.DisableCpuMemArena();
        opts.DisableProfiling();
        const QByteArray path = QFile::encodeName(onnxPath);
        m_session = std::make_unique<Ort::Session>(ortEnv(), path.constData(), opts);

        m_hasSid = false;
        Ort::AllocatorWithDefaultOptions alloc;
        for (size_t i = 0; i < m_session->GetInputCount(); ++i) {
            if (std::string(m_session->GetInputNameAllocated(i, alloc).get()) == "sid")
                m_hasSid = true;
        }
    } catch (const Ort::Exception &e) {
        m_session.reset();
        *error = QStringLiteral("Failed to load voice model: %1").arg(QString::fromUtf8(e.what()));
        return false;
    }
    m_modelPath = onnxPath;
    return true;
}

std::vector<float> PiperVoice::synthesize(const std::vector<int64_t> &ids, float lengthScale, int speaker, QString *error)
{
    if (!m_session || ids.empty())
        return {};
    try {
        auto memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<int64_t> input = ids;
        const std::array<int64_t, 2> inputShape{1, int64_t(input.size())};
        std::array<int64_t, 1> lengths{int64_t(input.size())};
        const std::array<int64_t, 1> lengthShape{1};
        std::array<float, 3> scales{m_noiseScale, lengthScale > 0 ? lengthScale : m_lengthScale, m_noiseW};
        const std::array<int64_t, 1> scalesShape{3};
        std::array<int64_t, 1> sid{qBound(0, speaker, m_numSpeakers - 1)};
        const std::array<int64_t, 1> sidShape{1};

        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, input.data(), input.size(), inputShape.data(), inputShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, lengths.data(), lengths.size(), lengthShape.data(), lengthShape.size()));
        inputs.push_back(Ort::Value::CreateTensor<float>(memInfo, scales.data(), scales.size(), scalesShape.data(), scalesShape.size()));
        std::vector<const char *> names{"input", "input_lengths", "scales"};
        if (m_hasSid) {
            inputs.push_back(Ort::Value::CreateTensor<int64_t>(memInfo, sid.data(), sid.size(), sidShape.data(), sidShape.size()));
            names.push_back("sid");
        }
        const char *outputNames[] = {"output"};
        auto outputs = m_session->Run(Ort::RunOptions{nullptr}, names.data(), inputs.data(), inputs.size(), outputNames, 1);
        const float *audio = outputs.front().GetTensorData<float>();
        const size_t count = outputs.front().GetTensorTypeAndShapeInfo().GetElementCount();
        return std::vector<float>(audio, audio + count);
    } catch (const Ort::Exception &e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        return {};
    }
}
