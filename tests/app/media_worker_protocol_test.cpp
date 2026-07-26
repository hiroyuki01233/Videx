#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryFile>

#include <cstdint>
#include <cmath>
#include <cstring>
#include <iostream>

namespace {

std::uint32_t readLittleEndian32(const QByteArray& data, const qsizetype offset) {
    std::uint32_t result = 0;
    for (qsizetype index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(
                      static_cast<unsigned char>(data[offset + index]))
                  << (static_cast<unsigned int>(index) * 8U);
    }
    return result;
}

std::uint64_t readLittleEndian64(const QByteArray& data, const qsizetype offset) {
    std::uint64_t result = 0;
    for (qsizetype index = 0; index < 8; ++index) {
        result |= static_cast<std::uint64_t>(
                      static_cast<unsigned char>(data[offset + index]))
                  << (static_cast<unsigned int>(index) * 8U);
    }
    return result;
}

void appendLittleEndian16(QByteArray& data, const std::uint16_t value) {
    data.append(static_cast<char>(value & 0xFFU));
    data.append(static_cast<char>((value >> 8U) & 0xFFU));
}

void appendLittleEndian32(QByteArray& data, const std::uint32_t value) {
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        data.append(static_cast<char>((value >> shift) & 0xFFU));
    }
}

