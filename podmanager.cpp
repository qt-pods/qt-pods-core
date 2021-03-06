///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//    This file is part of qt-pods.                                          //
//    Copyright (C) 2015 Jacob Dawid, jacob@omg-it.works                     //
//                                                                           //
//    qt-pods is free software: you can redistribute it and/or modify        //
//    it under the terms of the GNU General Public License as published by   //
//    the Free Software Foundation, either version 3 of the License, or      //
//    (at your option) any later version.                                    //
//                                                                           //
//    qt-pods is distributed in the hope that it will be useful,             //
//    but WITHOUT ANY WARRANTY; without even the implied warranty of         //
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          //
//    GNU General Public License for more details.                           //
//                                                                           //
//    You should have received a copy of the GNU General Public License      //
//    along with qt-pods. If not, see <http://www.gnu.org/licenses/>.        //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

// Own includes
#include "podmanager.h"

// Qt includes
#include <QDir>
#include <QSettings>
#include <QProcess>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>

PodManager::PodManager(QObject *parent)
    : QObject(parent) {
    qRegisterMetaType<QList<Pod> >("QList<Pod>");
    _networkAccessManager = new QNetworkAccessManager(this);
}

bool PodManager::isGitRepository(QString repository) {
    QDir dir(repository);
    QString gitPath = dir.filePath(".git");
    bool result = QFile::exists(gitPath);
    emit isGitRepositoryFinished(repository, result);
    return result;
}

bool PodManager::installPod(QString repository, Pod pod) {
    if(!isGitRepository(repository)) {
        emit installPodFinished(repository, pod, false);
        return false;
    }

    if(!addPodSubmodule(repository, pod)) {
        emit installPodFinished(repository, pod, false);
        return false;
    }

    // Try to store meta data in .gitmodules
    writePodInfo(repository, pod);

    generateQmakeFiles(repository);

    emit installPodFinished(repository, pod, true);
    return true;
}

bool PodManager::installPods(QString repository, QList<Pod> pods) {
    if(!isGitRepository(repository)) {
        emit installPodsFinished(repository, pods, false);
        return false;
    }

    bool success = true;
    foreach(Pod pod, pods) {
        success = success && addPodSubmodule(repository, pod);
    }

    if(!success) {
        emit installPodsFinished(repository, pods, false);
        return false;
    }

    generateQmakeFiles(repository);

    emit installPodsFinished(repository, pods, true);
    return true;
}

bool PodManager::removePod(QString repository, QString podName) {
    if(!isGitRepository(repository)) {
        emit removePodFinished(repository, podName, false);
        return false;
    }

    if(!removePodSubmodule(repository, podName)) {
        emit removePodFinished(repository, podName, false);
        return false;
    }

    generateQmakeFiles(repository);

    emit removePodFinished(repository, podName, true);
    return true;
}

bool PodManager::removePods(QString repository, QStringList podNames) {
    if(!isGitRepository(repository)) {
        emit removePodsFinished(repository, podNames, false);
        return false;
    }

    bool success = true;
    foreach(QString podName, podNames) {
        success = success && removePodSubmodule(repository, podName);
    }

    if(!success) {
        emit removePodsFinished(repository, podNames, false);
        return false;
    }

    generateQmakeFiles(repository);

    emit removePodsFinished(repository, podNames, true);
    return true;
}

bool PodManager::updatePod(QString repository, QString podName) {
    if(!isGitRepository(repository)) {
        emit updatePodFinished(repository, podName, false);
        return false;
    }

    bool result = updatePodSubmodule(repository, podName);
    emit updatePodFinished(repository, podName, result);
    return result;
}

bool PodManager::updatePods(QString repository, QStringList podNames) {
    if(!isGitRepository(repository)) {
        emit updatePodsFinished(repository, podNames, false);
        return false;
    }

    bool success = true;
    foreach(QString podName, podNames) {
        success = success && updatePodSubmodule(repository, podName);
    }

    if(!success) {
        emit updatePodsFinished(repository, podNames, false);
    }
    emit updatePodsFinished(repository, podNames, true);
    return true;
}

bool PodManager::updateAllPods(QString repository) {
    if(!isGitRepository(repository)) {
        emit updateAllPodsFinished(repository, false);
        return false;
    }

    bool result = false;
    QList<Pod> pods = listInstalledPods(repository);
    foreach(Pod pod, pods) {
        result = updatePod(repository, pod.name) && result;
    }

    if(!result) {
        emit updateAllPodsFinished(repository, false);
        return false;
    }

    generateQmakeFiles(repository);

    emit updateAllPodsFinished(repository, true);
    return true;
}

