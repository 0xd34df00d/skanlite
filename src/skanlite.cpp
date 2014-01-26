/* ============================================================
*
* Copyright (C) 2007-2012 by Kåre Särs <kare.sars@iki .fi>
* Copyright (C) 2009 by Arseniy Lartsev <receive-spam at yandex dot ru>
* Copyright (C) 2014 by Gregor Mitsch: port to KDE5 frameworks
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of
* the License or (at your option) version 3 or any later version
* accepted by the membership of KDE e.V. (or its successor approved
*  by the membership of KDE e.V.), which shall act as a proxy
* defined in Section 14 of version 3 of the license.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License.
* along with this program.  If not, see <http://www.gnu.org/licenses/>
*
* ============================================================ */

#include "skanlite.h"

#include "KSaneImageSaver.h"
#include "SaveLocation.h"

#include <QApplication>
#include <QScrollArea>
#include <QStringList>
#include <QFileDialog>
#include <QUrl>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QMessageBox>
#include <QTemporaryFile>
#include <QDebug>
#include <QImageWriter>
#include <QMimeType>
#include <QMimeDatabase>

#include <KAboutApplicationDialog>
#include <KLocalizedString>
#include <KMessageBox>
// #include <kio/netaccess.h> // FIXME KF5: see /usr/include/kio/netaccess.h
#include <KIO/StatJob>
#include <kio/global.h>
#include <KSharedConfig>
#include <KConfigGroup>
#include <KHelpClient>

#include <errno.h>