bool writeTestWave(QTemporaryFile& file) {
    constexpr std::uint32_t sampleCount = 48'000;
    constexpr std::uint32_t dataSize = sampleCount * 2U;
    QByteArray wave;
    wave.append("RIFF", 4);
    appendLittleEndian32(wave, 36U + dataSize);
    wave.append("WAVEfmt ", 8);
    appendLittleEndian32(wave, 16U);
    appendLittleEndian16(wave, 1U);
    appendLittleEndian16(wave, 1U);
    appendLittleEndian32(wave, 48'000U);
    appendLittleEndian32(wave, 96'000U);
    appendLittleEndian16(wave, 2U);
    appendLittleEndian16(wave, 16U);
    wave.append("data", 4);
    appendLittleEndian32(wave, dataSize);
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const auto sample = static_cast<std::int16_t>(
            (static_cast<std::int32_t>(index % 64U) - 32) * 800);
        appendLittleEndian16(wave, static_cast<std::uint16_t>(sample));
    }
    return file.write(wave) == wave.size() && file.flush();
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    if (argc != 3) {
        std::cerr << "expected worker and fixture paths\n";
        return 1;
    }

    QProcess worker;
    worker.start(QString::fromLocal8Bit(argv[1]),
                 {QStringLiteral("frame"), QString::fromLocal8Bit(argv[2]),
                  QStringLiteral("0")});
    if (!worker.waitForStarted(10'000) || !worker.waitForFinished(30'000)) {
        std::cerr << "frame worker timed out\n";
        return 1;
    }
    const QByteArray output = worker.readAllStandardOutput();
    if (worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0) {
        std::cerr << worker.readAllStandardError().toStdString() << '\n';
        return 1;
    }
    if (output.size() != 44 || output.first(4) != QByteArrayLiteral("VXF1") ||
        readLittleEndian32(output, 4) != 2U || readLittleEndian32(output, 8) != 2U ||
        readLittleEndian64(output, 20) != 16U) {
        std::cerr << "frame worker returned an invalid binary envelope\n";
        return 1;
    }
    const auto red = static_cast<unsigned char>(output[28]);
    const auto green = static_cast<unsigned char>(output[29]);
    const auto blue = static_cast<unsigned char>(output[30]);
    const auto alpha = static_cast<unsigned char>(output[31]);
    if (red < 240U || green > 20U || blue > 20U || alpha != 255U) {
        std::cerr << "frame worker returned invalid RGBA pixels\n";
        return 1;
    }

    QProcess timelineWorker;
    timelineWorker.start(QString::fromLocal8Bit(argv[1]),
                         {QStringLiteral("timeline-frame"), QStringLiteral("0"),
                          QStringLiteral("24"), QStringLiteral("1"), QStringLiteral("2"),
                          QStringLiteral("2"), QStringLiteral("1")});
    if (!timelineWorker.waitForStarted(10'000)) {
        std::cerr << "timeline frame worker did not start\n";
        return 1;
    }
    const QStringList fields{
        QStringLiteral("V"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"),
        QStringLiteral("1"), QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("0"),
        QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("0"),
        QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("1"), QStringLiteral("0"),
        QStringLiteral("0.5"), QStringLiteral("0.5"), QStringLiteral("0"), QStringLiteral("0"),
        QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("-"), QStringLiteral("0"),
        QStringLiteral("0.5"), QStringLiteral("0.5"), QStringLiteral("1"), QStringLiteral("1"),
        QStringLiteral("0"), QStringLiteral("0"), QStringLiteral("-"),
        QStringLiteral("0,1,0,0,1,1,0,0.5,0.5,0"), QStringLiteral("-"),
        QStringLiteral("0"), QString::fromLocal8Bit(argv[2])};
    timelineWorker.write(fields.join(QLatin1Char('\t')).toUtf8() + '\n');
    timelineWorker.closeWriteChannel();
    if (!timelineWorker.waitForFinished(30'000)) {
        std::cerr << "timeline frame worker timed out\n";
        return 1;
    }
    const QByteArray timelineOutput = timelineWorker.readAllStandardOutput();
    if (timelineWorker.exitStatus() != QProcess::NormalExit || timelineWorker.exitCode() != 0 ||
        timelineOutput.size() != 44 ||
        timelineOutput.first(4) != QByteArrayLiteral("VXF1") ||
        static_cast<unsigned char>(timelineOutput[28]) < 240U) {
        std::cerr << timelineWorker.readAllStandardError().toStdString() << '\n';
        return 1;
    }

    // Serve mode: one long-lived process must answer repeated requests with
    // the same bytes as the one-shot timeline-frame invocation.
    QProcess serveWorker;
    serveWorker.start(QString::fromLocal8Bit(argv[1]),
                      {QStringLiteral("timeline-frame-serve")});
    if (!serveWorker.waitForStarted(10'000)) {
        std::cerr << "serve worker did not start\n";
        return 1;
    }
    const QByteArray serveRequest =
        QByteArrayLiteral("REQ 0 24 1 2 2 1 1\n") +
        fields.join(QLatin1Char('\t')).toUtf8() + '\n';
    for (int round = 0; round < 2; ++round) {
        serveWorker.write(serveRequest);
        QByteArray serveOutput;
        while (serveOutput.size() < 44 && serveWorker.waitForReadyRead(15'000)) {
            serveOutput += serveWorker.readAllStandardOutput();
        }
        if (serveOutput != timelineOutput) {
            std::cerr << "serve round " << round
                      << " did not match the one-shot frame ("
                      << serveOutput.size() << " bytes)\n";
            std::cerr << serveWorker.readAllStandardError().toStdString() << '\n';
            return 1;
        }
    }
    serveWorker.closeWriteChannel();
    if (!serveWorker.waitForFinished(10'000) ||
        serveWorker.exitStatus() != QProcess::NormalExit ||
        serveWorker.exitCode() != 0) {
        std::cerr << "serve worker did not exit cleanly on stdin close\n";
        return 1;
    }

    QTemporaryFile waveFile(QDir::tempPath() + QStringLiteral("/videx-audio-XXXXXX.wav"));
    if (!waveFile.open() || !writeTestWave(waveFile)) {
        std::cerr << "could not create audio protocol fixture\n";
        return 1;
    }
    QProcess audioWorker;
    audioWorker.start(QString::fromLocal8Bit(argv[1]),
                      {QStringLiteral("audio"), waveFile.fileName(), QStringLiteral("0"),
                       QStringLiteral("10000")});
    if (!audioWorker.waitForStarted(10'000) || !audioWorker.waitForFinished(30'000)) {
        std::cerr << "audio worker timed out\n";
        return 1;
    }
    const QByteArray audioOutput = audioWorker.readAllStandardOutput();
    if (audioWorker.exitStatus() != QProcess::NormalExit || audioWorker.exitCode() != 0) {
        std::cerr << audioWorker.readAllStandardError().toStdString() << '\n';
        return 1;
    }
    if (audioOutput.size() <= 28 || audioOutput.first(4) != QByteArrayLiteral("VXA1") ||
        readLittleEndian32(audioOutput, 4) != 48'000U ||
        readLittleEndian32(audioOutput, 8) != 2U ||
        readLittleEndian64(audioOutput, 20) !=
            static_cast<std::uint64_t>(audioOutput.size() - 28)) {
        std::cerr << "audio worker returned an invalid binary envelope\n";
        return 1;
    }
    QProcess rampWorker;
    rampWorker.start(QString::fromLocal8Bit(argv[1]),
                     {QStringLiteral("audio-ramp"), waveFile.fileName(), QStringLiteral("0"),
                      QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("24"),
                      QStringLiteral("1"), QStringLiteral("1"),
                      QStringLiteral("0:0.5:0")});
    if (!rampWorker.waitForStarted(10'000) || !rampWorker.waitForFinished(30'000)) {
        std::cerr << "audio ramp worker timed out\n";
        return 1;
    }
    const QByteArray rampOutput = rampWorker.readAllStandardOutput();
    if (rampWorker.exitStatus() != QProcess::NormalExit || rampWorker.exitCode() != 0 ||
        rampOutput.size() <= 28 || rampOutput.first(4) != QByteArrayLiteral("VXA1") ||
        readLittleEndian32(rampOutput, 4) != 48'000U ||
        readLittleEndian32(rampOutput, 8) != 2U ||
        readLittleEndian64(rampOutput, 20) !=
            static_cast<std::uint64_t>(rampOutput.size() - 28)) {
        std::cerr << rampWorker.readAllStandardError().toStdString() << '\n';
        return 1;
    }
    QProcess timelineAudioWorker;
    timelineAudioWorker.start(QString::fromLocal8Bit(argv[1]),
        {QStringLiteral("timeline-audio"), QStringLiteral("0"), QStringLiteral("24"),
         QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("2"),
         QStringLiteral("24"), QStringLiteral("24")});
    if (!timelineAudioWorker.waitForStarted(10'000)) {
        std::cerr << "timeline audio worker did not start\n";
        return 1;
    }
    QStringList audioFields = fields;
    audioFields[0] = QStringLiteral("A");
    audioFields[2] = QStringLiteral("24");
    // Field layout: ... motion spec, gain spec, track order, path.
    audioFields[audioFields.size() - 3] = QStringLiteral("0,-60,0;23,0,0");
    audioFields.back() = waveFile.fileName();
    timelineAudioWorker.write(audioFields.join(QLatin1Char('\t')).toUtf8() + '\n');
    timelineAudioWorker.closeWriteChannel();
    if (!timelineAudioWorker.waitForFinished(30'000)) {
        std::cerr << "timeline audio worker timed out\n";
        return 1;
    }
    const QByteArray timelineAudioOutput = timelineAudioWorker.readAllStandardOutput();
    bool containsAudio = false;
    for (qsizetype index = 28; index < timelineAudioOutput.size(); ++index) {
        containsAudio = containsAudio || timelineAudioOutput[index] != '\0';
    }
    const qsizetype floatCount = (timelineAudioOutput.size() - 28) /
                                 static_cast<qsizetype>(sizeof(float));
    const qsizetype window = std::min<qsizetype>(12'000, floatCount / 4);
    double earlyMean = 0.0;
    double lateMean = 0.0;
    for (qsizetype index = 0; index < window; ++index) {
        float early = 0.0F;
        float late = 0.0F;
        std::memcpy(&early, timelineAudioOutput.constData() + 28 +
                                index * static_cast<qsizetype>(sizeof(float)), sizeof(float));
        std::memcpy(&late, timelineAudioOutput.constData() + 28 +
                               (floatCount - window + index) *
                                   static_cast<qsizetype>(sizeof(float)), sizeof(float));
        earlyMean += std::abs(early);
        lateMean += std::abs(late);
    }
    if (window > 0) {
        earlyMean /= static_cast<double>(window);
        lateMean /= static_cast<double>(window);
    }
    if (timelineAudioWorker.exitStatus() != QProcess::NormalExit ||
        timelineAudioWorker.exitCode() != 0 || timelineAudioOutput.size() <= 28 ||
        timelineAudioOutput.first(4) != QByteArrayLiteral("VXA1") ||
        readLittleEndian32(timelineAudioOutput, 4) != 48'000U ||
        readLittleEndian32(timelineAudioOutput, 8) != 2U || !containsAudio ||
        window == 0 || lateMean < 0.01 || lateMean < earlyMean * 100.0) {
        std::cerr << timelineAudioWorker.readAllStandardError().toStdString() << '\n';
        std::cerr << "gain automation envelope means: " << earlyMean << " -> "
                  << lateMean << '\n';
        return 1;
    }
    return 0;
}
