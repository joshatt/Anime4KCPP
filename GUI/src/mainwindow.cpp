#include "mainwindow.h"
#include "ui_mainwindow.h"

#ifdef _WIN32
#include <clocale>
#endif

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
#ifdef _WIN32
    std::setlocale(LC_CTYPE, ".UTF-8");
#endif
    //initialize translator
    translator = new QTranslator(this);
    languageSelector["en"] = Language::en;
    languageSelector["zh_CN"] = Language::zh_CN;
    languageSelector["zh_TW"] = Language::zh_TW;
    languageSelector["ja_JP"] = Language::ja_JP;
    languageSelector["fr_FR"] = Language::fr_FR;
    //initialize codec
    codecSelector["mp4v"] = Anime4KCPP::CODEC::MP4V;
    codecSelector["dxva"] = Anime4KCPP::CODEC::DXVA;
    codecSelector["avc1"] = Anime4KCPP::CODEC::AVC1;
    codecSelector["vp09"] = Anime4KCPP::CODEC::VP09;
    codecSelector["hevc"] = Anime4KCPP::CODEC::HEVC;
    codecSelector["av01"] = Anime4KCPP::CODEC::AV01;
    codecSelector["other"] = Anime4KCPP::CODEC::OTHER;
    //initialize textBrowser
    ui->fontComboBox->setFont(QFont("Consolas"));
    ui->fontComboBox->setCurrentFont(ui->fontComboBox->font());
    ui->spinBoxFontSize->setRange(9, 30);
    ui->spinBoxFontSize->setValue(9);
    initTextBrowser();
    //accept drops
    this->setAcceptDrops(true);
    //initialize tableView
    tableModel = new QStandardItemModel(this);
    tableModel->setColumnCount(5);
    tableModel->setHorizontalHeaderLabels({ "Input file",
                                           "Output file",
                                           "Full path",
                                           "Output path",
                                           "State" });
    ui->tableViewProcessingList->setModel(tableModel);
    //initialize processBar
    ui->progressBarProcessingList->reset();
    ui->progressBarProcessingList->setRange(0, 100);
    ui->progressBarProcessingList->setEnabled(false);
    ui->progressBarCurrentTask->reset();
    ui->progressBarCurrentTask->setRange(0, 100);
    //initialize arguments
    ui->spinBoxThreads->setMinimum(1);
    ui->spinBoxHDNLevel->setRange(1, 3);
    ui->doubleSpinBoxPushColorStrength->setRange(0.0, 1.0);
    ui->doubleSpinBoxPushGradientStrength->setRange(0.0, 1.0);
    ui->doubleSpinBoxZoomFactor->setRange(1.0, 10.0);
    ui->spinBoxOpenCLQueueNum->setMinimum(1);
    //initialize time and count
    totalTaskCount = totalProcessingTime = imageCount = videoCount = 0;
    //initialize config
    config = new QSettings("settings.ini", QSettings::IniFormat, this);
    readConfig(config);
    //initialize ffmpeg
    if (ui->actionCheck_FFmpeg->isChecked())
        foundFFmpegFlag = checkFFmpeg();
    //initialize GPU
    GPUState = GPUMode::UNINITIALZED;
    ui->spinBoxPlatformID->setMinimum(0);
    ui->spinBoxDeviceID->setMinimum(0);

    ui->pushButtonReleaseGPU->setEnabled(false);
    ui->pushButtonForceStop->setEnabled(false);
    ui->pushButtonPause->setEnabled(false);
    ui->pushButtonContinue->setEnabled(false);

    if (!ui->checkBoxACNet->isChecked())
    {
        ui->checkBoxHDN->setEnabled(false);
        ui->spinBoxHDNLevel->setEnabled(false);
    }
    //stop flag
    stop = false;
    pause = ProcessingState::NORMAL;
    //Register
    qRegisterMetaType<std::string>("std::string");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!ui->actionQuit_confirmation->isChecked())
    {
        writeConfig(config);
        event->accept();
        return;
    }

    if (QMessageBox::Yes == QMessageBox::warning(this, tr("Confirm"),
        tr("Do you really want to exit?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No))
    {
        writeConfig(config);
        event->accept();
        return;
    }

    event->ignore();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
        return;

    QStringList files;

    for (QUrl& url : urls)
    {
        QString file = url.toLocalFile();
        if (!files.contains(file))
            files.append(file);
    }

    QStandardItem* inputFile;
    QStandardItem* outputFile;
    QStandardItem* inputPath;
    QStandardItem* outputPath;
    QStandardItem* state;

    for (QString& file : files)
    {
        QFileInfo fileInfo(file);

        FileType type = fileType(fileInfo);

        if (type == FileType::BAD_TYPE)
        {
            errorHandler(ErrorType::BAD_TYPE);
            continue;
        }

        inputFile = new QStandardItem(fileInfo.fileName());
        if (type == FileType::VIDEO)
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getVideoOutputSuffix());
        else if (type == FileType::GIF)
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + ".gif");
        else if (getImageOutputSuffix().isEmpty())
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.fileName());
        else
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getImageOutputSuffix());
        inputPath = new QStandardItem(fileInfo.filePath());
        state = new QStandardItem(tr("ready"));
        if (ui->lineEditOutputPath->text().isEmpty())
            outputPath = new QStandardItem(QDir::currentPath());
        else
            outputPath = new QStandardItem(ui->lineEditOutputPath->text());
        tableModel->appendRow({ inputFile,outputFile,inputPath,outputPath,state });

        totalTaskCount++;
    }

    ui->labelTotalTaskCount->setText(QString("Total: %1 ").arg(totalTaskCount));
}

void MainWindow::readConfig(const QSettings* conf)
{
    QString language = conf->value("/GUI/language", "en").toString();
    bool quitConfirmatiom = conf->value("/GUI/quitConfirmatiom", true).toBool();
    bool checkFFmpegOnStart = conf->value("/GUI/checkFFmpeg", true).toBool();
    ffmpegPath = conf->value("/GUI/ffmpegPath", "ffmpeg").toString();

    QString imageSuffix = conf->value("/Suffix/image", "png:jpg:jpeg:bmp").toString();
    QString videoSuffix = conf->value("/Suffix/video", "mp4:mkv:avi:m4v:flv:3gp:wmv:mov:gif").toString();
    QString imageOutputSuffix = conf->value("/Suffix/imageOutput", "").toString();
    QString videoOutputSuffix = conf->value("/Suffix/videoOutput", "").toString();
    QString outputPath = conf->value("/Output/path", QApplication::applicationDirPath() + "/output").toString();
    QString outputPrefix = conf->value("/Output/perfix", "output_anime4kcpp_").toString();

    int passes = conf->value("/Arguments/passes", 2).toInt();
    int pushColorCount = conf->value("/Arguments/pushColorCount", 2).toInt();
    double pushColorStrength = conf->value("/Arguments/pushColorStrength", 0.3).toDouble();
    double pushGradientStrength = conf->value("/Arguments/pushGradientStrength", 1.0).toDouble();
    double zoomFactor = conf->value("/Arguments/zoomFactor", 2.0).toDouble();
    unsigned int threads = conf->value("/Arguments/threads", std::thread::hardware_concurrency()).toUInt();
    bool fastMode = conf->value("/Arguments/fastMode", false).toBool();
    int codec = conf->value("/Arguments/codec", 0).toInt();
    double fps = conf->value("/Arguments/fps", 0.0).toDouble();
    unsigned int pID = conf->value("/Arguments/pID", 0).toUInt();
    unsigned int dID = conf->value("/Arguments/dID", 0).toUInt();
    int OpenCLQueueNum = conf->value("/Arguments/OpenCLQueueNumber", 1).toInt();
    bool OpenCLParallelIO = conf->value("/Arguments/OpenCLParallelIO", false).toBool();
    bool alphaChannel = conf->value("/Arguments/alphaChannel", false).toBool();

    bool ACNet = conf->value("/ACNet/ACNet", false).toBool();
    bool HDN = conf->value("/ACNet/HDN", false).toBool();
    int HDNLevel = conf->value("/ACNet/HDNLevel", 1).toInt();

    bool enablePreprocessing = conf->value("/Preprocessing/enable", true).toBool();
    bool preMedian = conf->value("/Preprocessing/MedianBlur", false).toBool();
    bool preMean = conf->value("/Preprocessing/MeanBlur", false).toBool();
    bool preCAS = conf->value("/Preprocessing/CASSharpening", true).toBool();
    bool preGaussianWeak = conf->value("/Preprocessing/GaussianBlurWeak", false).toBool();
    bool preGaussian = conf->value("/Preprocessing/GaussianBlur", false).toBool();
    bool preBilateral = conf->value("/Preprocessing/BilateralFilter", false).toBool();
    bool preBilateralFaster = conf->value("/Preprocessing/BilateralFilterFaster", false).toBool();

    bool enablePostprocessing = conf->value("/Postprocessing/enable", true).toBool();
    bool postMedian = conf->value("/Postprocessing/MedianBlur", false).toBool();
    bool postMean = conf->value("/Postprocessing/MeanBlur", false).toBool();
    bool postCAS = conf->value("/Postprocessing/CASSharpening", false).toBool();
    bool postGaussianWeak = conf->value("/Postprocessing/GaussianBlurWeak", true).toBool();
    bool postGaussian = conf->value("/Postprocessing/GaussianBlur", false).toBool();
    bool postBilateral = conf->value("/Postprocessing/BilateralFilter", true).toBool();
    bool postBilateralFaster = conf->value("/Postprocessing/BilateralFilterFaster", false).toBool();

    //GUI options
    //set language
    switch (getLanguageValue(language))
    {
    case Language::en:
        on_actionEnglish_triggered();
        break;
    case Language::zh_CN:
        on_actionSimplifiedChinese_triggered();
        break;
    case Language::zh_TW:
        on_actionTraditionalChinese_triggered();
        break;
    case Language::ja_JP:
        on_actionJapanese_triggered();
        break;
    case Language::fr_FR:
        on_actionFrench_triggered();
        break;
    }
    ui->actionQuit_confirmation->setChecked(quitConfirmatiom);
    ui->actionCheck_FFmpeg->setChecked(checkFFmpegOnStart);
    //suffix
    ui->lineEditImageSuffix->setText(imageSuffix);
    ui->lineEditVideoSuffix->setText(videoSuffix);
    ui->lineEditImageOutputSuffix->setText(imageOutputSuffix);
    ui->lineEditVideoOutputSuffix->setText(videoOutputSuffix);
    //output
    ui->lineEditOutputPath->setText(outputPath);
    ui->lineEditOutputPrefix->setText(outputPrefix);
    //arguments
    ui->spinBoxPasses->setValue(passes);
    ui->spinBoxPushColorCount->setValue(pushColorCount);
    ui->doubleSpinBoxPushColorStrength->setValue(pushColorStrength);
    ui->doubleSpinBoxPushGradientStrength->setValue(pushGradientStrength);
    ui->doubleSpinBoxZoomFactor->setValue(zoomFactor);
    ui->spinBoxThreads->setValue(threads);
    ui->checkBoxFastMode->setChecked(fastMode);
    ui->comboBoxCodec->setCurrentIndex(codec);
    ui->doubleSpinBoxFPS->setValue(fps);
    ui->spinBoxPlatformID->setValue(pID);
    ui->spinBoxDeviceID->setValue(dID);
    ui->spinBoxOpenCLQueueNum->setValue(OpenCLQueueNum);
    ui->checkBoxOpenCLParallelIO->setChecked(OpenCLParallelIO);
    ui->checkBoxAlphaChannel->setChecked(alphaChannel);
    //ACNet
    ui->checkBoxACNet->setChecked(ACNet);
    ui->checkBoxHDN->setChecked(HDN);
    ui->spinBoxHDNLevel->setValue(HDNLevel);
    //preprocessing
    ui->checkBoxEnablePreprocessing->setChecked(enablePreprocessing);
    ui->checkBoxPreMedian->setChecked(preMedian);
    ui->checkBoxPreMean->setChecked(preMean);
    ui->checkBoxPreCAS->setChecked(preCAS);
    ui->checkBoxPreGaussianWeak->setChecked(preGaussianWeak);
    ui->checkBoxPreGaussian->setChecked(preGaussian);
    ui->checkBoxPreBilateral->setChecked(preBilateral);
    ui->checkBoxPreBilateralFaster->setChecked(preBilateralFaster);
    //postprocessing
    ui->checkBoxEnablePostprocessing->setChecked(enablePostprocessing);
    ui->checkBoxPostMedian->setChecked(postMedian);
    ui->checkBoxPostMean->setChecked(postMean);
    ui->checkBoxPostCAS->setChecked(postCAS);
    ui->checkBoxPostGaussianWeak->setChecked(postGaussianWeak);
    ui->checkBoxPostGaussian->setChecked(postGaussian);
    ui->checkBoxPostBilateral->setChecked(postBilateral);
    ui->checkBoxPostBilateralFaster->setChecked(postBilateralFaster);
}

