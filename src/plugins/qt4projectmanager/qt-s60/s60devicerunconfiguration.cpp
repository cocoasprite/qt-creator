/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#include "s60devicerunconfiguration.h"

#include "qt4project.h"
#include "qtversionmanager.h"
#include "profilereader.h"
#include "s60manager.h"
#include "s60devices.h"

#include <coreplugin/icore.h>
#include <coreplugin/messagemanager.h>
#include <utils/qtcassert.h>
#include <utils/pathchooser.h>
#include <projectexplorer/projectexplorerconstants.h>
#include <projectexplorer/project.h>

#include <QtGui/QRadioButton>

using namespace ProjectExplorer;
using namespace Qt4ProjectManager::Internal;

// ======== S60DeviceRunConfiguration
S60DeviceRunConfiguration::S60DeviceRunConfiguration(Project *project, const QString &proFilePath)
    : RunConfiguration(project),
    m_proFilePath(proFilePath),
    m_cachedTargetInformationValid(false),
    m_signingMode(SignSelf)
{
    if (!m_proFilePath.isEmpty())
        setName(tr("%1 on Device").arg(QFileInfo(m_proFilePath).completeBaseName()));
    else
        setName(tr("QtS60DeviceRunConfiguration"));

    connect(project, SIGNAL(activeBuildConfigurationChanged()),
            this, SLOT(invalidateCachedTargetInformation()));

    connect(project, SIGNAL(targetInformationChanged()),
            this, SLOT(invalidateCachedTargetInformation()));
}

S60DeviceRunConfiguration::~S60DeviceRunConfiguration()
{
}

QString S60DeviceRunConfiguration::type() const
{
    return "Qt4ProjectManager.DeviceRunConfiguration";
}

bool S60DeviceRunConfiguration::isEnabled() const
{
    Qt4Project *pro = qobject_cast<Qt4Project*>(project());
    QTC_ASSERT(pro, return false);
    ToolChain::ToolChainType type = pro->toolChainType(pro->activeBuildConfiguration());
    return type == ToolChain::GCCE; //TODO || type == ToolChain::ARMV5
}

QWidget *S60DeviceRunConfiguration::configurationWidget()
{
    return new S60DeviceRunConfigurationWidget(this);
}

void S60DeviceRunConfiguration::save(PersistentSettingsWriter &writer) const
{
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    writer.saveValue("ProFile", projectDir.relativeFilePath(m_proFilePath));
    writer.saveValue("SigningMode", (int)m_signingMode);
    writer.saveValue("CustomSignaturePath", m_customSignaturePath);
    writer.saveValue("CustomKeyPath", m_customKeyPath);
    RunConfiguration::save(writer);
}

void S60DeviceRunConfiguration::restore(const PersistentSettingsReader &reader)
{
    RunConfiguration::restore(reader);
    const QDir projectDir = QFileInfo(project()->file()->fileName()).absoluteDir();
    m_proFilePath = projectDir.filePath(reader.restoreValue("ProFile").toString());
    m_signingMode = (SigningMode)reader.restoreValue("SigningMode").toInt();
    m_customSignaturePath = reader.restoreValue("CustomSignaturePath").toString();
    m_customKeyPath = reader.restoreValue("CustomKeyPath").toString();
}

QString S60DeviceRunConfiguration::basePackageFilePath() const
{
    const_cast<S60DeviceRunConfiguration *>(this)->updateTarget();
    return m_baseFileName;
}

S60DeviceRunConfiguration::SigningMode S60DeviceRunConfiguration::signingMode() const
{
    return m_signingMode;
}

void S60DeviceRunConfiguration::setSigningMode(SigningMode mode)
{
    m_signingMode = mode;
}

QString S60DeviceRunConfiguration::customSignaturePath() const
{
    return m_customSignaturePath;
}

void S60DeviceRunConfiguration::setCustomSignaturePath(const QString &path)
{
    m_customSignaturePath = path;
}

QString S60DeviceRunConfiguration::customKeyPath() const
{
    return m_customKeyPath;
}

void S60DeviceRunConfiguration::setCustomKeyPath(const QString &path)
{
    m_customKeyPath = path;
}