Skanlite::Skanlite(const QString& device, QWidget* parent)
    : QDialog(parent)
    , m_aboutData(nullptr)
{  
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    
    QDialogButtonBox* dlgButtonBoxBottom = new QDialogButtonBox(this);
    dlgButtonBoxBottom->setStandardButtons(QDialogButtonBox::Help | QDialogButtonBox::Close);
    // was "User2:
    QPushButton* btnAbout = dlgButtonBoxBottom->addButton(i18n("About"), QDialogButtonBox::ButtonRole::ActionRole);
    // was "User1":
    QPushButton* btnSettings = dlgButtonBoxBottom->addButton(i18n("Settings"), QDialogButtonBox::ButtonRole::ActionRole);
    btnSettings->setIcon(QIcon::fromTheme("configure"));

    m_firstImage = true;

    m_ksanew = new KSaneIface::KSaneWidget(this);
    connect(m_ksanew, SIGNAL(imageReady(QByteArray &, int, int, int, int)),
            this,     SLOT(imageReady(QByteArray &, int, int, int, int)));
    connect(m_ksanew, SIGNAL(availableDevices(QList<KSaneWidget::DeviceInfo>)),
            this,     SLOT(availableDevices(QList<KSaneWidget::DeviceInfo>)));
    connect(m_ksanew, SIGNAL(userMessage(int, QString)),
            this,     SLOT(alertUser(int, QString)));
    connect(m_ksanew, SIGNAL(buttonPressed(QString, QString, bool)),
            this,     SLOT(buttonPressed(QString, QString, bool)));

    mainLayout->addWidget(m_ksanew);
    mainLayout->addWidget(dlgButtonBoxBottom);

    m_ksanew->initGetDeviceList();

    // read the size here...
    KConfigGroup window(KSharedConfig::openConfig(), "Window");
    QSize rect = window.readEntry("Geometry", QSize(740,400));
    resize(rect);

    connect(dlgButtonBoxBottom, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(this, &QDialog::finished, this, &Skanlite::saveWindowSize);
    connect(this, &QDialog::finished, this, &Skanlite::saveScannerOptions);
    connect(btnSettings, &QPushButton::clicked, this, &Skanlite::showSettingsDialog);
    connect(btnAbout, &QPushButton::clicked, this, &Skanlite::showAboutDialog);
    connect(dlgButtonBoxBottom, &QDialogButtonBox::helpRequested, this, &Skanlite::showHelp);

    //
    // Create the settings dialog
    //
    {
        m_settingsDialog = new QDialog(this);
                
        QVBoxLayout *mainLayout = new QVBoxLayout(m_settingsDialog);

        QWidget *settingsWidget = new QWidget(m_settingsDialog);
        m_settingsUi.setupUi(settingsWidget);
        m_settingsUi.revertOptions->setIcon(QIcon::fromTheme("edit-undo"));
        m_saveLocation = new SaveLocation(this);

        // add the supported image types
        const QList<QByteArray> tmpList = QImageWriter::supportedMimeTypes();
        m_filterList.clear();
        foreach (auto ba, tmpList)
        {
            m_filterList.append(QString::fromLatin1(ba));
        }

        qDebug() << m_filterList;
        
        // Put first class citizens at first place
        m_filterList.removeAll("image/jpeg");
        m_filterList.removeAll("image/tiff");
        m_filterList.removeAll("image/png");
        m_filterList.insert(0, "image/png");
        m_filterList.insert(1, "image/jpeg");
        m_filterList.insert(2, "image/tiff");

        m_filter16BitList << "image/png";
        //m_filter16BitList << "image/tiff";

        // fill m_filterList (...) and m_typeList (list of file suffixes)
        foreach (QString mimeStr, m_filterList) {
            QMimeType mimeType = QMimeDatabase().mimeTypeForName(mimeStr);
            m_filterList.append(mimeType.name());

            QStringList fileSuffixes = mimeType.suffixes();

            if (fileSuffixes.size() > 0) {
                m_typeList << fileSuffixes.first();
            }
        }

        m_settingsUi.imgFormat->addItems(m_typeList);
        m_saveLocation->u_imgFormat->addItems(m_typeList);

        mainLayout->addWidget(settingsWidget);
        
        QDialogButtonBox* dlgButtonBoxBottom = new QDialogButtonBox(this);
        dlgButtonBoxBottom->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Close);
        connect(dlgButtonBoxBottom, &QDialogButtonBox::accepted, m_settingsDialog, &QDialog::accept);
        connect(dlgButtonBoxBottom, &QDialogButtonBox::rejected, m_settingsDialog, &QDialog::reject);
        
        mainLayout->addWidget(dlgButtonBoxBottom);

        m_settingsDialog->setWindowTitle(i18n("Skanlite Settings"));

        connect(m_settingsUi.getDirButton, &QPushButton::clicked, this, &Skanlite::getDir);
        connect(m_settingsUi.revertOptions, &QPushButton::clicked, this, &Skanlite::defaultScannerOptions);
        readSettings();

        // default directory for the save dialog
        m_saveLocation->u_urlRequester->setUrl(m_settingsUi.saveDirLEdit->text());
        m_saveLocation->u_imgPrefix->setText(m_settingsUi.imgPrefix->text());
        m_saveLocation->u_imgFormat->setCurrentText(m_settingsUi.imgFormat->currentText());
    }

    // open the scan device
    if (m_ksanew->openDevice(device) == false) {
        QString dev = m_ksanew->selectDevice(0);
        if (dev.isEmpty()) {
            // either no scanner was found or then cancel was pressed.
            exit(0);
        }
        if (m_ksanew->openDevice(dev) == false) {
            // could not open a scanner
            KMessageBox::sorry(0, i18n("Opening the selected scanner failed."));
            exit(1);
        }
        else {
            setWindowTitle(i18nc("@title:window %1 = scanner maker, %2 = scanner model", "%1 %2 - Skanlite", m_ksanew->make(), m_ksanew->model()));
            m_deviceName = QString("%1:%2").arg(m_ksanew->make()).arg(m_ksanew->model());
        }
    }
    else {
        setWindowTitle(i18nc("@title:window %1 = scanner device", "%1 - Skanlite", device));
        m_deviceName = device;
    }

    // prepare the Show Image Dialog 
    {
        m_showImgDialog = new QDialog(this);
        
        QVBoxLayout *mainLayout = new QVBoxLayout(m_showImgDialog);
        
        QDialogButtonBox* dlgBtnBoxBottom = new QDialogButtonBox(m_showImgDialog);
        // "Close" (now Discard) and "User1"=Save
        dlgBtnBoxBottom->setStandardButtons(QDialogButtonBox::Discard | QDialogButtonBox::Save);

        mainLayout->addWidget(&m_imageViewer);
        mainLayout->addWidget(dlgBtnBoxBottom);
        
        m_showImgDialogSaveButton = dlgBtnBoxBottom->button(QDialogButtonBox::Save);
        m_showImgDialogSaveButton->setDefault(true); // still needed?
        
        m_showImgDialog->resize(640, 480);
        connect(dlgBtnBoxBottom, SIGNAL(accepted()), this, SLOT(saveImage()));
        connect(dlgBtnBoxBottom, SIGNAL(accepted()), m_showImgDialog, SLOT(accept()));
        connect(dlgBtnBoxBottom->button(QDialogButtonBox::Discard),
                                        SIGNAL(clicked()), m_showImgDialog, SLOT(reject()));
    }    
    

    // save the default sane options for later use
    m_ksanew->getOptVals(m_defaultScanOpts);

    // load saved options
    loadScannerOptions();

    m_ksanew->initGetDeviceList();

    m_firstImage = true;
}