void MainWindow::writeConfig(QSettings* conf)
{
    QString language = getLanguageString(currLanguage);
    bool quitConfirmatiom = ui->actionQuit_confirmation->isChecked();
    bool checkFFmpegOnStart = ui->actionCheck_FFmpeg->isChecked();

    QString imageSuffix = ui->lineEditImageSuffix->text();
    QString videoSuffix = ui->lineEditVideoSuffix->text();
    QString imageOutputSuffix = ui->lineEditImageOutputSuffix->text();
    QString videoOutputSuffix = ui->lineEditVideoOutputSuffix->text();
    QString outputPath = ui->lineEditOutputPath->text();
    QString outputPrefix = ui->lineEditOutputPrefix->text();

    int passes = ui->spinBoxPasses->value();
    int pushColorCount = ui->spinBoxPushColorCount->value();
    double pushColorStrength = ui->doubleSpinBoxPushColorStrength->value();
    double pushGradientStrength = ui->doubleSpinBoxPushGradientStrength->value();
    double zoomFactor = ui->doubleSpinBoxZoomFactor->value();
    unsigned int threads = ui->spinBoxThreads->value();
    bool fastMode = ui->checkBoxFastMode->isChecked();
    int codec = ui->comboBoxCodec->currentIndex();
    double fps = ui->doubleSpinBoxFPS->value();
    unsigned int pID = ui->spinBoxPlatformID->value();
    unsigned int dID = ui->spinBoxDeviceID->value();
    int OpenCLQueueNum = ui->spinBoxOpenCLQueueNum->value();
    bool OpenCLParallelIO = ui->checkBoxOpenCLParallelIO->isChecked();
    bool alphaChannel = ui->checkBoxAlphaChannel->isChecked();

    bool ACNet = ui->checkBoxACNet->isChecked();
    bool HDN = ui->checkBoxHDN->isChecked();
    int HDNLevel = ui->spinBoxHDNLevel->value();

    bool enablePreprocessing = ui->checkBoxEnablePreprocessing->isChecked();
    bool preMedian = ui->checkBoxPreMedian->isChecked();
    bool preMean = ui->checkBoxPreMean->isChecked();
    bool preCAS = ui->checkBoxPreCAS->isChecked();
    bool preGaussianWeak = ui->checkBoxPreGaussianWeak->isChecked();
    bool preGaussian = ui->checkBoxPreGaussian->isChecked();
    bool preBilateral = ui->checkBoxPreBilateral->isChecked();
    bool preBilateralFaster = ui->checkBoxPreBilateralFaster->isChecked();

    bool enablePostprocessing = ui->checkBoxEnablePostprocessing->isChecked();
    bool postMedian = ui->checkBoxPostMedian->isChecked();
    bool postMean = ui->checkBoxPostMean->isChecked();
    bool postCAS = ui->checkBoxPostCAS->isChecked();
    bool postGaussianWeak = ui->checkBoxPostGaussianWeak->isChecked();
    bool postGaussian = ui->checkBoxPostGaussian->isChecked();
    bool postBilateral = ui->checkBoxPostBilateral->isChecked();
    bool postBilateralFaster = ui->checkBoxPostBilateralFaster->isChecked();

    conf->setValue("/GUI/language", language);
    conf->setValue("/GUI/quitConfirmatiom", quitConfirmatiom);
    conf->setValue("/GUI/checkFFmpeg", checkFFmpegOnStart);
    conf->setValue("/GUI/ffmpegPath", ffmpegPath);

    conf->setValue("/Suffix/image", imageSuffix);
    conf->setValue("/Suffix/video", videoSuffix);
    conf->setValue("/Suffix/imageOutput", imageOutputSuffix);
    conf->setValue("/Suffix/videoOutput", videoOutputSuffix);
    conf->setValue("/Output/path", outputPath);
    conf->setValue("/Output/perfix", outputPrefix);

    conf->setValue("/Arguments/passes", passes);
    conf->setValue("/Arguments/pushColorCount", pushColorCount);
    conf->setValue("/Arguments/pushColorStrength", pushColorStrength);
    conf->setValue("/Arguments/pushGradientStrength", pushGradientStrength);
    conf->setValue("/Arguments/zoomFactor", zoomFactor);
    conf->setValue("/Arguments/threads", threads);
    conf->setValue("/Arguments/fastMode", fastMode);
    conf->setValue("/Arguments/codec", codec);
    conf->setValue("/Arguments/fps", fps);
    conf->setValue("/Arguments/pID", pID);
    conf->setValue("/Arguments/dID", dID);
    conf->setValue("/Arguments/OpenCLQueueNumber", OpenCLQueueNum);
    conf->setValue("/Arguments/OpenCLParallelIO", OpenCLParallelIO);
    conf->setValue("/Arguments/alphaChannel", alphaChannel);

    conf->setValue("/ACNet/ACNet", ACNet);
    conf->setValue("/ACNet/HDN", HDN);
    conf->setValue("/ACNet/HDNLevel", HDNLevel);

    conf->setValue("/Preprocessing/enable", enablePreprocessing);
    conf->setValue("/Preprocessing/MedianBlur", preMedian);
    conf->setValue("/Preprocessing/MeanBlur", preMean);
    conf->setValue("/Preprocessing/CASSharpening", preCAS);
    conf->setValue("/Preprocessing/GaussianBlurWeak", preGaussianWeak);
    conf->setValue("/Preprocessing/GaussianBlur", preGaussian);
    conf->setValue("/Preprocessing/BilateralFilter", preBilateral);
    conf->setValue("/Preprocessing/BilateralFilterFaster", preBilateralFaster);

    conf->setValue("/Postprocessing/enable", enablePostprocessing);
    conf->setValue("/Postprocessing/MedianBlur", postMedian);
    conf->setValue("/Postprocessing/MeanBlur", postMean);
    conf->setValue("/Postprocessing/CASSharpening", postCAS);
    conf->setValue("/Postprocessing/GaussianBlurWeak", postGaussianWeak);
    conf->setValue("/Postprocessing/GaussianBlur", postGaussian);
    conf->setValue("/Postprocessing/BilateralFilter", postBilateral);
    conf->setValue("/Postprocessing/BilateralFilterFaster", postBilateralFaster);
}

inline Language MainWindow::getLanguageValue(const QString& lang)
{
    return languageSelector[lang];
}

