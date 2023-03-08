// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2017-2019 Alejandro Sirgo Rica & Contributors

#include "imguruploader.h"
#include "src/utils/confighandler.h"
#include "src/utils/filenamehandler.h"
#include "src/utils/history.h"
#include "src/widgets/loadspinner.h"
#include "src/widgets/notificationwidget.h"
#include <QBuffer>
#include <QDesktopServices>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QShortcut>
#include <QUrlQuery>

ImgurUploader::ImgurUploader(const QPixmap& capture, QWidget* parent)
  : ImgUploaderBase(capture, parent)
{
    m_NetworkAM = new QNetworkAccessManager(this);
    connect(m_NetworkAM,
            &QNetworkAccessManager::finished,
            this,
            &ImgurUploader::handleReply);
}

void ImgurUploader::handleReply(QNetworkReply* reply)
{
    spinner()->deleteLater();
    m_currentImageName.clear();
    if (reply->error() == QNetworkReply::NoError) {
        QJsonDocument response = QJsonDocument::fromJson(reply->readAll());
        QJsonObject json = response.object();
        QJsonObject data = json[QStringLiteral("data")].toObject();
        setImageURL(data[QStringLiteral("link")].toString());

        auto deleteToken = data[QStringLiteral("deletehash")].toString();

        // save history
        m_currentImageName = imageURL().toString();
        int lastSlash = m_currentImageName.lastIndexOf("/");
        if (lastSlash >= 0) {
            m_currentImageName = m_currentImageName.mid(lastSlash + 1);
        }

        // save image to history
        History history;
        m_currentImageName =
          history.packFileName("imgur", deleteToken, m_currentImageName);
        history.save(pixmap(), m_currentImageName);

        emit uploadOk(imageURL());
    } else {
        setInfoLabelText(reply->errorString());
    }
    new QShortcut(Qt::Key_Escape, this, SLOT(close()));
}

void ImgurUploader::upload()
{
    // this code is taken from seamus-45's proposed custom upload solution from 2020 which was closed in favour of a plugin system:
    // https://github.com/flameshot-org/flameshot/pull/688/files
    // i've just ported this over to the newest version of flameshot
    QByteArray byteArray;
    QBuffer buffer(&byteArray);
    pixmap().save(&buffer, "PNG");

    QHttpMultiPart* multiPart =
      new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart titlePart;
    titlePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"title\""));
    titlePart.setBody("flameshot_screenshot");

    QHttpPart descPart;
    QString desc = FileNameHandler().parsedPattern();
    descPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"description\""));
    descPart.setBody(desc.toLatin1());

    QHttpPart imagePart;
    imagePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    imagePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant("form-data; name=\"image\"; filename=\"" + desc.toLatin1() + "\""));
    imagePart.setBody(byteArray);

    multiPart->append(titlePart);
    multiPart->append(descPart);
    multiPart->append(imagePart);

    QUrl uploadUrl = ConfigHandler().customUploadUrl();
    QString uploadSecret = ConfigHandler().customUploadSecret();

    QNetworkRequest request(uploadUrl);
    request.setRawHeader("Authorization", QStringLiteral("%1").arg(uploadSecret).toUtf8());

    m_NetworkAM->post(request, multiPart);
}

void ImgurUploader::deleteImage(const QString& fileName,
                                const QString& deleteToken)
{
    Q_UNUSED(fileName)
    bool successful = QDesktopServices::openUrl(
      QUrl(QStringLiteral("https://imgur.com/delete/%1").arg(deleteToken)));
    if (!successful) {
        notification()->showMessage(tr("Unable to open the URL."));
    }

    emit deleteOk();
}
