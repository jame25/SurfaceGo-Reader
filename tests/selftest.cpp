// Pipeline test tool:
//   sgreader-selftest split                         run sentence-splitter checks
//   sgreader-selftest book <file.epub|pdf>          parse a book, print stats
//   sgreader-selftest speak <voice.onnx> "text" [out.wav] [length_scale]
//                                                   phonemize + synthesize + word timings
#include "book.h"
#include "phonemizer.h"
#include "pipervoice.h"
#include "wordtiming.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstdio>

static int failures = 0;

static void expectSplit(const QString &text, const QStringList &expected)
{
    QStringList got;
    for (const TextSpan &s : splitSentences(text))
        got.append(text.mid(s.start, s.length));
    if (got != expected) {
        ++failures;
        std::printf("FAIL split: %s\n  got:      [%s]\n  expected: [%s]\n", qPrintable(text),
                    qPrintable(got.join(QStringLiteral("] ["))), qPrintable(expected.join(QStringLiteral("] ["))));
    } else {
        std::printf("ok   split: %s -> %lld sentence(s)\n", qPrintable(text.left(60)), qlonglong(got.size()));
    }
}

static int runSplitTests()
{
    expectSplit(QStringLiteral("Hello world. This is a test."), {"Hello world.", "This is a test."});
    expectSplit(QStringLiteral("Mr. Smith went to Washington. He said hi."), {"Mr. Smith went to Washington.", "He said hi."});
    expectSplit(QStringLiteral("“Where are you going?” she asked. “Home!”"),
                {"“Where are you going?” she asked.", "“Home!”"});
    expectSplit(QStringLiteral("J. R. R. Tolkien wrote books. Pi is 3.14 roughly."),
                {"J. R. R. Tolkien wrote books.", "Pi is 3.14 roughly."});
    expectSplit(QStringLiteral("Wait... what? No way!"), {"Wait... what?", "No way!"});
    expectSplit(QStringLiteral("See No. 5 for details. Then go."), {"See No. 5 for details.", "Then go."});
    expectSplit(QStringLiteral("He paused, e.g. for effect. Then spoke"), {"He paused, e.g. for effect.", "Then spoke"});
    expectSplit(QStringLiteral("No terminator here"), {"No terminator here"});
    expectSplit(QStringLiteral("It was I. Then you came."), {"It was I.", "Then you came."});

    const QString n = normalizeWhitespace(QStringLiteral("  a  b\n\tc­d  "));
    if (n != QStringLiteral("a b cd")) {
        ++failures;
        std::printf("FAIL normalizeWhitespace: '%s'\n", qPrintable(n));
    }
    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}

static int runBook(const QString &path, int from)
{
    QElapsedTimer t;
    t.start();
    QString err;
    auto book = loadBook(path, &err, [](int p) { std::fprintf(stderr, "\rloading %d%%", p); });
    std::fprintf(stderr, "\n");
    if (!book) {
        std::printf("ERROR: %s\n", qPrintable(err));
        return 1;
    }
    std::printf("title: %s\nauthor: %s\nformat: %s\nparagraphs: %lld\nsentences: %d\nchapters: %lld\nload: %lld ms\n",
                qPrintable(book->title), qPrintable(book->author), qPrintable(book->format),
                qlonglong(book->paragraphs.size()), book->sentenceCount(), qlonglong(book->chapters.size()), t.elapsed());
    for (int i = 0; i < qMin<qsizetype>(book->chapters.size(), 15); ++i)
        std::printf("  chapter %2d: %s%s -> para %d\n", i, qPrintable(QString(book->chapters[i].level * 2, ' ')),
                    qPrintable(book->chapters[i].title.left(60)), book->chapters[i].paragraph);
    for (int i = from; i < qMin(book->sentenceCount(), from + 25); ++i)
        std::printf("  [%d p%d] %s\n", i, book->sentences[i].paragraph, qPrintable(book->sentenceText(i).left(150)));
    return 0;
}