inline QString MainWindow::getLanguageString(const Language lang)
{
    switch (lang)
    {
    case Language::en:
        return "en";
    case Language::zh_CN:
        return "zh_CN";
    case Language::zh_TW:
        return "zh_TW";
    case Language::ja_JP:
        return "ja_JP";
    case Language::fr_FR:
        return "fr_FR";
    default:
        return "en";
    }
}

void MainWindow::errorHandler(const ErrorType err)
{
    switch (err)
    {
    case ErrorType::PROCESSING_LIST_EMPTY:
        QMessageBox::warning(this,
            tr("Error"),
            tr("Processing list empty"),
            QMessageBox::Ok);
        break;
    case ErrorType::FILE_NOT_EXIST:
        QMessageBox::warning(this,
            tr("Error"),
            tr("File does not exists"),
            QMessageBox::Ok);
        break;
    case ErrorType::DIR_NOT_EXIST:
        QMessageBox::warning(this,
            tr("Error"),
            tr("Dir does not exists"),
            QMessageBox::Ok);
        break;
    case ErrorType::TYPE_NOT_IMAGE:
        QMessageBox::warning(this,
            tr("Error"),
            tr("File type error, only image support"),
            QMessageBox::Ok);
        break;
    case ErrorType::BAD_TYPE:
        QMessageBox::warning(this,
            tr("Error"),
            tr("File type error, you can add it manually"),
            QMessageBox::Ok);
        break;
    case ErrorType::URL_INVALID:
        QMessageBox::warning(this,
            tr("Error"),
            tr("Invalid url, please check your input"),
            QMessageBox::Ok);
        break;
    case ErrorType::IMAGE_FORMAT_INVALID:
        QMessageBox::warning(this,
            tr("Error"),
            tr("Error image format, please check your input"),
            QMessageBox::Ok);
        break;
    case ErrorType::CUDA_NOT_SUPPORT:
        QMessageBox::warning(this,
            tr("Error"),
            tr("CUDA is not supported"),
            QMessageBox::Ok);
        break;
    }
}

void MainWindow::errorHandler(const QString& err)
{
    QMessageBox::critical(this,
        tr("Error"),
        err,
        QMessageBox::Ok);
}

void MainWindow::initTextBrowser()
{
    ui->textBrowserInfoOut->setText(
        "----------------------------------------------\n"
        "           Welcome to Anime4KCPP GUI          \n"
        "----------------------------------------------\n" +
        QString("         Anime4KCPP GUI v%1                 \n"
            "         Anime4KCPP Core v%2                \n"
            "----------------------------------------------\n").arg(ANIME4KCPP_GUI_VERSION,
                ANIME4KCPP_CORE_VERSION)
    );
    ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
}

bool MainWindow::checkFFmpeg()
{
    if (!QProcess::execute(ffmpegPath, QStringList() << "-version"))
    {
        ui->textBrowserInfoOut->insertPlainText(
            "----------------------------------------------\n"
            "               FFmpeg check OK                \n"
            "----------------------------------------------\n"
            "FFmpeg path: " + ffmpegPath + "\n"
        );
        ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
        return true;
    }
    QMessageBox::warning(this, tr("Warning"), tr("FFmpeg not found"), QMessageBox::Ok);
    ui->textBrowserInfoOut->insertPlainText(
        "----------------------------------------------\n"
        "             FFmpeg check failed              \n"
        "----------------------------------------------\n"
    );
    ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
    return  false;
}

QString MainWindow::formatSuffixList(const QString&& type, QString str)
{
    return type + "( *." + str.replace(QRegExp(":"), " *.") + ");;";
}

std::unique_ptr<Anime4KCPP::AC> MainWindow::getACUP()
{
    int passes = ui->spinBoxPasses->value();
    int pushColorCount = ui->spinBoxPushColorCount->value();
    double pushColorStrength = ui->doubleSpinBoxPushColorStrength->value();
    double pushGradientStrength = ui->doubleSpinBoxPushGradientStrength->value();
    double zoomFactor = ui->doubleSpinBoxZoomFactor->value();
    bool fastMode = ui->checkBoxFastMode->isChecked();
    bool videoMode = false;
    bool preprocessing = ui->checkBoxEnablePreprocessing->isChecked();
    bool postprocessing = ui->checkBoxEnablePostprocessing->isChecked();
    unsigned int threads = ui->spinBoxThreads->value();
    bool HDN = ui->checkBoxHDN->isChecked();
    int HDNLevel = ui->spinBoxHDNLevel->value();
    bool alpha = ui->checkBoxAlphaChannel->isChecked();
    uint8_t prefilters = 0;
    if (preprocessing)
    {
        if (ui->checkBoxPreMedian->isChecked())
            prefilters |= 1;
        if (ui->checkBoxPreMean->isChecked())
            prefilters |= 2;
        if (ui->checkBoxPreCAS->isChecked())
            prefilters |= 4;
        if (ui->checkBoxPreGaussianWeak->isChecked())
            prefilters |= 8;
        if (ui->checkBoxPreGaussian->isChecked())
            prefilters |= 16;
        if (ui->checkBoxPreBilateral->isChecked())
            prefilters |= 32;
        if (ui->checkBoxPreBilateralFaster->isChecked())
            prefilters |= 64;
    }
    uint8_t postfilters = 0;
    if (postprocessing)
    {
        if (ui->checkBoxPostMedian->isChecked())
            postfilters |= 1;
        if (ui->checkBoxPostMean->isChecked())
            postfilters |= 2;
        if (ui->checkBoxPostCAS->isChecked())
            postfilters |= 4;
        if (ui->checkBoxPostGaussianWeak->isChecked())
            postfilters |= 8;
        if (ui->checkBoxPostGaussian->isChecked())
            postfilters |= 16;
        if (ui->checkBoxPostBilateral->isChecked())
            postfilters |= 32;
        if (ui->checkBoxPostBilateralFaster->isChecked())
            postfilters |= 64;
    }

    Anime4KCPP::Parameters parameters(
        passes,
        pushColorCount,
        pushColorStrength,
        pushGradientStrength,
        zoomFactor,
        fastMode,
        videoMode,
        preprocessing,
        postprocessing,
        prefilters,
        postfilters,
        threads,
        HDN,
        HDNLevel,
        alpha
    );

    GPGPU GPGPUModel = (GPGPU)ui->comboBoxGPGPU->currentIndex();
    bool GPUMode = ui->checkBoxGPUMode->isChecked();
    bool ACNetMode = ui->checkBoxACNet->isChecked();

    if(GPUMode)
        switch (GPGPUModel)
        {
        case OpenCL:
            if (ACNetMode)
                return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::OpenCL_ACNet);
            else
                return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::OpenCL_Anime4K09);
        case CUDA:
#ifdef ENABLE_CUDA
            if (ACNetMode)
                return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::Cuda_ACNet);
            else
                return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::Cuda_Anime4K09);
#else
            errorHandler(ErrorType::CUDA_NOT_SUPPORT);
            return nullptr;
#endif
        }
    else
        if (ACNetMode)
            return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::CPU_ACNet);
        else
            return acCreator.createUP(parameters, Anime4KCPP::Processor::Type::CPU_Anime4K09);

    return nullptr;
}

FileType MainWindow::fileType(const QFileInfo& file)
{
    QString imageSuffix = ui->lineEditImageSuffix->text();
    QString videoSuffix = ui->lineEditVideoSuffix->text();

#if (QT_VERSION <= QT_VERSION_CHECK(5,14,0))
    auto imageSuffixes = imageSuffix.split(":", QString::SkipEmptyParts);
    auto videoSuffixes = videoSuffix.split(":", QString::SkipEmptyParts);
#else
    auto imageSuffixes = imageSuffix.split(":", Qt::SkipEmptyParts);
    auto videoSuffixes = videoSuffix.split(":", Qt::SkipEmptyParts);
#endif
    
    if (imageSuffixes.contains(file.suffix(), Qt::CaseInsensitive))
        return FileType::IMAGE;
    if (videoSuffixes.contains(file.suffix(), Qt::CaseInsensitive))
    {
        if (checkGIF(file.filePath()))
            return FileType::GIF;
        else
            return FileType::VIDEO;;
    }
    return FileType::BAD_TYPE;
}

QString MainWindow::getOutputPrefix()
{
    QString prefix = ui->lineEditOutputPrefix->text();
    if (prefix.isEmpty())
        return "output_anime4kcpp_";
    return prefix;
}

QString MainWindow::getImageOutputSuffix()
{
    if (ui->lineEditImageOutputSuffix->text().isEmpty())
        return "";
    return "." + ui->lineEditImageOutputSuffix->text();
}

QString MainWindow::getVideoOutputSuffix()
{
    QString suffix = ui->lineEditVideoOutputSuffix->text();
    if (suffix.isEmpty())
        return ".mkv";
    return "." + suffix;
}

inline Anime4KCPP::CODEC MainWindow::getCodec(const QString& codec)
{
    return codecSelector[codec];
}

void MainWindow::logToTextBrowser(const QString& info)
{
    ui->textBrowserInfoOut->insertPlainText(
        QDateTime::currentDateTime().toString(Qt::DateFormat::ISODateWithMs) + "  " + info + "\n");
    ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
}

bool MainWindow::checkGIF(const QString& file)
{
    return QFileInfo(file).suffix().toLower() == "gif";
}

