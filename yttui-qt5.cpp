#include <QApplication>

#include <QIcon>
#include <QMainWindow>
#include <QMenu>
#include <QMetaEnum>
#include <QMetaObject>
#include <QMouseEvent>
#include <QString>
#include <QSystemTrayIcon>
#include <QThread>

#include <KParts/Part>
#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginLoader>
#include <KService>

#include <kde_terminal_interface.h>

#include <unistd.h>
#include <pty.h>

#include "application.h"

application_host *host = nullptr;

class AppThread: public QThread
{
    Q_OBJECT
    int app_fd;
public:
    AppThread(int fd): QThread(), app_fd(fd){}
    ~AppThread() = default;

    friend bool app_quit(void*);
protected:
    void run() override
    {
        run_embedded(app_fd, host);
    }
};

class RightClickFilter: public QObject
{
    Q_OBJECT
protected:
    bool eventFilter(QObject *obj, QEvent *event)
    {
        if((event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease)) {
            QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
            if(mouseEvent->buttons() & Qt::RightButton) {
                return true;
            }
        }
        else if(event->type() == QEvent::ContextMenu) {
            return true;
        }
        return QObject::eventFilter(obj, event);
    }
};

QObject* find_obj_by_classname(QObject *in, const char* name)
{
    if(strcmp(name, in->metaObject()->className()) == 0)
        return in;

    for(QObject *child: in->children())
    {
        QObject *maybe = find_obj_by_classname(child, name);
        if(maybe)
            return maybe;
    }
    return nullptr;
}

class ApplicationWindow: public QMainWindow
{
    Q_OBJECT

    QIcon icon;
    QSystemTrayIcon *systray = nullptr;
    KParts::ReadOnlyPart *terminal = nullptr;
    QWidget *konsole = nullptr;

public:
    ApplicationWindow(int fd);

    void showMessage(const QString &title, const QString &message);

    // QWidget interface
protected:
    void closeEvent(QCloseEvent *event) override;
};

ApplicationWindow::ApplicationWindow(int fd): icon(":/icons/icon_0.png"), systray(new QSystemTrayIcon(icon, this))
{
    setWindowTitle("yttui-qt5");
    setWindowIcon(icon);

    KService::Ptr service = KService::serviceByDesktopName(QStringLiteral("konsolepart"));
    Q_ASSERT(service);
    KPluginFactory* factory = KPluginLoader(service->library()).factory();
    Q_ASSERT(factory);

    terminal = factory->create<KParts::ReadOnlyPart>(this);
    if(!QMetaObject::invokeMethod(terminal, "openTeletype", Qt::AutoConnection, Q_ARG(int, fd), Q_ARG(bool, false))) {
        fputs("Failed to set KonsolePart PTY\n", stderr);
        abort();
    }

    konsole = terminal->widget();
    setCentralWidget(konsole);

    QObject *td = find_obj_by_classname(konsole, "Konsole::TerminalDisplay");
    if(td) {
        td->installEventFilter(new RightClickFilter());
    }

    QMenu *systrayContextMenu = new QMenu(this);
    systrayContextMenu->addAction(QIcon::fromTheme("application-exit"), "Exit", QApplication::instance(), &QApplication::quit);

    systray->setContextMenu(systrayContextMenu);
    connect(systray, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
        if(reason == QSystemTrayIcon::Trigger) {
            if(isVisible()) {
                hide();
            } else {
                show();
            }
        }
    });
    systray->show();
}

void ApplicationWindow::showMessage(const QString &title, const QString &message)
{
    systray->showMessage(title, message, icon);
}

void ApplicationWindow::closeEvent(QCloseEvent *event)
{
    QMainWindow::closeEvent(event);
    systray->hide();
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    int term_fd = -1, app_fd = -1;
    if(openpty(&term_fd, &app_fd, 0, 0, 0)) {
        perror("openpty");
    }

    ApplicationWindow window(term_fd);
    window.show();

    bool app_quit = false;

    AppThread appthread(app_fd);
    QObject::connect(&appthread, &AppThread::finished, [&] {
        window.close();
    });
    QObject::connect(&app, &QApplication::aboutToQuit, [&] {
        app_quit = true;
        appthread.wait();
    });

    host = new application_host;
    host->quit = [&]{ return app_quit; };
    host->notify_channel_single_video = [&](const std::string &channel, const std::string &title) {
         window.showMessage(QStringLiteral("New video from %1").arg(QString::fromStdString(channel)), QString::fromStdString(title));
    };
    host->notify_channel_multiple_videos = [&](const std::string &channel, const int videos) {
         window.showMessage(QStringLiteral("New videos from %1").arg(QString::fromStdString(channel)),
                            QStringLiteral("There are %1 new videos.").arg(QString::number(videos)));
    };
    host->notify_channels_multiple_videos = [&](const int channels, const int videos) {
         window.showMessage("New videos from multiple channels",
                            QStringLiteral("There are %2 new videos from %1 channels.").arg(QString::number(channels), QString::number(videos)));
    };

    appthread.start();
    int rc = QApplication::exec();
    close(term_fd);
    close(app_fd);

    delete host;

    return rc;
}

#include "yttui-qt5.moc"