void Skanlite::showHelp()
{
    KHelpClient::invokeHelp("index", "skanlite");
}

void Skanlite::setAboutData(KAboutData* aboutData)
{
    m_aboutData = aboutData;
}

void Skanlite::closeEvent(QCloseEvent *event)
{
    saveWindowSize();
    saveScannerOptions();
    event->accept();
}

void Skanlite::saveWindowSize()
{
    KConfigGroup window(KSharedConfig::openConfig(), "Window");
    window.writeEntry("Geometry", size());
    window.sync();
}

// Pops up message box similar to what perror() would print
//************************************************************
static void perrorMessageBox(const QString &text)
{
    if (errno != 0) {
        KMessageBox::sorry(0, i18n("%1: %2", text, QString::fromLocal8Bit(strerror(errno))));
    }
    else {
        KMessageBox::sorry(0, text);
    }
}

void Skanlite::readSettings(void)
{
    // enable the widgets to allow modifying
    m_settingsUi.setQuality->setChecked(true);
    m_settingsUi.setPreviewDPI->setChecked(true);

    // read the saved parameters
    KConfigGroup saving(KSharedConfig::openConfig(), "Image Saving");
    m_settingsUi.saveModeCB->setCurrentIndex(saving.readEntry("SaveMode", (int)SaveModeManual));
    if (m_settingsUi.saveModeCB->currentIndex() != SaveModeAskFirst) m_firstImage = false;
    m_settingsUi.saveDirLEdit->setText(saving.readEntry("Location", QDir::homePath()));
    m_settingsUi.imgPrefix->setText(saving.readEntry("NamePrefix", i18nc("prefix for auto naming", "Image-")));
    m_settingsUi.imgFormat->setCurrentText(saving.readEntry("ImgFormat", "png"));
    m_settingsUi.imgQuality->setValue(saving.readEntry("ImgQuality", 90));
    m_settingsUi.setQuality->setChecked(saving.readEntry("SetQuality", false));
    m_settingsUi.showB4Save->setChecked(saving.readEntry("ShowBeforeSave", true));

    KConfigGroup general(KSharedConfig::openConfig(), "General");
    
    //m_settingsUi.previewDPI->setCurrentItem(general.readEntry("PreviewDPI", "100"), true); // FIXME KF5 is the 'true' parameter still needed?
    m_settingsUi.previewDPI->setCurrentText(general.readEntry("PreviewDPI", "100"));
    
    m_settingsUi.setPreviewDPI->setChecked(general.readEntry("SetPreviewDPI", false));
    if (m_settingsUi.setPreviewDPI->isChecked()) {
        m_ksanew->setPreviewResolution(m_settingsUi.previewDPI->currentText().toFloat());
    }
    else {
        m_ksanew->setPreviewResolution(0.0);
    }
    m_settingsUi.u_disableSelections->setChecked(general.readEntry("DisableAutoSelection", false));
    m_ksanew->enableAutoSelect(!m_settingsUi.u_disableSelections->isChecked());
}