bool MainWindow::mergeAudio2Video(const QString& dstFile, const QString& srcFile, const QString& tmpFile)
{
    QProcess p(this);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setProgram(ffmpegPath);
    p.setArguments(QStringList()
        << "-i" << tmpFile << "-i" << srcFile
        << "-c" << "copy" << "-map"
        << "0:v" << "-map" << "1"
        << "-map" << "-1:v" << "-y" << dstFile);
    logToTextBrowser("Merging audio with ffmpeg...");
    p.start(QIODevice::OpenMode::enum_type::ReadOnly);
    bool ret = p.waitForFinished(-1);
    if (ret)
    {
        logToTextBrowser("Merge complete, ffmpeg information:");
        logToTextBrowser(p.readAll());
    }
    else
    {
        logToTextBrowser("Merge error:");
        logToTextBrowser(p.errorString());
    }
    return ret;
}

bool MainWindow::video2GIF(const QString& srcFile, const QString& dstFile)
{
    bool flag =
        !QProcess::execute(ffmpegPath, QStringList()
            << "-i" << srcFile << "-vf"
            << "palettegen" << "-y "
            << dstFile << "_palette.png")
        && 
        !QProcess::execute(ffmpegPath, QStringList()
            <<"-i" << srcFile << "-i" << dstFile 
            << "_palette.png" << "-y" << "-lavfi" 
            << "paletteuse" << dstFile);
    
    QFile::remove(dstFile + "_palette.png");

    return flag;
}

void MainWindow::solt_done_renewState(int row, double pro, quint64 time)
{
    tableModel->setData(tableModel->index(row, 4), tr("done"), Qt::DisplayRole);
    ui->progressBarProcessingList->setValue(pro * 100);
    ui->textBrowserInfoOut->insertPlainText(
        (QDateTime::currentDateTime().toString(Qt::DateFormat::ISODateWithMs) + "  Finished " +
            tableModel->data(tableModel->index(row, 0)).toString() + ", processing time: %1 s\n").arg(time / 1000.0));
    ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
}

void MainWindow::solt_error_renewState(int row, QString err)
{
    tableModel->setData(tableModel->index(row, 4), tr("error"), Qt::DisplayRole);
    errorHandler(err);
}

void MainWindow::solt_allDone_remindUser()
{
    ui->labelElpsed->setText(QString("Elpsed: 0.0 s"));
    ui->labelRemaining->setText(QString("Remaining: 0.0 s"));
    QMessageBox::information(this,
        tr("Notice"),
        QString("All tasks done\nTotal time: %1 s").arg(totalProcessingTime / 1000.0),
        QMessageBox::Ok);
    totalProcessingTime = 0;
    ui->tableViewProcessingList->setEnabled(true);
    ui->progressBarProcessingList->setEnabled(false);
    ui->progressBarProcessingList->reset();
    ui->progressBarCurrentTask->reset();
    ui->pushButtonPickFiles->setEnabled(true);
    ui->pushButtonPickFolder->setEnabled(true);
    ui->pushButtonWebVideo->setEnabled(true);
    ui->pushButtonDelete->setEnabled(true);
    ui->pushButtonClear->setEnabled(true);
    ui->pushButtonStart->setEnabled(true);
    ui->pushButtonForceStop->setEnabled(false);
    ui->pushButtonPause->setEnabled(false);
    ui->pushButtonContinue->setEnabled(false);
    on_pushButtonClear_clicked();
}

void MainWindow::solt_showInfo_renewTextBrowser(std::string info)
{
    ui->textBrowserInfoOut->insertPlainText(
        QDateTime::currentDateTime().toString(Qt::DateFormat::ISODateWithMs) + "  " + QString::fromStdString(info));
    ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
}

void MainWindow::solt_updateProgress_updateCurrentTaskProgress(double v, double elpsed, double remaining)
{
    ui->progressBarCurrentTask->setValue(v * 100);
    ui->labelElpsed->setText(QString("Elpsed: %1 s").arg(elpsed, 0, 'f', 2, ' '));
    ui->labelRemaining->setText(QString("Remaining: %1 s").arg(remaining, 0, 'f', 2, ' '));
}

void MainWindow::on_actionQuit_triggered()
{
    this->close();
}

void MainWindow::on_pushButtonPickFiles_clicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, tr("pick files"), "./",
        formatSuffixList(tr("image"), ui->lineEditImageSuffix->text()) +
        formatSuffixList(tr("video"), ui->lineEditVideoSuffix->text()));
    if (files.isEmpty())
        return;
    files.removeDuplicates();

    QStandardItem* inputFile;
    QStandardItem* outputFile;
    QStandardItem* inputPath;
    QStandardItem* outputPath;
    QStandardItem* state;


    for (QString& file : files)
    {
        QFileInfo fileInfo(file);

        FileType type = fileType(fileInfo);

        if (type == FileType::BAD_TYPE)
        {
            errorHandler(ErrorType::BAD_TYPE);
            continue;
        }

        inputFile = new QStandardItem(fileInfo.fileName());
        if (type == FileType::VIDEO)
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getVideoOutputSuffix());
        else if (type == FileType::GIF)
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + ".gif");
        else if (getImageOutputSuffix().isEmpty())
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.fileName());
        else
            outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getImageOutputSuffix());
        inputPath = new QStandardItem(fileInfo.filePath());
        state = new QStandardItem(tr("ready"));
        if (ui->lineEditOutputPath->text().isEmpty())
            outputPath = new QStandardItem(QDir::currentPath());
        else
            outputPath = new QStandardItem(ui->lineEditOutputPath->text());
        tableModel->appendRow({ inputFile,outputFile,inputPath,outputPath,state });

        totalTaskCount++;
    }

    ui->labelTotalTaskCount->setText(QString("Total: %1 ").arg(totalTaskCount));
}

void MainWindow::on_pushButtonOutputPathPick_clicked()
{
    QString outputPath = QFileDialog::getExistingDirectory(this, tr("output directory"), "./");
    if (outputPath.isEmpty())
        return;
    ui->lineEditOutputPath->setText(outputPath);
}

void MainWindow::on_pushButtonWebVideo_clicked()
{
    bool ok = false;
    QString urlStr = QInputDialog::getText(this,
        tr("Web Video"),
        tr("Please input the url of web video"),
        QLineEdit::Normal,
        QString(),
        &ok
    ).simplified();

    if (!ok)
        return;

    QUrl url(urlStr);
    if (!QRegExp("^https?|ftp|file$").exactMatch(url.scheme()) || !url.isValid())
    {
        errorHandler(ErrorType::URL_INVALID);
        return;
    }
    QFileInfo fileInfo(url.path());

    QStandardItem* inputFile;
    QStandardItem* outputFile;
    QStandardItem* inputPath;
    QStandardItem* outputPath;
    QStandardItem* state;

    FileType type = fileType(fileInfo);

    if (type == FileType::BAD_TYPE)
    {
        errorHandler(ErrorType::BAD_TYPE);
        return;
    }

    inputFile = new QStandardItem(fileInfo.fileName());
    if (type == FileType::VIDEO)
        outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getVideoOutputSuffix());
    else if (type == FileType::GIF)
        outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + ".gif");
    else if (getImageOutputSuffix().isEmpty())
        outputFile = new QStandardItem(getOutputPrefix() + fileInfo.fileName());
    else
        outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getImageOutputSuffix());
    inputPath = new QStandardItem(urlStr);
    state = new QStandardItem(tr("ready"));
    if (ui->lineEditOutputPath->text().isEmpty())
        outputPath = new QStandardItem(QDir::currentPath());
    else
        outputPath = new QStandardItem(ui->lineEditOutputPath->text());
    tableModel->appendRow({ inputFile,outputFile,inputPath,outputPath,state });
}

void MainWindow::on_pushButtonClear_clicked()
{
    tableModel->removeRows(0, tableModel->rowCount());

    totalTaskCount = 0;
    ui->labelTotalTaskCount->setText("Total: 0 ");
}

void MainWindow::on_pushButtonDelete_clicked()
{
    tableModel->removeRow(ui->tableViewProcessingList->currentIndex().row());

    totalTaskCount--;
    ui->labelTotalTaskCount->setText(QString("Total: %1 ").arg(totalTaskCount));
}

void MainWindow::on_radioButtonFast_clicked()
{
    ui->checkBoxACNet->setChecked(false);
    ui->checkBoxHDN->setChecked(false);
    ui->spinBoxPasses->setValue(2);
    ui->spinBoxPushColorCount->setValue(2);
    ui->spinBoxThreads->setValue(std::thread::hardware_concurrency());
    ui->doubleSpinBoxPushColorStrength->setValue(0.3);
    ui->doubleSpinBoxPushGradientStrength->setValue(1.0);
    ui->doubleSpinBoxZoomFactor->setValue(2.0);
    ui->checkBoxFastMode->setChecked(false);
    //preprocessing
    ui->checkBoxEnablePreprocessing->setChecked(true);
    ui->checkBoxPreMedian->setChecked(false);
    ui->checkBoxPreMean->setChecked(false);
    ui->checkBoxPreCAS->setChecked(true);
    ui->checkBoxPreGaussianWeak->setChecked(false);
    ui->checkBoxPreGaussian->setChecked(false);
    ui->checkBoxPreBilateral->setChecked(false);
    ui->checkBoxPreBilateralFaster->setChecked(false);
    //postprocessing
    ui->checkBoxEnablePostprocessing->setChecked(true);
    ui->checkBoxPostMedian->setChecked(false);
    ui->checkBoxPostMean->setChecked(false);
    ui->checkBoxPostCAS->setChecked(false);
    ui->checkBoxPostGaussianWeak->setChecked(true);
    ui->checkBoxPostGaussian->setChecked(false);
    ui->checkBoxPostBilateral->setChecked(true);
    ui->checkBoxPostBilateralFaster->setChecked(false);
}

