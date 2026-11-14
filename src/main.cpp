#include "launcher_engine.h"
#include "zip_archive.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QLinearGradient>
#include <QPolygon>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QSettings>
#include <QSet>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextEdit>
#include <QTextCursor>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QProcess>
#include <QScreen>
#include <QSize>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTabWidget>
#include <QUrl>
#include <QClipboard>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonParseError>
#include <QEasingCurve>
#include <QSaveFile>
#include <QProcessEnvironment>
#include <QPointer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <utility>
#include <memory>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QString kGreen = "#39d98a";
QString kPanel = "#111216";
QString kCard  = "#1b1c20";
QString kMuted = "#969ba6";
QString kAccentBlue   = "#347ded";
QString kAccentPurple = "#8956ed";

// ── Tema renk paleti ──────────────────────────────────────────────────────────
struct Theme {
    QString bg, bgDeep, card, border, text, textDim, textMuted;
    QString green, accentBlue, accentPurple;
    QString scrollBg, scrollHandle, scrollHandleHover;
    QString inputBg, inputBorder, inputText;
    QString titleBar, statusBar;
    bool dark;
};

static Theme gTheme; // main() içinde doldurulur

static void applyTheme(bool dark) {
    if (dark) {
        gTheme = {
            "#181a1f", "#111216", "#1b1c20", "#292b30",
            "#eef0f4", "#c5c8cf", "#969ba6",
            "#39d98a", "#347ded", "#8956ed",
            "#111216", "#3b3f48", "#59606c",
            "#1e2026", "#2b2e35", "#dce0e7",
            "#101114", "#111317", true
        };
        // global renk değişkenlerini güncelle
        kPanel = gTheme.bgDeep;
        kCard  = gTheme.card;
        kMuted = gTheme.textMuted;
        kGreen = gTheme.green;
        kAccentBlue   = gTheme.accentBlue;
        kAccentPurple = gTheme.accentPurple;
    } else {
        gTheme = {
            "#f5f6f8", "#eaecf0", "#ffffff", "#c8cdd6",
            "#1a1d23", "#3a3d44", "#5a6070",
            "#17916a", "#1a5fc4", "#6633b8",
            "#eaecf0", "#9aa0ad", "#6b7280",
            "#ffffff", "#c8cdd6", "#1a1d23",
            "#dfe2e8", "#e8eaef", false
        };
        kPanel = gTheme.bgDeep;
        kCard  = gTheme.card;
        kMuted = gTheme.textMuted;
        kGreen = gTheme.green;
        kAccentBlue   = gTheme.accentBlue;
        kAccentPurple = gTheme.accentPurple;
    }
}

static bool isDarkTheme() { return gTheme.dark; }
struct TaskResult { bool ok = false; QString error, text; QJsonArray array; QJsonObject object, profile; LaunchSpec launch; };
QByteArray requestGitHubApi(const QString& url) {
    const QUrl parsed(url);
    if (!parsed.isValid() || parsed.scheme() != "https" || parsed.host().compare("api.github.com", Qt::CaseInsensitive) != 0)
        throw std::runtime_error("GitHub API için beklenmeyen bağlantı reddedildi.");
    QNetworkAccessManager manager;
    QNetworkRequest request(parsed);
    request.setRawHeader("User-Agent", "TitanLauncherCpp/1.0");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(30000);
    loop.exec();
    timer.stop();
    const QByteArray bytes = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = reply->errorString();
    const QUrl finalUrl = reply->url();
    const auto networkError = reply->error();
    delete reply;
    if (finalUrl.scheme() != "https" || finalUrl.host().compare("api.github.com", Qt::CaseInsensitive) != 0)
        throw std::runtime_error("GitHub API güvenli olmayan bir adrese yönlendirdi.");
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300)
        throw std::runtime_error(QString("Horizon sürüm bilgisi alınamadı (HTTP %1): %2").arg(status).arg(error).toStdString());
    return bytes;
}
QByteArray requestHorizonModsIndex() {
    const QUrl url("https://horizon-rust.github.io/horizon-mods/index.json");
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "TitanLauncherCpp/1.0");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QByteArray bytes;
    bool tooLarge = false;
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
        const QByteArray chunk = reply->readAll();
        if (bytes.size() + chunk.size() > 8 * 1024 * 1024) { tooLarge = true; reply->abort(); return; }
        bytes += chunk;
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(30000);
    loop.exec();
    timer.stop();
    bytes += reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString error = reply->errorString();
    const QUrl finalUrl = reply->url();
    const auto networkError = reply->error();
    delete reply;
    if (tooLarge || bytes.size() > 8 * 1024 * 1024) throw std::runtime_error("Horizon mod dizini 8 MB sınırını aşıyor.");
    if (finalUrl.scheme() != "https" || finalUrl.host().compare("horizon-rust.github.io", Qt::CaseInsensitive) != 0)
        throw std::runtime_error("Horizon mod dizini beklenmeyen bir adrese yönlendirdi.");
    if (networkError != QNetworkReply::NoError || status < 200 || status >= 300)
        throw std::runtime_error(QString("Horizon mod dizini alınamadı (HTTP %1): %2").arg(status).arg(error).toStdString());
    return bytes;
}
bool validHorizonModId(const QString& id) {
    return QRegularExpression("^[a-z0-9_]{1,64}$").match(id).hasMatch();
}
bool horizonModFileUrlAllowed(const QJsonObject& project, const QString& urlText, const QString& filename) {
    const QUrl url(urlText);
    if (!url.isValid() || url.scheme() != "https" || url.host().compare("github.com", Qt::CaseInsensitive) != 0 ||
        !url.userInfo().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty()) return false;
    const QString path = url.path();
    if (!path.endsWith('/' + filename)) return false;
    if (path.startsWith("/horizon-rust/horizon-mods/releases/download/")) return true;
    QUrl source(project.value("source").toString());
    if (!source.isValid() || source.scheme() != "https" || source.host().compare("github.com", Qt::CaseInsensitive) != 0) return false;
    QStringList parts = source.path().split('/', Qt::SkipEmptyParts);
    if (parts.size() != 2) return false;
    if (parts[1].endsWith(".git")) parts[1].chop(4);
    const QString prefix = '/' + parts[0] + '/' + parts[1] + "/releases/download/";
    return path.startsWith(prefix);
}
bool horizonModVersionUsable(const QJsonObject& project, const QJsonObject& version) {
    const QString kind = project.value("kind").toString();
    const QString extension = kind == "native-mod" ? ".hnmod" : ".hmod";
    const QJsonObject file = version.value("file").toObject();
    const QString name = file.value("name").toString();
    const QString hash = file.value("sha256").toString();
    const qint64 size = file.value("size").toVariant().toLongLong();
    if (version.value("game").toString() != "26.3" || (kind != "native-mod" && kind != "mod") ||
        !validHorizonModId(project.value("id").toString()) || name.isEmpty() || name != QFileInfo(name).fileName() ||
        name.startsWith('.') || !name.endsWith(extension, Qt::CaseInsensitive) || size <= 0 || size > 64LL * 1024 * 1024 ||
        !QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch() ||
        !horizonModFileUrlAllowed(project, file.value("url").toString(), name)) return false;
    if (kind == "mod") {
        const QJsonArray capabilities = version.value("capabilities").toArray();
        if (capabilities.size() != 1 || capabilities.first().toString() != "registries") return false;
    }
    return true;
}
QString horizonModSignatureIssue(const QJsonObject& project, const QJsonObject& version) {
    const QString key = project.value("key").toString().trimmed();
    const QString signature = version.value("file").toObject().value("signature").toString().trimmed();
    if (key.isEmpty() && signature.isEmpty()) return {};
    if (key.isEmpty()) return "Yayımcı imzası var ancak dizinde bu proje için Minisign anahtarı yok.";
    if (signature.isEmpty()) return "Projenin Minisign anahtarı var ancak bu sürüm imzasız.";
    if (key.size() > 256 || signature.toUtf8().size() > 16 * 1024)
        return "Yayımcı anahtarı veya imza beklenen boyut sınırını aşıyor.";
    return {};
}
QJsonObject newestHorizonModVersion(const QJsonObject& project) {
    for (const QJsonValue& value : project.value("versions").toArray()) {
        const QJsonObject version = value.toObject();
        if (horizonModVersionUsable(project, version)) return version;
    }
    return {};
}
bool horizonModRevoked(const QJsonObject& index, const QString& id, const QString& sha256) {
    for (const QJsonValue& value : index.value("revoked").toArray()) {
        const QJsonObject item = value.toObject();
        if (item.value("id").toString() == id && item.value("sha256").toString().compare(sha256, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}
QString tomlStringField(const QString& text, const QString& section, const QString& key) {
    QString currentSection;
    const QRegularExpression sectionLine("^\\s*\\[([^\\]]+)\\]\\s*(?:#.*)?$");
    const QRegularExpression fieldLine("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*\"([^\"]*)\"\\s*(?:#.*)?$");
    for (const QString& raw : text.split('\n')) {
        const QString line = raw.trimmed();
        const auto sectionMatch = sectionLine.match(line);
        if (sectionMatch.hasMatch()) { currentSection = sectionMatch.captured(1).trimmed(); continue; }
        if (currentSection != section) continue;
        const auto fieldMatch = fieldLine.match(line);
        if (fieldMatch.hasMatch() && fieldMatch.captured(1) == key) return fieldMatch.captured(2);
    }
    return {};
}
QStringList tomlStringArrayField(const QString& text, const QString& key) {
    const QRegularExpression line("^\\s*" + QRegularExpression::escape(key) + "\\s*=\\s*\\[([^\\]]*)\\]\\s*(?:#.*)?$");
    const QRegularExpression item("\"([^\"]*)\"");
    for (const QString& raw : text.split('\n')) {
        const auto match = line.match(raw);
        if (!match.hasMatch()) continue;
        QStringList result;
        auto it = item.globalMatch(match.captured(1));
        while (it.hasNext()) result.append(it.next().captured(1));
        return result;
    }
    return {};
}
bool allowedHorizonNativePayload(const QString& relative) {
    if (relative.endsWith(".rs") || relative.endsWith(".toml")) return true;
    const QStringList parts = relative.split('/');
    if (parts.size() >= 2 && (parts.first() == "data" || (parts.size() >= 3 && parts.at(1) == "data"))) return true;
    if (parts.size() <= 2) {
        const QString name = parts.last();
        for (const QString& prefix : {"README", "LICENSE", "LICENCE", "COPYING", "NOTICE"}) if (name.startsWith(prefix)) return true;
        if (name == ".gitignore") return true;
    }
    return false;
}
void validateHorizonNativeBundle(const QString& path, const QJsonObject& project, const QJsonObject& version) {
    ZipArchive archive;
    QString why;
    if (!archive.open(path, &why)) throw std::runtime_error((".hnmod ZIP açılamadı: " + why).toStdString());
    const auto entries = archive.entries();
    if (entries.isEmpty() || entries.size() > 512) throw std::runtime_error(".hnmod dosyası 1–512 ZIP girdisi içermeli.");
    QSet<QString> names;
    QHash<QString, QByteArray> payloads;
    quint64 total = 0;
    for (const ZipEntry& entry : entries) {
        QString name = entry.name;
        if (name.endsWith('/')) continue;
        if (name.contains('\\') || name.startsWith('/') || name.contains(':')) throw std::runtime_error(".hnmod içinde güvenli olmayan dosya yolu var.");
        const QStringList parts = name.split('/', Qt::KeepEmptyParts);
        for (const QString& part : parts) if (part.isEmpty() || part == "." || part == "..") throw std::runtime_error(".hnmod içinde güvenli olmayan dosya yolu var.");
        const quint32 mode = entry.externalAttributes >> 16;
        const quint32 type = mode & 0170000u;
        if (type != 0 && type != 0100000u) throw std::runtime_error(".hnmod dosyası bağlantı veya özel dosya girdisi içeriyor.");
        if (names.contains(name)) throw std::runtime_error(".hnmod içinde yinelenen ZIP dosya adı var.");
        names.insert(name);
        if (entry.size > 64u * 1024u * 1024u || total + entry.size > 64u * 1024u * 1024u)
            throw std::runtime_error(".hnmod açıldığında 64 MB sınırını aşıyor.");
        total += entry.size;
        QByteArray bytes = archive.read(entry.name, &why);
        if (!why.isEmpty()) throw std::runtime_error((".hnmod girdisi doğrulanamadı: " + why).toStdString());
        payloads.insert(name, bytes);
    }
    const QString id = project.value("id").toString();
    const QString versionId = version.value("version").toString();
    const QString manifestName = "horizon-native.json";
    if (!payloads.contains(manifestName)) throw std::runtime_error(".hnmod içinde horizon-native.json bulunamadı.");
    QJsonParseError parse{};
    const QJsonDocument nativeDoc = QJsonDocument::fromJson(payloads.value(manifestName), &parse);
    if (parse.error != QJsonParseError::NoError || !nativeDoc.isObject()) throw std::runtime_error(".hnmod horizon-native.json dosyası geçerli değil.");
    const QJsonObject native = nativeDoc.object();
    const QSet<QString> nativeFields{"schema","id","version","game","horizon","mixins","hooks"};
    for (auto it=native.begin();it!=native.end();++it) if(!nativeFields.contains(it.key()))
        throw std::runtime_error(".hnmod native manifest tanınmayan bir alan içeriyor.");
    const QString horizon = native.value("horizon").toString();
    const QString mixins = native.value("mixins").toString();
    const QString expectedApi = "native-1/" + horizon + '/' + mixins;
    if (native.value("schema").toInt() != 1 || native.value("id").toString() != id ||
        native.value("version").toString() != versionId || native.value("game").toString() != "26.3" ||
        !QRegularExpression("^[0-9a-f]{40}$").match(horizon).hasMatch() ||
        !QRegularExpression("^[0-9a-f]{40}$").match(mixins).hasMatch() ||
        (native.contains("hooks") && (!native.value("hooks").isDouble() || native.value("hooks").toDouble() < 1 || std::floor(native.value("hooks").toDouble()) != native.value("hooks").toDouble())) ||
        version.value("api").toString() != expectedApi)
        throw std::runtime_error(".hnmod kimliği, sürümü, Horizon sabitlemesi veya dizin API bilgisi eşleşmiyor.");
    const QString prefix = "mixins/" + id + '/';
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        if (it.key() == manifestName) continue;
        if (!it.key().startsWith(prefix) || !allowedHorizonNativePayload(it.key().mid(prefix.size())))
            throw std::runtime_error(".hnmod beklenmeyen kaynak dosyası içeriyor: " + it.key().toStdString());
    }
    const QString mixinPath = prefix + "horizon-mixin.toml";
    if (!payloads.contains(mixinPath)) throw std::runtime_error(".hnmod içinde horizon-mixin.toml bulunamadı.");
    const QString mixin = QString::fromUtf8(payloads.value(mixinPath));
    if (tomlStringField(mixin, "mod", "id") != id || tomlStringField(mixin, "mod", "version") != versionId ||
        tomlStringField(mixin, "compatibility", "horizon_revision") != horizon)
        throw std::runtime_error(".hnmod native manifest ile mixin manifest kimliği/sürümü uyuşmuyor.");
}
QJsonObject horizonNativeBundleManifest(const QString& path) {
    ZipArchive archive;
    QString why;
    if (!archive.open(path, &why)) throw std::runtime_error((".hnmod ZIP açılamadı: " + why).toStdString());
    const QByteArray bytes = archive.read("horizon-native.json", &why);
    if (!why.isEmpty() || bytes.isEmpty()) throw std::runtime_error(".hnmod içinde horizon-native.json okunamadı.");
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) throw std::runtime_error(".hnmod native manifest'i geçerli JSON değil.");
    return document.object();
}
void validateHorizonContentBundle(const QString& path, const QJsonObject& project, const QJsonObject& version) {
    ZipArchive archive;
    QString why;
    if (!archive.open(path, &why)) throw std::runtime_error((".hmod ZIP açılamadı: " + why).toStdString());
    const auto entries = archive.entries();
    if (entries.isEmpty() || entries.size() > 4096) throw std::runtime_error(".hmod dosyası boş veya çok fazla ZIP girdisi içeriyor.");
    const QString id = project.value("id").toString();
    const QString versionId = version.value("version").toString();
    QSet<QString> names;
    QByteArray manifest;
    quint64 total = 0;
    for (const ZipEntry& entry : entries) {
        QString name = entry.name;
        if (name.endsWith('/')) continue;
        if (name.contains('\\') || name.startsWith('/') || name.contains(':')) throw std::runtime_error(".hmod içinde güvenli olmayan dosya yolu var.");
        const QStringList parts = name.split('/', Qt::KeepEmptyParts);
        for (const QString& part : parts) if (part.isEmpty() || part == "." || part == "..") throw std::runtime_error(".hmod içinde güvenli olmayan dosya yolu var.");
        if (names.contains(name)) throw std::runtime_error(".hmod içinde yinelenen ZIP dosya adı var.");
        names.insert(name);
        if (name != "horizon-mod.toml" && name != "mod.wasm" && !name.startsWith("assets/") && !name.startsWith("data/"))
            throw std::runtime_error(".hmod beklenmeyen dosya içeriyor: " + name.toStdString());
        if (entry.size > 64u * 1024u * 1024u || total + entry.size > 64u * 1024u * 1024u ||
            name == "mod.wasm" && entry.size > 32u * 1024u * 1024u)
            throw std::runtime_error(".hmod izin verilen açılmış dosya boyutunu aşıyor.");
        total += entry.size;
        QByteArray bytes = archive.read(entry.name, &why);
        if (!why.isEmpty()) throw std::runtime_error((".hmod girdisi doğrulanamadı: " + why).toStdString());
        if (name == "horizon-mod.toml") manifest = bytes;
    }
    if (manifest.isEmpty()) throw std::runtime_error(".hmod içinde horizon-mod.toml bulunamadı.");
    const QString text = QString::fromUtf8(manifest);
    const QStringList capabilities = tomlStringArrayField(text, "capabilities");
    if (tomlStringField(text, {}, "id") != id || tomlStringField(text, {}, "version") != versionId ||
        tomlStringField(text, {}, "api") != version.value("api").toString() ||
        tomlStringField(text, {}, "api") != "0.2" || capabilities != QStringList{"registries"})
        throw std::runtime_error(".hmod kimliği, sürümü, API veya registries yeteneği dizin bilgisiyle uyuşmuyor.");
    if (!names.contains("mod.wasm") && capabilities != QStringList{"registries"})
        throw std::runtime_error(".hmod kod dosyası içermiyor ve içerik-modu değil.");
}
bool writeHorizonModEnabled(const QString& gameHome, const QString& id, QString* error) {
    const QString path = QDir(gameHome).filePath("horizon-mods.txt");
    QStringList lines;
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) { if (error) *error = existing.errorString(); return false; }
        lines = QString::fromUtf8(existing.readAll()).split('\n');
    }
    const QString key = "mod." + id + ':';
    bool found = false;
    for (QString& line : lines) {
        if (line.trimmed().startsWith(key)) { line = key + "true"; found = true; }
    }
    if (!found) lines.append(key + "true");
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    QSaveFile output(path);
    const QByteArray bytes = (lines.join('\n') + '\n').toUtf8();
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
        if (error) *error = output.errorString();
        return false;
    }
    return true;
}
QString horizonAssetUrl(const QJsonObject& release, const QString& assetName) {
    for (const QJsonValue& value : release.value("assets").toArray()) {
        const QJsonObject asset = value.toObject();
        if (asset.value("name").toString() != assetName) continue;
        const QUrl url(asset.value("browser_download_url").toString());
        if (url.isValid() && url.scheme() == "https" && url.host().compare("github.com", Qt::CaseInsensitive) == 0 &&
            url.path().startsWith("/horizon-rust/horizon/releases/download/")) return url.toString();
        throw std::runtime_error("Horizon sürümündeki indirme adresi beklenen resmî GitHub adresi değil.");
    }
    return {};
}
QString findHorizonExecutable(const QString& root, const QString& name) {
    if (root.trimmed().isEmpty() || !QFileInfo(root).isDir()) return {};
    QDirIterator it(root, QStringList{name}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    return it.hasNext() ? QFileInfo(it.next()).absoluteFilePath() : QString();
}
QString sha1File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}
QString sha256File(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return {};
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}
int runHorizonTool(const QString& executable, const QStringList& arguments, const QString& workingDirectory,
                   const QProcessEnvironment& environment, const LauncherEngine::Progress& progress,
                   QString* output, QString* error) {
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.setWorkingDirectory(workingDirectory);
    process.setProcessEnvironment(environment);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(15000)) {
        if (error) *error = process.errorString();
        return -1;
    }
    QByteArray allOutput;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(300);
        const QByteArray chunk = process.readAll();
        if (!chunk.isEmpty()) {
            allOutput += chunk;
            if (progress) progress(QString::fromUtf8(chunk).trimmed(), -1);
        }
    }
    allOutput += process.readAll();
    if (output) *output = QString::fromUtf8(allOutput);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QString("İşlem hata koduyla kapandı (%1).\n%2").arg(process.exitCode()).arg(QString::fromUtf8(allOutput).right(5000));
        return process.exitCode() == 0 ? -1 : process.exitCode();
    }
    return 0;
}
QPushButton* actionButton(const QString& text, const QString& color, const QString& hover, QWidget* parent = nullptr) {
    auto* b = new QPushButton(text, parent); b->setCursor(Qt::PointingHandCursor); b->setMinimumHeight(40);
    b->setStyleSheet(QString(
        "QPushButton{background:%1;color:#f4f5f8;border:0;border-radius:9px;padding:8px 12px;"
        "font-weight:800;text-align:center;transition: all 0.15s;}"
        "QPushButton:hover{background:%2;}"
        "QPushButton:pressed{padding-top:10px;}"
        "QPushButton:disabled{background:#303238;color:#777b84;}"
    ).arg(color, hover));
    QObject::connect(b, &QPushButton::clicked, b, [b] {
        if (b->property("tapAnimationActive").toBool()) return;
        b->setProperty("tapAnimationActive", true);
        auto* effect = new QGraphicsOpacityEffect(b);
        b->setGraphicsEffect(effect);
        effect->setOpacity(1.0);
        auto* animation = new QPropertyAnimation(effect, "opacity", b);
        animation->setDuration(200);
        animation->setKeyValueAt(0.0,  1.0);
        animation->setKeyValueAt(0.25, 0.65);
        animation->setKeyValueAt(0.65, 0.88);
        animation->setKeyValueAt(1.0,  1.0);
        animation->setEasingCurve(QEasingCurve::InOutSine);
        QObject::connect(animation, &QPropertyAnimation::finished, b, [b, effect, animation] {
            if (b->graphicsEffect() == effect) b->setGraphicsEffect(nullptr);
            b->setProperty("tapAnimationActive", false);
            animation->deleteLater();
        });
        animation->start();
    });
    return b;
}
void animateStackPage(QStackedWidget* stack, int index) {
    if (!stack || index < 0 || index >= stack->count() || index == stack->currentIndex()) return;
    QWidget* page = stack->widget(index);
    if (auto* oldAnimation = page->findChild<QPropertyAnimation*>("pageFadeAnimation")) {
        oldAnimation->stop();
        oldAnimation->deleteLater();
    }
    if (page->graphicsEffect()) page->setGraphicsEffect(nullptr);
    stack->setCurrentIndex(index);

    // Fade-in animasyonu
    auto* effect = new QGraphicsOpacityEffect(page);
    effect->setOpacity(0.0);
    page->setGraphicsEffect(effect);
    auto* fadeAnim = new QPropertyAnimation(effect, "opacity", page);
    fadeAnim->setObjectName("pageFadeAnimation");
    fadeAnim->setDuration(220);
    fadeAnim->setStartValue(0.0);
    fadeAnim->setEndValue(1.0);
    fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(fadeAnim, &QPropertyAnimation::finished, page, [page, effect, fadeAnim] {
        if (page->graphicsEffect() == effect) page->setGraphicsEffect(nullptr);
        fadeAnim->deleteLater();
    });
    fadeAnim->start();
}
class AnimatedComboBox final : public QComboBox {
public:
    using QComboBox::QComboBox;

protected:
    void showPopup() override {
        cancelPopupAnimation();
        closing_ = false;
        QComboBox::showPopup();

        QWidget* popup = view() ? view()->window() : nullptr;
        if (!popup || !popup->isVisible()) return;

        const QRect target = popup->geometry();
        const QRect comboGlobal(mapToGlobal(QPoint(0, 0)), size());
        const bool opensUp = target.center().y() < comboGlobal.center().y();
        QRect collapsed = target;
        if (opensUp) collapsed.setTop(target.bottom() - 1);
        else collapsed.setBottom(target.top() + 1);
        popup->setGeometry(collapsed);

        auto* animation = new QPropertyAnimation(popup, "geometry", this);
        popupAnimation_ = animation;
        animation->setDuration(175);
        animation->setStartValue(collapsed);
        animation->setEndValue(target);
        animation->setEasingCurve(QEasingCurve::OutCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this, animation] {
            if (popupAnimation_ == animation) popupAnimation_ = nullptr;
            animation->deleteLater();
        });
        animation->start();
    }

    void hidePopup() override {
        QWidget* popup = view() ? view()->window() : nullptr;
        if (!popup || !popup->isVisible()) {
            QComboBox::hidePopup();
            return;
        }
        if (closing_) return;

        cancelPopupAnimation();
        closing_ = true;
        const QRect start = popup->geometry();
        const QRect comboGlobal(mapToGlobal(QPoint(0, 0)), size());
        const bool opensUp = start.center().y() < comboGlobal.center().y();
        QRect collapsed = start;
        if (opensUp) collapsed.setTop(start.bottom() - 1);
        else collapsed.setBottom(start.top() + 1);

        auto* animation = new QPropertyAnimation(popup, "geometry", this);
        popupAnimation_ = animation;
        animation->setDuration(125);
        animation->setStartValue(start);
        animation->setEndValue(collapsed);
        animation->setEasingCurve(QEasingCurve::InCubic);
        connect(animation, &QPropertyAnimation::finished, this, [this, popup, animation] {
            closing_ = false;
            if (popup) QComboBox::hidePopup();
            if (popupAnimation_ == animation) popupAnimation_ = nullptr;
            animation->deleteLater();
        });
        animation->start();
    }

private:
    void cancelPopupAnimation() {
        if (!popupAnimation_) return;
        popupAnimation_->stop();
        popupAnimation_->deleteLater();
        popupAnimation_ = nullptr;
    }

    QPointer<QPropertyAnimation> popupAnimation_;
    bool closing_ = false;
};
QFrame* card(QWidget* parent = nullptr) {
    auto* f = new QFrame(parent);
    f->setStyleSheet(QString("QFrame{background:%1;border:1px solid %2;border-radius:12px;}")
        .arg(gTheme.card, gTheme.border));
    return f;
}
QLabel* label(const QString& text, int size = 12, const QString& color = "#e7e9ee", bool bold = false, QWidget* parent = nullptr) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QString("background:transparent;color:%1;font-size:%2px;%3")
        .arg(color).arg(size).arg(bold ? "font-weight:800;" : ""));
    return l;
}
QString playerName(const QJsonObject& cfg) {
    if (cfg.value("account_type").toString() == "microsoft") return cfg.value("ms_account").toObject().value("name").toString("Player");
    return cfg.value("username").toString("Steve");
}
bool versionNewer(const QString& a, const QString& b) {
    auto key = [](const QString& version) {
        QStringList out;
        for (const QString& part : version.section('-', 0, 0).split('.')) {
            const auto m = QRegularExpression("^(\\d+)").match(part);
            out << QString::number(m.hasMatch() ? m.captured(1).toLongLong() : 0);
        }
        while (out.size() < 4) out << "0";
        return out;
    };
    const QStringList ka = key(a), kb = key(b);
    for (int i = 0; i < 4; ++i) {
        const qlonglong na = ka[i].toLongLong(), nb = kb[i].toLongLong();
        if (na != nb) return na > nb;
    }
    return a > b;
}
}

class Splash final : public QWidget {
public:
    Splash() : QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint) {
        setFixedSize(340, 240); setStyleSheet("background:#111216;color:#f4f5f8;border-radius:14px;");
        setAttribute(Qt::WA_TranslucentBackground, false);
        auto* v = new QVBoxLayout(this); v->setContentsMargins(28, 26, 28, 26); v->setSpacing(10);
        auto* logo = label("TUKETICI <span style='color:#ff7b35'>LAUNCHER</span>", 18, "#f7f7f8", true, this);
        logo->setTextFormat(Qt::RichText); logo->setAlignment(Qt::AlignCenter); v->addWidget(logo);
        auto* subtitle = label("v1.0  •  Qt 6", 10, "#4e545f", false, this);
        subtitle->setAlignment(Qt::AlignCenter); v->addSpacing(2); v->addWidget(subtitle);
        v->addSpacing(6);
        auto* title = label("Başlatıcı hazırlanıyor", 13, "#d9dce2", true, this);
        title->setAlignment(Qt::AlignCenter); v->addWidget(title);
        auto* detail = label("Oyun profilleri kontrol ediliyor…", 11, kMuted, false, this);
        detail->setAlignment(Qt::AlignCenter); v->addWidget(detail);
        v->addSpacing(10);
        auto* bar = new QProgressBar(this);
        bar->setRange(0, 0); bar->setTextVisible(false); bar->setFixedHeight(5);
        bar->setStyleSheet(
            "QProgressBar{background:#202228;border:0;border-radius:2px;}"
            "QProgressBar::chunk{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,"
            "stop:0 #39d98a,stop:1 #347ded);border-radius:2px;}"
        );
        v->addWidget(bar);

        // Logo fade-in
        auto* logoEffect = new QGraphicsOpacityEffect(logo);
        logo->setGraphicsEffect(logoEffect);
        logoEffect->setOpacity(0.0);
        auto* logoFade = new QPropertyAnimation(logoEffect, "opacity", this);
        logoFade->setDuration(500);
        logoFade->setStartValue(0.0);
        logoFade->setEndValue(1.0);
        logoFade->setEasingCurve(QEasingCurve::OutCubic);
        logoFade->start(QAbstractAnimation::DeleteWhenStopped);

        if (auto* screen = QGuiApplication::primaryScreen())
            move(screen->availableGeometry().center() - rect().center());
    }
};

