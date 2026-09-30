#include "reader.h"
#include "appsettings.h"
#include "audiooutput.h"
#include "paragraphmodel.h"
#include "synthworker.h"
#include "voicemodel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QTextLayout>
#include <QtConcurrent>

namespace {

// X position right after `word` (i.e. where the following space starts) when
// laid out with `font`, using design metrics like Qt Quick text items do.
qreal advanceOf(const QString &word, const QFont &font)
{
    QTextLayout layout(word + QLatin1Char(' '), font);
    QTextOption opt;
    opt.setUseDesignMetrics(true);
    opt.setWrapMode(QTextOption::NoWrap);
    layout.setTextOption(opt);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    layout.endLayout();
    return line.isValid() ? line.cursorToX(int(word.size())) : 0.0;
}

// Letter spacing (px) that makes `word` in bold exactly as wide as in `font`.
qreal boldCompensation(const QString &word, const QFont &font)
{
    int clusters = 0;
    for (int i = 0; i < word.size(); ++i)
        if (!word.at(i).isMark() && !word.at(i).isLowSurrogate())
            ++clusters;
    if (clusters == 0)
        return 0.0;
    QFont bold = font;
    bold.setBold(true);
    const qreal target = advanceOf(word, font);
    qreal spacing = (target - advanceOf(word, bold)) / clusters;
    // One refinement pass: kerning/rounding make the first estimate slightly off.
    bold.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    spacing += (target - advanceOf(word, bold)) / clusters;
    return spacing;
}

constexpr int kLookaheadSentences = 4; // synthesized ahead of the audible sentence
constexpr int kMaxInFlight = 2;        // jobs queued in the synth thread at once

struct LoadResult {
    std::shared_ptr<Book> book;
    QString error;
};
}