void MainWindow::on_radioButtonBalance_clicked()
{
    ui->checkBoxACNet->setChecked(true);
    ui->spinBoxThreads->setValue(std::thread::hardware_concurrency());
    ui->doubleSpinBoxZoomFactor->setValue(2.0);
    ui->checkBoxFastMode->setChecked(false);
    ui->checkBoxEnablePreprocessing->setChecked(false);
    ui->checkBoxEnablePostprocessing->setChecked(false);
}

void MainWindow::on_radioButtonQuality_clicked()
{
    ui->checkBoxACNet->setChecked(true);
    ui->checkBoxHDN->setChecked(true);
    ui->spinBoxThreads->setValue(std::thread::hardware_concurrency());
    ui->doubleSpinBoxZoomFactor->setValue(2.0);
    ui->checkBoxFastMode->setChecked(false);
    ui->checkBoxEnablePreprocessing->setChecked(false);
    ui->checkBoxEnablePostprocessing->setChecked(false);
}

void MainWindow::on_checkBoxEnablePreprocessing_stateChanged(int arg1)
{
    if (arg1 == Qt::CheckState::Checked)
    {
        ui->checkBoxPreCAS->setEnabled(true);
        ui->checkBoxPreMean->setEnabled(true);
        ui->checkBoxPreMedian->setEnabled(true);
        ui->checkBoxPreGaussianWeak->setEnabled(true);
        ui->checkBoxPreGaussian->setEnabled(true);
        ui->checkBoxPreBilateral->setEnabled(true);
        ui->checkBoxPreBilateralFaster->setEnabled(true);
    }
    else
    {
        ui->checkBoxPreCAS->setEnabled(false);
        ui->checkBoxPreMean->setEnabled(false);
        ui->checkBoxPreMedian->setEnabled(false);
        ui->checkBoxPreGaussianWeak->setEnabled(false);
        ui->checkBoxPreGaussian->setEnabled(false);
        ui->checkBoxPreBilateral->setEnabled(false);
        ui->checkBoxPreBilateralFaster->setEnabled(false);
    }
}

void MainWindow::on_checkBoxEnablePostprocessing_stateChanged(int arg1)
{
    if (arg1 == Qt::CheckState::Checked)
    {
        ui->checkBoxPostCAS->setEnabled(true);
        ui->checkBoxPostMean->setEnabled(true);
        ui->checkBoxPostMedian->setEnabled(true);
        ui->checkBoxPostGaussianWeak->setEnabled(true);
        ui->checkBoxPostGaussian->setEnabled(true);
        ui->checkBoxPostBilateral->setEnabled(true);
        ui->checkBoxPostBilateralFaster->setEnabled(true);
    }
    else
    {
        ui->checkBoxPostCAS->setEnabled(false);
        ui->checkBoxPostMean->setEnabled(false);
        ui->checkBoxPostMedian->setEnabled(false);
        ui->checkBoxPostGaussianWeak->setEnabled(false);
        ui->checkBoxPostGaussian->setEnabled(false);
        ui->checkBoxPostBilateral->setEnabled(false);
        ui->checkBoxPostBilateralFaster->setEnabled(false);
    }
}