class TitleBar final : public QWidget {
public:
    explicit TitleBar(QWidget* target) : QWidget(target), target_(target) {
        setFixedHeight(42); setStyleSheet(QString("background:%1;").arg(gTheme.titleBar));
        auto* row = new QHBoxLayout(this); row->setContentsMargins(18, 0, 12, 0); row->setSpacing(8);
        auto* appIcon = new QLabel(this); appIcon->setPixmap(QIcon(":/app.ico").pixmap(20,20)); appIcon->setFixedSize(20,20); row->addWidget(appIcon);
        auto* logo = label("TUKETICI <span style='color:#ff7b35'>LAUNCHER</span>", 14, "#f8f8f8", true, this); logo->setTextFormat(Qt::RichText); row->addWidget(logo); row->addStretch();
        auto* gear = titleButton("⚙"); auto* min = titleButton("—"); auto* max = titleButton("□"); auto* close = titleButton("×");
        close->setStyleSheet("QPushButton{color:#d8dbe2;background:transparent;border:0;font-size:20px;border-radius:5px;}QPushButton:hover{background:#c83f4d;}");
        row->addWidget(gear); row->addWidget(min); row->addWidget(max); row->addWidget(close);
        QObject::connect(min, &QPushButton::clicked, target_, [target]{ target->showMinimized(); });
        QObject::connect(max, &QPushButton::clicked, target_, [target]{ target->isMaximized() ? target->showNormal() : target->showMaximized(); });
        QObject::connect(close, &QPushButton::clicked, target_, &QWidget::close);
        QObject::connect(gear, &QPushButton::clicked, target_, [target]{ if(auto* stack=target->findChild<QStackedWidget*>())animateStackPage(stack,4); });
    }
protected:
    void mousePressEvent(QMouseEvent* e) override { if (e->button() == Qt::LeftButton) offset_ = e->globalPosition().toPoint() - target_->frameGeometry().topLeft(); }
    void mouseMoveEvent(QMouseEvent* e) override { if ((e->buttons() & Qt::LeftButton) && !target_->isMaximized()) target_->move(e->globalPosition().toPoint() - offset_); }
private:
    QPushButton* titleButton(const QString& s) { auto* b = new QPushButton(s, this); b->setFixedSize(30, 28); b->setCursor(Qt::PointingHandCursor); b->setStyleSheet("QPushButton{color:#aeb3bd;background:transparent;border:0;font-size:15px;border-radius:5px;}QPushButton:hover{background:#292b30;}QPushButton:pressed{background:#34373e;}"); QObject::connect(b,&QPushButton::clicked,b,[b]{if(b->property("tapAnimationActive").toBool())return;b->setProperty("tapAnimationActive",true);auto* effect=new QGraphicsOpacityEffect(b);b->setGraphicsEffect(effect);auto* animation=new QPropertyAnimation(effect,"opacity",b);animation->setDuration(150);animation->setKeyValueAt(0.0,1.0);animation->setKeyValueAt(0.35,0.78);animation->setKeyValueAt(1.0,1.0);QObject::connect(animation,&QPropertyAnimation::finished,b,[b,effect,animation]{if(b->graphicsEffect()==effect)b->setGraphicsEffect(nullptr);b->setProperty("tapAnimationActive",false);animation->deleteLater();});animation->start();}); return b; }
    QWidget* target_; QPoint offset_;
};

class HeroBackdrop final : public QWidget {
public:
    explicit HeroBackdrop(QWidget* parent = nullptr) : QWidget(parent) {
        const QString path = QCoreApplication::applicationDirPath() + "/background.jpg"; if (QFileInfo::exists(path)) image_.load(path);
        setAttribute(Qt::WA_StyledBackground, true);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this); const QRect r = rect();
        if (!image_.isNull()) { QPixmap scaled = image_.scaled(r.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation); const int x=(scaled.width()-r.width())/2, y=(scaled.height()-r.height())/2; p.drawPixmap(r,scaled,QRect(x,y,r.width(),r.height())); p.fillRect(r,QColor(5,8,14,105)); }
        else { QLinearGradient g(0,0,width(),height()); g.setColorAt(0,QColor("#164753")); g.setColorAt(.48,QColor("#102b38")); g.setColorAt(1,QColor("#090c12")); p.fillRect(r,g); p.setPen(Qt::NoPen); for(int i=0;i<10;++i){int x=i*(width()/8)-90; QPolygon poly; poly<<QPoint(x,0)<<QPoint(x+70,0)<<QPoint(x+230,height()*.58)<<QPoint(x+125,height()*.45); p.setBrush(QColor(67,188,190,20+(i%3)*8)); p.drawPolygon(poly);} }
        QLinearGradient shade(0,height()*.38,0,height()); shade.setColorAt(0,QColor(3,6,10,5)); shade.setColorAt(1,QColor(3,5,8,225)); p.fillRect(r,shade);
    }
private: QPixmap image_;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle("TUKETICI LAUNCHER"); setWindowIcon(QIcon(":/app.ico")); setWindowFlags(Qt::Window | Qt::FramelessWindowHint); setMinimumSize(980,600); resize(1180,700);
        mediaNetwork_ = new QNetworkAccessManager(this);
        config_ = engine_.loadConfig(); ensureAccountStore(); profiles_ = engine_.readProfiles();
        auto* central = new QWidget(this); auto* root = new QVBoxLayout(central); root->setContentsMargins(0,0,0,0); root->setSpacing(0); root->addWidget(new TitleBar(this));
        auto* body = new QHBoxLayout(); body->setContentsMargins(0,0,0,0); body->setSpacing(0);
        pages_ = new QStackedWidget(central); pages_->addWidget(buildHomePage()); pages_->addWidget(buildModsPage()); pages_->addWidget(buildSkinsPage()); pages_->addWidget(buildSettingsPage()); pages_->addWidget(buildLogsPage()); pages_->addWidget(buildScreenshotsPage()); pages_->addWidget(buildHorizonPage());
        auto* homeShortcut = new QShortcut(QKeySequence(QStringLiteral("Alt+Home")), this); connect(homeShortcut,&QShortcut::activated,this,[this]{animateStackPage(pages_,0);});
        body->addWidget(pages_,1); body->addWidget(buildSidebar(central)); root->addLayout(body,1);
        auto* bottom = new QFrame(central); bottom->setObjectName("statusDock"); bottom->setFixedHeight(58); bottom->setStyleSheet(QString("QFrame#statusDock{background:%1;border-top:1px solid %2;}").arg(gTheme.bgDeep,gTheme.border));
        auto* statusRow = new QHBoxLayout(bottom); statusRow->setContentsMargins(20,7,22,7); statusRow->setSpacing(11);
        auto* statusIcon = label("↓",16,kGreen,true,bottom); statusIcon->setAlignment(Qt::AlignCenter); statusIcon->setFixedSize(34,34); statusIcon->setStyleSheet("background:#18372c;color:#39d98a;border-radius:17px;font-size:16px;font-weight:900;"); statusRow->addWidget(statusIcon);
        auto* statusText = new QVBoxLayout(); statusText->setContentsMargins(0,0,0,0); statusText->setSpacing(1); statusText->addWidget(label("İŞLEM DURUMU",8,kMuted,true,bottom)); statusText->addWidget(statusLabel_ = label("Sistem hazırlanıyor…",11,"#e9ebef",true,bottom)); statusRow->addLayout(statusText,1);
        statusRow->addStretch(); statusRow->addWidget(progress_ = new QProgressBar(bottom)); progress_->setFixedSize(230,9); progress_->setTextVisible(false); progress_->setRange(0,100); progress_->setValue(0); progress_->setStyleSheet("QProgressBar{background:#292c33;border:0;border-radius:4px;}QProgressBar::chunk{background:#39d98a;border-radius:4px;}"); statusRow->addWidget(progressPercent_ = label("0%",10,kMuted,true,bottom)); root->addWidget(bottom);
        setCentralWidget(central); refreshAccountSelector(); updateAccountCard(); refreshProfileSelector(); loadVersions(); refreshSkinPreview();
    }
    ~MainWindow() override { }

    void showSettingsPage() { animateStackPage(pages_,3); }
protected:
    void closeEvent(QCloseEvent* e) override {
        if (gameStarting_ || activeTasks_ > 0 || (horizonProcess_ && horizonProcess_->state() != QProcess::NotRunning && !horizonPlayMode_)) { if(loginCancel_)loginCancel_->store(true); e->ignore(); QMessageBox::information(this,"İşlem sürüyor","Arka plan işlemi tamamlanınca launcher kapanabilir."); return; }
        if (horizonProcess_ && horizonProcess_->state() != QProcess::NotRunning && horizonPlayMode_) { horizonProcess_->setParent(nullptr); horizonProcess_ = nullptr; }
        if (loginCancel_) loginCancel_->store(true);
        saveConfig();
        if (process_ && process_->state() != QProcess::NotRunning) process_->setParent(nullptr);
        e->accept();
    }
private:
    LauncherEngine engine_;
    QNetworkAccessManager* mediaNetwork_ = nullptr;
    QJsonObject config_;
    QJsonArray profiles_;
    QStackedWidget* pages_ = nullptr;
    QLabel *statusLabel_ = nullptr, *accountName_ = nullptr, *accountMode_ = nullptr, *skinPreview_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* progressPercent_ = nullptr;
    QPropertyAnimation* progressAnim_ = nullptr;
    QComboBox *versionBox_ = nullptr, *loaderBox_ = nullptr, *profileBox_ = nullptr, *accountBox_ = nullptr;
    QLineEdit *offlineName_ = nullptr, *skinQuery_ = nullptr, *skinPath_ = nullptr;

    QComboBox *skinModel_ = nullptr, *modView_ = nullptr, *marketProvider_ = nullptr, *marketType_ = nullptr, *marketLoader_ = nullptr, *marketVersion_ = nullptr, *worldTarget_ = nullptr;

    QComboBox* marketplaceSort_ = nullptr;
    QLineEdit* marketSearch_ = nullptr;
    QWidget* marketResults_ = nullptr; QVBoxLayout* marketResultsLayout_ = nullptr;
    QJsonArray marketplaceResultsData_;
    bool marketplaceHasSearched_ = false;
    bool marketplaceSearchInFlight_ = false;
    QWidget* installedMods_ = nullptr; QVBoxLayout* installedModsLayout_ = nullptr;
    QWidget* horizonModsList_ = nullptr; QVBoxLayout* horizonModsListLayout_ = nullptr;
    QLineEdit* horizonModsSearch_ = nullptr;
    QComboBox* horizonModsSort_ = nullptr;
    QLabel* horizonModsStatus_ = nullptr;
    QJsonArray horizonModProjects_;
    QDateTime horizonModsFetchedAt_;
    bool horizonModsLoading_ = false;
    QTextEdit *logViewer_ = nullptr; QLabel* screenshotPreview_ = nullptr; QComboBox* screenshotBox_ = nullptr;
    QSlider* ramSlider_ = nullptr;
    QLabel* ramValue_ = nullptr;
    QLineEdit *javaPath_ = nullptr, *jvmArgs_ = nullptr, *resolutionW_ = nullptr, *resolutionH_ = nullptr, *curseKey_ = nullptr;
    QComboBox *gcMode_ = nullptr, *closeBehavior_ = nullptr;
    QLineEdit* customClientId_ = nullptr;
    QCheckBox *highPriority_ = nullptr, *forceGpu_ = nullptr, *onlyInstalled_ = nullptr, *showSnapshots_ = nullptr;
    QProcess* process_ = nullptr; QFile processLog_;
    QProcess* horizonProcess_ = nullptr;
    QTimer* horizonWindowBrandTimer_ = nullptr;
#ifdef Q_OS_WIN
    HICON horizonWindowIcon_ = nullptr;