void S60DeviceRunConfiguration::updateTarget()
{
    if (m_cachedTargetInformationValid)
        return;
    Qt4Project *pro = static_cast<Qt4Project *>(project());
    Qt4PriFileNode * priFileNode = static_cast<Qt4Project *>(project())->rootProjectNode()->findProFileFor(m_proFilePath);
    if (!priFileNode) {
        m_baseFileName = QString::null;
        m_cachedTargetInformationValid = true;
        emit targetInformationChanged();
        return;
    }
    QtVersion *qtVersion = pro->qtVersion(pro->activeBuildConfiguration());
    ProFileReader *reader = priFileNode->createProFileReader();
    reader->setCumulative(false);
    reader->setQtVersion(qtVersion);

    // Find out what flags we pass on to qmake, this code is duplicated in the qmake step
    QtVersion::QmakeBuildConfig defaultBuildConfiguration = qtVersion->defaultBuildConfig();
    QtVersion::QmakeBuildConfig projectBuildConfiguration = QtVersion::QmakeBuildConfig(pro->value(pro->activeBuildConfiguration(), "buildConfiguration").toInt());
    QStringList addedUserConfigArguments;
    QStringList removedUserConfigArguments;
    if ((defaultBuildConfiguration & QtVersion::BuildAll) && !(projectBuildConfiguration & QtVersion::BuildAll))
        removedUserConfigArguments << "debug_and_release";
    if (!(defaultBuildConfiguration & QtVersion::BuildAll) && (projectBuildConfiguration & QtVersion::BuildAll))
        addedUserConfigArguments << "debug_and_release";
    if ((defaultBuildConfiguration & QtVersion::DebugBuild) && !(projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "release";
    if (!(defaultBuildConfiguration & QtVersion::DebugBuild) && (projectBuildConfiguration & QtVersion::DebugBuild))
        addedUserConfigArguments << "debug";

    reader->setUserConfigCmdArgs(addedUserConfigArguments, removedUserConfigArguments);

    if (!reader->readProFile(m_proFilePath)) {
        delete reader;
        Core::ICore::instance()->messageManager()->printToOutputPane(tr("Could not parse %1. The QtS60 Device run configuration %2 can not be started.").arg(m_proFilePath).arg(name()));
        return;
    }

    // Extract data
    const QDir baseProjectDirectory = QFileInfo(project()->file()->fileName()).absoluteDir();
    const QString relSubDir = baseProjectDirectory.relativeFilePath(QFileInfo(m_proFilePath).path());
    const QDir baseBuildDirectory = project()->buildDirectory(project()->activeBuildConfiguration());
    const QString baseDir = baseBuildDirectory.absoluteFilePath(relSubDir);

    // Directory
    QString m_workingDir;
    if (reader->contains("DESTDIR")) {
        m_workingDir = reader->value("DESTDIR");
        if (QDir::isRelativePath(m_workingDir)) {
            m_workingDir = baseDir + QLatin1Char('/') + m_workingDir;
        }
    } else {
        m_workingDir = baseDir;
    }

    m_baseFileName = QDir::cleanPath(m_workingDir + QLatin1Char('/') + reader->value("TARGET"));

    if (pro->toolChainType(pro->activeBuildConfiguration()) == ToolChain::GCCE)
        m_baseFileName += "_gcce";
    else
        m_baseFileName += "_armv5";
    if (projectBuildConfiguration & QtVersion::DebugBuild)
        m_baseFileName += "_udeb";
    else
        m_baseFileName += "_rel";

    delete reader;
    m_cachedTargetInformationValid = true;
    emit targetInformationChanged();
}

void S60DeviceRunConfiguration::invalidateCachedTargetInformation()
{
    m_cachedTargetInformationValid = false;
    emit targetInformationChanged();
}

// ======== S60DeviceRunConfigurationWidget

S60DeviceRunConfigurationWidget::S60DeviceRunConfigurationWidget(S60DeviceRunConfiguration *runConfiguration,
                                                                     QWidget *parent)
    : QWidget(parent),
    m_runConfiguration(runConfiguration)
{
    QVBoxLayout *mainBoxLayout = new QVBoxLayout();
    mainBoxLayout->setMargin(0);
    setLayout(mainBoxLayout);
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setMargin(0);
    mainBoxLayout->addLayout(formLayout);

    QLabel *nameLabel = new QLabel(tr("Name:"));
    m_nameLineEdit = new QLineEdit(m_runConfiguration->name());
    nameLabel->setBuddy(m_nameLineEdit);
    formLayout->addRow(nameLabel, m_nameLineEdit);

    m_sisxFileLabel = new QLabel(m_runConfiguration->basePackageFilePath() + ".sisx");
    formLayout->addRow(tr("Install File:"), m_sisxFileLabel);

    QWidget *signatureWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout();
    signatureWidget->setLayout(layout);
    mainBoxLayout->addWidget(signatureWidget);
    QRadioButton *selfSign = new QRadioButton(tr("Self-sign"));
    QHBoxLayout *customHBox = new QHBoxLayout();
    customHBox->setMargin(0);
    QVBoxLayout *radioLayout = new QVBoxLayout();
    QRadioButton *customSignature = new QRadioButton();
    radioLayout->addWidget(customSignature);
    radioLayout->addStretch(10);
    customHBox->addLayout(radioLayout);
    QFormLayout *customLayout = new QFormLayout();
    customLayout->setMargin(0);
    customLayout->setLabelAlignment(Qt::AlignRight);
    Core::Utils::PathChooser *signaturePath = new Core::Utils::PathChooser();
    signaturePath->setExpectedKind(Core::Utils::PathChooser::File);
    signaturePath->setPromptDialogTitle(tr("Choose certificate file (.cer)"));
    customLayout->addRow(new QLabel(tr("Custom signature:")), signaturePath);
    Core::Utils::PathChooser *keyPath = new Core::Utils::PathChooser();
    keyPath->setExpectedKind(Core::Utils::PathChooser::File);
    keyPath->setPromptDialogTitle(tr("Choose key file (.key / .pem)"));
    customLayout->addRow(new QLabel(tr("Key file:")), keyPath);
    customHBox->addLayout(customLayout);
    customHBox->addStretch(10);
    layout->addWidget(selfSign);
    layout->addLayout(customHBox);
    layout->addStretch(10);

    switch (m_runConfiguration->signingMode()) {
    case S60DeviceRunConfiguration::SignSelf:
        selfSign->setChecked(true);
        break;
    case S60DeviceRunConfiguration::SignCustom:
        customSignature->setChecked(true);
        break;
    }

    signaturePath->setPath(m_runConfiguration->customSignaturePath());
    keyPath->setPath(m_runConfiguration->customKeyPath());

    connect(m_nameLineEdit, SIGNAL(textEdited(QString)),
        this, SLOT(nameEdited(QString)));
    connect(m_runConfiguration, SIGNAL(targetInformationChanged()),
            this, SLOT(updateTargetInformation()));
    connect(selfSign, SIGNAL(toggled(bool)), this, SLOT(selfSignToggled(bool)));
    connect(customSignature, SIGNAL(toggled(bool)), this, SLOT(customSignatureToggled(bool)));
    connect(signaturePath, SIGNAL(changed(QString)), this, SLOT(signaturePathChanged(QString)));
    connect(keyPath, SIGNAL(changed(QString)), this, SLOT(keyPathChanged(QString)));
}

void S60DeviceRunConfigurationWidget::nameEdited(const QString &text)
{
    m_runConfiguration->setName(text);
}

void S60DeviceRunConfigurationWidget::updateTargetInformation()
{
    m_sisxFileLabel->setText(m_runConfiguration->basePackageFilePath() + ".sisx");
}

void S60DeviceRunConfigurationWidget::selfSignToggled(bool toggle)
{
    if (toggle)
        m_runConfiguration->setSigningMode(S60DeviceRunConfiguration::SignSelf);
}

void S60DeviceRunConfigurationWidget::customSignatureToggled(bool toggle)
{
    if (toggle)
        m_runConfiguration->setSigningMode(S60DeviceRunConfiguration::SignCustom);
}

void S60DeviceRunConfigurationWidget::signaturePathChanged(const QString &path)
{
    m_runConfiguration->setCustomSignaturePath(path);
}

void S60DeviceRunConfigurationWidget::keyPathChanged(const QString &path)
{
    m_runConfiguration->setCustomKeyPath(path);
}

// ======== S60DeviceRunConfigurationFactory

S60DeviceRunConfigurationFactory::S60DeviceRunConfigurationFactory(QObject *parent)
    : IRunConfigurationFactory(parent)
{
}

S60DeviceRunConfigurationFactory::~S60DeviceRunConfigurationFactory()
{
}

bool S60DeviceRunConfigurationFactory::canRestore(const QString &type) const
{
    return type == "Qt4ProjectManager.DeviceRunConfiguration";
}

QStringList S60DeviceRunConfigurationFactory::availableCreationTypes(Project *pro) const
{
    Qt4Project *qt4project = qobject_cast<Qt4Project *>(pro);
    if (qt4project) {
        QStringList applicationProFiles;
        QList<Qt4ProFileNode *> list = qt4project->applicationProFiles();
        foreach (Qt4ProFileNode * node, list) {
            applicationProFiles.append("QtS60DeviceRunConfiguration." + node->path());
        }
        return applicationProFiles;
    } else {
        return QStringList();
    }
}

QString S60DeviceRunConfigurationFactory::displayNameForType(const QString &type) const
{
    QString fileName = type.mid(QString("QtS60DeviceRunConfiguration.").size());
    return tr("%1 on Device").arg(QFileInfo(fileName).completeBaseName());
}

QSharedPointer<RunConfiguration> S60DeviceRunConfigurationFactory::create(Project *project, const QString &type)
{
    Qt4Project *p = qobject_cast<Qt4Project *>(project);
    Q_ASSERT(p);
    if (type.startsWith("QtS60DeviceRunConfiguration.")) {
        QString fileName = type.mid(QString("QtS60DeviceRunConfiguration.").size());
        return QSharedPointer<RunConfiguration>(new S60DeviceRunConfiguration(p, fileName));
    }
    Q_ASSERT(type == "Qt4ProjectManager.DeviceRunConfiguration");
    // The right path is set in restoreSettings
    QSharedPointer<RunConfiguration> rc(new S60DeviceRunConfiguration(p, QString::null));
    return rc;
}

// ======== S60DeviceRunConfigurationRunner

S60DeviceRunConfigurationRunner::S60DeviceRunConfigurationRunner(QObject *parent)
    : IRunConfigurationRunner(parent)
{
}

bool S60DeviceRunConfigurationRunner::canRun(QSharedPointer<RunConfiguration> runConfiguration, const QString &mode)
{
    return (mode == ProjectExplorer::Constants::RUNMODE)
            && (!runConfiguration.dynamicCast<S60DeviceRunConfiguration>().isNull());
}

RunControl* S60DeviceRunConfigurationRunner::run(QSharedPointer<RunConfiguration> runConfiguration, const QString &mode)
{
    QSharedPointer<S60DeviceRunConfiguration> rc = runConfiguration.dynamicCast<S60DeviceRunConfiguration>();
    Q_ASSERT(!rc.isNull());
    Q_ASSERT(mode == ProjectExplorer::Constants::RUNMODE);

    S60DeviceRunControl *runControl = new S60DeviceRunControl(rc);
    return runControl;
}

// ======== S60DeviceRunControl

S60DeviceRunControl::S60DeviceRunControl(QSharedPointer<RunConfiguration> runConfiguration)
    : RunControl(runConfiguration)
{
    m_makesis = new QProcess(this);
    connect(m_makesis, SIGNAL(readyReadStandardError()),
            this, SLOT(readStandardError()));
    connect(m_makesis, SIGNAL(readyReadStandardOutput()),
            this, SLOT(readStandardOutput()));
    connect(m_makesis, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(makesisProcessFailed()));
    connect(m_makesis, SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(makesisProcessFinished()));
    m_signsis = new QProcess(this);
    connect(m_signsis, SIGNAL(readyReadStandardError()),
            this, SLOT(readStandardError()));
    connect(m_signsis, SIGNAL(readyReadStandardOutput()),
            this, SLOT(readStandardOutput()));
    connect(m_signsis, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(signsisProcessFailed()));
    connect(m_signsis, SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(signsisProcessFinished()));
    m_install = new QProcess(this);
    connect(m_install, SIGNAL(readyReadStandardError()),
            this, SLOT(readStandardError()));
    connect(m_install, SIGNAL(readyReadStandardOutput()),
            this, SLOT(readStandardOutput()));
    connect(m_install, SIGNAL(error(QProcess::ProcessError)),
            this, SLOT(installProcessFailed()));
    connect(m_install, SIGNAL(finished(int,QProcess::ExitStatus)),
            this, SLOT(installProcessFinished()));
}

void S60DeviceRunControl::start()
{
    QSharedPointer<S60DeviceRunConfiguration> rc = runConfiguration().dynamicCast<S60DeviceRunConfiguration>();
    Q_ASSERT(!rc.isNull());

    Qt4Project *project = qobject_cast<Qt4Project *>(rc->project());

    m_baseFileName = rc->basePackageFilePath();
    m_workingDirectory = QFileInfo(m_baseFileName).absolutePath();
    m_qtDir = project->qtVersion(project->activeBuildConfiguration())->path();
    m_useCustomSignature = (rc->signingMode() == S60DeviceRunConfiguration::SignCustom);
    m_customSignaturePath = rc->customSignaturePath();
    m_customKeyPath = rc->customKeyPath();

    emit started();

    emit addToOutputWindow(this, tr("Creating %1.sisx ...").arg(QDir::toNativeSeparators(m_baseFileName)));

    Q_ASSERT(project);
    m_toolsDirectory = S60Manager::instance()->devices()->deviceForId(
            S60Manager::instance()->deviceIdFromDetectionSource(
            project->qtVersion(project->activeBuildConfiguration())
            ->autodetectionSource())).epocRoot
            + "/epoc32/tools";
    QString makesisTool = m_toolsDirectory + "/makesis.exe";
    QString packageFile = QFileInfo(m_baseFileName + ".pkg").fileName();
    m_makesis->setWorkingDirectory(m_workingDirectory);
    emit addToOutputWindow(this, tr("%1 %2").arg(QDir::toNativeSeparators(makesisTool), packageFile));
    m_makesis->start(makesisTool, QStringList()
        << packageFile,
        QIODevice::ReadOnly);
}

void S60DeviceRunControl::stop()
{
    // TODO
}

bool S60DeviceRunControl::isRunning() const
{
    return m_makesis->state() != QProcess::NotRunning;
}

void S60DeviceRunControl::readStandardError()
{
    QProcess *process = static_cast<QProcess *>(sender());
    QByteArray data = process->readAllStandardError();
    emit addToOutputWindowInline(this, QString::fromLocal8Bit(data.constData(), data.length()));
}

void S60DeviceRunControl::readStandardOutput()
{
    QProcess *process = static_cast<QProcess *>(sender());
    QByteArray data = process->readAllStandardOutput();
    emit addToOutputWindowInline(this, QString::fromLocal8Bit(data.constData(), data.length()));
}

void S60DeviceRunControl::makesisProcessFailed()
{
    processFailed("makesis.exe", m_makesis->error());
}

void S60DeviceRunControl::makesisProcessFinished()
{
    if (m_makesis->exitCode() != 0) {
        error(this, tr("An error occurred while creating the package."));
        emit finished();
        return;
    }
    QString signsisTool = m_toolsDirectory + "/signsis.exe";
    QString sisFile = QFileInfo(m_baseFileName + ".sis").fileName();
    QString sisxFile = QFileInfo(m_baseFileName + ".sisx").fileName();
    QString signature = (m_useCustomSignature ? m_customSignaturePath
                         : m_qtDir + "/selfsigned.cer");
    QString key = (m_useCustomSignature ? m_customKeyPath
                         : m_qtDir + "/selfsigned.key");
    QStringList arguments;
    arguments << sisFile
            << sisxFile << QDir::toNativeSeparators(signature)
            << QDir::toNativeSeparators(key);
    m_signsis->setWorkingDirectory(m_workingDirectory);
    emit addToOutputWindow(this, tr("%1 %2").arg(QDir::toNativeSeparators(signsisTool), arguments.join(tr(" "))));
    m_signsis->start(signsisTool, arguments, QIODevice::ReadOnly);
}

void S60DeviceRunControl::signsisProcessFailed()
{
    processFailed("signsis.exe", m_signsis->error());
}

void S60DeviceRunControl::signsisProcessFinished()
{
    if (m_signsis->exitCode() != 0) {
        error(this, tr("An error occurred while creating the package."));
        emit finished();
        return;
    }
    QString applicationInstaller = "cmd.exe";
    QStringList arguments;
    arguments << "/C" << QDir::toNativeSeparators(m_baseFileName + ".sisx");
    m_install->setWorkingDirectory(m_workingDirectory);
    emit addToOutputWindow(this, tr("%1 %2").arg(QDir::toNativeSeparators(applicationInstaller), arguments.join(tr(" "))));
    m_install->start(applicationInstaller, arguments, QIODevice::ReadOnly);
}

void S60DeviceRunControl::installProcessFailed()
{
    processFailed("ApplicationInstaller", m_install->error());
}

void S60DeviceRunControl::installProcessFinished()
{
    if (m_install->exitCode() != 0) {
        error(this, tr("An error occurred while creating the package."));
    }
    emit addToOutputWindow(this, tr("Finished."));
    emit finished();
}

void S60DeviceRunControl::processFailed(const QString &program, QProcess::ProcessError errorCode)
{
    QString errorString;
    switch (errorCode) {
    case QProcess::FailedToStart:
        errorString = tr("Failed to start %1.");
        break;
    case QProcess::Crashed:
        errorString = tr("%1 has unexpectedly finished.");
        break;
    default:
        errorString = tr("Some error has occurred while running %1.");
    }
    error(this, errorString.arg(program));
}