QList<Pod> PodManager::listInstalledPods(QString repository) {
    QList<Pod> pods;
    QDir dir(repository);

    // Check if there is a .gitmodules file available
    QString gitmodulesPath = dir.filePath(".gitmodules");
    if(QFile::exists(gitmodulesPath)) {
        // We can use QSettings to read the .gitmodules in INI format
        QSettings gitmodules(gitmodulesPath, QSettings::IniFormat);
        gitmodules.setIniCodec("UTF-8");

        // In git, each submodule has a child entry
        QStringList childGroups = gitmodules.childGroups();
        foreach(QString childGroup, childGroups) {
            if(childGroup.startsWith("submodule")) {
                // If it is a submodule, enter the group
                gitmodules.beginGroup(childGroup);
                Pod pod;
                pod.name        = gitmodules.value("path").toString();
                pod.url         = gitmodules.value("url").toString();

                // Try to access the local pod info file in order to
                // get meta information about the pod
                readPodInfo(repository, pod);

                // Append pod to list
                pods.append(pod);
                gitmodules.endGroup();
            }
        }
    }
    emit listInstalledPodsFinished(repository, pods);
    return pods;
}

QList<Pod> PodManager::listAvailablePods(QStringList sources) {
    if(_networkAccessManager->networkAccessible() == QNetworkAccessManager::NotAccessible) {
#ifdef QT_DEBUG
        qDebug() << "No network connection available.";
#endif
        emit listAvailablePodsFinished(sources, QList<Pod>());
        return QList<Pod>();
    }

    QList<Pod> pods;
    foreach(QString source, sources) {
        QNetworkRequest request;
        request.setUrl(QUrl(source));

        QNetworkReply *reply = _networkAccessManager->get(request);
        waitForReply(reply);

        QByteArray response = reply->readAll();

#ifdef QT_DEBUG
        if(reply->error() != QNetworkReply::NoError) {
            qDebug() << reply->errorString();
        }
#endif
        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(response, &parseError);

        if(QJsonParseError::NoError == parseError.error) {
            QJsonObject object      = document.object();
            QStringList keys        = object.keys();

            foreach(QString key, keys) {
                if(object.value(key).isObject()) {
                    // New format
                    QJsonObject metaInformationObject = object.value(key).toObject();
                    Pod pod;
                    pod.name        = key;
                    pod.url         = metaInformationObject.value("url").toString();
                    pod.author      = metaInformationObject.value("author").toString();
                    pod.description = metaInformationObject.value("description").toString();
                    pod.license     = metaInformationObject.value("license").toString();
                    pods.append(pod);

                } else {
                    // Old format: pod name and url
                    Pod pod;
                    pod.name        = key;
                    pod.url         = object.value(key).toString();
                    pods.append(pod);
                }
            }
        }
    }

    emit listAvailablePodsFinished(sources, pods);
    return pods;
}

void PodManager::generatePodsPri(QString repository) {
    // Get info about all installe pods
    QList<Pod> pods = listInstalledPods(repository);

    // Create a header
    QString header = QString("# Auto-generated by qt-pods. Do not edit.\n# Include this to your application project file with:\n# include(../pods.pri)\n# This file should be put under version control.\n");

    // Accumulate a list of include statements for all pods
    QString includePris = "";
    foreach(Pod pod, pods) {
        includePris += QString("include(%1/%1.pri)\n").arg(pod.name);
    }

    // Combine file contents
    QString podsPri = QString("%1\n%2\n")
        .arg(header)
        .arg(includePris);

    // Write to file
    QFile file(QDir(repository).filePath("pods.pri"));
    file.remove();
    if(file.open(QFile::ReadWrite)) {
        file.write(podsPri.toUtf8());
        file.close();
    }

    // Put under version control
    stageFile(repository, file.fileName());

    emit generatePodsPriFinished(repository);
}

void PodManager::generatePodsSubdirsPri(QString repository) {
    // Get info about all installed pods
    QList<Pod> pods = listInstalledPods(repository);

    // Create a header
    QString header = QString("# Auto-generated by qt-pods. Do not edit.\n# Include this to your subdirs project file with:\n# include(pods-subdirs.pri)\n# This file should be put under version control.\n");

    // Create a SUBDIRS entry that will extend the one provided in the *.pro
    QString subdirs = "SUBDIRS += ";
    foreach(Pod pod, pods) {
        subdirs += QString("\\\n\t%1 ").arg(pod.name);
    }

    // Combine file contents
    QString podsSubdirsPri = QString("%1\n%2\n\n")
        .arg(header)
        .arg(subdirs);

    // Write to file
    QFile file(QDir(repository).filePath("pods-subdirs.pri"));
    file.remove();
    if(file.open(QFile::ReadWrite)) {
        file.write(podsSubdirsPri.toUtf8());
        file.close();
    }

    // Put under version control
    stageFile(repository, file.fileName());

    emit generatePodsSubdirsPriFinished(repository);
}