#endif
    bool horizonPlayMode_ = false;
    QLineEdit* horizonMinecraftPath_ = nullptr;
    QLabel* horizonClientStatus_ = nullptr;
    QTextEdit* horizonOutput_ = nullptr;
    QPushButton *horizonVersionsButton_ = nullptr, *horizonInstallButton_ = nullptr, *horizonPrepareButton_ = nullptr, *horizonPlayButton_ = nullptr, *horizonStopButton_ = nullptr;
    QPushButton* horizonBuildModsButton_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QStackedWidget* modsSubpages_ = nullptr;
    QLineEdit* installedSearch_ = nullptr;
    QLabel* skinName_ = nullptr;
    QComboBox* logKind_ = nullptr;
    int activeTasks_ = 0;
    bool gameStarting_ = false;

    bool horizonLauncherHidden_ = false;
    QString selectedSkinPath_;
    std::shared_ptr<std::atomic_bool> loginCancel_;
    QPointer<QDialog> loginDialog_;

    template<class Fn, class Done>
    void runTask(Fn work, Done done) {
        ++activeTasks_; auto* watcher = new QFutureWatcher<TaskResult>(this);
        connect(watcher, &QFutureWatcher<TaskResult>::finished, this, [this, watcher, done]() mutable {
            TaskResult r = watcher->result(); watcher->deleteLater(); if(activeTasks_>0)--activeTasks_; done(std::move(r));
        });
        watcher->setFuture(QtConcurrent::run([this,work]() mutable { try { return work(); } catch (const std::exception& e) { TaskResult r; r.error = QString::fromUtf8(e.what()); engine_.appendLauncherLog("Hata: " + r.error); return r; } catch (...) { TaskResult r; r.error = "Beklenmeyen bir hata oluştu."; engine_.appendLauncherLog(r.error); return r; } }));
    }
    LauncherEngine::Progress progressCallback() {
        return [this](const QString& text, int value) { QMetaObject::invokeMethod(this, [this,text,value]{ setStatus(text,value); }, Qt::QueuedConnection); };
    }
    void setStatus(const QString& text, int value = -1) {
        if (statusLabel_) statusLabel_->setText(text);
        if (progress_ && value >= 0) {
            const int bounded = qBound(0, value, 100);
            // Progress bar değerini animasyonlu geçişle güncelle
            if (!progressAnim_) {
                progressAnim_ = new QPropertyAnimation(progress_, "value", this);
                progressAnim_->setDuration(220);
                progressAnim_->setEasingCurve(QEasingCurve::OutCubic);
            }
            if (progressAnim_->state() == QAbstractAnimation::Running)
                progressAnim_->stop();
            progressAnim_->setStartValue(progress_->value());
            progressAnim_->setEndValue(bounded);
            progressAnim_->start();
            if (progressPercent_) progressPercent_->setText(QString::number(bounded) + "%");
        }
    }
    void saveConfig() {
        QJsonObject activeProfile = engine_.activeProfile(config_);
        const QString profileKind = activeProfile.value("kind").toString();
        const bool isDefaultProfile = profileKind == "default";
        const bool isRustProfile = profileKind == "horizon-rust";
        const bool isStoredInstance = !isDefaultProfile && !isRustProfile;
        if (isDefaultProfile && versionBox_ && pages_ && pages_->currentIndex()==0 && !versionBox_->currentText().isEmpty()) config_.insert("version", versionBox_->currentText());
        if (isDefaultProfile && loaderBox_ && pages_ && pages_->currentIndex()==0) config_.insert("loader", loaderBox_->currentText());
        if (profileBox_ && profileBox_->currentIndex() >= 0) config_.insert("active_instance", profileBox_->currentData().toString());
        if (ramSlider_) { if (isStoredInstance) activeProfile.insert("ram", ramSlider_->value()); else config_.insert("ram", ramSlider_->value()); }
        if (javaPath_) { if (isStoredInstance) activeProfile.insert("custom_java", javaPath_->text().trimmed()); else config_.insert("custom_java", javaPath_->text().trimmed()); }
        if (jvmArgs_) { if (isStoredInstance) activeProfile.insert("jvm_arguments", jvmArgs_->text().trimmed()); else config_.insert("jvm_arguments", jvmArgs_->text().trimmed()); }
        if (gcMode_) config_.insert("gc_mode", gcMode_->currentText());
        if (closeBehavior_) config_.insert("close_behavior", closeBehavior_->currentText());
        if (highPriority_) config_.insert("high_priority", highPriority_->isChecked());
        if (forceGpu_) config_.insert("force_gpu", forceGpu_->isChecked());
        if (onlyInstalled_) config_.insert("only_installed", onlyInstalled_->isChecked());
        if (showSnapshots_) config_.insert("show_snapshots", showSnapshots_->isChecked());
        if (resolutionW_) config_.insert("res_w", resolutionW_->text().trimmed());
        if (resolutionH_) config_.insert("res_h", resolutionH_->text().trimmed());
        if (curseKey_) config_.insert("curseforge_api_key", curseKey_->text().trimmed());
        if (customClientId_) config_.insert("custom_ms_client_id", customClientId_->text().trimmed());
        config_.remove("horizon_launcher_path");
        config_.remove("horizon_client_directory");
        if (horizonMinecraftPath_) config_.insert("horizon_minecraft_path", horizonMinecraftPath_->text().trimmed());
        if (skinModel_) config_.insert("skin_model", skinModel_->currentText());
        if (isStoredInstance) {
            QString profileError;
            if (!engine_.saveProfile(activeProfile, &profileError)) setStatus("Profil ayarı kaydedilemedi: " + profileError, 0);
        }
        syncActiveAccountRecord();
        QString error; if (!engine_.saveConfig(config_, &error)) setStatus("Ayarlar kaydedilemedi: " + error, 0);
    }

    QString offlineAccountId(const QString& name) const {
        return "offline:" + LauncherEngine::offlineUuid(name).toLower();
    }
    QString microsoftAccountId(const QJsonObject& account) const {
        const QString uuid=account.value("id").toString().trimmed().toLower();
        return uuid.isEmpty()?QString():"microsoft:"+uuid;
    }
    void ensureAccountStore() {
        QJsonArray accounts=config_.value("accounts").toArray();
        const QString configuredName=config_.value("username").toString("Steve").trimmed();
        const QString offlineName=config_.value("account_type").toString()=="offline"&&!configuredName.isEmpty()?configuredName:"Steve";
        const QString offlineId=offlineAccountId(offlineName);
        auto hasId=[&accounts](const QString& id){for(const auto& value:accounts)if(value.toObject().value("id").toString()==id)return true;return false;};
        if(accounts.isEmpty()) accounts.append(QJsonObject{{"id",offlineId},{"type","offline"},{"name",offlineName}});
        const QJsonObject savedMicrosoft=config_.value("ms_account").toObject();
        const QString microsoftId=microsoftAccountId(savedMicrosoft);
        if(!microsoftId.isEmpty()&&!hasId(microsoftId)) accounts.append(QJsonObject{{"id",microsoftId},{"type","microsoft"},{"name",savedMicrosoft.value("name").toString("Player")},{"ms_account",savedMicrosoft}});
        QString activeId=config_.value("active_account").toString();
        if(activeId.isEmpty()||!hasId(activeId)) activeId=config_.value("account_type").toString()=="microsoft"&&!microsoftId.isEmpty()?microsoftId:offlineId;
        if(!hasId(activeId)) activeId=accounts.first().toObject().value("id").toString();
        config_.insert("accounts",accounts);config_.insert("active_account",activeId);
    }
    void syncActiveAccountRecord() {
        QJsonArray accounts=config_.value("accounts").toArray();
        const QString id=config_.value("active_account").toString();
        for(qsizetype i=0;i<accounts.size();++i){
            QJsonObject entry=accounts.at(i).toObject();
            if(entry.value("id").toString()!=id)continue;
            if(entry.value("type").toString()=="microsoft"){
                const QJsonObject microsoft=config_.value("ms_account").toObject();
                entry.insert("name",microsoft.value("name").toString(entry.value("name").toString("Player")));
                entry.insert("ms_account",microsoft);
            }else{
                entry.insert("name",config_.value("username").toString("Steve"));
            }
            accounts.replace(i,entry);config_.insert("accounts",accounts);return;
        }
    }
    void refreshAccountSelector() {
        if(!accountBox_)return;
        const QJsonArray accounts=config_.value("accounts").toArray();
        const QString active=config_.value("active_account").toString();
        const QSignalBlocker blocker(accountBox_);accountBox_->clear();
        for(const auto& value:accounts){
            const QJsonObject entry=value.toObject();
            const QString suffix=entry.value("type").toString()=="microsoft"?"Microsoft":"Çevrimdışı";
            accountBox_->addItem(entry.value("name").toString("Oyuncu")+"  •  "+suffix,entry.value("id").toString());
        }
        const int index=accountBox_->findData(active);accountBox_->setCurrentIndex(index<0?0:index);
    }
    void activateSavedAccount(const QString& id) {
        for(const auto& value:config_.value("accounts").toArray()){
            const QJsonObject entry=value.toObject();if(entry.value("id").toString()!=id)continue;
            config_.insert("active_account",id);config_.insert("account_type",entry.value("type").toString("offline"));
            if(entry.value("type").toString()=="microsoft"){
                config_.insert("ms_account",entry.value("ms_account").toObject());
                config_.insert("username",entry.value("name").toString("Player"));
            }else{
                config_.insert("ms_account",QJsonObject{});config_.insert("username",entry.value("name").toString("Steve"));
                if(offlineName_){const QSignalBlocker blocker(offlineName_);offlineName_->setText(entry.value("name").toString("Steve"));}
            }
            refreshAccountSelector();updateAccountCard();saveConfig();refreshSkinPreview();return;
        }
    }
    void addOfflineAccount(const QString& rawName) {
        const QString name=rawName.trimmed();
        if(!QRegularExpression("^[A-Za-z0-9_]{1,16}$").match(name).hasMatch()){
            QMessageBox::warning(this,"Çevrimdışı hesap","Ad 1–16 karakter olmalı; yalnızca İngilizce harf, rakam ve alt çizgi kullan.");return;
        }
        const QString id=offlineAccountId(name);QJsonArray accounts=config_.value("accounts").toArray();bool exists=false;
        for(const auto& value:accounts)if(value.toObject().value("id").toString()==id){exists=true;break;}
        if(!exists)accounts.append(QJsonObject{{"id",id},{"type","offline"},{"name",name}});
        config_.insert("accounts",accounts);config_.insert("active_account",id);config_.insert("account_type","offline");config_.insert("username",name);config_.insert("ms_account",QJsonObject{});
        refreshAccountSelector();updateAccountCard();saveConfig();refreshSkinPreview();setStatus(exists?"Çevrimdışı hesap seçildi":"Çevrimdışı hesap eklendi",100);
    }
    void upsertMicrosoftAccount(const QJsonObject& account,bool activate) {
        const QString id=microsoftAccountId(account);
        if(id.isEmpty()){QMessageBox::warning(this,"Microsoft hesabı","Microsoft profili için oyuncu kimliği alınamadı.");return;}
        QJsonArray accounts=config_.value("accounts").toArray();QJsonObject entry{{"id",id},{"type","microsoft"},{"name",account.value("name").toString("Player")},{"ms_account",account}};bool found=false;
        for(qsizetype i=0;i<accounts.size();++i)if(accounts.at(i).toObject().value("id").toString()==id){accounts.replace(i,entry);found=true;break;}
        if(!found)accounts.append(entry);config_.insert("accounts",accounts);
        if(activate||config_.value("active_account").toString()==id){config_.insert("active_account",id);config_.insert("account_type","microsoft");config_.insert("ms_account",account);config_.insert("username",account.value("name").toString("Player"));}
        refreshAccountSelector();updateAccountCard();saveConfig();refreshSkinPreview();
    }
    void removeActiveAccount() {
        const QString id=config_.value("active_account").toString();QJsonArray accounts=config_.value("accounts").toArray();
        if(accounts.size()<=1){QMessageBox::information(this,"Hesaplar","En az bir hesap kayıtlı kalmalı.");return;}
        const int removeIndex=accountBox_?accountBox_->currentIndex():-1;if(removeIndex<0||removeIndex>=accounts.size())return;
        const QJsonObject entry=accounts.at(removeIndex).toObject();
        if(QMessageBox::question(this,"Hesabı kaldır",QString("%1 hesabı bu launcher'dan kaldırılsın mı?").arg(entry.value("name").toString()))!=QMessageBox::Yes)return;
        accounts.removeAt(removeIndex);config_.insert("accounts",accounts);
        if(entry.value("id").toString()==id){config_.remove("active_account");config_.insert("active_account",accounts.first().toObject().value("id").toString());}
        refreshAccountSelector();activateSavedAccount(config_.value("active_account").toString());
    }

    QWidget* buildSidebar(QWidget* parent) {
        auto* side = new QWidget(parent); side->setFixedWidth(350); side->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bgDeep,gTheme.text));
        auto* v = new QVBoxLayout(side); v->setContentsMargins(18,14,18,15); v->setSpacing(9);
        auto* account = card(side); auto* row = new QHBoxLayout(account); row->setContentsMargins(12,10,10,10); row->setSpacing(10);
        auto* avatar = label("👤",22,"#f7f7f8",true,account); avatar->setAlignment(Qt::AlignCenter); avatar->setFixedSize(42,42); avatar->setStyleSheet("background:#292b31;border-radius:9px;font-size:21px;");
        auto* info = new QVBoxLayout(); row->addWidget(avatar); auto* signedLabel = label("HESAP ADI",9,kMuted,true,account); accountName_ = label("Steve",13,"#f2f3f6",true,account); accountMode_ = label("Çevrimdışı hesap",9,kMuted,false,account); info->addWidget(signedLabel); info->addWidget(accountName_); info->addWidget(accountMode_); row->addLayout(info,1);
        auto* auth = new QPushButton("+",account); auth->setToolTip("Microsoft hesabı ekle"); auth->setFixedSize(30,30); auth->setCursor(Qt::PointingHandCursor); auth->setStyleSheet("QPushButton{background:#27292e;color:white;border:0;border-radius:7px;font-size:18px;font-weight:800;}QPushButton:hover{background:#383b42;}"); row->addWidget(auth); connect(auth,&QPushButton::clicked,this,[this]{startMicrosoftLogin();}); v->addWidget(account);
        auto* home=actionButton("⌂  ANA MENÜ","#202227","#30343b",side); connect(home,&QPushButton::clicked,this,[this]{animateStackPage(pages_,0);}); v->addWidget(home);
        auto* horizon=actionButton("◈  RUST 26.3","#315e49","#397557",side); horizon->setIcon(QIcon(":/app.ico")); horizon->setIconSize(QSize(18,18)); connect(horizon,&QPushButton::clicked,this,[this]{animateStackPage(pages_,6);}); v->addWidget(horizon);
        auto* mods = actionButton("⬡  MOD YÖNETİMİ",kAccentBlue,"#4c91ff",side); auto* skin=actionButton("♜  SKİN DEĞİŞTİR",kAccentPurple,"#9b6bfa",side); auto* logs=actionButton("▣  LOG GÖRÜNTÜLEYİCİ","#1c1e23","#292b31",side); auto* settings=actionButton("⚙  AYARLAR","#27292e","#34363d",side);
        connect(mods,&QPushButton::clicked,this,[this]{animateStackPage(pages_,1); refreshInstalledMods();}); connect(skin,&QPushButton::clicked,this,[this]{animateStackPage(pages_,2);refreshSkinPreview();}); connect(logs,&QPushButton::clicked,this,[this]{animateStackPage(pages_,4);refreshLogs();}); connect(settings,&QPushButton::clicked,this,[this]{animateStackPage(pages_,3);}); v->addWidget(mods); v->addWidget(skin); v->addWidget(logs); v->addWidget(settings);
        auto* screenshots = actionButton("▧  EKRAN GÖRÜNTÜLERİ","#202227","#2e3037",side); connect(screenshots,&QPushButton::clicked,this,[this]{animateStackPage(pages_,5);refreshScreenshots();}); v->addWidget(screenshots); v->addStretch();
        auto* foot=label("TUKETICI LAUNCHER  •  Minecraft",9,"#646974",false,side); v->addWidget(foot); return side;
    }

    QWidget* buildHorizonPage() {
        auto* page = new QWidget();
        page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bgDeep,gTheme.text));
        auto* root = new QVBoxLayout(page);
        root->setContentsMargins(28,20,28,20);
        root->setSpacing(10);

        root->addWidget(label("RUST 26.3",24,kGreen,true,page));
        auto* intro = label("Titan, Horizon Launcher’ı kurmadan Horizon’un resmi Windows x64 oyun arşivini indirir. Titan lisans veya hesap sahipliği kontrolü yapmaz: çevrimdışı hesapla tek oyunculu/çevrimdışı kip; Microsoft oturumuyla çevrimiçi doğrulama isteyen sunucular kullanılabilir.",11,"#d8dbe2",false,page);
        intro->setWordWrap(true);
        root->addWidget(intro);

        auto* clientCard = card(page);
        auto* clientLayout = new QVBoxLayout(clientCard);
        clientLayout->setContentsMargins(14,11,14,11);
        clientLayout->setSpacing(7);
        clientLayout->addWidget(label("DOĞRUDAN HORIZON İSTEMCİSİ",11,kGreen,true,clientCard));
        horizonClientStatus_ = label("Henüz Horizon istemcisi indirilmedi.",10,kMuted,false,clientCard);
        horizonClientStatus_->setWordWrap(true);
        clientLayout->addWidget(horizonClientStatus_);
        auto* clientButtons = new QHBoxLayout();
        horizonInstallButton_ = actionButton("RESMÎ NIGHTLY’Yİ İNDİR / GÜNCELLE","#236d4d","#2c8b62",clientCard);
        horizonVersionsButton_ = actionButton("GITHUB SÜRÜMLERİ","#2b2e35","#383b43",clientCard);
        clientButtons->addWidget(horizonInstallButton_,1);
        clientButtons->addWidget(horizonVersionsButton_);
        clientLayout->addLayout(clientButtons);
        auto* trustNote = label("Arşiv GitHub’daki horizon-rust/horizon deposundan alınır; SHA256SUMS dosyasındaki SHA-256 değeriyle karşılaştırılır. Bu, aynı GitHub kaynağından gelen imzalı bir doğrulamanın yerine geçmez. Horizon’un istemci belgesi Windows’u şu an destek kapsamı dışında sayıyor; Windows ZIP’i bulunsa da çalışması garanti değil.",9,"#c2a56d",false,clientCard);
        trustNote->setWordWrap(true);
        clientLayout->addWidget(trustNote);
        root->addWidget(clientCard);

        auto* minecraftCard = card(page);
        auto* minecraftLayout = new QVBoxLayout(minecraftCard);
        minecraftLayout->setContentsMargins(14,11,14,11);
        minecraftLayout->setSpacing(7);
        minecraftLayout->addWidget(label("MINECRAFT JAVA 26.3 DOSYA KLASÖRÜ",11,kGreen,true,minecraftCard));
        auto* minecraftRow = new QHBoxLayout();
        horizonMinecraftPath_ = new QLineEdit(config_.value("horizon_minecraft_path").toString(engine_.minecraftDirectory()),minecraftCard);
        horizonMinecraftPath_->setStyleSheet(inputStyle());
        auto* browseMinecraft = actionButton("KLASÖR SEÇ","#2b2e35","#383b43",minecraftCard);
        minecraftRow->addWidget(horizonMinecraftPath_,1);
        minecraftRow->addWidget(browseMinecraft);
        minecraftLayout->addLayout(minecraftRow);
        auto* minecraftNote = label("İlk hazırlıkta Horizon araçları seçtiğin yerel kurulumdaki dosyaları okur. Eksikse Mojang’dan server.jar ve Adoptium’dan Java 25 JDK indirilebilir. Minecraft dosyaları bu launcher ile dağıtılmaz.",9,kMuted,false,minecraftCard);
        minecraftNote->setWordWrap(true);
        minecraftLayout->addWidget(minecraftNote);
        root->addWidget(minecraftCard);

        auto* actions = new QHBoxLayout();
        horizonPrepareButton_ = actionButton("26.3 KAYNAKLARINI HAZIRLA","#315e49","#397557",page);
        horizonPlayButton_ = actionButton("▶  RUST İSTEMCİSİNİ OYNA",kGreen,"#63e99f",page);
        horizonStopButton_ = actionButton("DURDUR","#522b32","#713942",page);
        horizonStopButton_->setEnabled(false);
        actions->addWidget(horizonPrepareButton_,1);
        actions->addWidget(horizonPlayButton_,1);
        actions->addWidget(horizonStopButton_);
        root->addLayout(actions);

        auto* outputCard = card(page);
        auto* outputLayout = new QVBoxLayout(outputCard);
        outputLayout->setContentsMargins(12,9,12,9);
        outputLayout->addWidget(label("TITAN / HORIZON İŞLEM BİLGİSİ",10,kGreen,true,outputCard));
        horizonOutput_ = new QTextEdit(outputCard);
        horizonOutput_->setReadOnly(true);
        horizonOutput_->setMinimumHeight(105);
        horizonOutput_->setStyleSheet("QTextEdit{background:#101115;color:#c9ced8;border:1px solid #30333a;border-radius:7px;font-family:Consolas,monospace;font-size:10px;padding:7px;}");
        outputLayout->addWidget(horizonOutput_);
        root->addWidget(outputCard,1);

        connect(horizonMinecraftPath_,&QLineEdit::editingFinished,this,[this]{saveConfig();});
        connect(browseMinecraft,&QPushButton::clicked,this,[this]{
            const QString start=horizonMinecraftPath_->text().trimmed().isEmpty()?QDir::homePath():horizonMinecraftPath_->text().trimmed();
            const QString path=QFileDialog::getExistingDirectory(this,"Minecraft 26.3 klasörünü seç",start);
            if(!path.isEmpty()){horizonMinecraftPath_->setText(path);config_.insert("horizon_minecraft_path",path);saveConfig();}
        });
        connect(horizonVersionsButton_,&QPushButton::clicked,this,[]{QDesktopServices::openUrl(QUrl("https://github.com/horizon-rust/horizon/releases"));});
        connect(horizonInstallButton_,&QPushButton::clicked,this,[this]{installHorizonClient();});
        connect(horizonPrepareButton_,&QPushButton::clicked,this,[this]{prepareHorizonClient();});
        connect(horizonPlayButton_,&QPushButton::clicked,this,[this]{playHorizonClient();});
        connect(horizonStopButton_,&QPushButton::clicked,this,[this]{
            if(!horizonProcess_||horizonProcess_->state()==QProcess::NotRunning)return;
            horizonProcess_->terminate();
            QPointer<QProcess> process=horizonProcess_;
            QTimer::singleShot(3000,this,[process]{if(process&&process->state()!=QProcess::NotRunning)process->kill();});
        });
        refreshHorizonClientStatus();
        return page;
    }

    QString horizonRootDirectory() const { return QDir(engine_.dataDirectory()).filePath("horizon"); }
    QString horizonGameHome() const { return QDir(horizonRootDirectory()).filePath("game-home"); }
    QString horizonClientDirectory() const {
        const QString tag=config_.value("horizon_client_tag").toString();
        if(!QRegularExpression("^nightly-[0-9]{4}-[0-9]{2}-[0-9]{2}$").match(tag).hasMatch())return {};
        return QDir(horizonRootDirectory()).filePath("releases/"+tag);
    }
    QString horizonNativeRoot() const { return QDir(horizonRootDirectory()).filePath("native"); }
    QString minisignExecutable(const LauncherEngine::Progress& progress) {
#ifdef Q_OS_WIN
        if (QSysInfo::currentCpuArchitecture().compare("x86_64", Qt::CaseInsensitive) != 0 &&
            QSysInfo::currentCpuArchitecture().compare("AMD64", Qt::CaseInsensitive) != 0)
            throw std::runtime_error("Titan'ın yerel mod derlemesi şu an yalnızca Windows x64 içindir.");
        const QString directory = QDir(horizonRootDirectory()).filePath("tools/minisign-0.12");
        const QString executable = QDir(directory).filePath("minisign.exe");
        const QString expectedExeHash = "5535be9e4e123831ebe6ef324aafe9dde507015c176191f9e20c3ad60567f9e1";
        if (sha256File(executable) == expectedExeHash) return executable;
        QDir().mkpath(directory);
        const QString archivePath = QDir(directory).filePath("minisign-0.12-win64.zip");
        const QString expectedArchiveHash = "37b600344e20c19314b2e82813db2bfdcc408b77b876f7727889dbd46d539479";
        engine_.downloadUrl("https://github.com/jedisct1/minisign/releases/download/0.12/minisign-0.12-win64.zip",
                            archivePath, {{"sha256", expectedArchiveHash}}, progress,
                            {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"}, 2 * 1024 * 1024);
        ZipArchive archive;
        QString why;
        if (!archive.open(archivePath, &why)) { QFile::remove(archivePath); throw std::runtime_error(("Minisign paketi açılamadı: " + why).toStdString()); }
        const QByteArray bytes = archive.read("minisign-win64/x86_64/minisign.exe", &why);
        QFile::remove(archivePath);
        if (!why.isEmpty() || bytes.isEmpty() || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != expectedExeHash)
            throw std::runtime_error("Minisign Windows x64 aracı ZIP SHA-256 ve çalıştırılabilir dosya sabitlemesiyle eşleşmiyor.");
        QSaveFile output(executable);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
            throw std::runtime_error("Minisign doğrulama aracı Titan veri klasörüne kaydedilemedi.");
        return executable;
#else
        Q_UNUSED(progress);
        throw std::runtime_error("Yayımcı Minisign doğrulaması için Windows x64 aracı yok.");
#endif
    }
    bool verifyHorizonModPublisher(const QString& packagePath, const QJsonObject& project,
                                   const QJsonObject& version, const LauncherEngine::Progress& progress) {
        const QString issue = horizonModSignatureIssue(project, version);
        if (!issue.isEmpty()) throw std::runtime_error(issue.toStdString());
        const QString key = project.value("key").toString().trimmed();
        const QString signature = version.value("file").toObject().value("signature").toString().trimmed();
        if (key.isEmpty() && signature.isEmpty()) return false;
        const QString minisign = minisignExecutable(progress);
        const QString signaturePath = QDir(engine_.dataDirectory()).filePath("horizon/mod-cache/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".minisig");
        QDir().mkpath(QFileInfo(signaturePath).absolutePath());
        QSaveFile signatureFile(signaturePath);
        const QByteArray signatureBytes = signature.toUtf8();
        if (!signatureFile.open(QIODevice::WriteOnly) || signatureFile.write(signatureBytes) != signatureBytes.size() || !signatureFile.commit())
            throw std::runtime_error("Yayımcı Minisign imzası geçici olarak yazılamadı.");
        QString output, error;
        const int code = runHorizonTool(minisign, {"-Vm", packagePath, "-P", key, "-x", signaturePath, "-q"},
                                        QDir::rootPath(), QProcessEnvironment::systemEnvironment(), {}, &output, &error);
        QFile::remove(signaturePath);
        if (code != 0) throw std::runtime_error(("Yayımcı Minisign imzası doğrulanmadı; paket kurulumu/derlemesi durduruldu.\n" + error.toStdString()).c_str());
        return true;
    }
    QString nativeBuildTarExecutable() const {
#ifdef Q_OS_WIN
        const QString windowsDirectory = qEnvironmentVariable("WINDIR", "C:/Windows");
        const QString tar = QDir(windowsDirectory).filePath("System32/tar.exe");
        return QFileInfo::exists(tar) ? tar : QString();
#else
        return QStandardPaths::findExecutable("tar");
#endif
    }
    QString nativeBuildPath(const QString& path) const {
#ifdef Q_OS_WIN
        const QString absolute = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
        bool asciiOnly = true;
        for (const QChar ch : absolute) if (ch.unicode() > 0x7f) { asciiOnly = false; break; }
        if (asciiOnly) return absolute;
        wchar_t shortPath[32768]{};
        const DWORD length = GetShortPathNameW(reinterpret_cast<LPCWSTR>(absolute.utf16()), shortPath, DWORD(std::size(shortPath)));
        if (length == 0 || length >= DWORD(std::size(shortPath)))
            throw std::runtime_error("Rust/MinGW derleyicisi bu Windows yolundaki Türkçe/Unicode karakterleri okuyamıyor ve kısa yol üretilemedi. Titan veri klasörünü ASCII karakterli bir konuma taşıyıp tekrar dene.");
        const QString result = QString::fromWCharArray(shortPath, int(length));
        for (const QChar ch : result) if (ch.unicode() > 0x7f)
            throw std::runtime_error("Rust/MinGW derleyicisi için bu klasöre ASCII kısa yol oluşturulamıyor. Titan veri klasörünü ASCII karakterli bir konuma taşı.");
        return QDir::toNativeSeparators(result);
#else
        return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
#endif
    }
    QProcessEnvironment nativeBuildEnvironment(const QString& toolchain, const QString& cargoHome,
                                                const QString& tempDirectory) const {
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        const QStringList scrub{"RUSTUP_TOOLCHAIN", "RUSTC_WRAPPER", "RUSTC_WORKSPACE_WRAPPER", "RUSTFLAGS",
                                "CARGO_ENCODED_RUSTFLAGS", "RUSTDOCFLAGS", "CARGO_ENCODED_RUSTDOCFLAGS",
                                "CARGO_INCREMENTAL", "RUSTC", "RUSTDOC", "RUSTUP_HOME", "CARGO_HOME", "CARGO_TARGET_DIR"};
        for (const QString& key : scrub) environment.remove(key);
        const QStringList currentKeys = environment.keys();
        for (const QString& key : currentKeys) {
            const QString upper = key.toUpper();
            if (upper.startsWith("CARGO_PROFILE_") || upper.startsWith("CARGO_BUILD_") || upper.startsWith("CARGO_TARGET_"))
                environment.remove(key);
        }
        const QString bin = QDir(toolchain).filePath("bin");
        const QString mingwBin = QDir(toolchain).filePath("mingw64/bin");
        environment.insert("PATH", nativeBuildPath(bin) + ";" + nativeBuildPath(mingwBin) + ";" + environment.value("PATH"));
        environment.insert("RUSTC", nativeBuildPath(QDir(bin).filePath("rustc.exe")));
        environment.insert("CARGO_HOME", nativeBuildPath(cargoHome));
        environment.insert("TMP", nativeBuildPath(tempDirectory));
        environment.insert("TEMP", nativeBuildPath(tempDirectory));
        environment.insert("TMPDIR", nativeBuildPath(tempDirectory));
        environment.insert("CARGO_BUILD_JOBS", "8");
        environment.insert("CARGO_TERM_PROGRESS_WHEN", "always");
        environment.insert("CARGO_TERM_PROGRESS_WIDTH", "100");
        return environment;
    }
    QString ensureNativeRustToolchain(const LauncherEngine::Progress& progress) {
#ifdef Q_OS_WIN
        const QString target = "x86_64-pc-windows-gnu";
        const QString version = "1.99.1";
        const QString distribution = "https://static.rust-lang.org/dist/2026-10-15/";
        const QString toolchainRoot = QDir(engine_.dataDirectory()).filePath("rust/" + version + "-" + target);
        const QString complete = QDir(toolchainRoot).filePath(".installed");
        const QString mingwGcc = QDir(toolchainRoot).filePath("mingw64/bin/gcc.exe");
        if (QFileInfo::exists(complete) && QFileInfo::exists(QDir(toolchainRoot).filePath("bin/cargo.exe")) &&
            QFileInfo::exists(QDir(toolchainRoot).filePath("bin/rustc.exe")) && QFileInfo::exists(mingwGcc)) return toolchainRoot;
        const QString tar = nativeBuildTarExecutable();
        if (tar.isEmpty()) throw std::runtime_error("Windows tar.exe bulunamadı; Windows 10/11 yerleşik arşiv aracı gerekli.");
        const QString rustRoot = QDir(engine_.dataDirectory()).filePath("rust");
        const QString downloads = QDir(horizonNativeRoot()).filePath("downloads/rust");
        const QString partial = QDir(rustRoot).filePath("." + version + "-" + target + ".partial");
        const QString temp = QDir(horizonNativeRoot()).filePath("tmp/toolchain");
        QDir().mkpath(downloads); QDir().mkpath(partial); QDir().mkpath(temp);
        QDir(partial).removeRecursively(); QDir(temp).removeRecursively();
        QDir().mkpath(partial); QDir().mkpath(temp);
        const QStringList names{"rustc", "cargo", "rust-std", "rust-mingw"};
        const QString rustFileName = "rust-" + version + "-" + target + ".tar.xz";
        const QString rustArchivePath = QDir(downloads).filePath(rustFileName);

        // SHA-256 hash'ini Rust resmi dağıtım sunucusundan dinamik oku
        QString rustHash;
        try {
            const QString hashFile = QDir(downloads).filePath(rustFileName + ".sha256");
            engine_.downloadUrl(distribution + rustFileName + ".sha256", hashFile,
                                {}, {}, {"static.rust-lang.org"}, 256);
            QFile hf(hashFile);
            if (hf.open(QIODevice::ReadOnly)) {
                rustHash = QString::fromLatin1(hf.readAll()).trimmed().split(' ').first().trimmed().toLower();
                hf.close(); QFile::remove(hashFile);
            }
        } catch (...) {}
        // Hash alınamazsa indirmeyi hash doğrulaması olmadan yap
        const QJsonObject hashArg = rustHash.size() == 64
            ? QJsonObject{{"sha256", rustHash}} : QJsonObject{};
        // Birleşik Rust toolchain arşivi: rustc+cargo+rust-std+rust-mingw tek dosyada
        if (sha256File(rustArchivePath) != rustHash)
            engine_.downloadUrl(distribution + rustFileName, rustArchivePath, hashArg, progress,
                                {"static.rust-lang.org"}, 2LL * 1024 * 1024 * 1024);
        const QString extract = QDir(temp).filePath("rust-combined");
        QDir().mkpath(extract);
        QString output, error;
        const int code = runHorizonTool(tar, {"-xJf", rustArchivePath, "-C", extract}, QDir::rootPath(),
                                        QProcessEnvironment::systemEnvironment(), progress, &output, &error);
        if (code != 0) throw std::runtime_error(("Rust toolchain arşivi açılamadı: " + error).toStdString());
        // Birleşik arşiv kök dizini
        const QString archiveRoot = QDir(extract).filePath("rust-" + version + "-" + target);
        if (!QFileInfo(archiveRoot).isDir())
            throw std::runtime_error("Rust toolchain arşivinin kök dizini bulunamadı.");
        // Birleşik arşivde her bileşenin kendi alt dizini var:
        //   rustc-1.99.1-x86_64-pc-windows-gnu/   (bin/, lib/, ...)
        //   cargo-1.99.1-x86_64-pc-windows-gnu/
        //   rust-std-x86_64-pc-windows-gnu/        (versiyonsuz olabilir)
        //   rust-mingw-1.99.1-x86_64-pc-windows-gnu/
        // Her alt dizinin içindeki bin/, lib/, share/ vb. partial'a düzleştirilerek kopyalanır.
        int totalCopied = 0;
        for (const QFileInfo& entry :
             QDir(archiveRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString compDir = entry.absoluteFilePath();
            QDirIterator it(compDir, QDir::Files | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString input = it.next();
                if (QFileInfo(input).isSymLink())
                    throw std::runtime_error("Rust arşivinde sembolik bağlantı reddedildi.");
                const QString relative = QDir(compDir).relativeFilePath(input);
                const QString fname = QFileInfo(relative).fileName();
                if (!ZipArchive::isSafeRelativePath(relative)
                    || fname == "manifest.in" || fname == "components"
                    || fname == "VERSION"     || fname == "rust-installer-version"
                    || fname == "install.sh"  || fname == "uninstall.sh"
                    || relative.startsWith("install-receipt")) continue;
                const QString destination = QDir(partial).filePath(relative);
                QDir().mkpath(QFileInfo(destination).absolutePath());
                QFile::remove(destination);
                if (!QFile::copy(input, destination))
                    throw std::runtime_error(
                        ("Rust dosyası kopyalanamadı: " + relative).toStdString());
                ++totalCopied;
            }
        }
        if (totalCopied == 0)
            throw std::runtime_error("Rust toolchain arşivinden hiç dosya kopyalanamadı.");
        // rustc ve lib/rustlib/<target>/lib varlığını doğrula
        if (!QFileInfo::exists(QDir(partial).filePath("bin/rustc.exe")))
            throw std::runtime_error("Rust toolchain arşivinden rustc.exe kopyalanamadı.");
        if (!QFileInfo(QDir(partial).filePath(
                "lib/rustlib/" + target + "/lib")).isDir())
            throw std::runtime_error(
                "Rust standart kütüphane bileşeni (rust-std) arşivde bulunamadı ya da eksik kopyalandı.");
        QFile::remove(rustArchivePath);
        QDir(extract).removeRecursively();
        const QString mingwUrl = "https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-23.1.3-14.0.0-msvcrt-r3/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r3.zip";
        const QString mingwHash = "62af32ea58acdea1318ea5dd8eb7017eaee694dd40b269d52a59c769dcfefb82";
        const QString mingwArchive = QDir(downloads).filePath("mingw-w64-winlibs.zip");
        if (sha256File(mingwArchive) != mingwHash)
            engine_.downloadUrl(mingwUrl, mingwArchive, {{"sha256", mingwHash}}, progress,
                                {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"}, 2LL * 1024 * 1024 * 1024);
        QString mingwOutput, mingwError;
        const int mingwCode = runHorizonTool(tar, {"-xf", mingwArchive, "-C", partial}, QDir::rootPath(),
                                        QProcessEnvironment::systemEnvironment(), progress, &mingwOutput, &mingwError);
        QFile::remove(mingwArchive);
        if (mingwCode != 0 || !QFileInfo::exists(QDir(partial).filePath("mingw64/bin/gcc.exe")))
            throw std::runtime_error(("Sabitlenmiş MinGW araçları açılamadı: " + mingwError).toStdString());
        QFile marker(QDir(partial).filePath(".installed"));
        if (!marker.open(QIODevice::WriteOnly) || marker.write(version.toUtf8()) < 0)
            throw std::runtime_error("Rust araç zinciri tamamlanma kaydı yazılamadı.");
        marker.close();
        QDir(toolchainRoot).removeRecursively();
        QDir().mkpath(QFileInfo(toolchainRoot).absolutePath());
        // rename dene (en hızlı); Windows'ta kilitli dosya varsa veya
        // farklı sürücüdeyse başarısız olur — o durumda kopyala.
        bool moved = false;
        for (int attempt = 0; attempt < 3 && !moved; ++attempt) {
            if (attempt > 0) { QEventLoop w; QTimer::singleShot(500, &w, &QEventLoop::quit); w.exec(); }
            QDir(toolchainRoot).removeRecursively();
            moved = QDir().rename(partial, toolchainRoot);
        }
        if (!moved) {
            // rename çalışmadı — QDirIterator ile manuel kopyala
            QDir().mkpath(toolchainRoot);
            QDirIterator cpIt(partial, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                              QDirIterator::Subdirectories);
            bool copyOk = true;
            while (cpIt.hasNext() && copyOk) {
                const QString src = cpIt.next();
                const QString rel = QDir(partial).relativeFilePath(src);
                const QString dst = QDir(toolchainRoot).filePath(rel);
                if (QFileInfo(src).isDir()) {
                    copyOk = QDir().mkpath(dst);
                } else {
                    QDir().mkpath(QFileInfo(dst).absolutePath());
                    QFile::remove(dst);
                    copyOk = QFile::copy(src, dst);
                }
            }
            if (!copyOk) {
                QDir(toolchainRoot).removeRecursively();
                throw std::runtime_error(
                    "Rust araç zinciri hedef klasöre taşınamadı. "
                    "Antivirüs yazılımı veya başka bir uygulama klasörü kilitliyor olabilir; "
                    "kapatıp tekrar deneyin.");
            }
            QDir(partial).removeRecursively();
        }
        QDir(temp).removeRecursively();
        return toolchainRoot;
#else
        Q_UNUSED(progress);
        throw std::runtime_error("HNMOD derleme şu an Windows x64 için hazırlanmıştır.");
#endif
    }
    QString ensureHorizonNativeSource(const QString& revision, const LauncherEngine::Progress& progress) {
        const QString sourceRoot = QDir(horizonNativeRoot()).filePath("source");
        const QString source = QDir(sourceRoot).filePath(revision);
        const QString marker = QDir(source).filePath(".horizon-revision");
        QFile markerFile(marker);
        if (markerFile.open(QIODevice::ReadOnly) && QString::fromUtf8(markerFile.readAll()).trimmed() == revision &&
            QFileInfo::exists(QDir(source).filePath("Cargo.toml"))) return source;
        // Mevcut bozuk/eksik kaynak dizinini temizle
        if (QFileInfo(source).isDir()) QDir(source).removeRecursively();
        const QString archivePath = QDir(horizonNativeRoot()).filePath("downloads/horizon-" + revision + ".zip");
        const QString staging = QDir(sourceRoot).filePath("." + revision + ".partial");
        QDir().mkpath(sourceRoot); QDir().mkpath(QFileInfo(archivePath).absolutePath());
        engine_.downloadUrl("https://codeload.github.com/horizon-rust/horizon/zip/" + revision,
                            archivePath, {}, progress, {"codeload.github.com"}, 256LL * 1024 * 1024);
        ZipArchive archive; QString why;
        if (!archive.open(archivePath, &why)) { QFile::remove(archivePath); throw std::runtime_error(("Horizon kaynak arşivi açılamadı: " + why).toStdString()); }
        const auto entries = archive.entries();
        if (entries.isEmpty() || entries.size() > 100000) { QFile::remove(archivePath); throw std::runtime_error("Sabitlenmiş Horizon kaynak arşivi boş veya çok büyük."); }
        QString top;
        quint64 expanded = 0;
        QSet<QString> names;
        for (const ZipEntry& entry : entries) {
            QString name = entry.name; name.replace('\\', '/');
            const bool directory = name.endsWith('/');
            if (directory) name.chop(1);
            if (!ZipArchive::isSafeRelativePath(name) || name.contains(':')) throw std::runtime_error("Horizon kaynak arşivinde güvenli olmayan yol var.");
            const QStringList parts = name.split('/');
            if (top.isEmpty()) top = parts.first();
            if (parts.size() == 1 && directory && parts.first() == top) continue;
            if (parts.size() < 2 || parts.last().isEmpty()) throw std::runtime_error("Horizon kaynak arşivinin klasör yapısı beklenmiyor.");
            if (parts.first() != top || names.contains(name)) throw std::runtime_error("Horizon kaynak arşivinde birden fazla kök veya yinelenen yol var.");
            names.insert(name);
            const quint32 mode = entry.externalAttributes >> 16;
            const quint32 type = mode & 0170000u;
            if (type == 0120000u) continue;
            if (!directory && type != 0 && type != 0100000u) throw std::runtime_error("Horizon kaynak arşivindeki özel dosya reddedildi.");
            if (!directory) {
                if (entry.size > 128u * 1024u * 1024u || expanded + entry.size > 512u * 1024u * 1024u)
                    throw std::runtime_error("Horizon kaynak arşivi açıldığında 512 MB sınırını aşıyor.");
                expanded += entry.size;
            }
        }
        if (!top.startsWith("horizon-")) throw std::runtime_error("Horizon kaynak arşivi beklenen depo adıyla gelmedi.");
        QDir(staging).removeRecursively(); QDir().mkpath(staging);
        if (!archive.extractUnder(staging, {}, &why)) { QFile::remove(archivePath); QDir(staging).removeRecursively(); throw std::runtime_error(("Horizon kaynak dosyaları çıkarılamadı: " + why).toStdString()); }
        QFile::remove(archivePath);
        const QString stagedRoot = QDir(staging).filePath(top);
        if (!QFileInfo::exists(QDir(stagedRoot).filePath("Cargo.toml")) || !QFileInfo::exists(QDir(stagedRoot).filePath("Cargo.lock"))) {
            QDir(staging).removeRecursively(); throw std::runtime_error("Sabitlenmiş Horizon kaynağında Cargo workspace bulunamadı.");
        }
        QFile writtenMarker(QDir(stagedRoot).filePath(".horizon-revision"));
        if (!writtenMarker.open(QIODevice::WriteOnly) || writtenMarker.write(revision.toUtf8()) < 0)
            throw std::runtime_error("Kaynak revision kaydı yazılamadı.");
        writtenMarker.close(); // dosyayı flush et ve kapat, rename öncesi
        QDir(source).removeRecursively();
        // rename dene; başarısız olursa (farklı sürücü, kilitli dosya) elle kopyala
        bool srcMoved = false;
        for (int attempt = 0; attempt < 3 && !srcMoved; ++attempt) {
            if (attempt > 0) { QEventLoop w; QTimer::singleShot(500, &w, &QEventLoop::quit); w.exec(); }
            QDir(source).removeRecursively();
            srcMoved = QDir().rename(stagedRoot, source);
        }
        if (!srcMoved) {
            QDir().mkpath(source);
            QDirIterator cpIt(stagedRoot, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
                              QDirIterator::Subdirectories);
            bool ok = true;
            while (cpIt.hasNext() && ok) {
                const QString s = cpIt.next();
                const QString rel = QDir(stagedRoot).relativeFilePath(s);
                const QString d = QDir(source).filePath(rel);
                if (QFileInfo(s).isDir()) { ok = QDir().mkpath(d); }
                else { QDir().mkpath(QFileInfo(d).absolutePath()); QFile::remove(d); ok = QFile::copy(s, d); }
            }
            if (!ok) {
                QDir(source).removeRecursively();
                throw std::runtime_error(
                    "Sabitlenmiş Horizon kaynağı cache'e taşınamadı. "
                    "Antivirüs veya başka bir uygulama klasörü kilitliyor olabilir.");
            }
            QDir(staging).removeRecursively();
        } else {
            QDir(staging).removeRecursively();
        }
        return source;
    }
    void refreshHorizonClientStatus() {
        if(!horizonClientStatus_)return;
        const QString directory=horizonClientDirectory();
        const QString client=findHorizonExecutable(directory,"horizon-client.exe");
        if(client.isEmpty()) {
            horizonClientStatus_->setText("Henüz kurulmadı. Titan yalnızca Horizon’un resmi oyun arşivini kendi Horizon klasörüne indirir; Horizon Launcher kurulmaz.");
            return;
        }
        horizonClientStatus_->setText(QString("Kurulu Nightly: %1\nDosya: %2").arg(config_.value("horizon_client_tag").toString("bilinmeyen sürüm"),QDir::toNativeSeparators(client)));
    }
#ifdef Q_OS_WIN
    bool brandRustWindow(DWORD processId) {
        if(!horizonWindowIcon_)
            horizonWindowIcon_=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,0,0,LR_DEFAULTSIZE));
        struct WindowInfo { DWORD processId; HICON icon; bool found; } info{processId,horizonWindowIcon_,false};
        EnumWindows([](HWND window, LPARAM parameter)->BOOL {
            auto* info=reinterpret_cast<WindowInfo*>(parameter);
            DWORD ownerProcess=0;
            GetWindowThreadProcessId(window,&ownerProcess);
            if(ownerProcess!=info->processId||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;
            SetWindowTextW(window,L"Rust 26.3");
            if(info->icon){
                SendMessageW(window,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(info->icon));
                SendMessageW(window,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(info->icon));
            }
            info->found=true;
            return FALSE;
        },reinterpret_cast<LPARAM>(&info));
        return info.found;
    }
#endif
    void setHorizonTaskBusy(bool busy) {
        if(horizonInstallButton_)horizonInstallButton_->setEnabled(!busy);
        if(horizonPrepareButton_)horizonPrepareButton_->setEnabled(!busy);
        if(horizonPlayButton_)horizonPlayButton_->setEnabled(!busy);
        if(horizonVersionsButton_)horizonVersionsButton_->setEnabled(!busy);
        if(horizonStopButton_)horizonStopButton_->setEnabled(false);
        if(horizonBuildModsButton_)horizonBuildModsButton_->setEnabled(!busy);
    }
    void buildHorizonNativeMods() {
        if (activeTasks_ > 0 || gameStarting_) { QMessageBox::information(this, "İşlem sürüyor", "Önce mevcut işlem tamamlansın."); return; }
        if (horizonProcess_ && horizonProcess_->state() != QProcess::NotRunning) { QMessageBox::information(this, "Rust çalışıyor", "Modları derlemeden önce Rust istemcisini kapat."); return; }
        const QString modsDirectory = QDir(horizonGameHome()).filePath("horizon-native-mods");
        QStringList bundles;
        QDirIterator iterator(modsDirectory, QStringList{"*.hnmod"}, QDir::Files | QDir::NoSymLinks);
        while (iterator.hasNext()) bundles.append(iterator.next());
        std::sort(bundles.begin(), bundles.end(), [](const QString& a, const QString& b) { return QFileInfo(a).fileName() < QFileInfo(b).fileName(); });
        if (bundles.isEmpty()) { QMessageBox::information(this, "Rust modları", "Derlenecek .HNMOD yok. Önce Rust Modları sekmesinden bir yerel mod yükle."); return; }
        QStringList modLines;
        for (const QString& bundle : bundles) {
            try {
                const QJsonObject manifest = horizonNativeBundleManifest(bundle);
                modLines.append(QString("• %1 %2").arg(manifest.value("id").toString(QFileInfo(bundle).completeBaseName()), manifest.value("version").toString("sürüm bilinmiyor")));
            } catch (...) { modLines.append("• " + QFileInfo(bundle).fileName() + " (manifest daha derleme öncesi doğrulanacak)"); }
        }
        const QString warning = QString("Aşağıdaki .HNMOD paketleri Rust 26.3 için ayrı bir istemciye derlenecek:\n\n%1\n\nGÜVENLİK: bu paketler sandbox içinde çalışmaz. Derleme sırasında modun Rust kodu ve bağımlılıkların derleme betikleri bilgisayarındaki kullanıcı hesabı izinleriyle çalışabilir; oyunda da aynı izinlere sahip olur. Minisign, imzalı dosyanın listelenen yayımlayıcı anahtarıyla eşleştiğini doğrular; imza kodun zararsız olduğunu garanti etmez. İmzasız paketler ayrıca işaretlenir.\n\nİlk derleme yaklaşık 2–5 GB araç ve bağımlılık indirebilir, birkaç dakika veya daha uzun sürebilir. Rust 1.99.1, MinGW ve Horizon mixin aracı Titan'ın veri klasöründe tutulur. Her derlemede bu uyarı gösterilir. Bu kodu ve kaynağını incelediysen derlemeyi başlat.")
            .arg(modLines.join('\n'));
        if (QMessageBox::warning(this, "Yerel kodu derlemeden önce güven uyarısı", warning,
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        horizonBuildModsButton_->setEnabled(false);
        horizonBuildModsButton_->setText("DERLENİYOR…");
        if (horizonModsStatus_) horizonModsStatus_->setText("Modların SHA-256 ve Minisign imzaları yeniden doğrulanıyor…");
        setStatus("Rust modlarının yayımlayıcı imzaları doğrulanıyor…", 5);
        runTask([this, bundles] {
            TaskResult r;
            const LauncherEngine::Progress progress = progressCallback();
            struct BuildMod { QString path, id, version, sha, horizon, mixins; QJsonObject project, listing; };
            const QByteArray indexBytes = requestHorizonModsIndex();
            QJsonParseError parse{};
            const QJsonDocument indexDocument = QJsonDocument::fromJson(indexBytes, &parse);
            if (parse.error != QJsonParseError::NoError || !indexDocument.isObject() || indexDocument.object().value("schema").toInt() != 1)
                throw std::runtime_error("İmza doğrulaması için resmî Horizon mod dizini okunamadı.");
            const QJsonObject index = indexDocument.object();
            QList<BuildMod> mods;
            QJsonObject modHashes;
            QString horizonRevision, mixinsRevision;
            QStringList unsignedMods;
            for (const QString& path : bundles) {
                const QJsonObject native = horizonNativeBundleManifest(path);
                const QString id = native.value("id").toString();
                const QString modVersion = native.value("version").toString();
                if (!validHorizonModId(id) || native.value("game").toString() != "26.3")
                    throw std::runtime_error((".HNMOD kimliği veya oyun sürümü geçersiz: " + QFileInfo(path).fileName()).toStdString());
                QJsonObject project, listing;
                bool found = false;
                for (const QJsonValue& value : index.value("projects").toArray()) {
                    const QJsonObject candidate = value.toObject();
                    if (candidate.value("id").toString() != id || candidate.value("kind").toString() != "native-mod") continue;
                    for (const QJsonValue& versionValue : candidate.value("versions").toArray()) {
                        const QJsonObject candidateVersion = versionValue.toObject();
                        if (candidateVersion.value("version").toString() == modVersion && horizonModVersionUsable(candidate, candidateVersion)) {
                            project = candidate; listing = candidateVersion; found = true; break;
                        }
                    }
                    if (found) break;
                }
                if (!found) throw std::runtime_error(("" + id + " " + modVersion + " artık resmî dizinde doğrulanamıyor; mod sayfasından yeniden indir.").toStdString());
                const QJsonObject file = listing.value("file").toObject();
                const QString hash = file.value("sha256").toString();
                if (horizonModRevoked(index, id, hash)) throw std::runtime_error((id + " sürümü Horizon tarafından iptal edilmiş; derleme engellendi.").toStdString());
                if (QFileInfo(path).size() != file.value("size").toVariant().toLongLong() || sha256File(path) != hash)
                    throw std::runtime_error((id + " .HNMOD dosyası dizindeki SHA-256 veya boyutla eşleşmiyor.").toStdString());
                validateHorizonNativeBundle(path, project, listing);
                const bool signedByPublisher = verifyHorizonModPublisher(path, project, listing, progress);
                if (!signedByPublisher) unsignedMods.append(id + " " + modVersion);
                const QString horizon = native.value("horizon").toString();
                const QString mixins = native.value("mixins").toString();
                if (horizonRevision.isEmpty()) { horizonRevision = horizon; mixinsRevision = mixins; }
                else if (horizonRevision != horizon || mixinsRevision != mixins)
                    throw std::runtime_error("Yüklü .HNMOD'lar farklı Horizon veya horizon-mixins commit'lerine sabitlenmiş. Bu Titan sürümü aynı commit'lere sabitlenmiş modları birlikte derleyebilir; uyumlu sürümleri yükle.");
                mods.append({path, id, modVersion, hash, horizon, mixins, project, listing});
                modHashes.insert(id, hash);
            }
            if (mods.isEmpty()) throw std::runtime_error("Derlenecek doğrulanmış .HNMOD bulunamadı.");
            const QString baseClient = findHorizonExecutable(horizonClientDirectory(), "horizon-client.exe");
            if (baseClient.isEmpty()) throw std::runtime_error("Önce Titan içinden resmî Rust 26.3 oyun arşivini indir.");
            const QString resources = QDir(QFileInfo(baseClient).absolutePath()).filePath(".local/horizon-client/26.3");
            if (!QFileInfo(resources).isDir()) throw std::runtime_error("Önce Horizon kaynaklarını hazırla; derlenen istemci 26.3 oyun kaynaklarını kullanacak.");
            QStorageInfo storage(QFileInfo(horizonNativeRoot()).absolutePath());
            if (storage.isValid() && storage.bytesAvailable() < 5LL * 1024 * 1024 * 1024 &&
                !QFileInfo::exists(QDir(engine_.dataDirectory()).filePath("rust/1.99.1-x86_64-pc-windows-gnu/.installed")))
                throw std::runtime_error("İlk Rust mod derlemesi için yaklaşık 5 GB boş alan gerekli; yeterli alan açıp tekrar dene.");
            const QString toolchain = ensureNativeRustToolchain(progress);
            const QString nativeRoot = horizonNativeRoot();
            const QString cargoHome = QDir(engine_.dataDirectory()).filePath("cargo");
            const QString tempDirectory = QDir(nativeRoot).filePath("tmp");
            QDir().mkpath(cargoHome); QDir().mkpath(tempDirectory);
            const QProcessEnvironment environment = nativeBuildEnvironment(toolchain, cargoHome, tempDirectory);
            const QString mixinsRoot = QDir(nativeRoot).filePath("tools/" + mixinsRevision);
            const QString mixinsExe = QDir(mixinsRoot).filePath("bin/horizon-mixins.exe");
            QDir().mkpath(mixinsRoot);
            if (!QFileInfo::exists(mixinsExe)) {
                progress("Sabitlenmiş horizon-mixins aracı derleniyor…", -1);
                QString output, error;
                const QString cargo = QDir(toolchain).filePath("bin/cargo.exe");
                const int code = runHorizonTool(nativeBuildPath(cargo), {"install", "--locked", "--git", "https://github.com/horizon-rust/horizon-mixins",
                                                        "--rev", mixinsRevision, "--root", nativeBuildPath(mixinsRoot), "horizon-mixins-cli"},
                                                QDir::rootPath(), environment, progress, &output, &error);
                if (code != 0 || !QFileInfo::exists(mixinsExe)) throw std::runtime_error(("horizon-mixins derlenemedi: " + error).toStdString());
            }
            const QString source = ensureHorizonNativeSource(horizonRevision, progress);
            const QString incoming = QDir(nativeRoot).filePath("incoming");
            QDir(incoming).removeRecursively(); QDir().mkpath(incoming);
            for (const BuildMod& mod : mods) {
                ZipArchive bundle; QString why;
                if (!bundle.open(mod.path, &why) || !bundle.extractUnder(QDir(incoming).filePath(mod.id), "mixins/" + mod.id, &why))
                    throw std::runtime_error((mod.id + " kaynakları güvenli şekilde açılamadı: " + why).toStdString());
            }
            const QString stage = QDir(nativeRoot).filePath("stage");
            QDir(stage).removeRecursively();
            QString output, error;
            const int applyCode = runHorizonTool(nativeBuildPath(mixinsExe), {"apply", "--workspace", nativeBuildPath(source), "--mods", nativeBuildPath(incoming), "--out", nativeBuildPath(stage)},
                                                 QDir::rootPath(), environment, progress, &output, &error);
            if (applyCode != 0) {
                // horizon-mixins başarısız — kaynak dizinini geçersiz kıl, sonraki denemede yeniden indirilir
                QFile::remove(QDir(source).filePath(".horizon-revision"));
                throw std::runtime_error(("horizon-mixins modları Horizon kaynağına uygulayamadı:\n" + error.toStdString()).c_str());
            }
            QJsonArray postBuildSteps;
            const QString postBuildFilePath = QDir(stage).filePath("native-post-build.json");
            if (QFileInfo::exists(postBuildFilePath)) {
                QFile postBuildFile(postBuildFilePath);
                if (!postBuildFile.open(QIODevice::ReadOnly) || postBuildFile.size() > 1024 * 1024)
                    throw std::runtime_error("HNMOD post-build adımları okunamıyor veya 1 MB sınırını aşıyor.");
                QJsonParseError postParse{};
                const QJsonDocument postDocument = QJsonDocument::fromJson(postBuildFile.readAll(), &postParse);
                if (postParse.error != QJsonParseError::NoError || !postDocument.isArray() || postDocument.array().size() > 128)
                    throw std::runtime_error("HNMOD native-post-build.json biçimi geçersiz veya çok fazla adım içeriyor.");
                postBuildSteps = postDocument.array();
                for (const QJsonValue& stepValue : postBuildSteps) {
                    const QJsonObject step = stepValue.toObject();
                    const QString modId = step.value("mod").toString();
                    const QString package = step.value("package").toString();
                    const QString binary = step.value("bin").toString();
                    if (!validHorizonModId(modId) || !QRegularExpression("^[A-Za-z0-9][A-Za-z0-9_.-]{0,127}$").match(package).hasMatch() ||
                        !QRegularExpression("^[A-Za-z0-9_-]{1,64}$").match(binary).hasMatch() ||
                        !step.value("args").isUndefined() && !step.value("args").isArray() || step.value("args").toArray().size() > 128)
                        throw std::runtime_error("HNMOD post-build adımı güvenli mod kimliği, Cargo paketi, ikili adı veya argüman listesi taşımıyor.");
                    for (const QJsonValue& arg : step.value("args").toArray())
                        if (!arg.isString() || arg.toString().size() > 4096) throw std::runtime_error("HNMOD post-build argümanı metin olmalı ve 4096 karakteri aşmamalı.");
                }
            }
            const QString cargo = QDir(toolchain).filePath("bin/cargo.exe");
            const QString targetDirectory = QDir(nativeRoot).filePath("target");
            QDir().mkpath(targetDirectory);
            QFile cargoToml(QDir(stage).filePath("Cargo.toml"));
            if (!cargoToml.open(QIODevice::ReadOnly)) throw std::runtime_error("Horizon mixin çıktısında Cargo.toml okunamadı.");
            const QString cargoText = QString::fromUtf8(cargoToml.readAll());
            const QString buildProfile = cargoText.contains(QRegularExpression("(?m)^\\s*\\[profile\\.fast\\]\\s*$")) ? "fast" : "release";
            QStringList arguments{"build", "--locked", "--profile", buildProfile};
            QFile clientCargo(QDir(stage).filePath("crates/horizon-client/Cargo.toml"));
            QString clientCargoText;
            if (clientCargo.open(QIODevice::ReadOnly)) clientCargoText = QString::fromUtf8(clientCargo.readAll());
            const auto featureSection = QRegularExpression("(?ms)^\\[features\\]\\s*(.*?)(?=^\\[|\\z)").match(clientCargoText);
            if (featureSection.hasMatch() && QRegularExpression("(?m)^\\s*mods\\s*=").match(featureSection.captured(1)).hasMatch())
                arguments << "--features" << "horizon-client/mods";
            arguments << "-p" << "horizon-client" << "--bin" << "horizon-client" << "--manifest-path" << nativeBuildPath(QDir(stage).filePath("Cargo.toml"));
            QProcessEnvironment buildEnvironment = environment;
            buildEnvironment.insert("CARGO_TARGET_DIR", nativeBuildPath(targetDirectory));
            // Incremental derleme: target/ dizini korunduğu için sonraki derlemeler hızlanır.
            // Farklı mod kombinasyonları farklı buildKey dizinlerine gider, çakışma olmaz.
            buildEnvironment.insert("CARGO_INCREMENTAL", "1");
            // codegen-units=8: paralel LLVM iş parçacığı, ilk derlemede de hız kazandırır.
            // release profili varsayılan olarak 1 kullanır; 8 yaklaşık 2-3x daha hızlı derler.
            if (!buildEnvironment.contains("RUSTFLAGS"))
                buildEnvironment.insert("RUSTFLAGS", "-C codegen-units=8");
            progress("Rust istemcisi derleniyor; Cargo ilerlemesi aşağıda görünecek…", -1);
            const int buildCode = runHorizonTool(nativeBuildPath(cargo), arguments, QDir::rootPath(), buildEnvironment, progress, &output, &error);
            if (buildCode != 0) throw std::runtime_error(("Rust istemcisi derlenemedi:\n" + error.toStdString()).c_str());
            const QString executable = QDir(targetDirectory).filePath(buildProfile + "/horizon-client.exe");
            if (!QFileInfo::exists(executable)) throw std::runtime_error("Cargo başarılı döndü ancak horizon-client.exe bulunamadı.");
            QCryptographicHash buildHash(QCryptographicHash::Sha256);
            buildHash.addData(horizonRevision.toUtf8()); buildHash.addData(mixinsRevision.toUtf8());
            for (const BuildMod& mod : mods) { buildHash.addData(mod.id.toUtf8()); buildHash.addData(mod.sha.toUtf8()); }
            const QString buildKey = QString::fromLatin1(buildHash.result().toHex());
            const QString installDirectory = QDir(nativeRoot).filePath("build/" + buildKey);
            QDir().mkpath(installDirectory);
            const QString installedExe = QDir(installDirectory).filePath("horizon-client.exe");
            if (sha256File(installedExe) != sha256File(executable)) {
                QFile::remove(installedExe);
                QFile input(executable); QSaveFile destination(installedExe);
                if (!input.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) throw std::runtime_error("Derlenen Rust istemcisi kurulum klasörüne kopyalanamadı.");
                while (!input.atEnd()) {
                    const QByteArray chunk = input.read(1024 * 1024);
                    if (chunk.isEmpty() && input.error() != QFileDevice::NoError) throw std::runtime_error("Derlenen istemci dosyası okunamadı.");
                    if (destination.write(chunk) != chunk.size()) throw std::runtime_error("Derlenen istemci dosyasına yazılamadı.");
                }
                if (!destination.commit()) throw std::runtime_error("Derlenen Rust istemcisi kaydedilemedi.");
            }
            for (qsizetype i = 0; i < postBuildSteps.size(); ++i) {
                const QJsonObject step = postBuildSteps.at(i).toObject();
                const QString modId = step.value("mod").toString();
                const QString package = step.value("package").toString();
                const QString binary = step.value("bin").toString();
                progress(modId + " post-build programı derleniyor…", -1);
                QStringList postArguments{"build", "--locked", "--profile", buildProfile, "-p", package, "--bin", binary,
                                         "--manifest-path", nativeBuildPath(QDir(stage).filePath("Cargo.toml"))};
                QProcessEnvironment postEnvironment = environment;
                postEnvironment.insert("CARGO_TARGET_DIR", nativeBuildPath(targetDirectory));
                const int postCompile = runHorizonTool(nativeBuildPath(cargo), postArguments, QDir::rootPath(), postEnvironment, progress, &output, &error);
                if (postCompile != 0) throw std::runtime_error((modId + " post-build programı derlenemedi:\n" + error).toStdString());
                const QString builtProgram = QDir(targetDirectory).filePath(buildProfile + "/" + binary + ".exe");
                const QString programDirectory = QDir(installDirectory).filePath("programs/" + QString::number(i));
                QDir().mkpath(programDirectory);
                const QString program = QDir(programDirectory).filePath(binary + ".exe");
                QFile::remove(program);
                if (!QFile::copy(builtProgram, program)) throw std::runtime_error((modId + " post-build programı kopyalanamadı.").toStdString());
                const QString dataDirectory = QDir(installDirectory).filePath("native-data/" + modId);
                const QString cacheDirectory = QDir(nativeRoot).filePath("cache/" + modId);
                const QString bundleDataDirectory = QDir(stage).filePath("native-data/" + modId);
                QDir().mkpath(dataDirectory); QDir().mkpath(cacheDirectory); QDir().mkpath(bundleDataDirectory);
                QProcessEnvironment runEnvironment = environment;
                runEnvironment.insert("HORIZON_NATIVE_MOD", modId);
                runEnvironment.insert("HORIZON_NATIVE_VERSION_DIR", nativeBuildPath(installDirectory));
                runEnvironment.insert("HORIZON_NATIVE_DATA_DIR", nativeBuildPath(dataDirectory));
                runEnvironment.insert("HORIZON_NATIVE_CACHE_DIR", nativeBuildPath(cacheDirectory));
                runEnvironment.insert("HORIZON_NATIVE_BUNDLE_DATA_DIR", nativeBuildPath(bundleDataDirectory));
                QStringList runArguments;
                for (const QJsonValue& arg : step.value("args").toArray()) runArguments.append(arg.toString());
                const int postRun = runHorizonTool(nativeBuildPath(program), runArguments, nativeBuildPath(installDirectory), runEnvironment, progress, &output, &error);
                if (postRun != 0) throw std::runtime_error((modId + " post-build programı başarısız oldu:\n" + error).toStdString());
            }
            const QJsonObject metadata{{"schema", 1}, {"horizon", horizonRevision}, {"mixins", mixinsRevision},
                                       {"profile", buildProfile}, {"mods", modHashes}, {"resources", resources},
                                       {"mingw_bin", QDir(toolchain).filePath("mingw64/bin")}};
            QSaveFile manifestFile(QDir(installDirectory).filePath("native-mods.json"));
            const QByteArray manifestBytes = QJsonDocument(metadata).toJson(QJsonDocument::Indented);
            if (!manifestFile.open(QIODevice::WriteOnly) || manifestFile.write(manifestBytes) != manifestBytes.size() || !manifestFile.commit())
                throw std::runtime_error("Derleme bilgisi kaydedilemedi.");
            r.object = {{"path", installedExe}, {"mods", modHashes}, {"key", buildKey},
                        {"mingw_bin", QDir(toolchain).filePath("mingw64/bin")},
                        {"unsigned", QJsonArray::fromStringList(unsignedMods)}};
            r.text = QString("Rust 26.3 istemcisi derlendi. Horizon commit: %1 · horizon-mixins: %2 · mod sayısı: %3\nİstemci: %4")
                .arg(horizonRevision, mixinsRevision).arg(mods.size()).arg(installedExe);
            if (!unsignedMods.isEmpty()) r.text += "\nİmzasız modlar: " + unsignedMods.join(", ");
            r.ok = true;
            return r;
        }, [this](TaskResult r) {
            if (horizonBuildModsButton_) { horizonBuildModsButton_->setEnabled(true); horizonBuildModsButton_->setText("⚙  RUST MODLARINI DERLE"); }
            if (!r.ok) {
                if (horizonModsStatus_) horizonModsStatus_->setText("Derleme başarısız: " + r.error);
                QMessageBox::critical(this, "Rust modları derlenemedi", r.error);
                setStatus("Rust mod derlemesi başarısız", 0);
                return;
            }
            config_.insert("horizon_native_client_path", r.object.value("path"));
            config_.insert("horizon_native_mod_hashes", r.object.value("mods"));
            config_.insert("horizon_native_build_key", r.object.value("key"));
            config_.insert("horizon_native_mingw_bin", r.object.value("mingw_bin"));
            saveConfig();
            if (horizonModsStatus_) horizonModsStatus_->setText(r.text);
            QMessageBox::information(this, "Rust istemcisi derlendi", r.text + "\n\nArtık profillerden Rust 26.3'ü başlatabilirsin.");
            setStatus("Rust istemcisi ve modlar hazır", 100);
        });
    }
    void installHorizonClient() {
        if(activeTasks_>0){QMessageBox::information(this,"İşlem sürüyor","Başka bir arka plan işi bitince tekrar dene.");return;}
        setHorizonTaskBusy(true);
        setStatus("Resmî Horizon Nightly sürümü aranıyor…",5);
        runTask([this]{
            TaskResult r;
            const QByteArray response=requestGitHubApi("https://api.github.com/repos/horizon-rust/horizon/releases?per_page=100");
            QJsonParseError parse{};
            const QJsonDocument document=QJsonDocument::fromJson(response,&parse);
            if(parse.error!=QJsonParseError::NoError||!document.isArray())throw std::runtime_error("GitHub sürüm listesi geçerli JSON değil.");
            QJsonObject latest;
            const QRegularExpression nightlyPattern("^nightly-[0-9]{4}-[0-9]{2}-[0-9]{2}$");
            for(const QJsonValue& value:document.array()) {
                const QJsonObject release=value.toObject();
                if(nightlyPattern.match(release.value("tag_name").toString()).hasMatch()&&
                   !horizonAssetUrl(release,"horizon-windows-x64.zip").isEmpty()&&
                   !horizonAssetUrl(release,"SHA256SUMS").isEmpty()) { latest=release;break; }
            }
            if(latest.isEmpty())throw std::runtime_error("Windows x64 arşivi olan resmî Horizon Nightly sürümü bulunamadı.");
            const QString tag=latest.value("tag_name").toString();
            const QString root=QDir(horizonRootDirectory()).filePath("releases/"+tag);
            const QString existing=findHorizonExecutable(root,"horizon-client.exe");
            const QString existingPrepare=findHorizonExecutable(root,"horizon-prepare.exe");
            const QString existingServer=findHorizonExecutable(root,"horizon-server.exe");
            if(!existing.isEmpty()&&!existingPrepare.isEmpty()&&!existingServer.isEmpty()) {
                r.object={{"tag",tag}};r.text="Bu Nightly zaten indirilmiş ve açılmış.";r.ok=true;return r;
            }
            const QString cache=QDir(horizonRootDirectory()).filePath("downloads");
            QDir().mkpath(cache);
            const QString sumsPath=QDir(cache).filePath("SHA256SUMS-"+tag+".txt");
            const QString archivePath=QDir(cache).filePath("horizon-windows-x64-"+tag+".zip");
            const QStringList trusted={"github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"};
            engine_.downloadUrl(horizonAssetUrl(latest,"SHA256SUMS"),sumsPath,{},progressCallback(),trusted);
            QFile sumsFile(sumsPath);
            if(!sumsFile.open(QIODevice::ReadOnly))throw std::runtime_error("Horizon checksum listesi açılamadı.");
            const QString sums=QString::fromUtf8(sumsFile.readAll());
            const QRegularExpression checksumPattern("^([0-9a-fA-F]{64})[ \\t]+\\*?horizon-windows-x64\\.zip[ \\t]*$",QRegularExpression::MultilineOption);
            const auto match=checksumPattern.match(sums);
            if(!match.hasMatch())throw std::runtime_error("SHA256SUMS içinde Windows arşivinin checksum değeri bulunamadı.");
            const QString checksum=match.captured(1).toLower();
            engine_.downloadUrl(horizonAssetUrl(latest,"horizon-windows-x64.zip"),archivePath,{{"sha256",checksum}},progressCallback(),trusted);
            QFile::remove(sumsPath);
            ZipArchive archive;QString why;
            if(!archive.open(archivePath,&why)||!archive.extractUnder(root,{},&why)) {
                QFile::remove(archivePath);
                throw std::runtime_error((why.isEmpty()?QString("Horizon arşivi açılamadı."):why).toStdString());
            }
            QFile::remove(archivePath);
            const QString client=findHorizonExecutable(root,"horizon-client.exe");
            const QString prepare=findHorizonExecutable(root,"horizon-prepare.exe");
            const QString server=findHorizonExecutable(root,"horizon-server.exe");
            if(client.isEmpty()||prepare.isEmpty()||server.isEmpty())throw std::runtime_error("Arşiv açıldı ancak horizon-client.exe, horizon-prepare.exe veya horizon-server.exe eksik.");
            r.object={{"tag",tag}};
            r.text=QString("%1 Nightly indirildi. SHA-256: %2").arg(tag,checksum);
            r.ok=true;return r;
        },[this](TaskResult r){
            setHorizonTaskBusy(false);
            if(!r.ok){if(horizonOutput_)horizonOutput_->append("İndirme başarısız: "+r.error);QMessageBox::critical(this,"Horizon indirilemedi",r.error);setStatus("Horizon istemcisi indirilemedi",0);return;}
            config_.insert("horizon_client_tag",r.object.value("tag"));config_.remove("horizon_client_directory");saveConfig();
            if(horizonOutput_)horizonOutput_->append(r.text);
            refreshHorizonClientStatus();setStatus("Horizon istemcisi hazır",100);
        });
    }
    void prepareHorizonClient() {
        if(activeTasks_>0){QMessageBox::information(this,"İşlem sürüyor","Başka bir arka plan işi bitince tekrar dene.");return;}
        const QString clientDirectory=horizonClientDirectory();
        const QString prepare=findHorizonExecutable(clientDirectory,"horizon-prepare.exe");
        if(prepare.isEmpty()){QMessageBox::warning(this,"Horizon istemcisi","Önce resmî Nightly arşivini Titan üzerinden indir.");return;}
        const QString minecraftPath=horizonMinecraftPath_?horizonMinecraftPath_->text().trimmed():QString();
        if(minecraftPath.isEmpty()||!QFileInfo(minecraftPath).isDir()){QMessageBox::warning(this,"Minecraft 26.3 klasörü","Önce kendi Minecraft klasörünü seç.");return;}
        const QString versionJson=QDir(minecraftPath).filePath("versions/26.3/26.3.json");
        const QString clientJar=QDir(minecraftPath).filePath("versions/26.3/26.3.jar");
        if(!QFileInfo::exists(versionJson)||!QFileInfo::exists(clientJar)){QMessageBox::warning(this,"Minecraft 26.3 bulunamadı","Seçilen klasörde 26.3 sürüm bilgisi veya istemci JAR’ı eksik. Resmî Minecraft Launcher’da Java Edition 26.3’ü kurup bir kez başlat.");return;}
        if(QMessageBox::question(this,"Horizon kaynaklarını hazırla",
            QString("Horizon’un resmi horizon-prepare aracı çalıştırılacak.\n\nMinecraft 26.3 dosyalarını buradan okuyacak:\n%1\n\nServer.jar Mojang’dan, Java 25 JDK Adoptium’dan indirilebilir. Hazırlanan dosyalar Titan’ın Horizon klasöründe kalır. Horizon Launcher kurulmaz. Devam edilsin mi?").arg(QDir::toNativeSeparators(minecraftPath)))!=QMessageBox::Yes)return;
        config_.insert("horizon_minecraft_path",minecraftPath);saveConfig();
        setHorizonTaskBusy(true);setStatus("Minecraft 26.3 Horizon için hazırlanıyor…",5);
        const QString javaHint=config_.value("custom_java").toString();
        runTask([this,prepare,minecraftPath,versionJson,javaHint]{
            TaskResult r;
            QFile versionFile(versionJson);
            if(!versionFile.open(QIODevice::ReadOnly))throw std::runtime_error("Minecraft 26.3 sürüm bilgisi okunamadı.");
            const QJsonObject metadata=QJsonDocument::fromJson(versionFile.readAll()).object();
            if(metadata.value("id").toString()!="26.3")throw std::runtime_error("Seçilen sürüm dosyasının kimliği 26.3 değil.");
            const QJsonObject server=metadata.value("downloads").toObject().value("server").toObject();
            const QString serverUrl=server.value("url").toString();
            const QString serverSha1=server.value("sha1").toString().toLower();
            const QUrl parsedServer(serverUrl);
            if(!parsedServer.isValid()||parsedServer.scheme()!="https"||
               !(parsedServer.host().endsWith(".mojang.com",Qt::CaseInsensitive)||parsedServer.host().endsWith(".minecraft.net",Qt::CaseInsensitive))||
               !QRegularExpression("^[0-9a-f]{40}$").match(serverSha1).hasMatch())
                throw std::runtime_error("26.3 server.jar için Mojang’ın geçerli HTTPS adresi veya SHA-1 değeri bulunamadı.");
            const QString gameHome=horizonGameHome();
            const QString resources=QDir(QFileInfo(prepare).absolutePath()).filePath(".local/horizon-client/26.3");
            const QString serverJar=QDir(horizonRootDirectory()).filePath("cache/minecraft-26.3-server.jar");
            QDir().mkpath(QFileInfo(serverJar).absolutePath());
            if(sha1File(serverJar).compare(serverSha1,Qt::CaseInsensitive)!=0)
                engine_.downloadUrl(serverUrl,serverJar,{{"sha1",serverSha1}},progressCallback(),{parsedServer.host()});
            const QString java=engine_.ensureJavaForVersion("26.3",javaHint,progressCallback());
            QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
            const QString javaBin=QFileInfo(java).absolutePath();
            environment.insert("JAVA_HOME",QDir(javaBin).absolutePath()+"/..");
            environment.insert("PATH",javaBin+";"+environment.value("PATH"));
            QDir().mkpath(gameHome);
            const QString manifestSha1="11f1aefe473eefba0af62e3286c33b692807b2eb";
            const QString manifestUrl="https://piston-meta.mojang.com/v1/packages/"+manifestSha1+"/26.3.json";
            const QString manifestFile=QDir(QFileInfo(prepare).absolutePath()).filePath(".local/minecraft/26.3/manifest.json");
            QDir().mkpath(QFileInfo(manifestFile).absolutePath());
            if(sha1File(manifestFile).compare(manifestSha1,Qt::CaseInsensitive)!=0)
                engine_.downloadUrl(manifestUrl,manifestFile,{{"sha1",manifestSha1}},progressCallback(),{"piston-meta.mojang.com"});
            QString output,processError;
            const int prepareCode=runHorizonTool(prepare,{"--prepare","--server-jar",serverJar,"--minecraft-dir",minecraftPath},gameHome,environment,progressCallback(),&output,&processError);
            if(prepareCode!=0)throw std::runtime_error((processError.isEmpty()?QString("Horizon prepare failed."):processError).toStdString());
            r.text=output;
            QString soundOutput,soundError;
            const int soundCode=runHorizonTool(prepare,{"sounds","--minecraft-dir",minecraftPath},gameHome,environment,progressCallback(),&soundOutput,&soundError);
            if(soundCode!=0)r.text+="\nSes dosyaları kopyalanamadı: "+soundError;
            if(!QFileInfo(resources).isDir())throw std::runtime_error("Horizon prepare tamamlandı ancak hazırlanmış 26.3 kaynak klasörü bulunamadı.\n"+output.right(4000).toStdString());
            r.object={{"resources",resources}};r.ok=true;return r;
        },[this](TaskResult r){
            setHorizonTaskBusy(false);
            if(!r.ok){if(horizonOutput_)horizonOutput_->append(r.error);QMessageBox::critical(this,"Hazırlık başarısız",r.error);setStatus("Horizon hazırlığı başarısız",0);return;}
            if(horizonOutput_)horizonOutput_->append(r.text.right(6000));
            setStatus("Minecraft 26.3 kaynakları hazır",100);
        });
    }
    void playHorizonClient() {
        if(gameStarting_||activeTasks_>0){QMessageBox::information(this,"İşlem sürüyor","Önce mevcut işlem tamamlansın.");return;}
        if(process_&&process_->state()!=QProcess::NotRunning){QMessageBox::warning(this,"Minecraft","Başka bir oyun zaten çalışıyor.");return;}
        if(horizonProcess_&&horizonProcess_->state()!=QProcess::NotRunning){QMessageBox::information(this,"Horizon","Horizon istemcisi zaten çalışıyor.");return;}
        const QString baseClient=findHorizonExecutable(horizonClientDirectory(),"horizon-client.exe");
        if(baseClient.isEmpty()){QMessageBox::warning(this,"Horizon istemcisi","Önce resmî Horizon Nightly arşivini indir.");return;}
        QJsonObject currentNativeHashes;
        const QString nativeModsDirectory=QDir(horizonGameHome()).filePath("horizon-native-mods");
        QDirIterator nativeMods(nativeModsDirectory,QStringList{"*.hnmod"},QDir::Files|QDir::NoSymLinks);
        while(nativeMods.hasNext()){
            const QString path=nativeMods.next();
            const QString hash=sha256File(path);
            if(!hash.isEmpty())currentNativeHashes.insert(QFileInfo(path).completeBaseName(),hash);
        }
        QString client=baseClient;
        if(!currentNativeHashes.isEmpty()){
            const QString compiled=config_.value("horizon_native_client_path").toString();
            const QJsonObject compiledHashes=config_.value("horizon_native_mod_hashes").toObject();
            if(compiled.isEmpty()||!QFileInfo::exists(compiled)||QJsonDocument(currentNativeHashes).toJson(QJsonDocument::Compact)!=QJsonDocument(compiledHashes).toJson(QJsonDocument::Compact)){
                QMessageBox::information(this,"Rust modları derlenmeli","Yüklü .HNMOD dosyaları değişmiş veya henüz derlenmemiş. Önce Mod Yöneticisi → Rust Modları → ‘Rust modlarını derle’ düğmesini kullan.");return;
            }
            client=compiled;
        }
        if(client.isEmpty()){QMessageBox::warning(this,"Horizon istemcisi","Önce resmî Horizon Nightly arşivini indir.");return;}
        const QString resources=QDir(QFileInfo(baseClient).absolutePath()).filePath(".local/horizon-client/26.3");
        if(!QFileInfo(resources).isDir()){QMessageBox::warning(this,"Minecraft 26.3 hazırlığı","Önce 26.3 kaynaklarını hazırla.");return;}
        const QJsonObject account=config_.value("ms_account").toObject();
        const bool microsoft=config_.value("account_type").toString()=="microsoft"&&!account.value("access_token").toString().isEmpty();
        QString username=microsoft?account.value("name").toString("Player"):config_.value("username").toString("Steve").trimmed();
        if(username.isEmpty())username="Steve";
        if(!QRegularExpression("^[A-Za-z0-9_]{1,16}$").match(username).hasMatch()){QMessageBox::warning(this,"Oyuncu adı","Horizon için oyuncu adı 1–16 harf, rakam veya alt çizgi olmalı.");return;}
        const QString sessionDirectory=QDir(horizonGameHome()).filePath("sessions");
        const QString msClientId=config_.value("custom_ms_client_id").toString();
        gameStarting_=true;setHorizonTaskBusy(true);setStatus(microsoft?"Horizon Microsoft oturumu hazırlanıyor…":"Horizon çevrimdışı modda başlatılıyor…",10);
        runTask([this,microsoft,account,username,sessionDirectory,msClientId]{
            TaskResult r;
            if(microsoft){
                QJsonObject fresh=account;
                if(account.contains("refresh_token"))fresh=engine_.refreshMicrosoft(msClientId,account.value("refresh_token").toString());
                const QString accessToken=fresh.value("access_token").toString();
                if(accessToken.isEmpty())throw std::runtime_error("Microsoft oyun oturumu boş. Titan’da yeniden giriş yap veya çevrimdışı hesabı seç.");
                if(fresh.value("xuid").toString().isEmpty())throw std::runtime_error("Horizon için Xbox oturum bilgisi eksik. Yeniden giriş yapabilir veya çevrimdışı hesabı seçebilirsin.");
                QDir().mkpath(sessionDirectory);
                const QString sessionPath=QDir(sessionDirectory).filePath("session-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json");
                const QJsonObject session{{"name",fresh.value("name")},{"uuid",fresh.value("id")},{"access_token",accessToken},{"user_type","msa"},{"xuid",fresh.value("xuid")}};
                QSaveFile file(sessionPath);
                const QByteArray sessionBytes=QJsonDocument(session).toJson(QJsonDocument::Compact);
                if(!file.open(QIODevice::WriteOnly)||file.write(sessionBytes)!=sessionBytes.size()||!file.commit())throw std::runtime_error("Horizon için geçici Microsoft oturum dosyası oluşturulamadı.");
                QFile::setPermissions(sessionPath,QFileDevice::ReadOwner|QFileDevice::WriteOwner);
                r.text=sessionPath;r.object=fresh;
            }else r.object=QJsonObject{{"name",username}};
            r.ok=true;return r;
        },[this,client,resources,microsoft,username](TaskResult r){
            gameStarting_=false;setHorizonTaskBusy(false);
            if(!r.ok){QMessageBox::critical(this,"Horizon başlatılamadı",r.error);setStatus("Horizon başlatılamadı",0);return;}
            if(microsoft&&r.object.contains("access_token")){upsertMicrosoftAccount(r.object,false);saveConfig();}
            const QString sessionPath=microsoft?r.text:QString();
            QStringList arguments{"--resources",resources};
            if(microsoft)arguments<<"--session-file"<<sessionPath;else arguments<<"--name"<<username;
            if(horizonOutput_)horizonOutput_->append(QString("> %1 %2").arg(QDir::toNativeSeparators(client),arguments.join(" ")));
            QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
            environment.insert("HORIZON_HOME",horizonGameHome());
            environment.remove("HORIZON_OFFLINE_SKIN");
            environment.remove("HORIZON_OFFLINE_SKIN_MODEL");
            environment.remove("HORIZON_OFFLINE_CAPE");
            if(!microsoft){
                QString offlineSkin=config_.value("active_skin_file").toString().trimmed();
                if(!QFileInfo(offlineSkin).isFile()&&QFileInfo(selectedSkinPath_).isFile())offlineSkin=selectedSkinPath_;
                if(!QFileInfo(offlineSkin).isFile())offlineSkin=QDir(engine_.skinsDirectory()).filePath(username+".png");
                if(QFileInfo(offlineSkin).isFile()){
                    QImage image(offlineSkin);
                    if(image.size()==QSize(128,128)){
                        const QString normalized=QDir(engine_.skinsDirectory()).filePath("horizon_"+username+".png");
                        QSaveFile output(normalized);
                        if(output.open(QIODevice::WriteOnly)&&image.scaled(64,64,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).save(&output,"PNG")&&output.commit())offlineSkin=normalized;
                        else{output.cancelWriting();if(horizonOutput_)horizonOutput_->append("Rust skin aktarılmadı: 128×128 görsel 64×64'e dönüştürülemedi.");offlineSkin.clear();}
                    }
                    const QImage checkedSkin(offlineSkin);
                    const bool supported=checkedSkin.size()==QSize(64,64)||checkedSkin.size()==QSize(64,32);
                    if(supported){
                        environment.insert("HORIZON_OFFLINE_SKIN",QDir::toNativeSeparators(QFileInfo(offlineSkin).absoluteFilePath()));
                        const QString skinModel=config_.value("skin_model").toString();
                        environment.insert("HORIZON_OFFLINE_SKIN_MODEL",skinModel.contains("İnce",Qt::CaseInsensitive)?"slim":"classic");
                        if(horizonOutput_)horizonOutput_->append("Çevrimdışı skin Rust'a aktarılıyor: "+QFileInfo(offlineSkin).fileName());
                    }else if(horizonOutput_)horizonOutput_->append("Rust skin aktarılmadı: Horizon 64×64 veya 64×32 PNG skin bekliyor.");
                }else if(horizonOutput_)horizonOutput_->append("Rust için seçili skin dosyası bulunamadı. Skinler sayfasında skin'i seçip ‘Profili Uygula’ düğmesine bas.");
            }else if((QFileInfo(config_.value("active_skin_file").toString()).isFile()||QFileInfo(selectedSkinPath_).isFile())&&horizonOutput_){
                horizonOutput_->append("Seçili hesap Microsoft hesabı. Horizon 26.3 şu an kendi özel skin aktarımını yalnızca çevrimdışı hesapta destekliyor.");
            }
            const QString mingwBin=config_.value("horizon_native_mingw_bin").toString();
            if(QFileInfo(mingwBin).isDir())environment.insert("PATH",mingwBin+";"+environment.value("PATH"));
            horizonPlayMode_=true;
            horizonProcess_=new QProcess(this);QProcess* game=horizonProcess_;
            game->setProgram(client);game->setArguments(arguments);game->setWorkingDirectory(horizonGameHome());game->setProcessEnvironment(environment);
            connect(game,&QProcess::readyReadStandardOutput,this,[this,game]{const QString text=QString::fromUtf8(game->readAllStandardOutput());if(!text.isEmpty()&&horizonOutput_)horizonOutput_->append(text.trimmed());});
            connect(game,&QProcess::readyReadStandardError,this,[this,game]{const QString text=QString::fromUtf8(game->readAllStandardError());if(!text.isEmpty()&&horizonOutput_)horizonOutput_->append(text.trimmed());});
            QPointer<MainWindow> self(this);
            connect(game,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),game,[self,game,sessionPath](int code,QProcess::ExitStatus exitStatus){
                if(!sessionPath.isEmpty())QFile::remove(sessionPath);
                if(self){if(self->horizonWindowBrandTimer_)self->horizonWindowBrandTimer_->stop();if(self->horizonProcess_==game)self->horizonProcess_=nullptr;self->horizonPlayMode_=false;self->horizonPlayButton_->setEnabled(true);self->horizonStopButton_->setEnabled(false);if(self->horizonLauncherHidden_){self->horizonLauncherHidden_=false;self->showMaximized();self->raise();self->activateWindow();}const bool success=exitStatus==QProcess::NormalExit&&code==0;self->setStatus(success?"Rust 26.3 kapandı.":QString("Rust 26.3 hata koduyla kapandı (%1).").arg(code),success?100:0);}
                game->deleteLater();
            });
            connect(game,&QProcess::errorOccurred,this,[this,game,sessionPath](QProcess::ProcessError error){
                if(error!=QProcess::FailedToStart)return;
                if(horizonWindowBrandTimer_)horizonWindowBrandTimer_->stop();
                if(horizonProcess_==game)horizonProcess_=nullptr;horizonPlayMode_=false;horizonPlayButton_->setEnabled(true);horizonStopButton_->setEnabled(false);
                if(!sessionPath.isEmpty())QFile::remove(sessionPath);
                if(horizonLauncherHidden_){horizonLauncherHidden_=false;showMaximized();raise();activateWindow();}
                const QString message="Horizon istemcisi açılamadı: "+game->errorString();if(horizonOutput_)horizonOutput_->append(message);setStatus("Horizon başlatılamadı",0);game->deleteLater();
            });
            horizonPlayButton_->setEnabled(false);horizonStopButton_->setEnabled(true);
#ifdef Q_OS_WIN
            connect(game,&QProcess::started,this,[this,game]{
                if(!horizonWindowBrandTimer_){
                    horizonWindowBrandTimer_=new QTimer(this);horizonWindowBrandTimer_->setInterval(400);
                    connect(horizonWindowBrandTimer_,&QTimer::timeout,this,[this]{
                        const DWORD processId=horizonWindowBrandTimer_->property("processId").toUInt();
                        if(brandRustWindow(processId))horizonWindowBrandTimer_->stop();
                    });
                }
                const DWORD processId=static_cast<DWORD>(game->processId());
                horizonWindowBrandTimer_->setProperty("processId",static_cast<quint32>(processId));horizonWindowBrandTimer_->start();
                QTimer::singleShot(30000,this,[this,processId]{if(horizonWindowBrandTimer_&&horizonWindowBrandTimer_->property("processId").toUInt()==static_cast<quint32>(processId))horizonWindowBrandTimer_->stop();});
            });
#endif
            game->start();
            if(!game->waitForStarted(8000))return;
            if(game->state()==QProcess::NotRunning)return;
            setStatus("Rust 26.3 çalışıyor",100);
            const QString behavior=closeBehavior_?closeBehavior_->currentText():config_.value("close_behavior").toString();
            if(behavior.startsWith("Gizle")){horizonLauncherHidden_=true;hide();}
            else if(behavior=="Kapat"){
                if(close())QTimer::singleShot(0,qApp,&QCoreApplication::quit);
            }
            if(!sessionPath.isEmpty())QTimer::singleShot(125000,this,[sessionPath]{if(QFileInfo::exists(sessionPath))QFile::remove(sessionPath);});
        });
    }
    QWidget* buildHomePage() {
        auto* page=new HeroBackdrop(); auto* v=new QVBoxLayout(page); v->setContentsMargins(38,25,38,28); v->setSpacing(10); v->addStretch();
        auto* headline=label("OYUN HAZIR",31,kGreen,true,page); v->addWidget(headline);
        v->addWidget(label("Profilini seç, adını belirle ve Minecraft'ı başlat.",12,"#e2e5eb",false,page));
        auto* accountCard=card(page); accountCard->setMaximumWidth(650); auto* accountLayout=new QVBoxLayout(accountCard); accountLayout->setContentsMargins(14,12,14,12); accountLayout->setSpacing(9);
        auto* accountTypeRow=new QHBoxLayout(); accountTypeRow->addWidget(label("KAYITLI HESAPLAR",10,"#b9bdc7",true,accountCard)); accountTypeRow->addStretch(); accountBox_=new AnimatedComboBox(accountCard); accountBox_->setMinimumWidth(230); accountBox_->setStyleSheet(comboStyle()); accountTypeRow->addWidget(accountBox_); auto* removeAccount=actionButton("HESABI KALDIR","#522b32","#713942",accountCard); removeAccount->setMinimumWidth(120); accountTypeRow->addWidget(removeAccount); accountLayout->addLayout(accountTypeRow);
        auto* nameRow=new QHBoxLayout(); auto* nameColumn=new QVBoxLayout(); nameColumn->setSpacing(4); nameColumn->addWidget(label("YENİ ÇEVRİMDIŞI HESAP ADI",9,kMuted,true,accountCard)); const QString initialOfflineName=config_.value("account_type").toString()=="offline"?config_.value("username").toString("Steve"):QString("Steve"); offlineName_=new QLineEdit(initialOfflineName,accountCard); offlineName_->setPlaceholderText("1–16 harf, rakam veya alt çizgi"); offlineName_->setMaxLength(16); offlineName_->setStyleSheet(inputStyle()); nameColumn->addWidget(offlineName_); nameRow->addLayout(nameColumn,1); auto* saveName=actionButton("ÇEVRİMDIŞI HESAP EKLE","#236d4d","#2c8b62",accountCard); saveName->setMinimumWidth(170); nameRow->addWidget(saveName,0,Qt::AlignBottom); accountLayout->addLayout(nameRow); v->addWidget(accountCard,0,Qt::AlignLeft);
        connect(accountBox_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){if(accountBox_->currentIndex()>=0)activateSavedAccount(accountBox_->currentData().toString());});
        connect(saveName,&QPushButton::clicked,this,[this]{addOfflineAccount(offlineName_->text());});
        connect(removeAccount,&QPushButton::clicked,this,[this]{removeActiveAccount();});
        auto* profileLine=new QHBoxLayout(); profileBox_=new AnimatedComboBox(page); profileBox_->setMinimumWidth(205); profileBox_->setStyleSheet(comboStyle()); profileLine->addWidget(profileBox_); versionBox_=new AnimatedComboBox(page); versionBox_->setMinimumWidth(140); versionBox_->setStyleSheet(comboStyle()); profileLine->addWidget(versionBox_); loaderBox_=new AnimatedComboBox(page); loaderBox_->addItems({"Vanilla","Fabric","Forge","NeoForge","Quilt"}); loaderBox_->setMinimumWidth(120); loaderBox_->setStyleSheet(comboStyle()); profileLine->addWidget(loaderBox_); auto* profileManager=actionButton("PROFİLLER ⚙","#25282e","#363a42",page); profileLine->addWidget(profileManager); profileLine->addStretch(); v->addLayout(profileLine);
        connect(profileManager,&QPushButton::clicked,this,[this,profileManager]{showProfileManager(profileManager);});
        connect(profileBox_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){onProfileChanged();}); connect(versionBox_,&QComboBox::currentTextChanged,this,[this]{if(pages_->currentIndex()==0)saveConfig();}); connect(loaderBox_,&QComboBox::currentTextChanged,this,[this]{saveConfig();});
        auto* row=new QHBoxLayout(); row->addStretch(); auto* play=actionButton("▶  OYNA",kGreen,"#63e99f",page); play->setMinimumWidth(142); play->setMinimumHeight(46); play->setStyleSheet("QPushButton{background:#39d98a;color:#092317;border:0;border-radius:9px;padding:8px 18px;font-size:15px;font-weight:900;}QPushButton:hover{background:#63e99f;}QPushButton:pressed{padding-top:10px;}QPushButton:disabled{background:#303238;color:#777b84;}"); connect(play,&QPushButton::clicked,this,[this]{launchGame();}); playButton_=play; row->addWidget(play); v->addLayout(row); return page;
    }

    QString comboStyle() const {
        return QString("QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:7px;padding:7px 9px;font-weight:700;}"
                       "QComboBox QAbstractItemView{background:%1;color:%2;selection-background-color:%4;}")
            .arg(gTheme.inputBg, gTheme.text, gTheme.inputBorder, gTheme.green.left(7));
    }
    QString inputStyle() const {
        return QString("QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:7px;padding:8px;}"
                       "QLineEdit:focus{border:1px solid %4;}")
            .arg(gTheme.inputBg, gTheme.text, gTheme.inputBorder, gTheme.green);
    }
    QWidget* buildModsPage() {
        auto* page=new QWidget(); page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bg,gTheme.text)); auto* root=new QVBoxLayout(page); root->setContentsMargins(20,18,20,18); root->setSpacing(12);
        root->addWidget(label("MOD YÖNETİCİSİ",20,kGreen,true,page));
        auto* tabs=new QHBoxLayout(); modView_=new AnimatedComboBox(page); modView_->addItems({"Yüklü Modlarım","Rust Modları","Çevrimiçi İçerik Ara"}); modView_->setStyleSheet(comboStyle()); tabs->addWidget(modView_); tabs->addStretch(); root->addLayout(tabs);
        modsSubpages_=new QStackedWidget(page); root->addWidget(modsSubpages_,1);
        auto* online=new QWidget(); auto* onlineV=new QVBoxLayout(online); onlineV->setContentsMargins(0,0,0,0); onlineV->setSpacing(9);
        auto* filters=new QHBoxLayout(); marketProvider_=new AnimatedComboBox(online); marketProvider_->addItems({"Modrinth","CurseForge"}); marketProvider_->setStyleSheet(comboStyle()); marketType_=new AnimatedComboBox(online); marketType_->addItem("Modlar","mod"); marketType_->addItem("Modpack'ler","modpack"); marketType_->addItem("Resource pack'ler","resourcepack"); marketType_->addItem("Shader'lar","shader"); marketType_->addItem("Data pack'ler","datapack"); marketType_->addItem("Dünyalar","world"); marketType_->setStyleSheet(comboStyle()); filters->addWidget(marketProvider_); filters->addWidget(marketType_); onlineV->addLayout(filters);
        auto* marketSearchRow=new QHBoxLayout(); marketSearch_=new QLineEdit(online); marketSearch_->setPlaceholderText("Mod, modpack, shader, data pack veya dünya ara"); marketSearch_->setStyleSheet(inputStyle()); marketSearchRow->addWidget(marketSearch_,1); marketplaceSort_=new AnimatedComboBox(online); marketplaceSort_->addItem("İndirmeye göre","downloads"); marketplaceSort_->addItem("İsme göre (A-Z)","name_asc"); marketplaceSort_->addItem("İsme göre (Z-A)","name_desc"); marketplaceSort_->setStyleSheet(comboStyle()); marketplaceSort_->setToolTip("Çevrimiçi sonuçları sıralama biçimi"); marketSearchRow->addWidget(marketplaceSort_); auto* search=actionButton("⌕  ARA","#2b2e35","#383b43",online); marketSearchRow->addWidget(search); onlineV->addLayout(marketSearchRow);
        auto* filterRow=new QHBoxLayout(); marketVersion_=new AnimatedComboBox(online); marketVersion_->addItem("Tümü"); marketVersion_->setStyleSheet(comboStyle()); marketLoader_=new AnimatedComboBox(online); marketLoader_->addItems({"Tümü","Vanilla","Fabric","Forge","NeoForge","Quilt"}); marketLoader_->setStyleSheet(comboStyle()); worldTarget_=new AnimatedComboBox(online); worldTarget_->setStyleSheet(comboStyle()); worldTarget_->addItem("Data pack hedef dünyası seç",""); worldTarget_->setVisible(false); filterRow->addWidget(marketVersion_); filterRow->addWidget(marketLoader_); filterRow->addWidget(worldTarget_); filterRow->addStretch(); onlineV->addLayout(filterRow);
        marketResults_=new QWidget(online); marketResultsLayout_=new QVBoxLayout(marketResults_); marketResultsLayout_->setContentsMargins(0,0,0,0); marketResultsLayout_->setSpacing(8); marketResultsLayout_->addWidget(label("Çevrimiçi modlar açılınca popüler sonuçlar gösterilir. Üstten arayabilir ve sıralamayı değiştirebilirsin.",11,kMuted,false,marketResults_)); marketResultsLayout_->addStretch(); auto* marketScroll=new QScrollArea(online); marketScroll->setWidgetResizable(true); marketScroll->setFrameShape(QFrame::NoFrame); marketScroll->setWidget(marketResults_); onlineV->addWidget(marketScroll,1);
        connect(search,&QPushButton::clicked,this,[this]{searchMarketplace();}); connect(marketSearch_,&QLineEdit::returnPressed,this,[this]{searchMarketplace();}); connect(marketplaceSort_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{if(marketplaceHasSearched_)renderMarketplaceResults();});
        connect(marketType_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{const QString type=marketType_->currentData().toString();marketLoader_->setVisible(type=="mod"||type=="modpack");worldTarget_->setVisible(type=="datapack");if(type=="datapack")refreshWorldSelector();});
        auto* installed=new QWidget(); auto* installedV=new QVBoxLayout(installed); installedV->setContentsMargins(0,0,0,0); auto* modControls=new QHBoxLayout(); installedSearch_=new QLineEdit(installed); installedSearch_->setPlaceholderText("Yüklü modlarda ara"); installedSearch_->setStyleSheet(inputStyle()); auto* add=actionButton("＋ MOD EKLE","#2b2e35","#383b43",installed); auto* enableAll=actionButton("Tümünü Aç","#236d4d","#2c8b62",installed); auto* disableAll=actionButton("Tümünü Kapat","#34363d","#464951",installed); modControls->addWidget(installedSearch_,1); modControls->addWidget(add); modControls->addWidget(enableAll); modControls->addWidget(disableAll); installedV->addLayout(modControls);
        installedMods_=new QWidget(installed); installedModsLayout_=new QVBoxLayout(installedMods_); installedModsLayout_->setContentsMargins(0,0,0,0); installedModsLayout_->setSpacing(7); installedModsLayout_->addStretch(); auto* installedScroll=new QScrollArea(installed); installedScroll->setWidgetResizable(true); installedScroll->setFrameShape(QFrame::NoFrame); installedScroll->setWidget(installedMods_); installedV->addWidget(installedScroll,1);
        connect(add,&QPushButton::clicked,this,[this]{addLocalMods();}); connect(enableAll,&QPushButton::clicked,this,[this]{toggleAllMods(true);}); connect(disableAll,&QPushButton::clicked,this,[this]{toggleAllMods(false);}); connect(installedSearch_,&QLineEdit::textChanged,this,[this]{refreshInstalledMods();});
        auto* rustMods=new QWidget(); auto* rustV=new QVBoxLayout(rustMods); rustV->setContentsMargins(0,0,0,0); rustV->setSpacing(8);
        auto* rustTop=new QHBoxLayout(); horizonModsSearch_=new QLineEdit(rustMods); horizonModsSearch_->setPlaceholderText("Horizon modlarında ara"); horizonModsSearch_->setStyleSheet(inputStyle()); horizonModsSort_=new QComboBox(rustMods); horizonModsSort_->addItem("İsme göre (A-Z)","name_asc"); horizonModsSort_->addItem("İsme göre (Z-A)","name_desc"); horizonModsSort_->addItem(".HNMOD önce","kind"); horizonModsSort_->setStyleSheet(comboStyle()); horizonModsSort_->setMinimumWidth(135); horizonModsSort_->setToolTip("Mod listesinin sıralama biçimi"); horizonBuildModsButton_=actionButton("⚙  RUST MODLARINI DERLE","#653b2e","#804c39",rustMods); horizonBuildModsButton_->setMinimumWidth(190); auto* refreshRust=actionButton("↻  YENİLE","#2b2e35","#383b43",rustMods); rustTop->addWidget(horizonModsSearch_,1); rustTop->addWidget(horizonModsSort_); rustTop->addWidget(horizonBuildModsButton_); rustTop->addWidget(refreshRust); rustV->addLayout(rustTop);
        auto* rustNotice=card(rustMods); auto* rustNoticeV=new QVBoxLayout(rustNotice); rustNoticeV->setContentsMargins(12,9,12,9); rustNoticeV->addWidget(label(".HMOD: Horizon sandbox'ında çalışan içerik modu.  .HNMOD: tam kullanıcı izinleriyle çalışabilen yerel Rust kodudur. Titan imzalı paketlerde yayımlayıcıyı Minisign ile doğrular ve onayından sonra Rust istemcisini senin bilgisayarında derler. İlk derleme Rust/MinGW araçlarını ve bağımlılıkları indirerek birkaç GB alan ve uzun süre gerektirebilir.",10,"#e6c576",false,rustNotice)); rustV->addWidget(rustNotice);
        horizonModsStatus_=label("Resmî Horizon mod dizini henüz yüklenmedi.",10,kMuted,false,rustMods); horizonModsStatus_->setWordWrap(true); rustV->addWidget(horizonModsStatus_);
        horizonModsList_=new QWidget(rustMods); horizonModsListLayout_=new QVBoxLayout(horizonModsList_); horizonModsListLayout_->setContentsMargins(0,0,0,0); horizonModsListLayout_->setSpacing(7); horizonModsListLayout_->addStretch(); auto* rustScroll=new QScrollArea(rustMods); rustScroll->setWidgetResizable(true); rustScroll->setFrameShape(QFrame::NoFrame); rustScroll->setWidget(horizonModsList_); rustV->addWidget(rustScroll,1);
        connect(refreshRust,&QPushButton::clicked,this,[this]{refreshHorizonMods(true);}); connect(horizonModsSearch_,&QLineEdit::textChanged,this,[this]{renderHorizonMods();}); connect(horizonModsSort_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{renderHorizonMods();}); connect(horizonBuildModsButton_,&QPushButton::clicked,this,[this]{buildHorizonNativeMods();});
        modsSubpages_->addWidget(installed); modsSubpages_->addWidget(rustMods); modsSubpages_->addWidget(online); connect(modView_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){animateStackPage(modsSubpages_,i); if(i==0)refreshInstalledMods(); else if(i==1)refreshHorizonMods(); else if(i==2&&!marketplaceHasSearched_&&!marketplaceSearchInFlight_&&marketProvider_->currentText()=="Modrinth")searchMarketplace();});
        return page;
    }
    QWidget* buildSkinsPage() {
        auto* page=new QWidget(); page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bg,gTheme.text)); auto* root=new QVBoxLayout(page); root->setContentsMargins(20,18,20,18); root->setSpacing(12); root->addWidget(label("SKİN VE KARAKTER MERKEZİ",20,kGreen,true,page));
        auto* body=new QHBoxLayout(); auto* previewCard=card(page); previewCard->setMinimumWidth(220); auto* pv=new QVBoxLayout(previewCard); pv->setContentsMargins(15,15,15,15); pv->addWidget(label("SKİN ÖNİZLEME",11,kMuted,true,previewCard)); skinPreview_=label("👤",42,"#f4f5f8",true,previewCard); skinPreview_->setAlignment(Qt::AlignCenter); skinPreview_->setMinimumSize(180,210); skinPreview_->setStyleSheet("background:#15171c;border:1px solid #30333a;border-radius:10px;font-size:46px;"); pv->addWidget(skinPreview_,1); pv->addWidget(skinName_=label("Steve",12,"#d9dce2",true,previewCard)); body->addWidget(previewCard,1);
        auto* ops=new QVBoxLayout(); auto* fileCard=card(page); auto* fv=new QVBoxLayout(fileCard); fv->setContentsMargins(14,12,14,12); fv->addWidget(label("PNG DOSYASINDAN SEÇ",12,kGreen,true,fileCard)); auto* frow=new QHBoxLayout(); skinPath_=new QLineEdit(fileCard); skinPath_->setReadOnly(true); skinPath_->setStyleSheet(inputStyle()); skinPath_->setPlaceholderText("Skin dosyası seçilmedi"); auto* browse=actionButton("GÖZ AT","#2b2e35","#383b43",fileCard); frow->addWidget(skinPath_,1); frow->addWidget(browse); fv->addLayout(frow); ops->addWidget(fileCard);
        auto* urlCard=card(page); auto* uv=new QVBoxLayout(urlCard); uv->setContentsMargins(14,12,14,12); uv->addWidget(label("KULLANICI ADINDAN VEYA URL'DEN AL",12,kGreen,true,urlCard)); auto* urow=new QHBoxLayout(); skinQuery_=new QLineEdit(urlCard); skinQuery_->setPlaceholderText("Minecraft kullanıcı adı veya PNG URL"); skinQuery_->setStyleSheet(inputStyle()); auto* fetch=actionButton("SKİNİ AL","#2b2e35","#383b43",urlCard); urow->addWidget(skinQuery_,1); urow->addWidget(fetch); uv->addLayout(urow); ops->addWidget(urlCard);
        auto* modelRow=new QHBoxLayout(); modelRow->addWidget(label("Model",11,kMuted,true,page)); skinModel_=new AnimatedComboBox(page); skinModel_->addItems({"Klasik (Steve - 4px)","İnce (Alex - 3px)"}); skinModel_->setCurrentText(config_.value("skin_model").toString("Klasik (Steve - 4px)")); skinModel_->setStyleSheet(comboStyle()); modelRow->addWidget(skinModel_,1); ops->addLayout(modelRow);
        auto* apply=actionButton("💾  LAUNCHER'DA UYGULA","#236d4d","#2c8b62",page); auto* upload=actionButton("☁  MOJANG HESABIMA YÜKLE",kAccentBlue,"#4c91ff",page); ops->addWidget(apply); ops->addWidget(upload); ops->addStretch(); body->addLayout(ops,2); root->addLayout(body,1);
        connect(browse,&QPushButton::clicked,this,[this]{selectSkinFile();}); connect(fetch,&QPushButton::clicked,this,[this]{fetchSkin();}); connect(skinQuery_,&QLineEdit::returnPressed,this,[this]{fetchSkin();}); connect(apply,&QPushButton::clicked,this,[this]{applyOfflineSkin();}); connect(upload,&QPushButton::clicked,this,[this]{uploadSkin();}); connect(skinModel_,&QComboBox::currentTextChanged,this,[this]{saveConfig();}); return page;
    }


    QWidget* buildSettingsPage() {
        auto* page=new QWidget(); page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bg,gTheme.text)); auto* root=new QVBoxLayout(page); root->setContentsMargins(20,18,20,18); root->addWidget(label("AYARLAR VE PERFORMANS",20,kGreen,true,page)); auto* content=new QWidget(); auto* v=new QVBoxLayout(content); v->setContentsMargins(0,0,0,0); v->setSpacing(10);
        auto* memory=card(content); auto* mv=new QVBoxLayout(memory); mv->setContentsMargins(14,12,14,12); mv->addWidget(label("BELLEK VE JAVA",12,kGreen,true,memory)); ramValue_=label("Ayrılan RAM",11,kMuted,false,memory); ramValue_->setWordWrap(true); mv->addWidget(ramValue_); ramSlider_=new QSlider(Qt::Horizontal,memory); ramSlider_->setRange(1,maxGameRamGiB()); ramSlider_->setValue(qBound(1,config_.value("ram").toInt(4),maxGameRamGiB())); mv->addWidget(ramSlider_); auto* javaRow=new QHBoxLayout(); javaPath_=new QLineEdit(config_.value("custom_java").toString(),memory); javaPath_->setPlaceholderText("Java yolu (boşsa otomatik seçilir)"); javaPath_->setStyleSheet(inputStyle()); auto* javaBrowse=actionButton("GÖZ AT","#2b2e35","#383b43",memory); javaRow->addWidget(javaPath_,1); javaRow->addWidget(javaBrowse); mv->addLayout(javaRow); auto* jvmRow=new QHBoxLayout(); jvmArgs_=new QLineEdit(config_.value("jvm_arguments").toString(),memory); jvmArgs_->setPlaceholderText("Ek JVM argümanları"); jvmArgs_->setStyleSheet(inputStyle()); jvmRow->addWidget(label("JVM",10,kMuted,true,memory)); jvmRow->addWidget(jvmArgs_,1); mv->addLayout(jvmRow); v->addWidget(memory);
        auto* perf=card(content); auto* pv=new QGridLayout(perf); pv->setContentsMargins(14,12,14,12); pv->setHorizontalSpacing(10); pv->setVerticalSpacing(8); pv->addWidget(label("OYUN ÇALIŞTIRMA",12,kGreen,true,perf),0,0,1,2); pv->addWidget(label("Çöp toplayıcı",11,kMuted,false,perf),1,0); gcMode_=new AnimatedComboBox(perf); gcMode_->addItems({"G1GC (Aikar - Önerilen / Düşük-Orta RAM)","G1GC (Varsayılan)","ZGC","Shenandoah"}); gcMode_->setCurrentText(config_.value("gc_mode").toString()); gcMode_->setStyleSheet(comboStyle()); pv->addWidget(gcMode_,1,1); highPriority_=new QCheckBox("Yüksek işlem önceliği",perf); highPriority_->setChecked(config_.value("high_priority").toBool(true)); forceGpu_=new QCheckBox("Yüksek performanslı GPU tercihi",perf); forceGpu_->setChecked(config_.value("force_gpu").toBool(true)); pv->addWidget(highPriority_,2,0); pv->addWidget(forceGpu_,2,1); pv->addWidget(label("Launcher davranışı",11,kMuted,false,perf),3,0); closeBehavior_=new AnimatedComboBox(perf); closeBehavior_->addItems({"Gizle (Oyun Bitince Aç)","Açık Tut","Kapat"}); closeBehavior_->setCurrentText(config_.value("close_behavior").toString()); closeBehavior_->setStyleSheet(comboStyle()); pv->addWidget(closeBehavior_,3,1); auto* res=new QHBoxLayout(); resolutionW_=new QLineEdit(config_.value("res_w").toString(),perf); resolutionH_=new QLineEdit(config_.value("res_h").toString(),perf); resolutionW_->setPlaceholderText("Genişlik"); resolutionH_->setPlaceholderText("Yükseklik"); resolutionW_->setStyleSheet(inputStyle()); resolutionH_->setStyleSheet(inputStyle()); res->addWidget(resolutionW_); res->addWidget(label("×",11,kMuted,false,perf)); res->addWidget(resolutionH_); pv->addLayout(res,4,0,1,2); onlyInstalled_=new QCheckBox("Yalnızca kurulu sürümleri göster",perf); onlyInstalled_->setChecked(config_.value("only_installed").toBool()); showSnapshots_=new QCheckBox("Snapshot sürümlerini göster",perf); showSnapshots_->setChecked(config_.value("show_snapshots").toBool()); pv->addWidget(onlyInstalled_,5,0); pv->addWidget(showSnapshots_,5,1); v->addWidget(perf);
        auto* api=card(content); auto* av=new QVBoxLayout(api); av->setContentsMargins(14,12,14,12); av->addWidget(label("CURSEFORGE API",12,kGreen,true,api)); curseKey_=new QLineEdit(config_.value("curseforge_api_key").toString(),api); curseKey_->setEchoMode(QLineEdit::Password); curseKey_->setPlaceholderText("Kendi CurseForge API anahtarın"); curseKey_->setStyleSheet(inputStyle()); av->addWidget(curseKey_); av->addWidget(label("Modrinth kullanımı için API anahtarı gerekmez. CurseForge API anahtarını CurseForge hesabından alıp buraya ekle.",10,kMuted,false,api)); v->addWidget(api);
        auto* access=card(content); auto* qv=new QVBoxLayout(access); qv->setContentsMargins(14,12,14,12); qv->addWidget(label("HIZLI ERİŞİM",12,kGreen,true,access));