void Skanlite::showSettingsDialog(void)
{
    readSettings();

    // show the dialog
    if (m_settingsDialog->exec()) {
        // save the settings
        KConfigGroup saving(KSharedConfig::openConfig(), "Image Saving");
        saving.writeEntry("SaveMode", m_settingsUi.saveModeCB->currentIndex());
        saving.writeEntry("Location", m_settingsUi.saveDirLEdit->text());
        saving.writeEntry("NamePrefix", m_settingsUi.imgPrefix->text());
        saving.writeEntry("ImgFormat", m_settingsUi.imgFormat->currentText());
        saving.writeEntry("SetQuality", m_settingsUi.setQuality->isChecked());
        saving.writeEntry("ImgQuality", m_settingsUi.imgQuality->value());
        saving.writeEntry("ShowBeforeSave", m_settingsUi.showB4Save->isChecked());
        saving.sync();

        KConfigGroup general(KSharedConfig::openConfig(), "General");
        general.writeEntry("PreviewDPI", m_settingsUi.previewDPI->currentText());
        general.writeEntry("SetPreviewDPI", m_settingsUi.setPreviewDPI->isChecked());
        general.writeEntry("DisableAutoSelection", m_settingsUi.u_disableSelections->isChecked());
        general.sync();

        // the previewDPI has to be set here
        if (m_settingsUi.setPreviewDPI->isChecked()) {
            m_ksanew->setPreviewResolution(m_settingsUi.previewDPI->currentText().toFloat());
        }
        else {
            // 0.0 means default value.
            m_ksanew->setPreviewResolution(0.0);
        }
        m_ksanew->enableAutoSelect(!m_settingsUi.u_disableSelections->isChecked());

        // pressing OK in the settings dialog means use those settings.
        m_saveLocation->u_urlRequester->setUrl(m_settingsUi.saveDirLEdit->text());
        m_saveLocation->u_imgPrefix->setText(m_settingsUi.imgPrefix->text());
        m_saveLocation->u_imgFormat->setCurrentText(m_settingsUi.imgFormat->currentText());

        m_firstImage = true;
    }
    else {
        //Forget Changes
        readSettings();
    }
}

void Skanlite::imageReady(QByteArray &data, int w, int h, int bpl, int f)
{
    // save the image data
    m_data = data;
    m_width = w;
    m_height = h;
    m_bytesPerLine = bpl;
    m_format = f;

    if (m_settingsUi.showB4Save->isChecked() == true) {
        /* copy the image data into m_img and show it*/
        m_img = m_ksanew->toQImageSilent(data, w, h, bpl, (KSaneIface::KSaneWidget::ImageFormat)f);
        m_imageViewer.setQImage(&m_img);
        m_imageViewer.zoom2Fit();
        m_showImgDialogSaveButton->setFocus();
        m_showImgDialog->exec();
        // save has been done as a result of save or then we got cancel
    }
    else {
        m_img = QImage(); // clear the image to ensure we save the correct one.
        saveImage();
    }
}