Reader::Reader(AppSettings *settings, VoiceModel *voices, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_voices(voices)
    , m_paragraphModel(new ParagraphModel(this))
    , m_generation(std::make_shared<std::atomic<quint64>>(0))
    , m_audio(std::make_unique<AudioOutput>())
{
    qRegisterMetaType<SegmentPtr>();
    m_audio->setLatencyOffsetMs(m_settings->latencyOffsetMs());

    m_worker = new SynthWorker(m_generation);
    m_worker->moveToThread(&m_synthThread);
    connect(&m_synthThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SynthWorker::segmentReady, this, &Reader::onSegmentReady);
    connect(m_worker, &SynthWorker::voiceLoaded, this, &Reader::onVoiceLoaded);
    connect(m_worker, &SynthWorker::synthFailed, this, [this](int, quint64 gen, const QString &error) {
        if (gen != m_generation->load())
            return;
        stop();
        Q_EMIT errorOccurred(tr("Speech synthesis failed: %1").arg(error));
    });
    m_synthThread.setObjectName(QStringLiteral("piper-synth"));
    m_synthThread.start();

    m_tick.setInterval(30);
    m_tick.setTimerType(Qt::PreciseTimer);
    connect(&m_tick, &QTimer::timeout, this, &Reader::tick);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(3000);
    connect(&m_saveTimer, &QTimer::timeout, this, &Reader::saveState);

    connect(m_settings, &AppSettings::voiceChanged, this, [this] {
        const bool wasPlaying = m_state == State::Playing;
        const int s = m_sentence;
        if (m_state != State::Stopped)
            stop();
        ensureVoice();
        if (wasPlaying)
            startFrom(s);
    });
    connect(m_settings, &AppSettings::voicesFolderChanged, this, [this] {
        if (!m_voiceReady)
            ensureVoice();
    });
    connect(m_voices, &VoiceModel::voicesChanged, this, [this] {
        if (!m_voiceReady && !m_voiceLoading)
            ensureVoice();
    });
    connect(m_settings, &AppSettings::lengthScaleChanged, this, &Reader::restartIfActive);
    connect(m_settings, &AppSettings::speakerChanged, this, &Reader::restartIfActive);
    connect(m_settings, &AppSettings::latencyOffsetMsChanged, this,
            [this] { m_audio->setLatencyOffsetMs(m_settings->latencyOffsetMs()); });
    connect(this, &Reader::styleChanged, this, [this] {
        rebuildHtml();
        Q_EMIT highlightChanged();
    });
    connect(m_settings, &AppSettings::wordHighlightChanged, this, &Reader::styleChanged);

    ensureVoice(); // load in the background so the first Play is instant
}

Reader::~Reader()
{
    saveState();
    ++(*m_generation);
    m_audio.reset();
    m_synthThread.quit();
    m_synthThread.wait();
}

QString Reader::title() const { return m_book ? m_book->title : QString(); }
QString Reader::author() const { return m_book ? m_book->author : QString(); }
QString Reader::bookPath() const { return m_book ? m_book->path : QString(); }
QObject *Reader::paragraphs() const { return m_paragraphModel; }

QVariantList Reader::chapters() const
{
    QVariantList list;
    if (!m_book)
        return list;
    for (const Chapter &c : m_book->chapters)
        list.append(QVariantMap{{QStringLiteral("title"), c.title},
                                {QStringLiteral("paragraph"), c.paragraph},
                                {QStringLiteral("level"), c.level}});
    return list;
}

int Reader::currentParagraph() const
{
    if (!m_book || m_sentence < 0 || m_sentence >= m_book->sentenceCount())
        return -1;
    return m_book->sentences.at(m_sentence).paragraph;
}

int Reader::sentenceStart() const
{
    if (!m_book || m_sentence < 0 || m_sentence >= m_book->sentenceCount())
        return 0;
    return m_book->sentences.at(m_sentence).start;
}

int Reader::sentenceLength() const
{
    if (!m_book || m_sentence < 0 || m_sentence >= m_book->sentenceCount())
        return 0;
    return m_book->sentences.at(m_sentence).length;
}

int Reader::wordStart() const
{
    if (!m_book || m_word < 0)
        return sentenceStart();
    const QList<TextSpan> words = m_book->sentenceWords(m_sentence);
    return m_word < words.size() ? words.at(m_word).start : sentenceStart();
}

int Reader::currentChapter() const
{
    const int p = currentParagraph();
    return m_book && p >= 0 ? m_book->chapterForParagraph(p) : -1;
}

double Reader::progress() const
{
    const int n = sentenceCount();
    return n > 1 && m_sentence > 0 ? double(m_sentence) / (n - 1) : 0.0;
}

int Reader::chapterParagraph(int chapter) const
{
    if (!m_book || chapter < 0 || chapter >= m_book->chapters.size())
        return 0;
    return m_book->chapters.at(chapter).paragraph;
}

// ---------------------------------------------------------------- book ----

void Reader::openBook(const QString &path)
{
    if (m_book && m_book->path == path)
        return;
    closeBook();

    const int token = ++m_loadToken;
    m_loading = true;
    m_loadProgress = 0;
    Q_EMIT loadingChanged();
    Q_EMIT loadProgressChanged();

    QPointer<Reader> self(this);
    auto progress = [self, token](int p) {
        QMetaObject::invokeMethod(qApp, [self, token, p] {
            if (self && self->m_loadToken == token && p != self->m_loadProgress) {
                self->m_loadProgress = p;
                Q_EMIT self->loadProgressChanged();
            }
        }, Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<LoadResult>(this);
    connect(watcher, &QFutureWatcher<LoadResult>::finished, this, [this, watcher, token] {
        watcher->deleteLater();
        if (token != m_loadToken)
            return;
        const LoadResult r = watcher->result();
        m_loading = false;
        Q_EMIT loadingChanged();
        if (!r.book) {
            Q_EMIT errorOccurred(r.error);
            return;
        }
        m_book = r.book;
        const QVariantMap st = m_settings->bookState(bookKey(m_book->path));
        const int count = m_book->sentenceCount();
        m_sentence = count > 0 ? qBound(0, st.value(QStringLiteral("sentence"), 0).toInt(), count - 1) : -1;
        m_word = -1;
        const int para = currentParagraph();
        m_initialViewParagraph = st.contains(QStringLiteral("viewParagraph"))
            ? qBound(0, st.value(QStringLiteral("viewParagraph")).toInt(), qMax(0, int(m_book->paragraphs.size()) - 1))
            : qMax(0, para);
        m_viewParagraph = m_initialViewParagraph;
        m_paragraphModel->setBook(m_book);
        rebuildHtml();
        setState(State::Stopped);
        Q_EMIT bookChanged();
        Q_EMIT highlightChanged();
        saveState();
    });
    watcher->setFuture(QtConcurrent::run([path, progress] {
        LoadResult r;
        r.book = loadBook(path, &r.error, progress);
        return r;
    }));
}

void Reader::closeBook()
{
    ++m_loadToken; // cancel a pending load
    if (m_loading) {
        m_loading = false;
        Q_EMIT loadingChanged();
    }
    if (!m_book)
        return;
    stop();
    saveState();
    const QString path = m_book->path;
    m_book.reset();
    m_paragraphModel->setBook(nullptr);
    m_sentence = -1;
    m_word = -1;
    m_html.clear();
    Q_EMIT bookChanged();
    Q_EMIT highlightChanged();
    Q_EMIT bookClosed(path);
}

void Reader::setViewParagraph(int paragraph)
{
    if (m_viewParagraph == paragraph)
        return;
    m_viewParagraph = paragraph;
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void Reader::saveState()
{
    if (!m_book)
        return;
    m_settings->setBookState(bookKey(m_book->path), {
        {QStringLiteral("sentence"), qMax(0, m_sentence)},
        {QStringLiteral("total"), m_book->sentenceCount()},
        {QStringLiteral("viewParagraph"), m_viewParagraph},
        {QStringLiteral("lastOpened"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("title"), m_book->title},
        {QStringLiteral("author"), m_book->author},
    });
}

// --------------------------------------------------------------- voice ----

void Reader::ensureVoice()
{
    QString path = m_settings->voice();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        path = m_voices->firstVoicePath();
        if (!path.isEmpty()) {
            m_requestedVoice.clear();
            m_settings->setVoice(path); // re-enters via voiceChanged
            return;
        }
    }
    if (path.isEmpty()) {
        m_voiceReady = false;
        m_voiceLoading = false;
        m_requestedVoice.clear();
        m_voiceError = tr("No Piper voice found. Copy a voice (.onnx + .onnx.json) into %1").arg(m_settings->voicesFolder());
        Q_EMIT voiceStatusChanged();
        return;
    }
    if (path == m_requestedVoice)
        return; // already loaded or loading
    m_requestedVoice = path;
    m_voiceReady = false;
    m_voiceLoading = true;
    m_voiceError.clear();
    Q_EMIT voiceStatusChanged();
    QMetaObject::invokeMethod(m_worker, [w = m_worker, path] { w->loadVoice(path); }, Qt::QueuedConnection);
}

void Reader::reloadVoice()
{
    m_requestedVoice.clear();
    m_voices->refresh(); // may already trigger ensureVoice() via voicesChanged
    ensureVoice();
}

void Reader::onVoiceLoaded(const QString &path, bool ok, const QString &error, int, const QStringList &speakers)
{
    if (path != m_requestedVoice)
        return;
    m_voiceLoading = false;
    m_voiceReady = ok;
    m_voiceError = error;
    m_speakers = ok ? speakers : QStringList();
    if (ok)
        m_loadedVoice = path;
    else
        m_requestedVoice.clear();
    Q_EMIT voiceStatusChanged();

    if (ok && m_pendingStart >= 0) {
        const int s = m_pendingStart;
        m_pendingStart = -1;
        startFrom(s);
    } else if (!ok) {
        m_pendingStart = -1;
        if (m_state != State::Stopped)
            stop();
        Q_EMIT errorOccurred(error);
    }
}

// ------------------------------------------------------------ playback ----

void Reader::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    Q_EMIT playbackChanged();
}

void Reader::setBuffering(bool b)
{
    if (m_buffering == b)
        return;
    m_buffering = b;
    Q_EMIT playbackChanged();
}

void Reader::startFrom(int sentence)
{
    if (!m_book || m_book->sentenceCount() == 0)
        return;
    sentence = qBound(0, sentence, m_book->sentenceCount() - 1);

    ++(*m_generation);
    m_audio->clear();
    m_inFlight = 0;
    m_nextToRequest = sentence;
    setCurrent(sentence, -1);

    if (!m_voiceReady) {
        m_pendingStart = sentence;
        ensureVoice();
        if (!m_voiceLoading) {
            m_pendingStart = -1;
            setState(State::Stopped);
            Q_EMIT errorOccurred(m_voiceError.isEmpty() ? tr("No voice available") : m_voiceError);
            return;
        }
        setState(State::Playing);
        setBuffering(true);
        return;
    }

    m_pendingStart = -1;
    setState(State::Playing);
    setBuffering(true);
    m_audio->setActive(true);
    fillPipeline();
    m_tick.start();
}

void Reader::fillPipeline()
{
    if (!m_book || m_state != State::Playing || m_pendingStart >= 0)
        return;
    const int count = m_book->sentenceCount();
    const int base = qMax(0, m_sentence);
    while (m_inFlight < kMaxInFlight && m_nextToRequest < count && m_nextToRequest <= base + kLookaheadSentences) {
        const int s = m_nextToRequest++;
        const SentenceRef &ref = m_book->sentences.at(s);
        const QString &ptext = m_book->paragraphs.at(ref.paragraph).text;
        SynthJob job;
        job.generation = m_generation->load();
        job.sentence = s;
        job.text = ptext.mid(ref.start, ref.length);
        for (const TextSpan &w : m_book->sentenceWords(s))
            job.words.append(ptext.mid(w.start, w.length));
        job.paragraphEnd = (s + 1 == m_book->paraFirstSentence.at(ref.paragraph + 1));
        job.heading = m_book->paragraphs.at(ref.paragraph).heading;
        job.lengthScale = float(m_settings->lengthScale());
        job.speaker = m_settings->speaker();
        ++m_inFlight;
        QMetaObject::invokeMethod(m_worker, [w = m_worker, job] { w->synthesize(job); }, Qt::QueuedConnection);
    }
}

void Reader::onSegmentReady(SegmentPtr seg)
{
    if (!seg || seg->generation != m_generation->load())
        return;
    m_inFlight = qMax(0, m_inFlight - 1);
    m_audio->enqueue(seg);
    fillPipeline();
}

void Reader::tick()
{
    if (m_state != State::Playing || !m_book)
        return;
    const AudioOutput::Position pos = m_audio->position();
    if (pos.segment && pos.segment->generation == m_generation->load()) {
        setBuffering(false);
        const int w = pos.segment->wordAt(pos.offset);
        if (pos.segment->sentence != m_sentence || w != m_word)
            setCurrent(pos.segment->sentence, w);
    } else if (m_inFlight > 0 && m_audio->queuedSegments() == 0) {
        setBuffering(true);
    }
    fillPipeline();

    if (m_nextToRequest >= m_book->sentenceCount() && m_inFlight == 0 && m_audio->finished()) {
        stop(); // end of book
        return;
    }
    const QString err = m_audio->lastError();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!err.isEmpty() && now - m_lastErrorShownMs > 10000) {
        m_lastErrorShownMs = now;
        Q_EMIT errorOccurred(err);
    }
}

void Reader::play()
{
    if (!m_book)
        return;
    if (m_state == State::Paused) {
        m_audio->setPaused(false);
        setState(State::Playing);
        fillPipeline();
        m_tick.start();
    } else if (m_state == State::Stopped) {
        startFrom(qMax(0, m_sentence));
    }
}

void Reader::pause()
{
    if (m_state != State::Playing)
        return;
    if (m_pendingStart >= 0) { // still waiting for the voice
        m_pendingStart = -1;
        setState(State::Stopped);
        setBuffering(false);
        return;
    }
    m_audio->setPaused(true);
    m_tick.stop();
    setState(State::Paused);
    setBuffering(false);
    saveState();
}

void Reader::togglePlay()
{
    if (m_state == State::Playing)
        pause();
    else
        play();
}

void Reader::stop()
{
    ++(*m_generation);
    m_pendingStart = -1;
    m_audio->clear();
    m_tick.stop();
    m_inFlight = 0;
    setState(State::Stopped);
    setBuffering(false);
    setCurrent(m_sentence, -1);
    saveState();
}

void Reader::nextSentence()
{
    if (!m_book || m_sentence + 1 >= m_book->sentenceCount())
        return;
    if (m_state == State::Playing) {
        startFrom(m_sentence + 1);
    } else {
        if (m_state == State::Paused)
            stop();
        setCurrent(m_sentence + 1, -1);
    }
}

void Reader::previousSentence()
{
    if (!m_book || m_sentence <= 0)
        return;
    if (m_state == State::Playing) {
        startFrom(m_sentence - 1);
    } else {
        if (m_state == State::Paused)
            stop();
        setCurrent(m_sentence - 1, -1);
    }
}

void Reader::playFromPosition(int paragraph, int charPos)
{
    if (!m_book)
        return;
    const int s = m_book->sentenceAt(paragraph, charPos);
    if (s >= 0)
        startFrom(s);
}

void Reader::restartIfActive()
{
    if (m_state == State::Playing)
        startFrom(m_sentence);
    else if (m_state == State::Paused)
        stop(); // new settings apply on the next Play
}

// ----------------------------------------------------------- highlight ----

void Reader::setCurrent(int sentence, int word)
{
    if (sentence == m_sentence && word == m_word)
        return;
    m_sentence = sentence;
    m_word = word;
    rebuildHtml();
    Q_EMIT highlightChanged();
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void Reader::rebuildHtml()
{
    m_html.clear();
    if (!m_book || m_sentence < 0 || m_sentence >= m_book->sentenceCount())
        return;
    const SentenceRef &s = m_book->sentences.at(m_sentence);
    const QString &text = m_book->paragraphs.at(s.paragraph).text;
    const QList<TextSpan> words = m_book->sentenceWords(m_sentence);
    const bool heading = m_book->paragraphs.at(s.paragraph).heading;
    const bool inverted = m_settings->wordHighlight() == QLatin1String("inverted");

    QString html;
    html.reserve(text.size() + 128);
    html += text.left(s.start).toHtmlEscaped();
    html += QStringLiteral("<span style=\"background-color:%1;color:%2\">")
                .arg(m_sentenceColor.name(), m_sentenceTextColor.name());
    int pos = s.start;
    for (int i = 0; i < words.size(); ++i) {
        const TextSpan &w = words.at(i);
        html += text.mid(pos, w.start - pos).toHtmlEscaped();
        const QString wt = text.mid(w.start, w.length).toHtmlEscaped();
        if (i == m_word && inverted)
            // Swapped sentence colours; same font, so the width never changes.
            html += QStringLiteral("<span style=\"background-color:%1;color:%2\">")
                        .arg(m_sentenceTextColor.name(), m_sentenceColor.name())
                    + wt + QStringLiteral("</span>");
        else if (i == m_word && heading)
            html += wt; // headings are already bold
        else if (i == m_word)
            html += QStringLiteral("<b style=\"letter-spacing:%1px\">")
                        .arg(boldCompensation(text.mid(w.start, w.length), m_textFont), 0, 'f', 3)
                    + wt + QStringLiteral("</b>");
        else
            html += wt;
        pos = w.end();
    }
    html += text.mid(pos, s.start + s.length - pos).toHtmlEscaped();
    html += QStringLiteral("</span>");
    html += text.mid(s.start + s.length).toHtmlEscaped();
    m_html = html;
}