static void writeWav(const QString &path, const std::vector<float> &audio, int rate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    auto u32 = [&](quint32 v) { v = qToLittleEndian(v); f.write(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { v = qToLittleEndian(v); f.write(reinterpret_cast<const char *>(&v), 2); };
    const quint32 dataBytes = quint32(audio.size() * 2);
    f.write("RIFF"); u32(36 + dataBytes); f.write("WAVEfmt "); u32(16); u16(1); u16(1);
    u32(quint32(rate)); u32(quint32(rate * 2)); u16(2); u16(16); f.write("data"); u32(dataBytes);
    float peak = 0.01f;
    for (float s : audio) peak = std::max(peak, std::abs(s));
    for (float s : audio) u16(quint16(qint16(std::clamp(s * 32767.f * 0.92f / peak, -32767.f, 32767.f))));
}

static int runSpeak(const QString &model, const QString &text, const QString &out, float lengthScale)
{
    Phonemizer ph;
    PiperVoice voice;
    QString err;
    QElapsedTimer t;
    t.start();
    if (!voice.load(model, &err)) {
        std::printf("ERROR: %s\n", qPrintable(err));
        return 1;
    }
    std::printf("voice loaded in %lld ms (rate %d, espeak '%s', threads %d)\n", t.elapsed(), voice.sampleRate(),
                qPrintable(voice.espeakVoice()), PiperVoice::threadCount());
    if (!ph.setVoice(voice.espeakVoice())) {
        std::printf("ERROR: espeak voice\n");
        return 1;
    }
    const QString phon = ph.phonemize(text);
    const auto ids = phonemesToIds(phon, voice.phonemeIdMap());
    std::printf("phonemes: %s\nids: %zu\n", qPrintable(phon), ids.size());
    t.restart();
    const std::vector<float> audio = voice.synthesize(ids, lengthScale, 0, &err);
    const double secs = double(audio.size()) / voice.sampleRate();
    std::printf("synth: %.2f s audio in %lld ms (RTF %.3f)\n", secs, t.elapsed(), t.elapsed() / 1000.0 / qMax(secs, 0.001));

    const QList<TextSpan> spans = splitWords(text, 0, int(text.size()));
    std::vector<double> weights, pauses;
    QStringList words;
    for (const TextSpan &s : spans) {
        const QString w = text.mid(s.start, s.length);
        words.append(w);
        weights.push_back(ph.wordWeight(w));
        const QChar last = w.isEmpty() ? QChar() : w.back();
        pauses.push_back(last == ',' ? 2.0 : (last == ';' || last == ':') ? 3.0 : (last == '.' || last == '!' || last == '?') ? 4.0 : 0.0);
    }
    const auto times = estimateWordTimes(audio.data(), audio.size(), voice.sampleRate(), weights, pauses);
    for (size_t i = 0; i < times.size(); ++i)
        std::printf("  %6.3f - %6.3f  (w %.1f)  %s\n", times[i].start / double(voice.sampleRate()),
                    times[i].end / double(voice.sampleRate()), weights[i], qPrintable(words[qsizetype(i)]));
    if (!out.isEmpty())
        writeWav(out, audio, voice.sampleRate());
    return audio.empty() ? 1 : 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("surfacego-reader"));
    QCoreApplication::setApplicationName(QStringLiteral("surfacego-reader-selftest"));
    const QStringList a = app.arguments();
    if (a.size() >= 2 && a[1] == QLatin1String("split"))
        return runSplitTests();
    if (a.size() >= 3 && a[1] == QLatin1String("book"))
        return runBook(a[2], a.size() > 3 ? a[3].toInt() : 0);
    if (a.size() >= 4 && a[1] == QLatin1String("speak"))
        return runSpeak(a[2], a[3], a.value(4), a.size() > 5 ? a[5].toFloat() : 1.0f);
    std::printf("usage: %s split | book <file> [first-sentence] | speak <voice.onnx> <text> [out.wav] [length_scale]\n", argv[0]);
    return 2;
}
