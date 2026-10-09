#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QTextStream>
#include <QtQml>

// 这里把 qt 的告警/错误额外落一份到 ./config/qml.log
static void tagmeowMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type != QtWarningMsg && type != QtCriticalMsg && type != QtFatalMsg)
    {
        return;
    }

    QDir().mkpath(QStringLiteral("config"));

    QFile file(QStringLiteral("config/qml.log"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(QStringLiteral("[yyyy-MM-dd HH:mm:ss] "))
        << (context.file != nullptr ? context.file : "") << QLatin1Char(':') << context.line << QLatin1Char(' ')
        << message << Qt::endl;
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(tagmeowMessageHandler);

    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("windows", "Main");

    return QGuiApplication::exec();
}
