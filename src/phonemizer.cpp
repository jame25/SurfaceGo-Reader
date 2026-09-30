#include "phonemizer.h"

#include <QRegularExpression>
#include <espeak-ng/speak_lib.h>

namespace {

bool g_espeakInitialized = false;

bool ensureEspeak()
{
    if (g_espeakInitialized)
        return true;
    // Synchronous mode, no audio output: we only use the phonemizer.
    const QByteArray dataPath = qgetenv("SGREADER_ESPEAK_DATA");
    const int rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0,
                                       dataPath.isEmpty() ? nullptr : dataPath.constData(), 0);
    g_espeakInitialized = rate > 0;
    return g_espeakInitialized;
}

bool isClosing(QChar c)
{
    switch (c.unicode()) {
    case '"': case '\'': case ')': case ']': case '}':
    case 0x201D: case 0x2019: case 0x00BB: case 0x203A: case 0x201C: case 0x2018: case 0x00AB:
        return true;
    default:
        return c.isSpace();
    }
}

// eSpeak consumes one clause per call and reads one character past the clause
// boundary. Returns the clause terminator punctuation ('.', ',', '?', ...) or null.
QChar clauseTerminator(QString consumed, bool hasLookahead)
{
    if (hasLookahead && !consumed.isEmpty()) {
        consumed.chop(1);
        while (!consumed.isEmpty() && consumed.back().isSpace())
            consumed.chop(1);
    }
    for (int i = int(consumed.size()) - 1; i >= 0; --i) {
        const QChar c = consumed.at(i);
        if (isClosing(c))
            continue;
        switch (c.unicode()) {
        case '.': case '!': case '?': case ',': case ';': case ':':
            return c;
        case 0x2026:
            return QLatin1Char('.');
        default:
            return {};
        }
    }
    return {};
}

bool isVowel(char32_t c)
{
    static const std::u32string vowels = U"aeiouyæɑɒɔəɚɛɜɝɪʊʌɐɨʉøœɯɤɵɘɞɶʏ";
    return vowels.find(c) != std::u32string::npos;
}

} // namespace

Phonemizer::Phonemizer()
{
    m_ok = ensureEspeak();
}

Phonemizer::~Phonemizer() = default;

bool Phonemizer::setVoice(const QString &espeakVoice)
{
    if (!m_ok)
        return false;
    if (espeakVoice == m_voice)
        return true;
    const QByteArray name = espeakVoice.toUtf8();
    if (espeak_SetVoiceByName(name.constData()) != EE_OK)
        return false;
    m_voice = espeakVoice;
    m_weightCache.clear();
    return true;
}

QString Phonemizer::phonemize(const QString &input)
{
    if (!m_ok)
        return {};
    static const QRegularExpression langSwitch(QStringLiteral("\\([a-z]{2,3}(-[a-z0-9]+)*\\)"));

    QString text = input;
    // Dashes used as parentheticals should produce a short pause, like a comma.
    text.replace(QStringLiteral(" — "), QStringLiteral(", "));
    text.replace(QChar(0x2014), QStringLiteral(", "));
    text.replace(QStringLiteral(" – "), QStringLiteral(", "));

    const QByteArray utf8 = text.toUtf8();
    const char *base = utf8.constData();
    const char *endAll = base + utf8.size();
    const void *ptr = base;
    QString out;
    int guard = 0;
    while (ptr && guard++ < 10000) {
        const char *start = static_cast<const char *>(ptr);
        const char *ph = espeak_TextToPhonemes(&ptr, espeakCHARS_UTF8, espeakPHONEMES_IPA);
        const char *end = ptr ? static_cast<const char *>(ptr) : endAll;
        if (end < start)
            end = start;
        QString clause = QString::fromUtf8(ph ? ph : "").trimmed();
        clause.remove(langSwitch);
        const QChar punct = clauseTerminator(QString::fromUtf8(start, int(end - start)), ptr != nullptr);
        if (clause.isEmpty())
            continue;
        if (!out.isEmpty() && !out.endsWith(QLatin1Char(' ')))
            out += QLatin1Char(' ');
        out += clause;
        if (!punct.isNull())
            out += punct;
    }
    return out.trimmed();
}

double Phonemizer::wordWeight(const QString &word)
{
    QString key;
    key.reserve(word.size());
    for (QChar c : word)
        if (c.isLetterOrNumber() || c == '\'' || c.unicode() == 0x2019 || c == '.' || c == '%' || c == '$')
            key.append(c.toLower());
    while (!key.isEmpty() && !key.back().isLetterOrNumber() && key.back() != '%')
        key.chop(1);
    if (key.isEmpty() || !m_ok)
        return 0.0;
    auto it = m_weightCache.constFind(key);
    if (it != m_weightCache.constEnd())
        return it.value();

    const QString ph = phonemize(key).normalized(QString::NormalizationForm_D);
    double w = 0;
    for (char32_t c : ph.toUcs4()) {
        if (c == U'ˈ' || c == U'ˌ' || QChar::category(c) == QChar::Mark_NonSpacing)
            continue;
        if (c == U'ː')
            w += 0.5;
        else if (c == U' ')
            w += 0.3;
        else if (c == U'.' || c == U',' || c == U'!' || c == U'?' || c == U';' || c == U':')
            continue;
        else if (isVowel(c))
            w += 1.35;
        else
            w += 1.0;
    }
    w = qMax(w, 0.6);
    if (m_weightCache.size() > 50000)
        m_weightCache.clear();
    m_weightCache.insert(key, w);
    return w;
}

std::vector<int64_t> phonemesToIds(const QString &phonemes, const QHash<char32_t, std::vector<int64_t>> &idMap)
{
    std::vector<int64_t> ids;
    const auto pad = idMap.value(U'_', {0});
    const auto bos = idMap.value(U'^', {1});
    const auto eos = idMap.value(U'$', {2});
    const QList<uint> cps = phonemes.normalized(QString::NormalizationForm_D).toUcs4();
    ids.reserve(cps.size() * 2 + 4);
    ids.insert(ids.end(), bos.begin(), bos.end());
    ids.insert(ids.end(), pad.begin(), pad.end());
    for (uint cp : cps) {
        auto it = idMap.constFind(char32_t(cp));
        if (it == idMap.constEnd())
            continue; // unknown phoneme: skipped, like piper does
        ids.insert(ids.end(), it->begin(), it->end());
        ids.insert(ids.end(), pad.begin(), pad.end());
    }
    ids.insert(ids.end(), eos.begin(), eos.end());
    return ids;
}
