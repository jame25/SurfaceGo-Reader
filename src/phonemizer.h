#pragma once

#include <QHash>
#include <QString>
#include <vector>

// eSpeak NG based phonemizer reproducing piper-phonemize's output (IPA with
// clause punctuation) using the stock libespeak-ng API. Not thread-safe:
// use from a single worker thread.
class Phonemizer {
public:
    Phonemizer();
    ~Phonemizer();

    bool setVoice(const QString &espeakVoice);
    QString voice() const { return m_voice; }

    // Full text -> IPA string, e.g. "Hello, world." -> "həlˈoʊ, wˈɜːld."
    QString phonemize(const QString &text);

    // Relative spoken duration weight of a single word (cached).
    double wordWeight(const QString &word);

private:
    QString m_voice;
    bool m_ok = false;
    QHash<QString, double> m_weightCache;
};

// Maps an IPA phoneme string to Piper phoneme ids using the voice's phoneme_id_map.
std::vector<int64_t> phonemesToIds(const QString &phonemes, const QHash<char32_t, std::vector<int64_t>> &idMap);
