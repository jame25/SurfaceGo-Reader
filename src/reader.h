#pragma once

#include "book.h"
#include "segment.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>

class AppSettings;
class AudioOutput;
class ParagraphModel;
class SynthWorker;
class VoiceModel;

// Controller exposed to QML: open book, playback state machine, highlight
// state and reading-position persistence.
class Reader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool bookOpen READ bookOpen NOTIFY bookChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)
    Q_PROPERTY(QString title READ title NOTIFY bookChanged)
    Q_PROPERTY(QString author READ author NOTIFY bookChanged)
    Q_PROPERTY(QString bookPath READ bookPath NOTIFY bookChanged)
    Q_PROPERTY(QObject *paragraphs READ paragraphs CONSTANT)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY bookChanged)
    Q_PROPERTY(int sentenceCount READ sentenceCount NOTIFY bookChanged)
    Q_PROPERTY(int initialViewParagraph READ initialViewParagraph NOTIFY bookChanged)

    Q_PROPERTY(int currentSentence READ currentSentence NOTIFY highlightChanged)
    Q_PROPERTY(int currentParagraph READ currentParagraph NOTIFY highlightChanged)
    Q_PROPERTY(int currentWord READ currentWord NOTIFY highlightChanged)
    Q_PROPERTY(int sentenceStart READ sentenceStart NOTIFY highlightChanged)
    Q_PROPERTY(int sentenceLength READ sentenceLength NOTIFY highlightChanged)
    Q_PROPERTY(int wordStart READ wordStart NOTIFY highlightChanged)
    Q_PROPERTY(QString highlightHtml READ highlightHtml NOTIFY highlightChanged)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY highlightChanged)
    Q_PROPERTY(double progress READ progress NOTIFY highlightChanged)

    Q_PROPERTY(bool playing READ playing NOTIFY playbackChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playbackChanged)
    Q_PROPERTY(bool buffering READ buffering NOTIFY playbackChanged)

    Q_PROPERTY(bool voiceReady READ voiceReady NOTIFY voiceStatusChanged)
    Q_PROPERTY(bool voiceLoading READ voiceLoading NOTIFY voiceStatusChanged)
    Q_PROPERTY(QString voiceError READ voiceError NOTIFY voiceStatusChanged)
    Q_PROPERTY(QStringList speakers READ speakers NOTIFY voiceStatusChanged)

    Q_PROPERTY(QColor sentenceColor MEMBER m_sentenceColor NOTIFY styleChanged)
    Q_PROPERTY(QColor sentenceTextColor MEMBER m_sentenceTextColor NOTIFY styleChanged)
    // Font of body paragraphs in the view; used to keep the bold word's width
    // identical to its regular width so the surrounding text never reflows.
    Q_PROPERTY(QFont textFont MEMBER m_textFont NOTIFY styleChanged)

public:
    Reader(AppSettings *settings, VoiceModel *voices, QObject *parent = nullptr);
    ~Reader() override;

    bool bookOpen() const { return bool(m_book); }
    bool loading() const { return m_loading; }
    int loadProgress() const { return m_loadProgress; }
    QString title() const;
    QString author() const;
    QString bookPath() const;
    QObject *paragraphs() const;
    QVariantList chapters() const;
    int sentenceCount() const { return m_book ? m_book->sentenceCount() : 0; }
    int initialViewParagraph() const { return m_initialViewParagraph; }

    int currentSentence() const { return m_sentence; }
    int currentParagraph() const;
    int currentWord() const { return m_word; }
    int sentenceStart() const;
    int sentenceLength() const;
    int wordStart() const;
    QString highlightHtml() const { return m_html; }
    int currentChapter() const;
    double progress() const;

    bool playing() const { return m_state == State::Playing; }
    bool paused() const { return m_state == State::Paused; }
    bool buffering() const { return m_buffering; }

    bool voiceReady() const { return m_voiceReady; }
    bool voiceLoading() const { return m_voiceLoading; }
    QString voiceError() const { return m_voiceError; }
    QStringList speakers() const { return m_speakers; }

    Q_INVOKABLE void openBook(const QString &path);
    Q_INVOKABLE void closeBook();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void nextSentence();
    Q_INVOKABLE void previousSentence();
    Q_INVOKABLE void playFromPosition(int paragraph, int charPos);
    Q_INVOKABLE void setViewParagraph(int paragraph);
    Q_INVOKABLE int chapterParagraph(int chapter) const;
    Q_INVOKABLE void reloadVoice();
    Q_INVOKABLE void saveState();

Q_SIGNALS:
    void bookChanged();
    void loadingChanged();
    void loadProgressChanged();
    void highlightChanged();
    void playbackChanged();
    void voiceStatusChanged();
    void styleChanged();
    void errorOccurred(const QString &message);
    void bookClosed(const QString &path);

private:
    enum class State { Stopped, Playing, Paused };

    void startFrom(int sentence);
    void fillPipeline();
    void tick();
    void setCurrent(int sentence, int word);
    void rebuildHtml();
    void setState(State s);
    void setBuffering(bool b);
    void onSegmentReady(SegmentPtr seg);
    void onVoiceLoaded(const QString &path, bool ok, const QString &error, int sampleRate, const QStringList &speakers);
    void ensureVoice();
    void restartIfActive();

    AppSettings *m_settings;
    VoiceModel *m_voices;
    ParagraphModel *m_paragraphModel;
    std::shared_ptr<Book> m_book;
    bool m_loading = false;
    int m_loadProgress = 0;
    int m_loadToken = 0;
    int m_initialViewParagraph = 0;
    int m_viewParagraph = 0;

    // TTS pipeline
    QThread m_synthThread;
    SynthWorker *m_worker = nullptr;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    std::unique_ptr<AudioOutput> m_audio;
    QTimer m_tick;
    QTimer m_saveTimer;
    State m_state = State::Stopped;
    bool m_buffering = false;
    int m_nextToRequest = 0;
    int m_inFlight = 0;
    int m_pendingStart = -1;
    qint64 m_lastErrorShownMs = 0;

    // Voice
    QString m_loadedVoice;
    QString m_requestedVoice;
    bool m_voiceReady = false;
    bool m_voiceLoading = false;
    QString m_voiceError;
    QStringList m_speakers;

    // Highlight
    int m_sentence = -1;
    int m_word = -1;
    QString m_html;
    QColor m_sentenceColor = QColor(0xa5, 0xd6, 0xa7);
    QColor m_sentenceTextColor = QColor(0x10, 0x10, 0x10);
    QFont m_textFont;
};