void PodManager::generateSubdirsPro(QString repository) {
    QDir dir(repository);

    // By convention, the umbrella subdirs project is called the same as the repository name.
    // If the repository does not contain such a file, we are going to create it with default
    // content.
    QFile file(QDir(repository).filePath(QString("%1.pro").arg(dir.dirName())));

    // Just create one if it doesn't exist yet.
    if(!file.exists()) {
        if(file.open(QFile::ReadWrite)) {
            QString subdirsPro = "# Auto-generated by qt-pods.\n# This file should be put under version control.\nTEMPLATE = subdirs\ninclude(pods-subdirs.pri)\nSUBDIRS +=\n";
            file.write(subdirsPro.toUtf8());
            file.close();
        }
    }

    // Whether it has existed or not, put under version control
    stageFile(repository, file.fileName());

    emit generateSubdirsProFinished(repository);
}

bool PodManager::checkPod(QString repository, QString podName) {
    QDir dir(repository);
    bool isValidPod = (podName == podName.toLower()) &&
            dir.cd(podName) &&
            QFile::exists(dir.filePath("LICENSE")) &&
            QFile::exists(dir.filePath("README.md")) &&
            QFile::exists(dir.filePath(podName + ".pri")) &&
            QFile::exists(dir.filePath(podName + ".pro"));
    emit checkPodFinished(repository, podName, isValidPod);
    return isValidPod;
}

bool PodManager::createProject(QString repository) {
    if(!isGitRepository(repository)) {
        int gitInitResult = QProcess::execute(QString("git init \"%1\"").arg(repository));
        if(gitInitResult != 0) {
            emit createProjectFinished(repository, false);
            return false;
        }
    }

    if(!isGitRepository(repository)) {
        emit createProjectFinished(repository, false);
        return false;
    }

    generateQmakeFiles(repository);

    emit createProjectFinished(repository, true);
    return true;
}

void PodManager::makeSureInRepositoryDirectory(QString repository) {
    QDir::setCurrent(repository);
}

bool PodManager::removePodSubmodule(QString repository, QString podName) {
    makeSureInRepositoryDirectory(repository);
    return runCommand(QString("git submodule deinit -f %1").arg(podName)) &&
        runCommand(QString("git rm -rf %1").arg(podName)) &&
        runCommand(QString("rm -rf %1/.git/modules/%2").arg(repository).arg(podName)) &&
        purgePodInfo(repository, podName);
}

bool PodManager::addPodSubmodule(QString repository, Pod pod) {
    makeSureInRepositoryDirectory(repository);
    return runCommand(QString("git submodule add %1 %2").arg(pod.url).arg(pod.name)) &&
        writePodInfo(repository, pod);
}

bool PodManager::updatePodSubmodule(QString repository, QString podName) {
    QDir::setCurrent(QDir(repository).absoluteFilePath(podName));
    bool result = runCommand(QString("git stash")) &&
        runCommand(QString("git checkout master")) &&
        runCommand(QString("git pull"));
    return result;
}

bool PodManager::purgePodInfo(QString repository, QString podName) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    podinfo.remove(podName);
    podinfo.sync();

    return stageFile(repository, ".podinfo");
}

bool PodManager::writePodInfo(QString repository, Pod pod) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    podinfo.beginGroup(pod.name);
        podinfo.setValue("author", pod.author);
        podinfo.setValue("description", pod.description);
        podinfo.setValue("license", pod.license);
        podinfo.setValue("website", pod.website);
        podinfo.endGroup();
    podinfo.sync();

    return stageFile(repository, ".podinfo");
}

void PodManager::readPodInfo(QString repository, Pod& pod) {
    QDir dir(repository);
    QString podinfoPath = dir.filePath(".podinfo");
    QSettings podinfo(podinfoPath, QSettings::IniFormat);
    podinfo.setIniCodec("UTF-8");
    QStringList childGroups = podinfo.childGroups();
    if(childGroups.contains(pod.name)) {
        podinfo.beginGroup(pod.name);
        pod.author = podinfo.value("author").toString();
        pod.description = podinfo.value("description").toString();
        pod.license = podinfo.value("license").toString();
        pod.website = podinfo.value("website").toString();
        podinfo.endGroup();
    }
}

bool PodManager::stageFile(QString repository, QString fileName) {
    makeSureInRepositoryDirectory(repository);
    return runCommand(QString("git add %1").arg(fileName));
}

void PodManager::waitForReply(QNetworkReply *reply) {
    QEventLoop loop;
    connect(reply, SIGNAL(finished()), &loop, SLOT(quit()));
    loop.exec();
}

void PodManager::generateQmakeFiles(QString repository) {
    makeSureInRepositoryDirectory(repository);
    generatePodsPri(repository);
    generatePodsSubdirsPri(repository);
    generateSubdirsPro(repository);
}

bool PodManager::runCommand(QString command) {
    return 0 == QProcess::execute(command);
}

QString PodManager::runCommandAndParse(QString command) {
    QProcess *process = new QProcess;
    process->start(command);
    process->waitForFinished();
    return process->readAllStandardOutput();
}