void MainWindow::on_pushButtonPreview_clicked()
{
    QFileInfo previewFile(ui->lineEditPreview->text());
    if (!previewFile.exists())
    {
        errorHandler(ErrorType::FILE_NOT_EXIST);
        return;
    }

    FileType type = fileType(previewFile);

    if (type == FileType::BAD_TYPE)
    {
        errorHandler(ErrorType::BAD_TYPE);
        return;
    }

    ui->pushButtonPreview->setEnabled(false);

    std::unique_ptr<Anime4KCPP::AC> ac = getACUP();
    switch (type)
    {
    case FileType::IMAGE:
        try
        {
            ac->setVideoMode(false);
            ac->loadImage(previewFile.filePath().toUtf8().toStdString());
            ac->process();
            ac->showImage();
        }
        catch (const std::exception& err)
        {
            errorHandler(err.what());
        }
        break;
    case FileType::GIF:
    case FileType::VIDEO:
    {
        try
        {
            std::string currInputPath = previewFile.absoluteFilePath().toUtf8().toStdString();

            cv::VideoCapture videoCapture(currInputPath, cv::CAP_FFMPEG);
            if (!videoCapture.isOpened())
                throw std::runtime_error("Error: Unable to open the video file");

            double fps = ui->doubleSpinBoxFPS->value();
            double zoomFactor = ui->doubleSpinBoxZoomFactor->value();
            int delay = 500.0 / (fps < 1.0 ? videoCapture.get(cv::CAP_PROP_FPS) : fps);
            char keyCode = 'q';
            cv::Mat frame;
            std::string windowName =
                "Previewing, press 'q','ESC' or 'Enter' to exit, "
                "'space' to pause, 'd' to fast forward, 'a' to fast backward, "
                "'w' to forward, 's' to backward";
            cv::namedWindow(windowName, cv::WindowFlags::WINDOW_NORMAL);
            cv::resizeWindow(windowName,
                videoCapture.get(cv::CAP_PROP_FRAME_WIDTH) * zoomFactor + 0.5,
                videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT) * zoomFactor + 0.5);
            ac->setVideoMode(false);

            while (videoCapture.read(frame))
            {
                ac->loadImage(frame);
                ac->process();
                ac->saveImage(frame);
                cv::imshow(windowName, frame);

                keyCode = cv::waitKey(delay) & 0xff;

                if (cv::getWindowProperty(windowName, cv::WindowPropertyFlags::WND_PROP_AUTOSIZE) != cv::WindowFlags::WINDOW_NORMAL ||
                    keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                    break;
                else if (keyCode == 0x20)
                {
                    keyCode = cv::waitKey(0);
                    if (keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                        break;
                }
                else
                {
                    switch (keyCode)
                    {
                    case 'a':
                        videoCapture.set(
                            cv::CAP_PROP_POS_FRAMES,
                            videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                        break;
                    case 'd':
                        videoCapture.set(
                            cv::CAP_PROP_POS_FRAMES,
                            videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                        break;
                    case 's':
                        videoCapture.set(
                            cv::CAP_PROP_POS_FRAMES,
                            videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                        break;
                    case 'w':
                        videoCapture.set(
                            cv::CAP_PROP_POS_FRAMES,
                            videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                        break;
                    }
                }
            }
            videoCapture.release();
            cv::destroyWindow(windowName);
        }
        catch (const std::exception& err)
        {
            errorHandler(err.what());
        }
    }
    }

    ui->pushButtonPreview->setEnabled(true);
}

void MainWindow::on_pushButtonPreviewPick_clicked()
{
    QString fileName = QFileDialog::getOpenFileName(this, tr("pick files"), "./",
        formatSuffixList(tr("image"), ui->lineEditImageSuffix->text()) +
        formatSuffixList(tr("video"), ui->lineEditVideoSuffix->text()));
    if (fileName.isEmpty())
        return;
    ui->lineEditPreview->setText(fileName);
}

void MainWindow::on_pushButtonStart_clicked()
{
    if (ui->checkBoxGPUMode->isChecked() && (ui->checkBoxEnablePreprocessing->isChecked() || ui->checkBoxEnablePostprocessing->isChecked()))
    {
        if (QMessageBox::Yes == QMessageBox::information(this,
            tr("Notice"),
            tr("You are using GPU acceleration but still enabled"
                "preprocessing or postprocessing, which is not GPU acceletation yet, "
                "and may slow down processing for GPU (usually still faster than CPU), close them?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes))
        {
            ui->checkBoxEnablePreprocessing->setChecked(false);
            ui->checkBoxEnablePostprocessing->setChecked(false);
        }
    }

    int rows = tableModel->rowCount();
    if (!rows)
    {
        errorHandler(ErrorType::PROCESSING_LIST_EMPTY);
        return;
    }

    ui->pushButtonStart->setEnabled(false);
    ui->pushButtonClear->setEnabled(false);
    ui->pushButtonDelete->setEnabled(false);
    ui->pushButtonPickFiles->setEnabled(false);
    ui->pushButtonPickFolder->setEnabled(false);
    ui->pushButtonWebVideo->setEnabled(false);
    ui->progressBarProcessingList->setEnabled(true);
    ui->tableViewProcessingList->setEnabled(false);
    ui->pushButtonForceStop->setEnabled(true);
    ui->pushButtonPause->setEnabled(true);

    QtConcurrent::run([this, rows]() {

        //QList<<input_path,outpt_path>,row>
        QList<QPair<QPair<QString, QString>, int>> images;
        QList<QPair<QPair<QString, QString>, int>> videos;
        //read info
        {
            QDir outputPathMaker;
            QString outputPath;
            QString outputFileName;
            for (int i = 0; i < rows; i++)
            {
                QString filePath(tableModel->index(i, 2).data().toString());
                outputPathMaker.setPath(tableModel->index(i, 3).data().toString());
                outputPath = outputPathMaker.absolutePath();
                outputFileName = tableModel->index(i, 1).data().toString();

                outputPathMaker.mkpath(outputPath);

                if (fileType(filePath) == FileType::IMAGE)
                {
                    images << QPair<QPair<QString, QString>, int>(QPair<QString, QString>(filePath,
                        outputPath + "/" +
                        outputFileName), i);
                    imageCount++;
                }
                else
                {
                    videos << QPair<QPair<QString, QString>, int>(QPair<QString, QString>(filePath,
                        outputPath + "/" +
                        outputFileName), i);
                    videoCount++;
                }
            }
        }

        double total = imageCount + videoCount;

        Communicator cm;
        connect(&cm, SIGNAL(done(int, double, quint64)),
            this, SLOT(solt_done_renewState(int, double, quint64)), Qt::QueuedConnection);
        connect(&cm, SIGNAL(error(int, QString)),
            this, SLOT(solt_error_renewState(int, QString)), Qt::QueuedConnection);
        connect(&cm, SIGNAL(showInfo(std::string)),
            this, SLOT(solt_showInfo_renewTextBrowser(std::string)), Qt::QueuedConnection);
        connect(&cm, SIGNAL(allDone()),
            this, SLOT(solt_allDone_remindUser()), Qt::QueuedConnection);
        connect(&cm, SIGNAL(updateProgress(double, double, double)),
            this, SLOT(solt_updateProgress_updateCurrentTaskProgress(double, double, double)), Qt::QueuedConnection);
        std::chrono::steady_clock::time_point startTimeForAll = std::chrono::steady_clock::now();
        if (imageCount)
        {
#if defined(_MSC_VER) || defined(USE_TBB)
            Parallel::parallel_for(0, images.size(), [this, total, &images, &cm](int i) {
                std::unique_ptr<Anime4KCPP::AC> ac = getACUP();
                std::chrono::steady_clock::time_point startTime, endTime;
                ac->setVideoMode(false);
                auto& image = images.at(i);
                try
                {
                    ac->loadImage(image.first.first.toUtf8().toStdString());
                    emit cm.showInfo(("Image: " + image.first.first).toStdString() + ", start processing...\n");
                    startTime = std::chrono::steady_clock::now();
                    ac->process();
                    endTime = std::chrono::steady_clock::now();
                    ac->saveImage(image.first.second.toUtf8().toStdString());
                }
                catch (const std::exception& err)
                {
                    emit cm.error(image.second, QString::fromStdString(err.what()));
                }

                imageCount--;
                
                emit cm.done(image.second, 1.0 - ((imageCount + videoCount) / total),
                    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
                });
#else
#pragma omp parallel for
            for (int i = 0; i < images.size(); i++)
            {
                std::unique_ptr<Anime4KCPP::AC> ac = getACUP();
                std::chrono::steady_clock::time_point startTime, endTime;
                ac->setVideoMode(false);
                auto& image = images.at(i);
                try
                {
                    ac->loadImage(image.first.first.toUtf8().toStdString());
                    emit cm.showInfo(ac->getInfo() + "processing...\n");
                    startTime = std::chrono::steady_clock::now();
                    ac->process();
                    endTime = std::chrono::steady_clock::now();
                    ac->saveImage(image.first.second.toUtf8().toStdString());
                }
                catch (const std::exception& err)
                {
                    emit cm.error(image.second, QString::fromStdString(err.what()));
                }

                imageCount--;

                emit cm.done(image.second, 1.0 - ((imageCount + videoCount) / total),
                    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
            }
#endif
        }
        if (videoCount)
        {
            std::unique_ptr<Anime4KCPP::AC> ac = getACUP();
            std::chrono::steady_clock::time_point startTime, endTime;
            ac->setVideoMode(true);
            for (QPair<QPair<QString, QString>, int> const& video : videos)
            {
                try
                {
                    ac->loadVideo(video.first.first.toUtf8().toStdString());
                    ac->setVideoSaveInfo(
                        video.first.second.toUtf8().toStdString() + 
                        std::string("_tmp_out.mp4"), getCodec(ui->comboBoxCodec->currentText()), 
                        ui->doubleSpinBoxFPS->value());
                    emit cm.showInfo(("Video: " + video.first.first).toStdString() + ", start processing...\n");
                    startTime = std::chrono::steady_clock::now();
                    ac->processWithProgress([this, &ac, &startTime, &cm](double v) {
                        if (stop)
                        {
                            if (pause == ProcessingState::PAUSED)
                            {
                                ac->continueVideoProcess();
                                pause = ProcessingState::NORMAL;
                            }
                            ac->stopVideoProcess();
                            return;
                        }
                        if (pause == ProcessingState::PAUSE)
                        {
                            ac->pauseVideoProcess();
                            pause = ProcessingState::PAUSED;
                        }
                        else if (pause == ProcessingState::CONTINUE)
                        {
                            ac->continueVideoProcess();
                            pause = ProcessingState::NORMAL;
                        }
                        else if (pause == ProcessingState::NORMAL)
                        {
                            double elpsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startTime).count() / 1000.0;
                            double remaining = elpsed / v - elpsed;
                            emit cm.updateProgress(v, elpsed, remaining);
                        }
                        });
                    endTime = std::chrono::steady_clock::now();
                    ac->saveVideo();
                }
                catch (const std::exception& err)
                {
                    emit cm.error(video.second, QString::fromStdString(err.what()));
                }

                if (stop)
                {
                    stop = false;
                    break;
                }

                if (foundFFmpegFlag)
                {
                    QString tmpFilePath = video.first.second + "_tmp_out.mp4";
                    if (!checkGIF(video.first.second))
                    {
                        if (mergeAudio2Video(video.first.second, video.first.first, tmpFilePath))
                            QFile::remove(tmpFilePath);
                    }
                    else
                    {
                        if (video2GIF(tmpFilePath, video.first.second))
                            QFile::remove(tmpFilePath);
                    }
                }

                videoCount--;

                emit cm.done(video.second, 1.0 - (videoCount / total),
                    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());
            }
        }
        
        std::chrono::steady_clock::time_point endTimeForAll = std::chrono::steady_clock::now();
        totalProcessingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTimeForAll - startTimeForAll).count();
        
        emit cm.allDone();
        });
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::information(this,
        tr("About"),
        QString(
            "Anime4KCPP GUI\n\n"
            "Anime4KCPP GUI v%1\n"
            "Anime4KCPP core v%2\n\n"
            "Parallel library: %3\n\n"
            "Build on %4 %5\n\n"
            "GitHub: https://github.com/TianZerL/Anime4KCPP\n\n"
            "Copyright (c) 2020 TianZerL\n\n"
            "Thanks for:\n"
            "semmyenator (Traditional Chinese,Japanese and French translation)")
        .arg(ANIME4KCPP_GUI_VERSION, ANIME4KCPP_CORE_VERSION, PARALLEL_LIBRARY, __DATE__, __TIME__),
        QMessageBox::Ok);
}

void MainWindow::on_tabWidgetMain_tabBarClicked(int index)
{
    if (index == 1)
        ui->radioButtonCustom->setChecked(true);
}

void MainWindow::on_actionSimplifiedChinese_triggered()
{
    QString filePath = QCoreApplication::applicationDirPath() + "/language/Anime4KCPP_GUI_zh_CN.qm";
    if (!QFileInfo(filePath).exists())
        filePath = QFileDialog::getOpenFileName(this, tr("Chinese translation file"), "./", "Anime4KCPP_GUI_zh_CN.qm");
    translator->load(filePath);
    qApp->installTranslator(translator);
    ui->retranslateUi(this);
    currLanguage = Language::zh_CN;
}

void MainWindow::on_actionTraditionalChinese_triggered()
{
    QString filePath = QCoreApplication::applicationDirPath() + "/language/Anime4KCPP_GUI_zh_TW.qm";
    if (!QFileInfo(filePath).exists())
        filePath = QFileDialog::getOpenFileName(this, tr("Chinese translation file"), "./", "Anime4KCPP_GUI_zh_TW.qm");
    translator->load(filePath);
    qApp->installTranslator(translator);
    ui->retranslateUi(this);
    currLanguage = Language::zh_TW;
}

void MainWindow::on_actionJapanese_triggered()
{
    QString filePath = QCoreApplication::applicationDirPath() + "/language/Anime4KCPP_GUI_ja_JP.qm";
    if (!QFileInfo(filePath).exists())
        filePath = QFileDialog::getOpenFileName(this, tr("Chinese translation file"), "./", "Anime4KCPP_GUI_ja_JP.qm");
    translator->load(filePath);
    qApp->installTranslator(translator);
    ui->retranslateUi(this);
    currLanguage = Language::ja_JP;
}

void MainWindow::on_actionFrench_triggered()
{
    QString filePath = QCoreApplication::applicationDirPath() + "/language/Anime4KCPP_GUI_fr_FR.qm";
    if (!QFileInfo(filePath).exists())
        filePath = QFileDialog::getOpenFileName(this, tr("Chinese translation file"), "./", "Anime4KCPP_GUI_fr_FR.qm");
    translator->load(filePath);
    qApp->installTranslator(translator);
    ui->retranslateUi(this);
    currLanguage = Language::fr_FR;
}

void MainWindow::on_actionEnglish_triggered()
{
    qApp->removeTranslator(translator);
    ui->retranslateUi(this);
    currLanguage = Language::en;
}

void MainWindow::on_actionSet_FFmpeg_path_triggered()
{
    QString tmpFFmpegPath = QFileDialog::getOpenFileName(this, tr("FFmpeg path"), "./");
    bool ok = false;
    tmpFFmpegPath = QInputDialog::getText(this,
        tr("FFmpeg path"),
        tr("Please input the FFmpeg path"),
        QLineEdit::Normal,
        tmpFFmpegPath,
        &ok
    ).simplified();

    if (ok)
    {
        QString oldPath = ffmpegPath;
        ffmpegPath = '"' + tmpFFmpegPath + '"';
        if (!ffmpegPath.contains("ffmpeg", Qt::CaseInsensitive) || !checkFFmpeg())
        {
            errorHandler("The path is not correct");
            ffmpegPath = oldPath;
        }
    }

}

void MainWindow::on_actionSet_FFmpeg_path_hovered()
{
    ui->actionSet_FFmpeg_path->setStatusTip("FFmpeg: " + ffmpegPath);
}

void MainWindow::on_pushButtonClearText_clicked()
{
    ui->textBrowserInfoOut->clear();
    initTextBrowser();
}

void MainWindow::on_spinBoxFontSize_valueChanged(int value)
{
    ui->textBrowserInfoOut->setFont(QFont(ui->fontComboBox->font().family(), value));
}

void MainWindow::on_fontComboBox_currentFontChanged(const QFont& font)
{
    ui->textBrowserInfoOut->setFont(QFont(font.family(), ui->spinBoxFontSize));
}

void MainWindow::on_pushButtonCopyText_clicked()
{
    QApplication::clipboard()->setText(ui->textBrowserInfoOut->toPlainText());
    QMessageBox::information(this,
        tr("Notice"),
        tr("Log has been copied to the clipboard"),
        QMessageBox::Ok);
}

void MainWindow::on_pushButtonPreviewOriginal_clicked()
{
    QString filePath = ui->lineEditPreview->text();
    if (filePath.isEmpty())
    {
        errorHandler(ErrorType::FILE_NOT_EXIST);
        return;
    }

    QFileInfo fileInfo(filePath);
    FileType type = fileType(fileInfo);

    if (type == FileType::BAD_TYPE)
    {
        errorHandler(ErrorType::BAD_TYPE);
        return;
    }

    ui->pushButtonPreviewOriginal->setEnabled(false);

    switch (type)
    {
    case FileType::IMAGE:
    {
        cv::Mat orgImg = cv::imread(filePath.toUtf8().toStdString(), cv::IMREAD_COLOR);
        cv::imshow("original image", orgImg);
        cv::waitKey();
        cv::destroyWindow("original image");
        break;
    }
    case FileType::GIF:
    case FileType::VIDEO:
    {
        std::string currInputPath = fileInfo.absoluteFilePath().toUtf8().toStdString();

        cv::VideoCapture videoCapture(currInputPath, cv::CAP_FFMPEG);
        try
        {
            if (!videoCapture.isOpened())
                throw std::runtime_error("Error: Unable to open the video file");
        }
        catch (const std::exception& err)
        {
            errorHandler(err.what());
            break;
        }

        double fps = ui->doubleSpinBoxFPS->value();
        int delay = 1000.0 / (fps < 1.0 ? videoCapture.get(cv::CAP_PROP_FPS) : fps);
        char keyCode = 'q';
        std::string windowName =
            "Previewing original image, press 'q','ESC' or 'Enter' to exit, "
            "'space' to pause, 'd' to fast forward, 'a' to fast backward, "
            "'w' to forward, 's' to backward";

        cv::Mat frame;
        cv::namedWindow(windowName, cv::WindowFlags::WINDOW_NORMAL);
        cv::resizeWindow(windowName,
            videoCapture.get(cv::CAP_PROP_FRAME_WIDTH) + 0.5,
            videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT) + 0.5);

        while (videoCapture.read(frame))
        {
            cv::imshow(windowName, frame);

            keyCode = cv::waitKey(delay) & 0xff;

            if (cv::getWindowProperty(windowName, cv::WindowPropertyFlags::WND_PROP_AUTOSIZE) != cv::WindowFlags::WINDOW_NORMAL ||
                keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                break;
            else if (keyCode == 0x20)
            {
                keyCode = cv::waitKey(0);
                if (keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                    break;
            }
            else
            {
                switch (keyCode)
                {
                case 'a':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                    break;
                case 'd':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                    break;
                case 's':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                    break;
                case 'w':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                    break;
                }
            }
        }

        videoCapture.release();
        cv::destroyWindow(windowName);
    }
    }

    ui->pushButtonPreviewOriginal->setEnabled(true);
}

void MainWindow::on_pushButtonPreviewOnlyResize_clicked()
{
    QString filePath = ui->lineEditPreview->text();
    if (filePath.isEmpty())
    {
        errorHandler(ErrorType::FILE_NOT_EXIST);
        return;
    }

    QFileInfo fileInfo(filePath);
    FileType type = fileType(fileInfo);

    if (type == FileType::BAD_TYPE)
    {
        errorHandler(ErrorType::BAD_TYPE);
        return;
    }

    ui->pushButtonPreviewOnlyResize->setEnabled(false);

    double zoomFactor = ui->doubleSpinBoxZoomFactor->value();
    switch (type)
    {
    case FileType::IMAGE:
    {
        cv::Mat orgImg = cv::imread(filePath.toUtf8().toStdString(), cv::IMREAD_COLOR);
        cv::resize(orgImg, orgImg, cv::Size(0, 0), zoomFactor, zoomFactor, cv::INTER_CUBIC);
        cv::imshow("resized image", orgImg);
        cv::waitKey();
        cv::destroyWindow("resized image");
        break;
    }
    case FileType::GIF:
    case FileType::VIDEO:
    {
        std::string currInputPath = fileInfo.absoluteFilePath().toUtf8().toStdString();
        cv::VideoCapture videoCapture(currInputPath, cv::CAP_FFMPEG);

        try
        {
            if (!videoCapture.isOpened())
                throw std::runtime_error("Error: Unable to open the video file");
        }
        catch (const std::exception& err)
        {
            errorHandler(err.what());
            break;
        }

        double fps = ui->doubleSpinBoxFPS->value();
        int delay = 1000.0 / (fps < 1.0 ? videoCapture.get(cv::CAP_PROP_FPS) : fps);
        char keyCode = 'q';
        std::string windowName =
            "Previewing resize only image, press 'q','ESC' or 'Enter' to exit, "
            "'space' to pause, 'd' to fast forward, 'a' to fast backward, "
            "'w' to forward, 's' to backward";

        cv::Mat frame;
        cv::namedWindow(windowName, cv::WindowFlags::WINDOW_NORMAL);
        cv::resizeWindow(windowName,
            videoCapture.get(cv::CAP_PROP_FRAME_WIDTH) * zoomFactor + 0.5,
            videoCapture.get(cv::CAP_PROP_FRAME_HEIGHT) * zoomFactor + 0.5);

        while (videoCapture.read(frame))
        {
            cv::resize(frame, frame, cv::Size(0, 0), zoomFactor, zoomFactor, cv::INTER_CUBIC);
            cv::imshow(windowName, frame);

            keyCode = cv::waitKey(delay) & 0xff;

            if (cv::getWindowProperty(windowName, cv::WindowPropertyFlags::WND_PROP_AUTOSIZE) != cv::WindowFlags::WINDOW_NORMAL ||
                keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                break;
            else if (keyCode == 0x20)
            {
                keyCode = cv::waitKey(0);
                if (keyCode == 'q' || keyCode == 0x1b || keyCode == 0x0d)
                    break;
            }
            else
            {
                switch (keyCode)
                {
                case 'a':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                    break;
                case 'd':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 10.0);
                    break;
                case 's':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) - videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                    break;
                case 'w':
                    videoCapture.set(
                        cv::CAP_PROP_POS_FRAMES,
                        videoCapture.get(cv::CAP_PROP_POS_FRAMES) + videoCapture.get(cv::CAP_PROP_FPS) * 2.0);
                    break;
                }
            }
        }

        videoCapture.release();
        cv::destroyWindow(windowName);
    }
    }

    ui->pushButtonPreviewOnlyResize->setEnabled(true);
}

void MainWindow::on_pushButtonPickFolder_clicked()
{
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("please select a folder"), "./");
    if (folderPath.isEmpty())
        return;
    QDir folder(folderPath);
    QDirIterator folderIter(folderPath, QDir::Files, QDirIterator::Subdirectories);

    QStandardItem* inputFile;
    QStandardItem* outputFile;
    QStandardItem* inputPath;
    QStandardItem* outputPath;
    QStandardItem* state;

    while (folderIter.hasNext())
    {
        folderIter.next();
        QFileInfo fileInfo = folderIter.fileInfo();
        FileType type = fileType(fileInfo);
        QString path = folder.relativeFilePath(fileInfo.absolutePath());
        if (type != FileType::BAD_TYPE)
        {
            inputFile = new QStandardItem(fileInfo.fileName());
            if (type == FileType::VIDEO)
                outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getVideoOutputSuffix());
            else if (type == FileType::GIF)
                outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + ".gif");
            else if (getImageOutputSuffix().isEmpty())
                outputFile = new QStandardItem(getOutputPrefix() + fileInfo.fileName());
            else
                outputFile = new QStandardItem(getOutputPrefix() + fileInfo.baseName() + getImageOutputSuffix());
            inputPath = new QStandardItem(fileInfo.filePath());
            state = new QStandardItem(tr("ready"));
            if (ui->lineEditOutputPath->text().isEmpty())
                outputPath = new QStandardItem(QDir::currentPath() + "/" + path);
            else
                outputPath = new QStandardItem(ui->lineEditOutputPath->text() + "/" + path);
            tableModel->appendRow({ inputFile,outputFile,inputPath,outputPath,state });

            totalTaskCount++;
        }
    }

    ui->labelTotalTaskCount->setText(QString("Total: %1 ").arg(totalTaskCount));
}

void MainWindow::on_checkBoxGPUMode_stateChanged(int state)
{
    if ((state == Qt::Checked) && (GPUState == GPUMode::UNINITIALZED))
    {
        unsigned int currPlatFormID = ui->spinBoxPlatformID->value(), currDeviceID = ui->spinBoxDeviceID->value();
        GPGPU GPGPUModel = (GPGPU)ui->comboBoxGPGPU->currentIndex();
        bool ACNetMode = ui->checkBoxACNet->isChecked();
        bool supported = false;
        std::string info;

        switch (GPGPUModel)
        {
        case OpenCL:
            {
                int OpenCLQueueNum = ui->spinBoxOpenCLQueueNum->value();
                bool OpenCLParallelIO = ui->checkBoxOpenCLParallelIO->isChecked();
                Anime4KCPP::OpenCL::GPUInfo ret = Anime4KCPP::OpenCL::checkGPUSupport(currPlatFormID, currDeviceID);
                supported = ret;
                info = ret();
                if (supported)
                {
                    if (ACNetMode)
                        acCreator.pushManager<Anime4KCPP::OpenCL::Manager<Anime4KCPP::OpenCL::ACNet>>(
                            currPlatFormID, currDeviceID,
                            Anime4KCPP::CNNType::Default,
                            OpenCLQueueNum,
                            OpenCLParallelIO);
                    else
                        acCreator.pushManager<Anime4KCPP::OpenCL::Manager<Anime4KCPP::OpenCL::Anime4K09>>(
                            currPlatFormID, currDeviceID,
                            OpenCLQueueNum,
                            OpenCLParallelIO);
                }
            }
            break;
        case CUDA:
#ifdef ENABLE_CUDA
        {
            Anime4KCPP::Cuda::GPUInfo ret = Anime4KCPP::Cuda::checkGPUSupport(currDeviceID);
            supported = ret;
            info = ret();
            if (supported)
            {
                acCreator.pushManager<Anime4KCPP::Cuda::Manager>(currDeviceID);
            }
        }
#else
            errorHandler(ErrorType::CUDA_NOT_SUPPORT);
            ui->comboBoxGPGPU->setCurrentIndex(GPGPU::OpenCL);
            ui->checkBoxGPUMode->setCheckState(Qt::Unchecked);
#endif 
            break;
        }

        if (!supported)
        {
            QMessageBox::warning(this,
                tr("Warning"),
                QString::fromStdString(info),
                QMessageBox::Ok);
            GPUState = GPUMode::UNSUPPORT;
            ui->checkBoxGPUMode->setCheckState(Qt::Unchecked);
        }
        else
        {
            try
            {
                acCreator.init();
            }
            catch (const std::exception& err)
            {
                QMessageBox::warning(this,
                    tr("Warning"),
                    QString::fromStdString(err.what()),
                    QMessageBox::Ok);

                ui->checkBoxGPUMode->setCheckState(Qt::Unchecked);
                return;
            }

            GPUState = GPUMode::INITIALZED;
            QMessageBox::information(this,
                tr("Notice"),
                "initialize successful!\n" +
                QString::fromStdString(info),
                QMessageBox::Ok);
            ui->textBrowserInfoOut->insertPlainText("GPU initialize successfully!\n" + QString::fromStdString(info) + "\n");
            ui->textBrowserInfoOut->moveCursor(QTextCursor::End);
            ui->spinBoxPlatformID->setEnabled(false);
            ui->spinBoxDeviceID->setEnabled(false);
            ui->pushButtonReleaseGPU->setEnabled(true);
            ui->spinBoxOpenCLQueueNum->setEnabled(false);
            ui->checkBoxOpenCLParallelIO->setEnabled(false);
        }
    }
    else if ((state == Qt::Checked) && (GPUState == GPUMode::UNSUPPORT))
    {
        QMessageBox::warning(this,
            tr("Warning"),
            tr("Unsupport GPU acceleration in this platform"),
            QMessageBox::Ok);
        ui->checkBoxGPUMode->setCheckState(Qt::Unchecked);
    }
}

void MainWindow::on_actionList_GPUs_triggered()
{
    on_pushButtonListGPUs_clicked();
}

void MainWindow::on_actionBenchmark_triggered()
{
    int pID = ui->spinBoxPlatformID->value(), dID = ui->spinBoxDeviceID->value();

    double CPUScore = Anime4KCPP::benchmark<Anime4KCPP::CPU::ACNet>();
    double OpenCLScore = Anime4KCPP::benchmark<Anime4KCPP::OpenCL::ACNet>(pID, dID);
#ifdef ENABLE_CUDA
    double CudaScore = Anime4KCPP::benchmark<Anime4KCPP::Cuda::ACNet>(dID);
#endif 

    QMessageBox::information(this,
        tr("Benchmark"),
        QString(
            tr("Benchmark result:") +
            "\n\nCPU score: %1\n"
            "OpenCL score: %2\n"
            "(pID = %3, dID = %4)\n"
#ifdef ENABLE_CUDA
            "CUDA score: %5\n"
            "(dID = %6)\n"
#endif
        ).arg(CPUScore).arg(OpenCLScore).arg(pID).arg(dID)
#ifdef ENABLE_CUDA
        .arg(CudaScore).arg(dID)
#endif
        ,QMessageBox::Ok);
}

void MainWindow::on_pushButtonListGPUs_clicked()
{
    Anime4KCPP::OpenCL::GPUList openclGPUList = Anime4KCPP::OpenCL::listGPUs();

    bool flag = openclGPUList.platforms == 0;

    std::string displayInfo ="OpenCL:\n" + openclGPUList();

#ifdef ENABLE_CUDA
    Anime4KCPP::Cuda::GPUList cudaGPUList = Anime4KCPP::Cuda::listGPUs();
    flag = flag && (cudaGPUList.devices == 0);
    displayInfo += "\nCUDA:\n" + cudaGPUList();
#endif

    if (flag)
    {
        QMessageBox::warning(this,
            tr("Warning"),
            tr("Unsupport GPU acceleration in this platform"),
            QMessageBox::Ok);
        return;
    }

    QMessageBox::information(this,
        tr("Notice"),
        QString::fromStdString(displayInfo),
        QMessageBox::Ok);
}

void MainWindow::on_pushButtonOutputPathOpen_clicked()
{
    QDir outputPath(ui->lineEditOutputPath->text());
    if (!outputPath.exists())
    {
        errorHandler(ErrorType::DIR_NOT_EXIST);
        return;
    }
    QDesktopServices::openUrl(QUrl("file:///" + outputPath.absolutePath(), QUrl::TolerantMode));
}

void MainWindow::on_pushButtonReleaseGPU_clicked()
{
    if (GPUState == GPUMode::INITIALZED)
    {
        acCreator.deinit(true);
        GPUState = GPUMode::UNINITIALZED;
    }

    QMessageBox::information(this,
        tr("Notice"),
        tr("Successfully release GPU"),
        QMessageBox::Ok);

    ui->checkBoxGPUMode->setCheckState(Qt::Unchecked);
    ui->spinBoxPlatformID->setEnabled(true);
    ui->spinBoxDeviceID->setEnabled(true);
    ui->pushButtonReleaseGPU->setEnabled(false);

    if (ui->comboBoxGPGPU->currentIndex() == GPGPU::OpenCL)
    {
        ui->spinBoxOpenCLQueueNum->setEnabled(true);
        ui->checkBoxOpenCLParallelIO->setEnabled(true);
    }
    else if (ui->comboBoxGPGPU->currentIndex() == GPGPU::CUDA)
    {
        ui->spinBoxOpenCLQueueNum->setEnabled(false);
        ui->checkBoxOpenCLParallelIO->setEnabled(false);
    }
}

void MainWindow::on_checkBoxACNet_stateChanged(int state)
{
    if (GPUState == GPUMode::INITIALZED)
    {
        acCreator.deinit(true);
        GPUState = GPUMode::UNINITIALZED;
        on_checkBoxGPUMode_stateChanged(ui->checkBoxGPUMode->checkState());
    }
    if (state == Qt::Checked)
    {
        ui->spinBoxPasses->setEnabled(false);
        ui->spinBoxPushColorCount->setEnabled(false);
        ui->doubleSpinBoxPushColorStrength->setEnabled(false);
        ui->doubleSpinBoxPushGradientStrength->setEnabled(false);
        ui->tabPreprocessing->setEnabled(false);
        ui->tabPostprocessing->setEnabled(false);
        ui->checkBoxHDN->setEnabled(true);
        ui->spinBoxHDNLevel->setEnabled(true);
    }
    else
    {
        ui->spinBoxPasses->setEnabled(true);
        ui->spinBoxPushColorCount->setEnabled(true);
        ui->doubleSpinBoxPushColorStrength->setEnabled(true);
        ui->doubleSpinBoxPushGradientStrength->setEnabled(true);
        ui->tabPreprocessing->setEnabled(true);
        ui->tabPostprocessing->setEnabled(true);
        ui->checkBoxHDN->setEnabled(false);
        ui->spinBoxHDNLevel->setEnabled(false);
    }
}

void MainWindow::on_pushButtonForceStop_clicked()
{
    if (QMessageBox::Yes == QMessageBox::warning(this, tr("Confirm"),
        tr("Do you really want to stop all tasks?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No))
    {
        stop = true;
    }
}

void MainWindow::on_pushButtonPause_clicked()
{
    pause = ProcessingState::PAUSE;
    ui->pushButtonPause->setEnabled(false);
    ui->pushButtonContinue->setEnabled(true);
}

void MainWindow::on_pushButtonContinue_clicked()
{
    pause = ProcessingState::CONTINUE;
    ui->pushButtonPause->setEnabled(true);
    ui->pushButtonContinue->setEnabled(false);
}

void MainWindow::on_comboBoxGPGPU_currentIndexChanged(int idx)
{
    acCreator.deinit(true);
    GPUState = GPUMode::UNINITIALZED;
    ui->spinBoxPlatformID->setEnabled(true);
    ui->spinBoxDeviceID->setEnabled(true);
    ui->pushButtonReleaseGPU->setEnabled(false);
    if (idx == GPGPU::OpenCL)
    {
        ui->spinBoxOpenCLQueueNum->setEnabled(true);
        ui->checkBoxOpenCLParallelIO->setEnabled(true);
    }
    else if (idx == GPGPU::CUDA)
    {
        ui->spinBoxOpenCLQueueNum->setEnabled(false);
        ui->checkBoxOpenCLParallelIO->setEnabled(false);
    }
    on_checkBoxGPUMode_stateChanged(ui->checkBoxGPUMode->checkState());
}
