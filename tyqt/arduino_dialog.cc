/*
 * ty, a collection of GUI and command-line tools to manage Teensy devices
 *
 * Distributed under the MIT license (see LICENSE.txt or http://opensource.org/licenses/MIT)
 * Copyright (c) 2015 Niels Martignène <niels.martignene@gmail.com>
 */

#ifdef _WIN32
    #include <windows.h>
#endif
#include <QCoreApplication>
#include <QFileDialog>
#include <QSysInfo>
#include <QWinEventNotifier>

#include "ty.h"
#include "arduino_dialog.hh"

using namespace std;

ArduinoDialog::ArduinoDialog(QWidget *parent, Qt::WindowFlags f)
    : QDialog(parent, f)
{
    setupUi(this);

    connect(&install_, &ArduinoInstallation::changed, this, &ArduinoDialog::refresh);
    connect(&install_, &ArduinoInstallation::log, this, &ArduinoDialog::addLog);
    connect(&install_, &ArduinoInstallation::error, this, &ArduinoDialog::addError);

    refresh();
}

void ArduinoDialog::keyPressEvent(QKeyEvent *ev)
{
    // Easy way to disable the "default button" feature of QDialog, without changing each button
    if(ev->key() == Qt::Key_Enter || ev->key() == Qt::Key_Return)
        return;

    QDialog::keyPressEvent(ev);
}

void ArduinoDialog::refresh()
{
    statusLabel->setVisible(!install_.path().isEmpty());
    integrateButton->setEnabled(false);
    restoreButton->setEnabled(false);

    if (install_.path().isEmpty())
        return;

    const char *color = NULL;
    QString text;

    if (install_.isIntegrated()) {
        color = "green";
        text = tr("Arduino %1 / Teensyduino %2\nAlready using TyQt").arg(install_.arduinoVersion(),
                                                                         install_.teensyduinoVersion());

        if (!background_process_)
            restoreButton->setEnabled(true);
    } else if (install_.isValid()) {
        color = "orange";
        text = tr("Arduino %1 / Teensyduino %2\nNot using TyQt").arg(install_.arduinoVersion(),
                                                                     install_.teensyduinoVersion());

        if (!background_process_)
            integrateButton->setEnabled(true);
    } else {
        color = "red";
        text = tr("Not a valid Arduino/Teensyduino directory");
    }

    QPalette pal;
    if (color)
        pal.setColor(QPalette::WindowText, color);
    statusLabel->setPalette(pal);
    statusLabel->setText(text);
}

void ArduinoDialog::addLog(const QString &msg)
{
    appendMessage(msg);
}

void ArduinoDialog::addError(const QString &msg)
{
    QTextCharFormat fmt;
    fmt.setForeground(QColor("red"));

    appendMessage(msg, fmt);
}

void ArduinoDialog::on_arduinoPath_editingFinished()
{
    install_.setPath(arduinoPath->text());
}

void ArduinoDialog::on_browseButton_clicked()
{
    auto path = QFileDialog::getExistingDirectory(this);
    if (path.isEmpty())
        return;
    path = QDir::toNativeSeparators(path);

    arduinoPath->setText(path);
    install_.setPath(path);
}

void ArduinoDialog::on_integrateButton_clicked()
{
    if (background_process_)
        return;

    logText->clear();

    if (!install_.integrate())
        executeAsRoot("integrate");
}

void ArduinoDialog::on_restoreButton_clicked()
{
    if (background_process_)
        return;

    logText->clear();

    if (!install_.restore())
        executeAsRoot("restore");
}

void ArduinoDialog::appendMessage(const QString &msg, const QTextCharFormat &fmt)
{
    auto document = logText->document();
    QTextCursor cursor(document);

    cursor.movePosition(QTextCursor::End);
    if (!document->isEmpty())
        cursor.insertBlock();
    cursor.insertText(msg, fmt);
}

void ArduinoDialog::executeAsRoot(const QString &command)
{
#ifdef _WIN32
    if (QSysInfo::windowsVersion() >= QSysInfo::WV_VISTA) {
        installWithUAC(command);
        return;
    }

    QString executable = QDir::toNativeSeparators(QCoreApplication::applicationDirPath() + "/tyqtc.exe");
#else
    QString executable = QCoreApplication::applicationDirPath() + "/tyqt";
#endif

    QTextCharFormat fmt;
    fmt.setFontItalic(true);

    appendMessage("");
    appendMessage(tr("Try to restart this command as root with :"));
    appendMessage(tr("\"%1\" %2 \"%3\"").arg(executable, command, install_.absolutePath()), fmt);
}

#ifdef _WIN32

void ArduinoDialog::installWithUAC(const QString &command)
{
    addLog("Trying with elevated rights");
    QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 1000);

    SHELLEXECUTEINFO info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = "runas";
    info.lpFile = "cmd";
    info.nShow = SW_SHOW;

    auto parameters = QString("/C \"\"%1\\%2\" %3 \"%4\" & pause\"").arg(QDir::toNativeSeparators(QCoreApplication::applicationDirPath()), "tyqtc.exe",
                                                                         command, QDir::toNativeSeparators(install_.absolutePath())).toLocal8Bit();
    info.lpParameters = parameters.constData();

    BOOL success = ShellExecuteEx(&info);
    if (!success) {
        addLog("Cannot execute with Administrator rights");
        return;
    }
    if (!info.hProcess)
        return;

    background_process_ = true;

    auto notifier = new QWinEventNotifier(info.hProcess, this);
    connect(notifier, &QWinEventNotifier::activated, this, [=](HANDLE h) {
        TY_UNUSED(h);

        background_process_ = false;
        install_.update();

        notifier->setEnabled(false);
        notifier->deleteLater();
    });
    notifier->setEnabled(true);
}

#endif