void Skanlite::saveImage()
{
    qDebug() << "saveImage()";
    
    // ask the first time if we are in "ask on first" mode
    if ((m_settingsUi.saveModeCB->currentIndex() == SaveModeAskFirst) && m_firstImage) {
        if (m_saveLocation->exec() != QFileDialog::Accepted) return;
        m_firstImage = false;
    }

    QString dir = m_saveLocation->u_urlRequester->url().toLocalFile();
    QString prefix = m_saveLocation->u_imgPrefix->text();
    QString imgFormat = m_saveLocation->u_imgFormat->currentText().toLower();
    int fileNumber = m_saveLocation->u_numStartFrom->value();
    QStringList filterList = m_filterList;
    if ((m_format==KSaneIface::KSaneWidget::FormatRGB_16_C) ||
        (m_format==KSaneIface::KSaneWidget::FormatGrayScale16))
    {
        filterList = m_filter16BitList;
        if (imgFormat != "png") {
            imgFormat = "png";
            KMessageBox::information(this, i18n("The image will be saved in the PNG format, as Skanlite only supports saving 16 bit color images in the PNG format."));
        }
    }

    // find next available file name for name suggestion
    QUrl fileUrl;
    QString fname;
    for (int i=fileNumber; i<=m_saveLocation->u_numStartFrom->maximum(); ++i) {
        fname = QString("%1%2.%3")
        .arg(prefix)
        .arg(i, 4, 10, QChar('0'))
        .arg(imgFormat);

        fileUrl = QUrl(QString("%1/%2").arg(dir).arg(fname));
        if (fileUrl.isLocalFile()) {
            if (!QFileInfo(fileUrl.toLocalFile()).exists()) {
                break;
            }
        }
        else {
            //if (!KIO::NetAccess::exists(fileUrl, true, this)) { // FIXME KF5 ok?
            KIO::StatJob* statJob = KIO::stat(fileUrl);
            if (!statJob->exec()) {
                break;
            }
            else {
                //statJob-> // FIXME KF5 TODO: determine if stat was ok, if not then break
                //break;
            }
        }
    }

    if (m_settingsUi.saveModeCB->currentIndex() == SaveModeManual) {
        // prepare the save dialog
        QFileDialog saveDialog(this, i18n("New Image File Name"), m_settingsUi.saveDirLEdit->text());
        saveDialog.setAcceptMode(QFileDialog::AcceptSave);
        saveDialog.setFileMode(QFileDialog::AnyFile);
        
        // ask for a filename if requested.
        qDebug() << fileUrl.url();
        // qDebug() <<  fileUrl.toLocalFile(); // returns ""
        saveDialog.selectFile(fileUrl.url());
        
        QStringList actualFilterList = filterList;
        QString currentMimeFilter = "image/" + imgFormat;
        saveDialog.setMimeTypeFilters(actualFilterList);

        // FIXME KF5 / WAIT: probably due to a bug in QFileDialog integration the desired file type filter will not be selected (it defaults to the first one: png)
        // saveDialog.selectMimeTypeFilter(currentMimeFilter); // does not work
        // saveDialog.selectNameFilter("*." + imgFormat); // does not work either

        do {            
            if (saveDialog.exec() != QFileDialog::Accepted) return;

            Q_ASSERT(!saveDialog.selectedUrls().isEmpty());
            fileUrl = saveDialog.selectedUrls().first();
            //kDebug() << "-----Save-----" << fname;

            //if (KIO::NetAccess::exists(fileUrl, true, this)) { // FIXME KF5
            if (false) {
            
                if (KMessageBox::warningContinueCancel(this,
                    i18n("Do you want to overwrite \"%1\"?", fileUrl.fileName()),
                     QString(),
                     KGuiItem(i18n("Overwrite")),
                     KStandardGuiItem::cancel(),
                     QString("editorWindowSaveOverwrite")
                     ) ==  KMessageBox::Continue)
                     {
                         break;
                     }
            }
            else {
                break;
            }
        } while (true);
    }

    m_firstImage = false;

    // Get the quality
    int quality = -1;
    if (m_settingsUi.setQuality->isChecked()) {
        quality = m_settingsUi.imgQuality->value();
    }

    QFileInfo fileInfo(fileUrl.path());

    //kDebug() << "suffix" << fileInfo.suffix() << "localFile" << fileUrl.pathOrUrl();
    fname = fileUrl.path();
    QTemporaryFile tmp;
    if (!fileUrl.isLocalFile()) {
        // tmp.setSuffix('.'+fileInfo.suffix()); // FIXME KF5 needed?
        tmp.open();
        fname = tmp.fileName();
        tmp.close(); // we just want the filename
    }

    // Save
    if ((m_format==KSaneIface::KSaneWidget::FormatRGB_16_C) ||
        (m_format==KSaneIface::KSaneWidget::FormatGrayScale16))
    {
        KSaneImageSaver saver;
        if (saver.savePngSync(fname, m_data, m_width, m_height, m_format)) {
            m_showImgDialog->close(); // closing the window if it is closed should not be a problem.
        }
        else {
            perrorMessageBox(i18n("Failed to save image"));
        }
    }
    else  {
        // create the image if needed.
        if (m_img.width() < 1) {
            m_img = m_ksanew->toQImage(m_data, m_width, m_height, m_bytesPerLine, (KSaneIface::KSaneWidget::ImageFormat)m_format);
        }
        if (m_img.save(fname, 0, quality)) {
            m_showImgDialog->close(); // calling close() on a closed window does nothing.
        }
        else {
            perrorMessageBox(i18n("Failed to save image"));
        }
    }

    if (!fileUrl.isLocalFile()) {
        
        // FIXME KF5:
//         if (!KIO::NetAccess::upload( fname, fileUrl, this )) {
//             KMessageBox::sorry(0, i18n("Failed to upload image"));
//         }
        
    }

    // Save the file base name without number
    QString baseName = fileInfo.completeBaseName();
    while ((baseName.size() > 1) && (baseName[baseName.size()-1].isNumber())) {
        baseName.remove(baseName.size()-1, 1);
    }
    m_saveLocation->u_imgPrefix->setText(baseName);

    // Save the number
    QString fileNumStr = fileInfo.completeBaseName();
    fileNumStr.remove(baseName);
    fileNumber = fileNumStr.toInt();
    if (fileNumber) {
        m_saveLocation->u_numStartFrom->setValue(fileNumber+1);
    }

    if (m_settingsUi.saveModeCB->currentIndex() == SaveModeManual) {
        // Save last used dir, prefix and suffix.
        m_saveLocation->u_urlRequester->setUrl(KIO::upUrl(fileUrl).path());
        m_saveLocation->u_imgFormat->setCurrentText(fileInfo.suffix());
    }
}