auto* folderRow=new QHBoxLayout(); auto* gameFolder=actionButton(".minecraft","#2b2e35","#383b43",access); auto* modsFolder=actionButton("Mods","#2b2e35","#383b43",access); auto* skinsFolder=actionButton("Skinler","#2b2e35","#383b43",access); folderRow->addWidget(gameFolder);folderRow->addWidget(modsFolder);folderRow->addWidget(skinsFolder);qv->addLayout(folderRow);
connect(gameFolder,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(engine_.minecraftDirectory()));});connect(modsFolder,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(engine_.modsDirectory(engine_.activeGameDirectory(config_))));});connect(skinsFolder,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(engine_.skinsDirectory()));});
auto* clientRow=new QHBoxLayout();clientRow->addWidget(label("Microsoft istemci kimliği",10,kMuted,false,access));customClientId_=new QLineEdit(config_.value("custom_ms_client_id").toString(),access);customClientId_->setPlaceholderText("Varsayılan kimliği kullan");customClientId_->setStyleSheet(inputStyle());clientRow->addWidget(customClientId_,1);qv->addLayout(clientRow);v->addWidget(access);

        auto* save=actionButton("AYARLARI KAYDET","#236d4d","#2c8b62",content); v->addWidget(save); auto* sc=new QScrollArea(page); sc->setWidgetResizable(true); sc->setFrameShape(QFrame::NoFrame); sc->setWidget(content); root->addWidget(sc,1);
        connect(save,&QPushButton::clicked,this,[this]{saveConfig();setStatus("Ayarlar kaydedildi",100);}); connect(closeBehavior_,&QComboBox::currentTextChanged,this,[this]{saveConfig();}); connect(ramSlider_,&QSlider::valueChanged,this,[this](int n){ramValue_->setText(ramDescription(n));}); connect(javaBrowse,&QPushButton::clicked,this,[this]{const QString path=QFileDialog::getOpenFileName(this,"Java çalıştırılabilir dosyasını seç","","Java (java.exe javaw.exe)");if(!path.isEmpty())javaPath_->setText(path);}); connect(onlyInstalled_,&QCheckBox::toggled,this,[this]{loadVersions();}); connect(showSnapshots_,&QCheckBox::toggled,this,[this]{loadVersions();}); connect(curseKey_,&QLineEdit::editingFinished,this,[this]{saveConfig();}); connect(customClientId_,&QLineEdit::editingFinished,this,[this]{saveConfig();}); ramValue_->setText(ramDescription(ramSlider_->value())); return page;
    }
    QWidget* buildLogsPage() {
        auto* page=new QWidget(); page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bg,gTheme.text)); auto* root=new QVBoxLayout(page); root->setContentsMargins(20,18,20,18); auto* row=new QHBoxLayout(); row->addWidget(label("LOG GÖRÜNTÜLEYİCİ",20,kGreen,true,page)); row->addStretch(); logKind_=new AnimatedComboBox(page); logKind_->addItems({"Launcher logu","Minecraft process logu"}); logKind_->setStyleSheet(comboStyle()); auto* refresh=actionButton("YENİLE","#2b2e35","#383b43",page); row->addWidget(logKind_); row->addWidget(refresh); root->addLayout(row); logViewer_=new QTextEdit(page); logViewer_->setReadOnly(true); logViewer_->setStyleSheet("QTextEdit{background:#111216;color:#d5d8df;border:1px solid #2b2d33;border-radius:9px;padding:10px;font-family:Consolas;}"); root->addWidget(logViewer_,1); connect(refresh,&QPushButton::clicked,this,[this]{refreshLogs();}); connect(logKind_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshLogs();}); return page;
    }
    QWidget* buildScreenshotsPage() {
        auto* page=new QWidget();page->setStyleSheet(QString("background:%1;color:%2;").arg(gTheme.bg,gTheme.text));auto* root=new QVBoxLayout(page);root->setContentsMargins(20,18,20,18);root->setSpacing(12);
        auto* row=new QHBoxLayout();row->addWidget(label("EKRAN GÖRÜNTÜLERİ",20,kGreen,true,page));row->addStretch();screenshotBox_=new AnimatedComboBox(page);screenshotBox_->setMinimumWidth(260);screenshotBox_->setStyleSheet(comboStyle());row->addWidget(screenshotBox_);auto* refresh=actionButton("YENİLE","#2b2e35","#383b43",page);auto* folder=actionButton("KLASÖRÜ AÇ","#2b2e35","#383b43",page);row->addWidget(refresh);row->addWidget(folder);root->addLayout(row);
        screenshotPreview_=new QLabel("Bu profilde henüz ekran görüntüsü yok.",page);screenshotPreview_->setAlignment(Qt::AlignCenter);screenshotPreview_->setMinimumSize(600,360);screenshotPreview_->setStyleSheet("background:#101216;border:1px solid #30343b;border-radius:12px;color:#969ba6;");root->addWidget(screenshotPreview_,1);
        connect(refresh,&QPushButton::clicked,this,[this]{refreshScreenshots();});connect(folder,&QPushButton::clicked,this,[this]{openScreenshotsFolder();});connect(screenshotBox_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int){showSelectedScreenshot();});
        return page;
    }
    QString screenshotsDirectory() const { return QDir(engine_.activeGameDirectory(config_)).filePath("screenshots"); }
    void refreshScreenshots() {
        if(!screenshotBox_)return;const QString selected=screenshotBox_->currentData().toString();QDir dir(screenshotsDirectory());
        const QFileInfoList files=dir.entryInfoList({"*.png","*.jpg","*.jpeg"},QDir::Files,QDir::Time);
        const QSignalBlocker blocker(screenshotBox_);screenshotBox_->clear();
        for(const QFileInfo& file:files)screenshotBox_->addItem(file.fileName(),file.absoluteFilePath());
        const int index=screenshotBox_->findData(selected);screenshotBox_->setCurrentIndex(index<0?(files.isEmpty()?-1:0):index);
        screenshotBox_->setEnabled(!files.isEmpty());showSelectedScreenshot();
    }
    void showSelectedScreenshot() {
        if(!screenshotPreview_||!screenshotBox_)return;const QString path=screenshotBox_->currentData().toString();
        if(path.isEmpty()){screenshotPreview_->setPixmap({});screenshotPreview_->setText("Bu profilde henüz ekran görüntüsü yok. Minecraft'ta F2 ile görüntü al.");return;}
        QPixmap image(path);if(image.isNull()){screenshotPreview_->setPixmap({});screenshotPreview_->setText("Görsel açılamadı: "+QFileInfo(path).fileName());return;}
        screenshotPreview_->setText(QString());screenshotPreview_->setPixmap(image.scaled(screenshotPreview_->size()-QSize(32,32),Qt::KeepAspectRatio,Qt::SmoothTransformation));
    }
    void openScreenshotsFolder() {const QString path=screenshotsDirectory();QDir().mkpath(path);QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
        double windowsVisibleRamGiB() const {
#ifdef Q_OS_WIN
        MEMORYSTATUSEX state{}; state.dwLength=sizeof(state); if(GlobalMemoryStatusEx(&state)) return double(state.ullTotalPhys)/(1024.0*1024.0*1024.0);
#endif
        return 8.0;
    }
    double currentlyAvailableRamGiB() const {
#ifdef Q_OS_WIN
        MEMORYSTATUSEX state{}; state.dwLength=sizeof(state); if(GlobalMemoryStatusEx(&state)) return double(state.ullAvailPhys)/(1024.0*1024.0*1024.0);
#endif
        return 0.0;
    }
    int maxGameRamGiB() const {
        return qBound(1, int(std::floor(windowsVisibleRamGiB()-2.0)), 64);
    }
    QString ramDescription(int allocated) const {
        const QLocale locale;
        const QString visible=locale.toString(windowsVisibleRamGiB(),'f',1);
        const double availableGiB=currentlyAvailableRamGiB();
        const QString available=availableGiB>0.0?locale.toString(availableGiB,'f',1):QString();
#ifdef Q_OS_WIN
        ULONGLONG installedKb=0;
        if(GetPhysicallyInstalledSystemMemory(&installedKb)) {
            const QString installed=locale.toString(double(installedKb)/(1024.0*1024.0),'f',1);
            QString result=QString("Minecraft'e ayrılan: %1 GiB  •  Windows'un gördüğü toplam: %2 GiB  •  Kurulu: %3 GiB")
                .arg(allocated).arg(visible).arg(installed);
            if(!available.isEmpty())result+=QString("  •  Şu an kullanılabilir: %1 GiB").arg(available);
            result+=QString("\nOyuna en fazla %1 GiB ayır; yaklaşık 2 GiB'yi Windows için bırak.").arg(maxGameRamGiB());
            return result;
        }
#endif
        QString result=QString("Minecraft'e ayrılan: %1 GiB  •  Windows'un gördüğü toplam: %2 GiB").arg(allocated).arg(visible);
        if(!available.isEmpty())result+=QString("  •  Şu an kullanılabilir: %1 GiB").arg(available);
        result+=QString("\nKurulu RAM değeri Windows'tan okunamadı; Görev Yöneticisi > Performans > Bellek'te kurulu ve donanıma ayrılmış miktarı kontrol edebilirsin. Oyuna en fazla %1 GiB ayır; yaklaşık 2 GiB'yi Windows için bırak.").arg(maxGameRamGiB());
        return result;
    }
    void refreshProfileSelector() {
        if(!profileBox_) return; profiles_=engine_.readProfiles(); const QString selected=config_.value("active_instance").toString("default"); profileBox_->blockSignals(true); profileBox_->clear();
        for(const auto& v:profiles_){const auto p=v.toObject();const QString name=p.value("name").toString(p.value("id").toString());const QString pid=p.value("id").toString();if(p.value("kind").toString()=="horizon-rust")profileBox_->addItem(QIcon(":/app.ico"),name,pid);else profileBox_->addItem(name,pid);}
        int idx=profileBox_->findData(selected); profileBox_->setCurrentIndex(idx<0?0:idx); profileBox_->blockSignals(false);
        // onProfileChanged'i sadece görsel güncelleme için çağır, kaydetme
        if(!profileBox_ || profileBox_->currentIndex()<0) return;
        const QJsonObject profile=engine_.activeProfile(config_);
        const QSignalBlocker bv(versionBox_); const QSignalBlocker bl(loaderBox_); const QSignalBlocker br(ramSlider_);
        const QString kind=profile.value("kind").toString();
        if(kind=="default"||kind=="horizon-rust"){if(versionBox_)versionBox_->setCurrentText(config_.value("version").toString());if(loaderBox_)loaderBox_->setCurrentText(config_.value("loader").toString("Vanilla"));}
        else{if(versionBox_)versionBox_->setCurrentText(profile.value("minecraft_version").toString());if(loaderBox_)loaderBox_->setCurrentText(profile.value("loader").toString("Vanilla"));}
        const int ram=profile.contains("ram")?profile.value("ram").toInt(4):config_.value("ram").toInt(4);
        if(ramSlider_)ramSlider_->setValue(qBound(1,ram,maxGameRamGiB()));
        if(javaPath_){const QSignalBlocker bj(javaPath_);javaPath_->setText(profile.contains("custom_java")?profile.value("custom_java").toString():config_.value("custom_java").toString());}
        if(jvmArgs_){const QSignalBlocker bjv(jvmArgs_);jvmArgs_->setText(profile.contains("jvm_arguments")?profile.value("jvm_arguments").toString():config_.value("jvm_arguments").toString());}
        if(horizonMinecraftPath_){const QSignalBlocker bh(horizonMinecraftPath_);horizonMinecraftPath_->setText(config_.value("horizon_minecraft_path").toString(engine_.minecraftDirectory()));}
        if(skinPreview_)refreshSkinPreview();
        if(pages_&&pages_->currentIndex()==1&&modsSubpages_&&modView_&&modView_->currentIndex()==0)refreshInstalledMods();
        if(pages_&&pages_->currentIndex()==6)refreshScreenshots();
        if(worldTarget_)refreshWorldSelector();
    }
    void onProfileChanged() {
        if(!profileBox_ || profileBox_->currentIndex()<0) return;
        const QString id=profileBox_->currentData().toString();
        const QString oldId=config_.value("active_instance").toString("default");
        if (oldId != id) persistProfileSettings(oldId);
        config_.insert("active_instance",id);
        // Doğrudan engine'e yaz — UI state'i okumaya gerek kalmadan
        QString cfgErr;
        engine_.saveConfig(config_, &cfgErr);
        engine_.appendLauncherLog("Profil degistirildi: " + id);
        const QJsonObject profile=engine_.activeProfile(config_);
        const QSignalBlocker blockVersion(versionBox_);
        const QSignalBlocker blockLoader(loaderBox_);
        const QSignalBlocker blockRam(ramSlider_);
        const QString kind=profile.value("kind").toString();
        const bool rustProfile=kind=="horizon-rust";
        const bool managed=kind!="default"&&!rustProfile;
        if(rustProfile){
            loaderBox_->setCurrentText("Vanilla"); loaderBox_->setEnabled(false);
            if(versionBox_->findText("26.3")<0)versionBox_->insertItem(0,"26.3");
            versionBox_->setCurrentText("26.3"); versionBox_->setEnabled(false);
            if (ramSlider_) ramSlider_->setValue(qBound(1, config_.value("ram").toInt(4), maxGameRamGiB()));
            if (javaPath_) javaPath_->setText(config_.value("custom_java").toString());
            if (jvmArgs_) jvmArgs_->setText(config_.value("jvm_arguments").toString());
        } else if(managed){
            loaderBox_->setCurrentText(profile.value("loader").toString("Vanilla"));
            loaderBox_->setEnabled(false);
            versionBox_->setCurrentText(profile.value("minecraft_version").toString());
            versionBox_->setEnabled(false);
            if (ramSlider_) ramSlider_->setValue(qBound(1, profile.value("ram").toInt(config_.value("ram").toInt(4)), maxGameRamGiB()));
            if (javaPath_) javaPath_->setText(profile.value("custom_java").toString());
            if (jvmArgs_) jvmArgs_->setText(profile.value("jvm_arguments").toString());
        } else {
            loaderBox_->setEnabled(true); versionBox_->setEnabled(true);
            if(!config_.value("version").toString().isEmpty())versionBox_->setCurrentText(config_.value("version").toString());
            loaderBox_->setCurrentText(config_.value("loader").toString("Vanilla"));
            if (ramSlider_) ramSlider_->setValue(qBound(1, config_.value("ram").toInt(4), maxGameRamGiB()));
            if (javaPath_) javaPath_->setText(config_.value("custom_java").toString());
            if (jvmArgs_) jvmArgs_->setText(config_.value("jvm_arguments").toString());
        }
        if(playButton_)playButton_->setText(rustProfile?"▶  RUST 26.3’Yİ OYNA":"▶  OYNA");
        saveConfig(); if(worldTarget_)refreshWorldSelector(); if(pages_ && pages_->currentIndex()==1 && modView_ && modView_->currentIndex()==0)refreshInstalledMods(); if(pages_&&pages_->currentIndex()==6)refreshScreenshots();
    }
    void persistProfileSettings(const QString& id) {
        if (id.isEmpty() || id == "default" || id == "titan-rust-26.3" || !ramSlider_ || !javaPath_ || !jvmArgs_) return;
        for (const auto& value : engine_.readProfiles()) {
            QJsonObject profile=value.toObject();
            if (profile.value("id").toString()!=id) continue;
            profile.insert("ram",ramSlider_->value());
            profile.insert("custom_java",javaPath_->text().trimmed());
            profile.insert("jvm_arguments",jvmArgs_->text().trimmed());
            QString error; if(!engine_.saveProfile(profile,&error)) setStatus("Profil ayarı kaydedilemedi: "+error,0);
            return;
        }
    }
    void showProfileManager(QWidget* anchor) {
        const QJsonObject profile=engine_.activeProfile(config_);
        const QString id=profile.value("id").toString("default");
        const bool isDefault=id=="default";
        const bool isManagedInstance=!isDefault&&profile.value("kind").toString()!="horizon-rust";
        QMenu menu(this);
        QAction* create=menu.addAction("Yeni oyun profili oluştur…");
        QAction* import=menu.addAction("Modpack içe aktar…");
        menu.addSeparator();
        QAction* rename=menu.addAction("Profili yeniden adlandır…");
        QAction* duplicate=menu.addAction("Profili kopyala…");
        QAction* exportAction=menu.addAction("Profili ZIP olarak dışa aktar…");
        QAction* open=menu.addAction("Profil klasörünü aç");
        QAction* remove=menu.addAction("Profili sil…");
        rename->setEnabled(isManagedInstance); duplicate->setEnabled(isManagedInstance); remove->setEnabled(isManagedInstance); exportAction->setEnabled(isManagedInstance);
        QAction* selected=menu.exec(anchor->mapToGlobal(QPoint(0,anchor->height())));
        if(selected==create) createInstanceDialog();
        else if(selected==import) importModpackFile();
        else if(selected==rename) renameActiveProfile();
        else if(selected==duplicate) cloneActiveProfile();
        else if(selected==exportAction) exportActiveProfile();
        else if(selected==open) QDesktopServices::openUrl(QUrl::fromLocalFile(profile.value("directory").toString(engine_.minecraftDirectory())));
        else if(selected==remove) deleteActiveProfile();
    }
    void createInstanceDialog() {
        if (versionBox_->count()==0) { QMessageBox::information(this,"Minecraft sürümleri","Sürüm listesi yüklenene kadar bekle, sonra tekrar aç."); return; }
        QDialog dialog(this); dialog.setWindowTitle("Yeni oyun profili"); dialog.setStyleSheet("QDialog{background:#17191e;color:#eef0f4;}");
        auto* layout=new QVBoxLayout(&dialog); auto* form=new QFormLayout();
        auto* name=new QLineEdit("Yeni Profil",&dialog); name->setStyleSheet(inputStyle());
        auto* version=new QComboBox(&dialog); version->addItems([&]{QStringList values;for(int i=0;i<versionBox_->count();++i)values<<versionBox_->itemText(i);return values;}()); version->setCurrentText(versionBox_->currentText()); version->setStyleSheet(comboStyle());
        auto* loader=new QComboBox(&dialog); loader->addItems({"Vanilla","Fabric","Forge","NeoForge","Quilt"}); loader->setCurrentText(loaderBox_->currentText()); loader->setStyleSheet(comboStyle());
        form->addRow("Profil adı",name); form->addRow("Minecraft sürümü",version); form->addRow("Mod loader",loader); layout->addLayout(form);
        auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog); buttons->button(QDialogButtonBox::Ok)->setText("Oluştur"); buttons->button(QDialogButtonBox::Cancel)->setText("İptal"); layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted) return;
        const QString profileName=name->text().trimmed(), mcVersion=version->currentText(), selectedLoader=loader->currentText();
        if(profileName.isEmpty()||mcVersion.isEmpty()) { QMessageBox::warning(this,"Profil","Profil adı ve sürüm gerekli."); return; }
        setStatus("Oyun profili hazırlanıyor…",5);
        const QString java=config_.value("custom_java").toString();
        runTask([this,profileName,mcVersion,selectedLoader,java]{TaskResult r;const QString launchId=engine_.installVersion(mcVersion,selectedLoader,{},java,progressCallback());QString error;r.object=engine_.createInstance(profileName,mcVersion,selectedLoader,{},launchId,&error);if(r.object.isEmpty())throw std::runtime_error(error.toStdString());r.ok=true;return r;},[this](TaskResult r){if(!r.ok){QMessageBox::critical(this,"Profil oluşturulamadı",r.error);setStatus("Profil oluşturulamadı",0);return;}config_.insert("active_instance",r.object.value("id"));refreshProfileSelector();setStatus("Yeni oyun profili hazır",100);});
    }
    void renameActiveProfile() {
        const QJsonObject profile=engine_.activeProfile(config_); bool ok=false;
        const QString name=QInputDialog::getText(this,"Profili yeniden adlandır","Yeni profil adı:",QLineEdit::Normal,profile.value("name").toString(),&ok).trimmed();
        if(!ok||name.isEmpty()) return;
        QString error;if(!engine_.renameProfile(profile.value("id").toString(),name,&error)) QMessageBox::warning(this,"Profil",error); else refreshProfileSelector();
    }
    void cloneActiveProfile() {
        const QJsonObject profile=engine_.activeProfile(config_); bool ok=false;
        const QString defaultName=profile.value("name").toString()+" (Kopya)";
        const QString name=QInputDialog::getText(this,"Profili kopyala","Kopya adı:",QLineEdit::Normal,defaultName,&ok).trimmed();
        if(!ok||name.isEmpty()) return;
        QString error;const QJsonObject copy=engine_.cloneProfile(profile.value("id").toString(),name,&error);
        if(copy.isEmpty()){QMessageBox::warning(this,"Profil kopyalanamadı",error);return;}
        config_.insert("active_instance",copy.value("id"));refreshProfileSelector();setStatus("Profil kopyalandı",100);
    }
    void deleteActiveProfile() {
        const QJsonObject profile=engine_.activeProfile(config_); const QString id=profile.value("id").toString();
        if(QMessageBox::question(this,"Profili sil",QString("%1 profili ve içindeki oyun dosyaları kalıcı olarak silinsin mi?").arg(profile.value("name").toString()))!=QMessageBox::Yes)return;
        QString error;if(!engine_.deleteProfile(id,&error)){QMessageBox::warning(this,"Profil silinemedi",error);return;}
        config_.insert("active_instance","default");refreshProfileSelector();setStatus("Profil silindi",100);
    }
    void exportActiveProfile() {
        const QJsonObject profile=engine_.activeProfile(config_);
        const QString name=profile.value("name").toString("Minecraft").replace(QRegularExpression("[^A-Za-z0-9 _-]"),"_");
        const QString path=QFileDialog::getSaveFileName(this,"Profili ZIP olarak dışa aktar",name+".zip","ZIP arşivi (*.zip)");
        if(path.isEmpty())return;
        setStatus("Profil arşivleniyor…",5);
        runTask([this,id=profile.value("id").toString(),path]{TaskResult r;QString error;if(!engine_.exportProfile(id,path,&error))throw std::runtime_error(error.toStdString());r.text=path;r.ok=true;return r;},[this](TaskResult r){if(!r.ok){QMessageBox::critical(this,"Dışa aktarma başarısız",r.error);setStatus("Dışa aktarma başarısız",0);}else{QMessageBox::information(this,"Profil dışa aktarıldı",r.text);setStatus("Profil ZIP olarak kaydedildi",100);}});
    }
    void importModpackFile() {
        const QString path=QFileDialog::getOpenFileName(this,"Modpack içe aktar","","Modpack arşivleri (*.mrpack *.zip)");
        if(path.isEmpty())return;
        const QString key=config_.value("curseforge_api_key").toString(), java=config_.value("custom_java").toString();
        setStatus("Modpack içe aktarılıyor…",5);
        runTask([this,path,key,java]{TaskResult r;r.object=engine_.importModpack(path,key,java,progressCallback());r.ok=!r.object.isEmpty();return r;},[this](TaskResult r){if(!r.ok){QMessageBox::critical(this,"Modpack içe aktarılamadı",r.error);setStatus("Modpack içe aktarılamadı",0);return;}config_.insert("active_instance",r.object.value("id"));refreshProfileSelector();QMessageBox::information(this,"Modpack kuruldu",r.object.value("name").toString()+" profili içe aktarıldı.");setStatus("Modpack profili hazır",100);});
    }
    void updateAccountCard() {
        if(!accountName_) return; const QString name=playerName(config_); accountName_->setText(name); accountMode_->setText(config_.value("account_type").toString()=="microsoft"?"Microsoft hesabı":"Çevrimdışı hesap");
    }
    void loadVersions() {
        if(!versionBox_) return;
        const bool localOnly=onlyInstalled_?onlyInstalled_->isChecked():config_.value("only_installed").toBool(); const bool snapshots=showSnapshots_?showSnapshots_->isChecked():config_.value("show_snapshots").toBool();
        setStatus("Minecraft sürümleri taranıyor…",8);
        runTask([this,localOnly,snapshots]{TaskResult r; QJsonArray versions=engine_.localVersions(); if(!localOnly){QJsonArray remote=engine_.remoteVersions(snapshots,progressCallback()); for(const auto& x:remote)if(!versions.contains(x))versions.append(x);} r.array=versions;r.ok=true;return r;},[this](TaskResult r){if(!r.ok){setStatus("Sürüm listesi alınamadı: "+r.error,0);return;} QStringList versions;for(const auto& v:r.array)versions<<v.toString();std::sort(versions.begin(),versions.end(),versionNewer);const QString saved=config_.value("version").toString();versionBox_->blockSignals(true);versionBox_->clear();versionBox_->addItems(versions);if(!saved.isEmpty()&&versionBox_->findText(saved)<0)versionBox_->insertItem(0,saved);if(!saved.isEmpty())versionBox_->setCurrentText(saved);else if(!versions.isEmpty())versionBox_->setCurrentIndex(0);versionBox_->blockSignals(false);if(marketVersion_){marketVersion_->clear();marketVersion_->addItem("Tümü");marketVersion_->addItems(versions);}setStatus(versions.isEmpty()?"Kurulu sürüm bulunamadı.":"Hazır",100);saveConfig();});
    }
    void searchMarketplace() {
        if(marketplaceSearchInFlight_)return;
        const QString provider=marketProvider_->currentText(), type=marketType_->currentData().toString(), query=marketSearch_->text().trimmed();
        const QString version=marketVersion_->currentText(), loader=marketLoader_->currentText(), key=config_.value("curseforge_api_key").toString();
        if(type=="datapack" && worldTarget_->currentData().toString().isEmpty()) refreshWorldSelector();
        marketplaceSearchInFlight_=true; marketplaceHasSearched_=false; marketplaceResultsData_={};
        clearLayout(marketResultsLayout_); marketResultsLayout_->addWidget(label("Arama yapılıyor…",12,kMuted,false,marketResults_)); setStatus("Mod ve paketler aranıyor…",15);
        runTask([this,provider,type,query,version,loader,key]{TaskResult r;r.array=engine_.searchMarketplace(provider,type,query,version,loader,key);r.ok=true;return r;},[this](TaskResult r){marketplaceSearchInFlight_=false;if(!r.ok){clearLayout(marketResultsLayout_);marketResultsLayout_->addWidget(label(r.error,12,"#f38ba8",false,marketResults_));marketResultsLayout_->addStretch();setStatus("Arama başarısız",0);return;}marketplaceResultsData_=r.array;marketplaceHasSearched_=true;renderMarketplaceResults();setStatus(QString("%1 sonuç bulundu").arg(r.array.size()),100);});
    }
    void renderMarketplaceResults() {
        if(!marketResultsLayout_||!marketResults_)return;
        clearLayout(marketResultsLayout_);
        if(marketplaceResultsData_.isEmpty()){
            marketResultsLayout_->addWidget(label("Sonuç bulunamadı.",12,kMuted,false,marketResults_));
            marketResultsLayout_->addStretch();
            return;
        }
        QVector<QJsonObject> hits;
        hits.reserve(marketplaceResultsData_.size());
        for(const QJsonValue& value:marketplaceResultsData_)if(value.isObject())hits.append(value.toObject());
        const QString sortMode=marketplaceSort_?marketplaceSort_->currentData().toString():QStringLiteral("downloads");
        std::stable_sort(hits.begin(),hits.end(),[sortMode](const QJsonObject& left,const QJsonObject& right){
            const QString leftName=left.value("title").toString();
            const QString rightName=right.value("title").toString();
            const int nameOrder=QString::localeAwareCompare(leftName,rightName);
            if(sortMode=="name_asc")return nameOrder<0;
            if(sortMode=="name_desc")return nameOrder>0;
            const qint64 leftDownloads=left.value("downloads").toVariant().toLongLong();
            const qint64 rightDownloads=right.value("downloads").toVariant().toLongLong();
            if(leftDownloads!=rightDownloads)return leftDownloads>rightDownloads;
            return nameOrder<0;
        });
        for(const QJsonObject& hit:hits)appendMarketplaceResult(hit);
        marketResultsLayout_->addStretch();
    }
    bool trustedMediaHost(const QString& rawHost, const QString& provider) const {
        const QString host=rawHost.toLower();
        if(provider.compare("Modrinth",Qt::CaseInsensitive)==0) return host=="cdn.modrinth.com";
        if(provider.compare("CurseForge",Qt::CaseInsensitive)==0) return host=="forgecdn.net"||host.endsWith(".forgecdn.net");
        return false;
    }
    void loadMarketplaceImage(QLabel* target,const QString& source,const QString& provider) {
        if(!target||!mediaNetwork_)return;
        const QUrl url(source);
        if(!url.isValid()||url.scheme().compare("https",Qt::CaseInsensitive)!=0||!trustedMediaHost(url.host(),provider))return;
        QNetworkRequest request(url);request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);request.setRawHeader("User-Agent","TuketiciLauncher/1.0 (Qt C++)");
        QNetworkReply* reply=mediaNetwork_->get(request);QPointer<QLabel> safeTarget(target);
        constexpr qint64 maxImageBytes=8*1024*1024;
        connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 received,qint64){if(received>maxImageBytes)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[this,reply,safeTarget,provider]{
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();const QUrl finalUrl=reply->url();const QByteArray bytes=reply->readAll();
            if(safeTarget&&reply->error()==QNetworkReply::NoError&&status>=200&&status<300&&finalUrl.scheme().compare("https",Qt::CaseInsensitive)==0&&trustedMediaHost(finalUrl.host(),provider)){
                QImage image;if(image.loadFromData(bytes))safeTarget->setPixmap(QPixmap::fromImage(image).scaled(safeTarget->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
            }
            reply->deleteLater();
        });
    }
    void loadHorizonModIcon(QLabel* target,const QJsonObject& project) {
        if(!target||!mediaNetwork_)return;
        const QString source=project.value("icon").toString().trimmed();
        if(source.isEmpty())return;
        QUrl url(source);
        if(url.isRelative())url=QUrl("https://horizon-rust.github.io/horizon-mods/").resolved(url);
        if(!url.isValid()||url.scheme().compare("https",Qt::CaseInsensitive)!=0||url.host().isEmpty()||!url.userInfo().isEmpty())return;
        QNetworkRequest request(url);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setRawHeader("User-Agent","TitanLauncherCpp/1.0");
        QNetworkReply* reply=mediaNetwork_->get(request);
        QPointer<QLabel> safeTarget(target);
        constexpr qint64 maxIconBytes=256*1024;
        connect(reply,&QNetworkReply::downloadProgress,reply,[reply](qint64 received,qint64){if(received>maxIconBytes)reply->abort();});
        connect(reply,&QNetworkReply::finished,this,[reply,safeTarget]{
            const int status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QUrl finalUrl=reply->url();
            const QByteArray bytes=reply->readAll();
            if(safeTarget&&reply->error()==QNetworkReply::NoError&&status>=200&&status<300&&bytes.size()<=maxIconBytes&&finalUrl.scheme().compare("https",Qt::CaseInsensitive)==0){
                QBuffer buffer;
                buffer.setData(bytes);
                if(buffer.open(QIODevice::ReadOnly)){
                    QImageReader reader(&buffer);
                    reader.setDecideFormatFromContent(true);
                    const QSize dimensions=reader.size();
                    if(dimensions.isValid()&&dimensions.width()<=4096&&dimensions.height()<=4096){
                        reader.setScaledSize(dimensions.scaled(QSize(128,128),Qt::KeepAspectRatio));
                        const QImage image=reader.read();
                        if(!image.isNull())safeTarget->setPixmap(QPixmap::fromImage(image).scaled(safeTarget->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
                    }
                }
            }
            reply->deleteLater();
        });
    }
    void showMarketplaceMedia(const QJsonObject& hit,QPushButton* sourceButton) {
        QPointer<QPushButton> safeButton(sourceButton);if(safeButton){safeButton->setEnabled(false);safeButton->setText("YÜKLENİYOR…");}
        const QString key=config_.value("curseforge_api_key").toString();
        runTask([this,hit,key]{TaskResult r;r.object=engine_.marketplaceMedia(hit,key);r.ok=true;return r;},[this,hit,safeButton](TaskResult result){
            if(safeButton){safeButton->setEnabled(true);safeButton->setText("GÖRSELLER");}
            if(!result.ok){QMessageBox::warning(this,"Medya önizlemesi",result.error);return;}
            const QString provider=hit.value("provider").toString();QStringList imageUrls,imageTitles,videoUrls;
            for(const QJsonValue& value:result.object.value("images").toArray()){const QJsonObject image=value.toObject();imageUrls<<image.value("url").toString();imageTitles<<image.value("title").toString();}
            for(const QJsonValue& value:result.object.value("videos").toArray())if(value.isString())videoUrls<<value.toString();
            QDialog dialog(this);dialog.setWindowTitle(hit.value("title").toString("İçerik medyası"));dialog.setMinimumSize(790,560);dialog.setStyleSheet("QDialog{background:#17191e;color:#eef0f4;} QLabel{color:#e5e7ed;}");
            auto* root=new QVBoxLayout(&dialog);root->setContentsMargins(18,16,18,16);root->setSpacing(10);
            auto* heading=label(hit.value("title").toString("İçerik medyası"),17,kGreen,true,&dialog);root->addWidget(heading);
            auto* preview=new QLabel(&dialog);preview->setAlignment(Qt::AlignCenter);preview->setMinimumSize(740,390);preview->setStyleSheet("background:#101216;border:1px solid #30343b;border-radius:12px;color:#969ba6;");preview->setText("Bu proje için görsel yok.");root->addWidget(preview,1);
            auto* caption=label("",10,kMuted,false,&dialog);caption->setWordWrap(true);root->addWidget(caption);
            auto* controls=new QHBoxLayout();auto* previous=actionButton("‹  ÖNCEKİ","#25282e","#363a42",&dialog);auto* counter=label("0 / 0",10,kMuted,true,&dialog);counter->setAlignment(Qt::AlignCenter);auto* next=actionButton("SONRAKİ  ›","#25282e","#363a42",&dialog);controls->addWidget(previous);controls->addStretch();controls->addWidget(counter);controls->addStretch();controls->addWidget(next);root->addLayout(controls);
            if(!videoUrls.isEmpty()){auto* videoLine=new QHBoxLayout();videoLine->addWidget(label("VİDEOLAR",9,kMuted,true,&dialog));for(int i=0;i<videoUrls.size();++i){auto* open=actionButton(QString("▶  VİDEO %1'İ AÇ").arg(i+1),"#25282e","#363a42",&dialog);const QString url=videoUrls[i];connect(open,&QPushButton::clicked,&dialog,[url]{const QUrl safeUrl(url);if(safeUrl.scheme().compare("https",Qt::CaseInsensitive)==0)QDesktopServices::openUrl(safeUrl);});videoLine->addWidget(open);}videoLine->addStretch();root->addLayout(videoLine);}
            int index=0;auto updateImage=[this,preview,caption,counter,&imageUrls,&imageTitles,&index,provider]{
                if(imageUrls.isEmpty()){preview->setPixmap({});preview->setText("Bu proje için görsel yok.");caption->clear();counter->setText("0 / 0");return;}
                index=(index+imageUrls.size())%imageUrls.size();preview->setPixmap({});preview->setText("Görsel yükleniyor…");caption->setText(imageTitles.value(index));counter->setText(QString("%1 / %2").arg(index+1).arg(imageUrls.size()));loadMarketplaceImage(preview,imageUrls[index],provider);
            };
            connect(previous,&QPushButton::clicked,&dialog,[&index,&imageUrls,updateImage]{if(!imageUrls.isEmpty()){--index;updateImage();}});connect(next,&QPushButton::clicked,&dialog,[&index,&imageUrls,updateImage]{if(!imageUrls.isEmpty()){++index;updateImage();}});
            previous->setEnabled(!imageUrls.isEmpty());next->setEnabled(!imageUrls.isEmpty());updateImage();dialog.exec();
        });
    }
    void appendMarketplaceResult(const QJsonObject& hit) {
        auto* f=card(marketResults_); auto* row=new QHBoxLayout(f); row->setContentsMargins(12,10,12,10); row->setSpacing(10);
        auto* cover=label("◇",20,"#9096a2",true,f);cover->setAlignment(Qt::AlignCenter);cover->setFixedSize(58,58);cover->setStyleSheet("background:#111318;border:1px solid #30343b;border-radius:10px;color:#8f96a3;font-size:20px;");loadMarketplaceImage(cover,hit.value("icon_url").toString(),hit.value("provider").toString());row->addWidget(cover);
        auto* info=new QVBoxLayout(); auto* title=label(hit.value("title").toString("İsimsiz içerik"),13,"#f1f3f6",true,f); auto* author=label(hit.value("author").toString("Bilinmiyor")+"  •  "+QString::number(hit.value("downloads").toVariant().toLongLong())+" indirme",10,kMuted,false,f); auto* desc=label(hit.value("description").toString(),10,"#b8bcc5",false,f); desc->setWordWrap(true); desc->setMaximumHeight(38); info->addWidget(title);info->addWidget(author);info->addWidget(desc);row->addLayout(info,1);
        auto* media=actionButton("GÖRSELLER","#25282e","#363a42",f);media->setMinimumWidth(96);row->addWidget(media);connect(media,&QPushButton::clicked,this,[this,hit,media]{showMarketplaceMedia(hit,media);});
        auto* download=actionButton("⬇  İNDİR","#236d4d","#2c8b62",f); download->setMinimumWidth(100); row->addWidget(download); connect(download,&QPushButton::clicked,this,[this,hit,download]{downloadMarketplaceHit(hit,download);}); marketResultsLayout_->addWidget(f);
    }
    void downloadMarketplaceHit(const QJsonObject& hit,QPushButton* button) {
        button->setEnabled(false);button->setText("İndiriliyor…"); const QString version=marketVersion_->currentText(), loader=marketLoader_->currentText(), key=config_.value("curseforge_api_key").toString(), gameDir=engine_.activeGameDirectory(config_), java=config_.value("custom_java").toString(), world=worldTarget_?worldTarget_->currentData().toString():QString();
        setStatus("İçerik indiriliyor…",10);
        runTask([this,hit,version,loader,key,gameDir,java,world]{TaskResult r;const QString path=engine_.downloadMarketplaceItem(hit,version,loader,key,gameDir,progressCallback(),world);r.text=path;if(hit.value("project_type").toString()=="modpack"){const QString provider=hit.value("provider").toString();r.object=provider=="CurseForge"?engine_.installCurseForgePack(path,hit,key,java,progressCallback()):engine_.installModrinthPack(path,hit,java,progressCallback());r.text="modpack";}r.ok=true;return r;},[this,button,hit](TaskResult r){button->setEnabled(true);if(!r.ok){button->setText("TEKRAR DENE");QMessageBox::critical(this,"İndirme/Kurulum Hatası",r.error);setStatus("İndirme başarısız",0);return;}if(r.text=="modpack"){const QString id=r.object.value("id").toString();refreshProfileSelector();const int index=profileBox_->findData(id);if(index>=0)profileBox_->setCurrentIndex(index);QMessageBox::information(this,"Modpack kuruldu",r.object.value("name").toString()+" profili kuruldu ve seçildi.");button->setText("KURULDU");}else{button->setText("İNDİRİLDİ");if(hit.value("project_type").toString()=="mod")refreshInstalledMods();if(hit.value("project_type").toString()=="world")refreshWorldSelector();setStatus(hit.value("project_type").toString()+" indirildi",100);}setStatus("İşlem tamamlandı",100);});
    }
    void refreshWorldSelector() {
        if(!worldTarget_)return;
        const QString selected=worldTarget_->currentData().toString();
        const QStringList worlds=engine_.worldNames(engine_.activeGameDirectory(config_));
        worldTarget_->blockSignals(true);worldTarget_->clear();worldTarget_->addItem("Data pack hedef dünyası seç","");
        for(const QString& world:worlds)worldTarget_->addItem(world,world);
        int index=worldTarget_->findData(selected);worldTarget_->setCurrentIndex(index<0?0:index);worldTarget_->blockSignals(false);
    }
    void refreshHorizonMods(bool force = false) {
        if (!horizonModsStatus_ || horizonModsLoading_) return;
        if (!force && !horizonModProjects_.isEmpty() && horizonModsFetchedAt_.secsTo(QDateTime::currentDateTimeUtc()) < 600) {
            renderHorizonMods();
            return;
        }
        horizonModsLoading_ = true;
        horizonModsStatus_->setText("Resmî Horizon mod dizini alınıyor…");
        runTask([] {
            TaskResult r;
            const QByteArray bytes = requestHorizonModsIndex();
            QJsonParseError parse{};
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
            if (parse.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Horizon mod dizini geçerli JSON değil.");
            const QJsonObject index = doc.object();
            if (index.value("schema").toInt() != 1) throw std::runtime_error("Bu Horizon mod dizini şeması desteklenmiyor.");
            for (const QJsonValue& value : index.value("projects").toArray()) {
                const QJsonObject project = value.toObject();
                if (project.value("kind").toString() != "native-mod" && project.value("kind").toString() != "mod") continue;
                QJsonObject version;
                for (const QJsonValue& versionValue : project.value("versions").toArray()) {
                    const QJsonObject candidate = versionValue.toObject();
                    if (horizonModVersionUsable(project, candidate) &&
                        !horizonModRevoked(index, project.value("id").toString(), candidate.value("file").toObject().value("sha256").toString())) {
                        version = candidate;
                        break;
                    }
                }
                if (version.isEmpty()) continue;
                QJsonObject listed = project;
                listed.insert("_titan_version", version);
                r.array.append(listed);
            }
            r.ok = true;
            return r;
        }, [this](TaskResult r) {
            horizonModsLoading_ = false;
            if (!r.ok) {
                horizonModsStatus_->setText("Dizin alınamadı: " + r.error);
                return;
            }
            horizonModProjects_ = r.array;
            horizonModsFetchedAt_ = QDateTime::currentDateTimeUtc();
            horizonModsStatus_->setText(QString("Horizon’un resmî mod dizininden %1 adet 26.3 mod sürümü bulundu. Dosya SHA-256’sı ve kayıtlı yayımlayıcı Minisign imzası kurulumda doğrulanır; imzasız modlar ayrıca belirtilir.").arg(horizonModProjects_.size()));
            renderHorizonMods();
        });
    }
    void renderHorizonMods() {
        if (!horizonModsListLayout_ || !horizonModsList_) return;
        clearLayout(horizonModsListLayout_);
        const QString query = horizonModsSearch_ ? horizonModsSearch_->text().trimmed() : QString();
        const QString sortMode = horizonModsSort_ ? horizonModsSort_->currentData().toString() : QStringLiteral("name_asc");
        QVector<QJsonObject> projects;
        projects.reserve(horizonModProjects_.size());
        for (const QJsonValue& value : horizonModProjects_) projects.append(value.toObject());
        std::stable_sort(projects.begin(), projects.end(), [sortMode](const QJsonObject& left, const QJsonObject& right) {
            const QString leftKind = left.value("kind").toString();
            const QString rightKind = right.value("kind").toString();
            if (sortMode == "kind" && leftKind != rightKind) return leftKind == "native-mod";
            const QString leftName = left.value("name").toString(left.value("id").toString());
            const QString rightName = right.value("name").toString(right.value("id").toString());
            const int nameOrder = QString::localeAwareCompare(leftName, rightName);
            if (nameOrder != 0) return sortMode == "name_desc" ? nameOrder > 0 : nameOrder < 0;
            return QString::localeAwareCompare(left.value("id").toString(), right.value("id").toString()) < 0;
        });
        int shown = 0;
        for (const QJsonObject& project : projects) {
            const QJsonObject version = project.value("_titan_version").toObject();
            const QJsonObject file = version.value("file").toObject();
            const QString id = project.value("id").toString();
            const QString kind = project.value("kind").toString();
            const QString title = project.value("name").toString(id);
            const QString summary = project.value("summary").toString();
            if (!query.isEmpty() && !(title + ' ' + id + ' ' + summary).contains(query, Qt::CaseInsensitive)) continue;
            const QString extension = kind == "native-mod" ? ".hnmod" : ".hmod";
            const QString destination = QDir(horizonGameHome()).filePath((kind == "native-mod" ? "horizon-native-mods/" : "horizon-mods/") + id + extension);
            const bool installed = QFileInfo::exists(destination);
            const bool hasSignature = !project.value("key").toString().trimmed().isEmpty() && !file.value("signature").toString().trimmed().isEmpty();
            const QStringList authors = [&] { QStringList out; for (const QJsonValue& author : project.value("authors").toArray()) out.append(author.toString()); return out; }();
            auto* panel = card(horizonModsList_);
            auto* panelRow = new QHBoxLayout(panel);
            panelRow->setContentsMargins(12,10,12,10);
            panelRow->setSpacing(12);
            auto* icon = new QLabel("◇",panel);
            icon->setAlignment(Qt::AlignCenter);
            icon->setFixedSize(68,68);
            icon->setStyleSheet("background:#111318;border:1px solid #30343b;border-radius:10px;color:#8f96a3;font-size:22px;");
            panelRow->addWidget(icon,0,Qt::AlignTop);
            loadHorizonModIcon(icon,project);
            auto* column = new QVBoxLayout();
            column->setContentsMargins(0,0,0,0);
            column->setSpacing(5);
            panelRow->addLayout(column,1);
            auto* top = new QHBoxLayout();
            auto* heading = label(title + "  ·  " + id, 12, "#e8eaf0", true, panel);
            heading->setWordWrap(true);
            top->addWidget(heading,1);
            top->addWidget(label(kind == "native-mod" ? "RUST KODU · .HNMOD" : "SANDBOX · .HMOD",9,kind == "native-mod" ? "#f08d74" : kGreen,true,panel));
            column->addLayout(top);
            auto* description = label(summary.isEmpty() ? "Açıklama yok." : summary,10,"#c7cad2",false,panel);
            description->setWordWrap(true);
            column->addWidget(description);
            const QString signatureIssue = horizonModSignatureIssue(project, version);
            const QString signatureText = !signatureIssue.isEmpty() ? "İmza kaydı hatalı" : (hasSignature ? "Yayımcı Minisign imzası kurulumda doğrulanır" : "İmzasız yayımlanmış");
            const QString details = QString("Sürüm %1  ·  %2  ·  %3  ·  %4  ·  SHA-256: indirmede doğrulanır")
                .arg(version.value("version").toString(), authors.join(", ").isEmpty() ? "Yazar belirtilmemiş" : authors.join(", "),
                     project.value("license").toString("Lisans belirtilmemiş"), signatureText);
            auto* detailLabel = label(details,9,kMuted,false,panel);
            detailLabel->setWordWrap(true);
            column->addWidget(detailLabel);
            auto* actions = new QHBoxLayout();
            auto* source = actionButton("KAYNAK KODU","#25282e","#363a42",panel);
            actions->addWidget(source);
            const QString installText = kind == "native-mod" ? (installed ? "YENİDEN AL" : "İNDİR (.HNMOD)") : (installed ? "GÜNCELLE" : "＋  YÜKLE");
            auto* install = actionButton(installText, installed ? "#2b2e35" : "#236d4d", installed ? "#3a3d44" : "#2c8b62", panel);
            install->setMinimumWidth(105);
            actions->addStretch();
            if (installed) {
                auto* del = actionButton("KALDIR", "#522b32", "#713942", panel);
                del->setMinimumWidth(80);
                actions->addWidget(del);
                connect(del, &QPushButton::clicked, this, [this, id, kind, destination]{
                    const QString typeName = kind == "native-mod" ? ".hnmod" : ".hmod";
                    const QString msg = kind == "native-mod"
                        ? QString("'%1%2' kaldırılsın mı?\n\nBu .HNMOD kaldırıldıktan sonra Rust istemcisini yeniden derlemeniz gerekecek.").arg(id, typeName)
                        : QString("'%1%2' kaldırılsın mı?").arg(id, typeName);
                    if (QMessageBox::question(this, "Horizon modunu kaldır", msg) != QMessageBox::Yes) return;
                    if (!QFile::remove(destination)) {
                        QMessageBox::warning(this, "Kaldırılamadı", "Mod dosyası silinemedi: " + destination);
                        return;
                    }
                    if (kind == "native-mod") {
                        // Derleme önbelleğini geçersiz kıl
                        config_.remove("horizon_native_client_path");
                        config_.remove("horizon_native_mod_hashes");
                        config_.remove("horizon_native_build_key");
                        saveConfig();
                        setStatus(id + " kaldırıldı — Rust istemcisini yeniden derleyin", 100);
                        QMessageBox::information(this, "Mod kaldırıldı",
                            id + " kaldırıldı. Değişikliklerin geçerli olması için "
                            "\"Rust Modlarını Derle\" butonuna basın.");
                    } else {
                        setStatus(id + " kaldırıldı", 100);
                    }
                    renderHorizonMods();
                });
            }
            actions->addWidget(install);
            column->addLayout(actions);
            horizonModsListLayout_->addWidget(panel);
            connect(source,&QPushButton::clicked,this,[this,project]{QDesktopServices::openUrl(QUrl(project.value("source").toString()));});
            connect(install,&QPushButton::clicked,this,[this,project,version,install]{installHorizonMod(project,version,install);});
            ++shown;
        }
        if (shown == 0) horizonModsListLayout_->addWidget(label(horizonModProjects_.isEmpty() ? "Mod dizinini yenile." : "Aramana uygun 26.3 Horizon modu bulunamadı.",11,kMuted,false,horizonModsList_));
        horizonModsListLayout_->addStretch();
    }
    void installHorizonMod(const QJsonObject& project, const QJsonObject& version, QPushButton* button) {
        if (activeTasks_ > 0) { QMessageBox::information(this, "İşlem sürüyor", "Mevcut arka plan işi bitince tekrar dene."); return; }
        const QString id = project.value("id").toString();
        const QString kind = project.value("kind").toString();
        const QJsonObject file = version.value("file").toObject();
        const QString name = file.value("name").toString();
        if (!validHorizonModId(id) || !horizonModVersionUsable(project,version)) { QMessageBox::warning(this,"Horizon modu","Mod dizini girdisi güvenlik kontrollerinden geçemedi."); return; }
        const QString signatureIssue = horizonModSignatureIssue(project, version);
        if (!signatureIssue.isEmpty()) { QMessageBox::critical(this, "Yayımcı imzası hatalı", signatureIssue + " Kurulum engellendi."); return; }
        if (kind == "native-mod") {
            const QString signature = project.value("key").toString().trimmed().isEmpty() ? "Bu sürüm imzasız; indirip derlemeden önce kaynak kodunu kendin incele." : "Titan dosya SHA-256'sını ve yayımlayıcı Minisign imzasını kurulumdan önce doğrulayacak.";
            const QString warning = QString("%1 (%2) yerel Rust kaynak kodu içeriyor. .HNMOD modları sandbox içinde çalışmaz; özel istemci derlemesine eklenir ve derleme betikleriyle oyun kodu bu bilgisayardaki kullanıcı hesabının dosyalarına ve izinlerine erişebilir. İlk derleme ayrıca büyük araç indirmeleri ve uzun süre gerektirebilir.\n\nKaynak: %3\n%4\n\nPaketin resmi Horizon mod dizininden SHA-256 ile eşleşmesi tek başına güvenli olduğunu kanıtlamaz. İndirip Titan'ın Horizon klasörüne hazırlamak istiyor musun?")
                .arg(project.value("name").toString(id),version.value("version").toString(),project.value("source").toString(),signature);
            if (QMessageBox::warning(this,"Güvenlik uyarısı: yerel Rust kodu",warning,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        } else {
            const QString info = QString("%1 Horizon’un sandbox içindeki içerik modu olarak yüklenecek. Bu tür modlar oyuna yeni içerik ekler; Java modlarını çalıştırmaz ve sistem dosyalarına doğrudan erişim sağlamaz.\n\nYükleme hedefi: Rust 26.3 oyun klasörü\nSürüm: %2")
                .arg(project.value("name").toString(id),version.value("version").toString());
            if (QMessageBox::question(this,"Horizon modunu yükle",info,QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes) return;
        }
        button->setEnabled(false);
        button->setText("İNDİRİLİYOR…");
        const QString gameHome = horizonGameHome();
        const QString registryHash = file.value("sha256").toString();
        const QString registryUrl = file.value("url").toString();
        const qint64 expectedSize = file.value("size").toVariant().toLongLong();
        setStatus("Horizon modu doğrulanıyor ve indiriliyor…",5);
        runTask([this,project,version,gameHome,id,kind,name,registryHash,registryUrl,expectedSize] {
            TaskResult r;
            QJsonParseError parse{};
            const QJsonDocument indexDoc = QJsonDocument::fromJson(requestHorizonModsIndex(),&parse);
            if (parse.error != QJsonParseError::NoError || !indexDoc.isObject() || indexDoc.object().value("schema").toInt()!=1)
                throw std::runtime_error("Kurulumdan önce Horizon mod dizini yeniden doğrulanamadı.");
            const QJsonObject index = indexDoc.object();
            if (horizonModRevoked(index,id,registryHash)) throw std::runtime_error("Bu Horizon mod sürümü dizinde iptal edilmiş; kurulum engellendi.");
            bool stillListed=false;
            QJsonObject signedProject, signedVersion;
            for (const QJsonValue& value:index.value("projects").toArray()) {
                const QJsonObject latestProject=value.toObject();
                if(latestProject.value("id").toString()!=id||latestProject.value("kind").toString()!=kind)continue;
                for(const QJsonValue& versionValue:latestProject.value("versions").toArray()){
                    const QJsonObject currentVersion=versionValue.toObject();
                    const QJsonObject currentFile=currentVersion.value("file").toObject();
                    if(currentVersion.value("version").toString()==version.value("version").toString()&&
                       currentFile.value("sha256").toString()==registryHash&&currentFile.value("url").toString()==registryUrl&&
                       currentFile.value("size").toVariant().toLongLong()==expectedSize&&horizonModVersionUsable(latestProject,currentVersion)){
                        stillListed=true;signedProject=latestProject;signedVersion=currentVersion;
                    }
                }
            }
            if(!stillListed)throw std::runtime_error("Mod dizini değişmiş. Listeyi yenileyip tekrar dene.");
            const QString cache=QDir(engine_.dataDirectory()).filePath("horizon/mod-cache/"+name);
            const QStringList trusted={"github.com","release-assets.githubusercontent.com","objects.githubusercontent.com"};
            engine_.downloadUrl(registryUrl,cache,{{"sha256",registryHash}},progressCallback(),trusted,64LL*1024*1024);
            if(QFileInfo(cache).size()!=expectedSize){QFile::remove(cache);throw std::runtime_error("İndirilen dosyanın boyutu mod diziniyle eşleşmiyor.");}
            if(kind=="native-mod")validateHorizonNativeBundle(cache,signedProject,signedVersion);
            else validateHorizonContentBundle(cache,signedProject,signedVersion);
            const bool signedByPublisher=verifyHorizonModPublisher(cache,signedProject,signedVersion,progressCallback());
            const QString extension=kind=="native-mod"?".hnmod":".hmod";
            const QString directory=QDir(gameHome).filePath(kind=="native-mod"?"horizon-native-mods":"horizon-mods");
            QDir().mkpath(directory);
            const QString destination=QDir(directory).filePath(id+extension);
            QFile source(cache);
            QSaveFile output(destination);
            if(!source.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly))throw std::runtime_error("Mod dosyası oyun klasörüne yazılamadı.");
            while(!source.atEnd()){
                const QByteArray chunk=source.read(1024*1024);
                if(chunk.isEmpty()&&source.error()!=QFileDevice::NoError)throw std::runtime_error("İndirilen mod dosyası okunamadı.");
                if(output.write(chunk)!=chunk.size())throw std::runtime_error("Mod dosyası oyun klasörüne kopyalanamadı.");
            }
            if(!output.commit())throw std::runtime_error("Mod dosyası oyun klasörüne kaydedilemedi.");
            QString settingsError;
            if(!writeHorizonModEnabled(gameHome,id,&settingsError)){QFile::remove(destination);throw std::runtime_error(("Mod etkinleştirme ayarı kaydedilemedi: "+settingsError).toStdString());}
            QFile::remove(cache);
            r.object={{"kind",kind},{"id",id},{"version",version.value("version").toString()},{"path",destination},{"signed",signedByPublisher}};
            r.ok=true;
            return r;
        },[this,button](TaskResult r){
            button->setEnabled(true);
            if(!r.ok){button->setText("TEKRAR DENE");QMessageBox::critical(this,"Horizon mod kurulamadı",r.error);setStatus("Horizon mod kurulumu başarısız",0);return;}
            button->setText("GÜNCELLE");
            renderHorizonMods();
            if(r.object.value("kind").toString()=="native-mod"){
                config_.remove("horizon_native_client_path");config_.remove("horizon_native_mod_hashes");config_.remove("horizon_native_build_key");saveConfig();
                const QString trust=r.object.value("signed").toBool()?"Paketin SHA-256 değeri ve yayımlayıcı Minisign imzası doğrulandı.":"Paketin SHA-256 değeri doğrulandı; bu mod imzasız yayımlanmış.";
                QMessageBox::information(this,".HNMOD indirildi ve doğrulandı",trust+" Şimdi Rust Modları sekmesindeki ‘Rust modlarını derle’ düğmesini kullan; derlenmeden mod oyunda etkinleşmez.");
                setStatus(".HNMOD doğrulandı; Rust istemcisini derlemeye hazır",100);
            }else{
                QMessageBox::information(this,"Horizon modu kuruldu",r.object.value("signed").toBool()?"Mod SHA-256, yayımlayıcı Minisign imzası ve paket yapısı doğrulanarak Rust 26.3 oyun klasörüne kuruldu.":"Mod SHA-256 ve paket yapısı doğrulanarak Rust 26.3 oyun klasörüne kuruldu. Bu mod imzasız yayımlanmış.");
                setStatus("Horizon .hmod kuruldu",100);
            }
        });
    }
    void refreshInstalledMods() {
        if(!installedModsLayout_)return;clearLayout(installedModsLayout_);const QString gameDir=engine_.activeGameDirectory(config_);const QStringList mods=engine_.installedMods(gameDir,installedSearch_?installedSearch_->text():QString());
        if(mods.isEmpty())installedModsLayout_->addWidget(label("Mod klasöründe dosya bulunamadı.",12,kMuted,false,installedMods_));
        for(const QString& name:mods){auto* f=card(installedMods_);auto* row=new QHBoxLayout(f);row->setContentsMargins(10,7,10,7);auto* title=label(name,11,"#e8eaf0",false,f);row->addWidget(title,1);auto* toggle=actionButton(name.endsWith(".disabled",Qt::CaseInsensitive)?"ETKİNLEŞTİR":"DEVRE DIŞI","#2b2e35","#3a3d44",f);toggle->setMinimumWidth(105);auto* del=actionButton("SİL","#522b32","#713942",f);del->setMinimumWidth(55);row->addWidget(toggle);row->addWidget(del);connect(toggle,&QPushButton::clicked,this,[this,name]{QString e;if(!engine_.setModEnabled(engine_.activeGameDirectory(config_),name,!name.endsWith(".disabled",Qt::CaseInsensitive),&e))QMessageBox::warning(this,"Mod",e);refreshInstalledMods();});connect(del,&QPushButton::clicked,this,[this,name]{if(QMessageBox::question(this,"Modu sil","'"+name+"' dosyası silinsin mi?")==QMessageBox::Yes){QString e;if(!engine_.deleteMod(engine_.activeGameDirectory(config_),name,&e))QMessageBox::warning(this,"Mod",e);refreshInstalledMods();}});installedModsLayout_->addWidget(f);}
        installedModsLayout_->addStretch();
    }
    void toggleAllMods(bool enabled) {const auto files=engine_.installedMods(engine_.activeGameDirectory(config_));for(const auto& name:files){QString e;engine_.setModEnabled(engine_.activeGameDirectory(config_),name,enabled,&e);}refreshInstalledMods();setStatus(enabled?"Tüm modlar etkinleştirildi":"Tüm modlar devre dışı bırakıldı",100);}
    void addLocalMods() {const QStringList files=QFileDialog::getOpenFileNames(this,"Mod dosyaları seç","","Minecraft modları (*.jar)");if(files.isEmpty())return;const QString dir=engine_.modsDirectory(engine_.activeGameDirectory(config_));QDir().mkpath(dir);int copied=0;for(const auto& path:files){const QString dest=QDir(dir).filePath(QFileInfo(path).fileName());if(!QFileInfo::exists(dest)&&QFile::copy(path,dest))++copied;}refreshInstalledMods();setStatus(QString("%1 mod eklendi").arg(copied),100);}
    void selectSkinFile() {const QString path=QFileDialog::getOpenFileName(this,"Minecraft skin seç","","PNG görselleri (*.png)");if(path.isEmpty())return;const QImage image(path);if(image.isNull()){QMessageBox::critical(this,"Skin","PNG dosyası açılamadı.");return;}if(!((image.width()==64&&(image.height()==64||image.height()==32))||(image.width()==128&&image.height()==128))&&QMessageBox::question(this,"Skin boyutu","Bu görsel standart 64×64 skin boyutunda değil. Yine de kullanılsın mı?")!=QMessageBox::Yes)return;selectedSkinPath_=path;skinPath_->setText(path);showSkinPreview(path);}
    void fetchSkin() {const QString query=skinQuery_->text().trimmed();if(query.isEmpty()){QMessageBox::warning(this,"Skin","Kullanıcı adı veya URL gir.");return;}setStatus("Skin indiriliyor…",15);runTask([this,query]{TaskResult r;r.text=engine_.downloadSkin(query,progressCallback());r.ok=true;return r;},[this](TaskResult r){if(!r.ok){QMessageBox::critical(this,"Skin bulunamadı",r.error);setStatus("Skin indirilemedi",0);return;}selectedSkinPath_=r.text;skinPath_->setText(r.text);showSkinPreview(r.text);setStatus("Skin indirildi",100);});}
    void showSkinPreview(const QString& path) {QImage skin(path);if(skin.isNull()){skinPreview_->setPixmap({});skinPreview_->setText("👤");return;}QImage head(8,8,QImage::Format_ARGB32);head.fill(Qt::transparent);QPainter p(&head);p.setRenderHint(QPainter::SmoothPixmapTransform,false);p.drawImage(QRect(0,0,8,8),skin,QRect(8,8,8,8));if(skin.width()>=64&&skin.height()>=64)p.drawImage(QRect(0,0,8,8),skin,QRect(40,8,8,8));p.end();skinPreview_->setPixmap(QPixmap::fromImage(head).scaled(128,128,Qt::KeepAspectRatio,Qt::FastTransformation));skinPreview_->setText("");skinName_->setText(QFileInfo(path).fileName());}
    void refreshSkinPreview() {if(!skinPreview_)return;QString path=config_.value("active_skin_file").toString();if(!QFileInfo::exists(path)){const QString user=playerName(config_);path=QDir(engine_.skinsDirectory()).filePath(user+".png");}if(QFileInfo::exists(path)){selectedSkinPath_=path;if(skinPath_)skinPath_->setText(path);showSkinPreview(path);}else{skinPreview_->setPixmap({});skinPreview_->setText("👤");if(skinName_)skinName_->setText(playerName(config_));}}
    void applyOfflineSkin() {if(selectedSkinPath_.isEmpty()||!QFileInfo(selectedSkinPath_).isFile()){QMessageBox::warning(this,"Skin","Önce skin seç veya indir.");return;}QImage image(selectedSkinPath_);if(image.isNull()){QMessageBox::critical(this,"Skin","Skin PNG olarak okunamadı.");return;}if(image.size()==QSize(128,128))image=image.scaled(64,64,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);if(image.size()!=QSize(64,64)&&image.size()!=QSize(64,32)){QMessageBox::warning(this,"Desteklenmeyen skin boyutu","Horizon 26.3 için skin 64×64 veya eski biçimde 64×32 PNG olmalı.");return;}const QString dest=QDir(engine_.skinsDirectory()).filePath(playerName(config_)+".png");QDir().mkpath(QFileInfo(dest).absolutePath());QSaveFile output(dest);if(!output.open(QIODevice::WriteOnly)||!image.save(&output,"PNG")||!output.commit()){output.cancelWriting();QMessageBox::critical(this,"Skin","Skin dosyası launcher klasörüne kaydedilemedi.");return;}selectedSkinPath_=dest;config_.insert("active_skin_file",dest);config_.insert("skin_model",skinModel_->currentText());saveConfig();skinPath_->setText(dest);showSkinPreview(dest);setStatus("Skin launcher profiline uygulandı",100);}
    void uploadSkin() {if(config_.value("account_type").toString()!="microsoft"){QMessageBox::warning(this,"Microsoft hesabı gerekli","Resmi skin yüklemek için Microsoft hesabıyla giriş yap.");return;}if(selectedSkinPath_.isEmpty()){QMessageBox::warning(this,"Skin","Önce PNG skin seç.");return;}QJsonObject account=config_.value("ms_account").toObject();const QString clientId=config_.value("custom_ms_client_id").toString();const QString path=selectedSkinPath_;const QString variant=skinModel_->currentText().contains("İnce")?"slim":"classic";setStatus("Microsoft oturumu kontrol ediliyor…",20);runTask([this,account,clientId,path,variant]{TaskResult r;QJsonObject fresh=account;if(account.contains("refresh_token"))fresh=engine_.refreshMicrosoft(clientId,account.value("refresh_token").toString());const auto result=engine_.uploadSkin(fresh.value("access_token").toString(),path,variant);r.object=fresh;r.ok=result.first;r.text=result.second;if(!r.ok)r.error=result.second;return r;},[this](TaskResult r){if(!r.object.isEmpty())upsertMicrosoftAccount(r.object,false);if(!r.ok){QMessageBox::critical(this,"Skin yüklenemedi",r.error);setStatus("Skin yüklenemedi",0);}else{QMessageBox::information(this,"Skin yüklendi",r.text);setStatus("Resmi Minecraft skin'i güncellendi",100);}});}
    void refreshLogs() {if(!logViewer_||!logKind_)return;const QString path=logKind_->currentIndex()==0?engine_.launcherLogPath():engine_.processLogPath();QFile f(path);if(!f.open(QIODevice::ReadOnly)){logViewer_->setPlainText("Log dosyası henüz oluşturulmadı.");return;}logViewer_->setPlainText(QString::fromUtf8(f.readAll()));logViewer_->moveCursor(QTextCursor::End);}
    void clearLayout(QLayout* layout) {if(!layout)return;while(QLayoutItem* item=layout->takeAt(0)){if(QWidget* w=item->widget())w->deleteLater();if(item->layout())clearLayout(item->layout());delete item;}}









    void startMicrosoftLogin() {
        if(loginCancel_)loginCancel_->store(true);loginCancel_=std::make_shared<std::atomic_bool>(false);const auto cancel=loginCancel_;
        auto* dialog=new QDialog(this);loginDialog_=dialog;dialog->setWindowTitle("Microsoft oturum doğrulaması");dialog->setFixedSize(430,250);dialog->setStyleSheet("QDialog{background:#17191e;color:#eef0f4;}");auto* v=new QVBoxLayout(dialog);v->setContentsMargins(22,18,22,18);auto* title=label("MICROSOFT DOĞRULAMASI",14,kGreen,true,dialog);v->addWidget(title);auto* code=label("Giriş kodu alınıyor…",21,"#f2f3f7",true,dialog);code->setAlignment(Qt::AlignCenter);v->addWidget(code,1);auto* instruction=label("Kod hazır olduğunda tarayıcı açılacak.",11,kMuted,false,dialog);instruction->setWordWrap(true);instruction->setAlignment(Qt::AlignCenter);v->addWidget(instruction);auto* row=new QHBoxLayout();auto* copy=actionButton("KODU KOPYALA","#2b2e35","#383b43",dialog);auto* cancelButton=actionButton("İPTAL ET","#522b32","#713942",dialog);row->addWidget(copy);row->addWidget(cancelButton);v->addLayout(row);connect(copy,&QPushButton::clicked,dialog,[this,code]{QApplication::clipboard()->setText(code->text());});connect(cancelButton,&QPushButton::clicked,dialog,[cancel,dialog]{cancel->store(true);dialog->close();});connect(dialog,&QDialog::finished,dialog,[cancel]{cancel->store(true);});dialog->show();setStatus("Microsoft doğrulama kodu isteniyor…",5);
        const QString clientId=config_.value("custom_ms_client_id").toString();
        runTask([this,clientId]{TaskResult r;r.object=engine_.requestDeviceCode(clientId);r.ok=true;return r;},[this,dialog,code,instruction,cancel,clientId](TaskResult r){if(!dialog||cancel->load())return;if(!r.ok){dialog->close();QMessageBox::critical(this,"Microsoft girişi",r.error);setStatus("Microsoft girişi başarısız",0);return;}const QString userCode=r.object.value("user_code").toString();const QString url=r.object.value("verification_uri").toString("https://www.microsoft.com/link");const QString deviceCode=r.object.value("device_code").toString();const int interval=r.object.value("interval").toInt(5),expires=r.object.value("expires_in").toInt(900);code->setText(userCode);instruction->setText("Kodu Microsoft sayfasında onayla. Launcher giriş sonucunu bekliyor.");QApplication::clipboard()->setText(userCode);QDesktopServices::openUrl(QUrl(url));setStatus("Microsoft giriş onayı bekleniyor…",25);
            runTask([this,clientId,deviceCode,interval,expires,cancel]{TaskResult x;const QJsonObject tokens=engine_.pollDeviceToken(clientId,deviceCode,interval,expires,[cancel]{return cancel->load();},progressCallback());x.object=engine_.exchangeMicrosoftToken(tokens.value("access_token").toString(),tokens.value("refresh_token").toString());x.ok=true;return x;},[this,dialog,cancel](TaskResult result){if(!dialog)return;dialog->close();if(cancel->load())return;if(!result.ok){QMessageBox::critical(this,"Microsoft girişi başarısız",result.error);setStatus("Microsoft girişi başarısız",0);return;}upsertMicrosoftAccount(result.object,true);QMessageBox::information(this,"Giriş başarılı","Microsoft hesabı kaydedildi: "+result.object.value("name").toString());setStatus("Microsoft hesabı eklendi",100);});
        });
    }
    void launchGame() {
        if(gameStarting_) return;
        const QJsonObject profile=engine_.activeProfile(config_);
        if(profile.value("kind").toString()=="horizon-rust"){playHorizonClient();return;}
        const bool managed=profile.value("kind").toString()!="default";
        const QString version=managed?profile.value("minecraft_version").toString():versionBox_->currentText();
        const QString loader=managed?profile.value("loader").toString("Vanilla"):loaderBox_->currentText();
        const QString profileLaunch=managed?profile.value("launch_version").toString():QString();
        const QString loaderVersion=managed?profile.value("loader_version").toString():QString();
        const QString gameDir=profile.value("directory").toString(engine_.minecraftDirectory());
        if(version.isEmpty()||version=="Kurulu Sürüm Yok"||version=="Sürümler taranıyor…") { QMessageBox::warning(this,"Sürüm seçilmedi","Minecraft sürümü seç veya kurulu sürümleri yenile.");return; }
        QString username,uuid,token="0"; const QJsonObject account=config_.value("ms_account").toObject();
        if(config_.value("account_type").toString()=="microsoft") {
            if(account.value("access_token").toString().isEmpty()){QMessageBox::warning(this,"Giriş gerekli","Önce Microsoft hesabınla giriş yap.");return;}
            username=account.value("name").toString("Player");uuid=account.value("id").toString();token=account.value("access_token").toString();
        } else { username=config_.value("username").toString("Steve").trimmed();if(username.isEmpty())username="Steve";uuid=LauncherEngine::offlineUuid(username); }
        const QString activeAccountId=config_.value("active_account").toString();
        saveConfig();
        QJsonObject cfg=config_;
        const QJsonObject savedProfile=engine_.activeProfile(config_);
        if(managed) for(const QString& key:{"ram","custom_java","jvm_arguments"}) if(savedProfile.contains(key)) cfg.insert(key,savedProfile.value(key));
        gameStarting_=true;playButton_->setEnabled(false);playButton_->setText("HAZIRLANIYOR…");setStatus(QString("%1 / %2 hazırlanıyor…").arg(version,loader),5);
        runTask([this,version,loader,loaderVersion,profileLaunch,gameDir,username,uuid,token,account,cfg,profile,managed]{
            TaskResult r; QJsonObject fresh=account; QString authToken=token,player=username,playerUuid=uuid;
            if(cfg.value("account_type").toString()=="microsoft"&&account.contains("refresh_token")) {
                try { fresh=engine_.refreshMicrosoft(cfg.value("custom_ms_client_id").toString(),account.value("refresh_token").toString());authToken=fresh.value("access_token").toString(authToken);player=fresh.value("name").toString(player);playerUuid=fresh.value("id").toString(playerUuid); } catch(...) {}
            }
            QJsonObject launchProfile=profile;
            if(managed) launchProfile=engine_.repairLegacyModrinthPack(profile,cfg.value("custom_java").toString(),progressCallback());
            const QString launchVersion=managed?launchProfile.value("minecraft_version").toString(version):version;
            const QString launchLoader=managed?launchProfile.value("loader").toString(loader):loader;
            const QString launchLoaderVersion=managed?launchProfile.value("loader_version").toString(loaderVersion):loaderVersion;
            QString target=managed?launchProfile.value("launch_version").toString(profileLaunch):profileLaunch;
            if(target.isEmpty()||!engine_.localVersions().contains(target)) target=engine_.installVersion(launchVersion,launchLoader,launchLoaderVersion,cfg.value("custom_java").toString(),progressCallback());
            r.launch=engine_.createLaunchSpec(target,player,playerUuid,authToken,gameDir,cfg,progressCallback());
            if(launchProfile.value("loader").toString()!=profile.value("loader").toString()||launchProfile.value("launch_version").toString()!=profile.value("launch_version").toString())r.profile=launchProfile;
            r.object=fresh;r.ok=true;return r;
        },[this,activeAccountId](TaskResult r){gameStarting_=false;playButton_->setEnabled(true);playButton_->setText("▶  OYNA");if(!r.ok){QMessageBox::critical(this,"Başlatma hatası",r.error);setStatus("Minecraft başlatılamadı",0);return;}if(!r.object.isEmpty()&&r.object.contains("access_token")){upsertMicrosoftAccount(r.object,false);if(config_.value("active_account").toString()==activeAccountId)updateAccountCard();}if(!r.profile.isEmpty()){refreshProfileSelector();setStatus("Eski modpack profili düzeltildi; oyun başlatılıyor…",100);}startMinecraftProcess(r.launch);});
    }
    void startMinecraftProcess(const LaunchSpec& spec) {
        if(process_&&process_->state()!=QProcess::NotRunning){QMessageBox::warning(this,"Minecraft","Bir oyun süreci zaten çalışıyor.");return;}
        engine_.appendLauncherLog(QString("Minecraft başlatılıyor: %1").arg(spec.versionId));
        process_=new QProcess(this);process_->setWorkingDirectory(spec.workingDirectory);process_->setProgram(spec.java);process_->setArguments(spec.arguments);
        QProcessEnvironment env=QProcessEnvironment::systemEnvironment();if(config_.value("force_gpu").toBool(true)){env.insert("SHIM_MCCOMPAT","0x800000001");env.insert("CUDA_VISIBLE_DEVICES","0");env.insert("GPU_MAX_ALLOC_PERCENT","100");env.insert("GPU_USE_SYNC_OBJECTS","1");env.insert("GPU_SINGLE_ALLOC_PERCENT","100");env.insert("GPU_MAX_HEAP_SIZE","100");env.insert("GPU_FORCE_64BIT_PTR","1");setGpuPreference(spec.java);}process_->setProcessEnvironment(env);
#ifdef Q_OS_WIN
        const bool high=config_.value("high_priority").toBool(true);process_->setCreateProcessArgumentsModifier([high](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW;if(high)args->flags|=HIGH_PRIORITY_CLASS;});
#endif
        processLog_.setFileName(engine_.processLogPath());if(processLog_.open(QIODevice::WriteOnly|QIODevice::Append)){processLog_.write("\r\n\r\n=== Minecraft başlatıldı: "+QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8()+" ===\r\n");processLog_.write((spec.java+" <oyun argümanları güvenlik için gizlendi>\r\n").toUtf8());processLog_.flush();}
        connect(process_,&QProcess::readyReadStandardOutput,this,[this]{const QByteArray b=process_->readAllStandardOutput();if(processLog_.isOpen()){processLog_.write(b);processLog_.flush();}});connect(process_,&QProcess::readyReadStandardError,this,[this]{const QByteArray b=process_->readAllStandardError();if(processLog_.isOpen()){processLog_.write(b);processLog_.flush();}});
        connect(process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){if(processLog_.isOpen())processLog_.close();if(closeBehavior_&&closeBehavior_->currentText().startsWith("Gizle")&&isHidden())showMaximized();playButton_->setEnabled(true);playButton_->setText("▶  OYNA");setStatus(code==0?"Minecraft kapandı.":QString("Minecraft kapandı (kod %1)").arg(code),100);if(code!=0)QMessageBox::warning(this,"Minecraft kapandı",QString("Çıkış kodu %1. Ayrıntılar için process logunu aç.").arg(code));});
        connect(process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){if(process_->state()==QProcess::NotRunning){if(processLog_.isOpen())processLog_.close();playButton_->setEnabled(true);playButton_->setText("▶  OYNA");setStatus("Minecraft süreci başlatılamadı: "+process_->errorString(),0);}});
        process_->start();if(!process_->waitForStarted(8000)){QMessageBox::critical(this,"Java başlatılamadı",process_->errorString());playButton_->setEnabled(true);playButton_->setText("▶  OYNA");setStatus("Java başlatılamadı",0);return;}
        setStatus(QString("Minecraft %1 çalışıyor").arg(spec.versionId),100);const QString behavior=closeBehavior_?closeBehavior_->currentText():config_.value("close_behavior").toString();if(behavior.startsWith("Gizle"))hide();else if(behavior=="Kapat"){process_->setParent(nullptr);process_=nullptr;close();}
    }
    void setGpuPreference(const QString& javaPath) {
#ifdef Q_OS_WIN
        QSettings gpu("HKEY_CURRENT_USER\\Software\\Microsoft\\DirectX\\UserGpuPreferences",QSettings::NativeFormat);gpu.setValue(javaPath,"GpuPreference=2;");
#else
        Q_UNUSED(javaPath);
#endif
    }
};

int main(int argc,char* argv[]) {
    QApplication app(argc,argv);
    QApplication::setOrganizationName("SoldierGaming");
    QApplication::setApplicationName("TitanLauncherCpp");
    QApplication::setWindowIcon(QIcon(":/app.ico"));
    QApplication::setStyle("Fusion");

    // Tema: her zaman koyu
    applyTheme(true);

    // Tema'ya göre scrollbar stili
    const QString scrollBg    = gTheme.scrollBg;
    const QString scrollHnd   = gTheme.scrollHandle;
    const QString scrollHndHv = gTheme.scrollHandleHover;
    app.setStyleSheet(QString(R"(
        QScrollBar:vertical{background:%1;width:9px;margin:3px 2px;border:0;border-radius:4px;}
        QScrollBar::handle:vertical{background:%2;min-height:28px;border-radius:4px;}
        QScrollBar::handle:vertical:hover{background:%3;}
        QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;border:0;background:transparent;}
        QScrollBar::up-arrow:vertical,QScrollBar::down-arrow:vertical{width:0;height:0;background:transparent;}
        QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
        QScrollBar:horizontal{background:%1;height:9px;margin:2px 3px;border:0;border-radius:4px;}
        QScrollBar::handle:horizontal{background:%2;min-width:28px;border-radius:4px;}
        QScrollBar::handle:horizontal:hover{background:%3;}
        QScrollBar::add-line:horizontal,QScrollBar::sub-line:horizontal{width:0;border:0;background:transparent;}
        QScrollBar::left-arrow:horizontal,QScrollBar::right-arrow:horizontal{width:0;height:0;background:transparent;}
        QScrollBar::add-page:horizontal,QScrollBar::sub-page:horizontal{background:transparent;}
    )").arg(scrollBg, scrollHnd, scrollHndHv));
    QFont font("Segoe UI",10);app.setFont(font);
    Splash splash;splash.show();
    QTimer::singleShot(950,&splash,[&splash]{
        auto* window = new MainWindow();
        window->setAttribute(Qt::WA_DeleteOnClose);
        // Pencere fade-in
        auto* windowEffect = new QGraphicsOpacityEffect(window);
        window->setGraphicsEffect(windowEffect);
        windowEffect->setOpacity(0.0);
        auto* windowFade = new QPropertyAnimation(windowEffect, "opacity", window);
        windowFade->setDuration(350);
        windowFade->setStartValue(0.0);
        windowFade->setEndValue(1.0);
        windowFade->setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(windowFade, &QPropertyAnimation::finished, window, [window, windowEffect, windowFade] {
            if (window->graphicsEffect() == windowEffect) window->setGraphicsEffect(nullptr);
            windowFade->deleteLater();
        });
        window->showMaximized();
        windowFade->start();
        splash.close();
    });
    return app.exec();
}







