#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <voicevox_core.h>

namespace {
QString coreError(const QString &operation, VoicevoxResultCode result)
{
    return operation + QStringLiteral(": ")
        + QString::fromUtf8(voicevox_error_result_to_message(result));
}

// This process owns every native object and handles one request at a time.
// Killing it cancels native inference without involving the GUI process.
class QualityWorker {
public:
    explicit QualityWorker(QString bundle) : bundle_(std::move(bundle)) {}
    ~QualityWorker() { reset(); }

    QString synthesize(const QString &text, int style, double rate, const QString &path)
    {
        if (!synthesizer_) {
            const QString error = initialize();
            if (!error.isEmpty()) {
                reset();
                return error;
            }
        }
        char *query = nullptr;
        const QByteArray utf8Text = text.toUtf8();
        auto result = voicevox_synthesizer_create_audio_query(synthesizer_, utf8Text.constData(),
                                                             style, &query);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Creating audio query"), result);
        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(QByteArray(query), &parseError);
        voicevox_json_free(query);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return QStringLiteral("VOICEVOX returned an invalid audio query");
        QJsonObject audioQuery = document.object();
        audioQuery.insert(QStringLiteral("speedScale"), rate);
        audioQuery.insert(QStringLiteral("prePhonemeLength"), 0.05);
        audioQuery.insert(QStringLiteral("postPhonemeLength"), 0.1);
        const QByteArray adjustedQuery = QJsonDocument(audioQuery).toJson(QJsonDocument::Compact);
        uintptr_t wavLength = 0;
        uint8_t *wav = nullptr;
        result = voicevox_synthesizer_synthesis(synthesizer_, adjustedQuery.constData(), style,
                                                voicevox_make_default_synthesis_options(),
                                                &wavLength, &wav);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Synthesizing speech"), result);
        QString error;
        QFile output(path);
        if (wavLength > static_cast<uintptr_t>(std::numeric_limits<qint64>::max())) {
            error = QStringLiteral("Synthesized audio is too large");
        } else if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            error = QStringLiteral("Opening audio file: ") + output.errorString();
        } else if (output.write(reinterpret_cast<const char *>(wav), static_cast<qint64>(wavLength))
                   != static_cast<qint64>(wavLength) || !output.flush()) {
            error = QStringLiteral("Writing audio file: ") + output.errorString();
            output.close();
            output.remove();
        }
        voicevox_wav_free(wav);
        return error;
    }

private:
    QString initialize()
    {
        const QByteArray runtimePath = QDir(bundle_).filePath(
            QStringLiteral("quality/lib/libvoicevox_onnxruntime.so.1.17.3")).toUtf8();
        auto runtimeOptions = voicevox_make_default_load_onnxruntime_options();
        runtimeOptions.filename = runtimePath.constData();
        const VoicevoxOnnxruntime *runtime = nullptr;
        auto result = voicevox_onnxruntime_load_once(runtimeOptions, &runtime);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Loading inference runtime"), result);
        const QByteArray dictionaryPath = QDir(bundle_).filePath(QStringLiteral("dictionary")).toUtf8();
        result = voicevox_open_jtalk_rc_new(dictionaryPath.constData(), &dictionary_);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Loading shared Japanese dictionary"), result);
        auto options = voicevox_make_default_initialize_options();
        options.acceleration_mode = VOICEVOX_ACCELERATION_MODE_CPU;
        options.cpu_num_threads = 2;
        result = voicevox_synthesizer_new(runtime, dictionary_, options, &synthesizer_);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Initializing speech synthesizer"), result);
        const QByteArray modelPath = QDir(bundle_).filePath(QStringLiteral("quality/models/0.vvm")).toUtf8();
        VoicevoxVoiceModelFile *model = nullptr;
        result = voicevox_voice_model_file_open(modelPath.constData(), &model);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Opening voice model"), result);
        result = voicevox_synthesizer_load_voice_model(synthesizer_, model,
                                                      voicevox_make_default_load_voice_model_options());
        voicevox_voice_model_file_delete(model);
        if (result != VOICEVOX_RESULT_OK)
            return coreError(QStringLiteral("Loading voice model"), result);
        return {};
    }

    void reset()
    {
        if (synthesizer_)
            voicevox_synthesizer_delete(synthesizer_);
        if (dictionary_)
            voicevox_open_jtalk_rc_delete(dictionary_);
        synthesizer_ = nullptr;
        dictionary_ = nullptr;
    }

    QString bundle_;
    OpenJtalkRc *dictionary_ = nullptr;
    VoicevoxSynthesizer *synthesizer_ = nullptr;
};

QString validate(const QJsonObject &request)
{
    const QJsonValue id = request.value(QStringLiteral("id"));
    if (!id.isDouble() || id.toDouble() != std::floor(id.toDouble())
        || std::abs(id.toDouble()) > 9007199254740991.0)
        return QStringLiteral("id must be an integer");
    const QJsonValue text = request.value(QStringLiteral("text"));
    if (!text.isString() || text.toString().trimmed().isEmpty() || text.toString().contains(QChar(0)))
        return QStringLiteral("text must be a nonempty string without NUL characters");
    const QJsonValue style = request.value(QStringLiteral("style"));
    if (!style.isDouble() || (style.toDouble() != 2 && style.toDouble() != 3 && style.toDouble() != 8))
        return QStringLiteral("style must be 2, 3, or 8");
    const QJsonValue rate = request.value(QStringLiteral("rate"));
    if (!rate.isDouble() || rate.toDouble() < 0.75 || rate.toDouble() > 1.25)
        return QStringLiteral("rate must be between 0.75 and 1.25");
    const QJsonValue path = request.value(QStringLiteral("path"));
    if (!path.isString() || path.toString().contains(QChar(0)) || !QDir::isAbsolutePath(path.toString()))
        return QStringLiteral("path must be an absolute file path without NUL characters");
    return {};
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("bundle"), QStringLiteral("Local speech bundle directory"),
                      QStringLiteral("directory")});
    parser.process(app);
    if (!parser.isSet(QStringLiteral("bundle")) || parser.value(QStringLiteral("bundle")).isEmpty()) {
        std::cerr << "--bundle is required\n";
        return 2;
    }
    QualityWorker worker(QDir(parser.value(QStringLiteral("bundle"))).absolutePath());
    std::string line;
    while (std::getline(std::cin, line)) {
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(line), &parseError);
        const QJsonObject request = document.object();
        QString error = (parseError.error != QJsonParseError::NoError || !document.isObject())
            ? QStringLiteral("Request must be a JSON object") : validate(request);
        if (error.isEmpty())
            error = worker.synthesize(request.value(QStringLiteral("text")).toString(),
                                      request.value(QStringLiteral("style")).toInt(),
                                      request.value(QStringLiteral("rate")).toDouble(),
                                      request.value(QStringLiteral("path")).toString());
        QJsonObject response{{QStringLiteral("id"), request.value(QStringLiteral("id")).isUndefined()
                                  ? QJsonValue(QJsonValue::Null) : request.value(QStringLiteral("id"))},
                             {QStringLiteral("ok"), error.isEmpty()}};
        if (!error.isEmpty())
            response.insert(QStringLiteral("error"), error);
        const QByteArray encoded = QJsonDocument(response).toJson(QJsonDocument::Compact);
        std::cout.write(encoded.constData(), encoded.size());
        std::cout << std::endl;
    }
    return 0;
}