void Skanlite::getDir(void)
{
    // FIXME KF5 / WAIT: this is not working yet due to a bug in frameworkintegration:
    // see commit: 2c1ee08a21a1f16f9c2523718224598de8fc0d4f for kf5/src/frameworks/frameworkintegration/tests/qfiledialogtest.cpp
    QString dir = QFileDialog::getExistingDirectory(m_settingsDialog, QString(), m_settingsUi.saveDirLEdit->text());
    if (!dir.isEmpty()) {
        m_settingsUi.saveDirLEdit->setText(dir);
    }
}

void Skanlite::showAboutDialog(void)
{
    KAboutApplicationDialog(*m_aboutData).exec();
}

void Skanlite::saveScannerOptions()
{
    KConfigGroup saving(KSharedConfig::openConfig(), "Image Saving");
    saving.writeEntry("NumberStartsFrom", m_saveLocation->u_numStartFrom->value());

    if (!m_ksanew) return;

    KConfigGroup options(KSharedConfig::openConfig(), QString("Options For %1").arg(m_deviceName));
    QMap <QString, QString> opts;
    m_ksanew->getOptVals(opts);
    QMap<QString, QString>::const_iterator it = opts.constBegin();
    while (it != opts.constEnd()) {
        options.writeEntry(it.key(), it.value());
        ++it;
    }
    options.sync();
}

void Skanlite::defaultScannerOptions()
{
    if (!m_ksanew) return;

    m_ksanew->setOptVals(m_defaultScanOpts);
}

void Skanlite::loadScannerOptions()
{
    KConfigGroup saving(KSharedConfig::openConfig(), "Image Saving");
    m_saveLocation->u_numStartFrom->setValue(saving.readEntry("NumberStartsFrom", 1));

    if (!m_ksanew) return;

    KConfigGroup scannerOptions(KSharedConfig::openConfig(), QString("Options For %1").arg(m_deviceName));
    m_ksanew->setOptVals(scannerOptions.entryMap());
}

void Skanlite::availableDevices(const QList<KSaneWidget::DeviceInfo> &deviceList)
{
    for (int i=0; i<deviceList.size(); i++) {
        qDebug() << deviceList[i].name;
    }
}

void Skanlite::alertUser(int type, const QString &strStatus)
{
    switch (type) {
        case KSaneWidget::ErrorGeneral:
            KMessageBox::sorry(0, strStatus, "Skanlite Test");
            break;
        default:
            KMessageBox::information(0, strStatus, "Skanlite Test");
    }
}

void Skanlite::buttonPressed(const QString &optionName, const QString &optionLabel, bool pressed)
{
    qDebug() << "Button" << optionName << optionLabel << ((pressed) ? "pressed" : "released");
}
