#include "voicemodel.h"
#include "appsettings.h"

#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

VoiceModel::VoiceModel(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(m_settings, &AppSettings::voicesFolderChanged, this, &VoiceModel::refresh);
    refresh();
}

void VoiceModel::refresh()
{
    QVariantList list;
    QDirIterator it(m_settings->voicesFolder(), {QStringLiteral("*.onnx")}, QDir::Files | QDir::Readable,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QString json = path + QStringLiteral(".json");
        if (!QFile::exists(json)) {
            json = path.left(path.size() - 5) + QStringLiteral(".json");
            if (!QFile::exists(json))
                continue;
        }
        QFile f(json);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject cfg = QJsonDocument::fromJson(f.readAll()).object();
        QVariantMap v;
        v[QStringLiteral("path")] = path;
        v[QStringLiteral("name")] = QFileInfo(path).completeBaseName();
        v[QStringLiteral("language")] = cfg.value(QLatin1String("language")).toObject().value(QLatin1String("code")).toString(
            cfg.value(QLatin1String("espeak")).toObject().value(QLatin1String("voice")).toString());
        v[QStringLiteral("quality")] = cfg.value(QLatin1String("audio")).toObject().value(QLatin1String("quality")).toString();
        v[QStringLiteral("speakers")] = cfg.value(QLatin1String("num_speakers")).toInt(1);
        list.append(v);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().compare(b.toMap().value(QStringLiteral("name")).toString(),
                                                                           Qt::CaseInsensitive) < 0;
    });
    m_voices = list;
    Q_EMIT voicesChanged();
}

int VoiceModel::indexOfPath(const QString &path) const
{
    for (int i = 0; i < m_voices.size(); ++i)
        if (m_voices.at(i).toMap().value(QStringLiteral("path")).toString() == path)
            return i;
    return -1;
}

QString VoiceModel::firstVoicePath() const
{
    return m_voices.isEmpty() ? QString() : m_voices.first().toMap().value(QStringLiteral("path")).toString();
}
