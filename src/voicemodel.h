#pragma once

#include <QObject>
#include <QVariantList>

class AppSettings;

// Piper voices (*.onnx with a matching *.onnx.json) in the voices folder.
class VoiceModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)
    Q_PROPERTY(int count READ count NOTIFY voicesChanged)

public:
    explicit VoiceModel(AppSettings *settings, QObject *parent = nullptr);

    QVariantList voices() const { return m_voices; }
    int count() const { return int(m_voices.size()); }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE int indexOfPath(const QString &path) const;
    QString firstVoicePath() const;

Q_SIGNALS:
    void voicesChanged();

private:
    AppSettings *m_settings;
    QVariantList m_voices; // {name, path, language, quality, sampleRate}
};
