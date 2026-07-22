#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QStandardPaths>
#include <QTextStream>

#ifdef _WIN32
#include <windows.h>
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "MainWindow.h"

namespace {
QFile* logFile = nullptr;

#ifdef _WIN32
// Hard crashes (access violations etc.) bypass C++ exception handling and
// the Qt message handler entirely — this SEH filter writes a last-gasp
// line to the log so crashes are never silent. Uses raw stdio: the Qt
// objects may be in an arbitrary state mid-crash.
char crashLogPath[1024] = {};

void crashNote(const char* what, unsigned long code, void* addr) {
    if (!crashLogPath[0]) {
        return;
    }
    FILE* f = nullptr;
    fopen_s(&f, crashLogPath, "a");
    if (f) {
        std::fprintf(f, "CRASH: %s code 0x%08lX at address %p\n", what, code, addr);
        std::fclose(f);
    }
}

LONG WINAPI crashHandler(EXCEPTION_POINTERS* info) {
    crashNote("unhandled SEH exception", info->ExceptionRecord->ExceptionCode,
               info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

// Every qDebug/qInfo/qWarning/qCritical — from M2Suite code and Qt itself
// (media backend errors, plugin loading, etc.) — lands in the log with a
// timestamp, so crashes and silent failures leave a trail.
void logHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (!logFile) {
        return;
    }
    const char* level = "DBG";
    switch (type) {
        case QtInfoMsg: level = "INF"; break;
        case QtWarningMsg: level = "WRN"; break;
        case QtCriticalMsg: level = "CRT"; break;
        case QtFatalMsg: level = "FTL"; break;
        default: break;
    }
    QTextStream out(logFile);
    out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"))
        << ' ' << level << ' ' << msg;
    if (context.file) {
        out << "  (" << context.file << ':' << context.line << ')';
    }
    out << '\n';
    out.flush();
    if (type == QtFatalMsg) {
        logFile->close();
    }
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("M2Suite"));
    QApplication::setApplicationName(QStringLiteral("M2Suite"));
    app.setWindowIcon(QIcon(QStringLiteral(":/m2suite_icon.png")));

    // Log next to the executable: <appdir>/m2suite.log, truncated per run.
    QString logPath = QCoreApplication::applicationDirPath() + QStringLiteral("/m2suite.log");
    QFile log(logPath);
    if (log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        logFile = &log;
        qInstallMessageHandler(logHandler);
        qInfo() << "M2Suite starting," << QT_VERSION_STR;
    }
#ifdef _WIN32
    QByteArray pathUtf8 = QDir::toNativeSeparators(logPath).toLocal8Bit();
    if (pathUtf8.size() < int(sizeof(crashLogPath))) {
        std::snprintf(crashLogPath, sizeof(crashLogPath), "%s", pathUtf8.constData());
    }
    SetUnhandledExceptionFilter(crashHandler);
    // Also catch paths that don't raise SEH: std::terminate (unhandled C++
    // exception on a worker thread, noexcept violation) and abort().
    std::set_terminate([]() {
        crashNote("std::terminate (unhandled C++ exception?)", 0, nullptr);
        std::abort();
    });
    std::signal(SIGABRT, [](int) { crashNote("abort() called", 0, nullptr); });
#endif

    m2suite::MainWindow window;
    window.show();

    int rc = app.exec();
    qInfo() << "M2Suite exiting, rc =" << rc;
    logFile = nullptr;
    return rc;
}
