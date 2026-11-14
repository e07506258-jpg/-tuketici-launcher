#include "launcher_engine.h"
#include "zip_archive.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QUuid>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVector>
#include <QXmlStreamReader>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#include <shlobj.h>
#endif

namespace {
const QString kClientId = "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb";
QString sealText(const QString& value) {
#ifdef Q_OS_WIN
    if (value.isEmpty()) return value;
    DATA_BLOB in{}, out{};
    QByteArray raw = value.toUtf8();
    in.cbData = DWORD(raw.size()); in.pbData = reinterpret_cast<BYTE*>(raw.data());
    if (CryptProtectData(&in, L"TitanLauncherSecret", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        QByteArray bytes(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
        LocalFree(out.pbData);
        return "DPAPI:" + QString::fromLatin1(bytes.toBase64());
    }
#endif
    return value;
}
QString unsealText(const QString& value) {
    if (!value.startsWith("DPAPI:")) return value;
#ifdef Q_OS_WIN
    QByteArray raw = QByteArray::fromBase64(value.mid(6).toLatin1());
    DATA_BLOB in{}, out{};
    in.cbData = DWORD(raw.size()); in.pbData = reinterpret_cast<BYTE*>(raw.data());
    if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        QByteArray bytes(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
        LocalFree(out.pbData);
        return QString::fromUtf8(bytes);
    }
#endif
    return {};
}
QString qjsonError(const QByteArray& body) {
    const QJsonObject obj = QJsonDocument::fromJson(body).object();
    QString message = obj.value("error_description").toString();
    if (message.isEmpty()) message = obj.value("errorMessage").toString();
    if (message.isEmpty()) message = obj.value("message").toString();
    if (message.isEmpty()) message = obj.value("error").toString();
    if (message.isEmpty()) message = QString::fromUtf8(body.left(500));
    return message;
}
QUrlQuery encodedQuery(const QMap<QString, QString>& values) {
    QUrlQuery q;
    for (auto it = values.cbegin(); it != values.cend(); ++it) q.addQueryItem(it.key(), it.value());
    return q;
}
QString cleanUrl(const QString& base, const QMap<QString, QString>& params) {
    QUrl u(base); u.setQuery(encodedQuery(params)); return u.toString(QUrl::FullyEncoded);
}
bool isVersionPlaceholder(const QString& s) {
    return s.isEmpty() || s == "Sürümler taranıyor..." || s == "Kurulu Sürüm Yok" || s == "Tümü";
}
QString safeFileName(const QString& raw) {
    return QFileInfo(raw).fileName().replace(QRegularExpression("[^A-Za-z0-9._ -]"), "_");
}
QStringList arrayToStrings(const QJsonValue& value) {
    QStringList out;
    for (const auto& item : value.toArray()) if (item.isString()) out << item.toString();
    return out;
}
QVector<int> versionSortKey(const QString& version) {
    QVector<int> key;
    const auto parts = version.section('-', 0, 0).split('.');
    for (const QString& part : parts) {
        const auto m = QRegularExpression("^(\\d+)").match(part);
        key.append(m.hasMatch() ? m.captured(1).toInt() : 0);
    }
    while (key.size() < 4) key.append(0);
    if (key.size() > 4) key.resize(4);
    return key;
}
QPair<QString, QString> modrinthPackLoader(const QJsonObject& dependencies) {
    const QList<QPair<QString, QString>> keys = {
        {"quilt-loader", "Quilt"}, {"neoforge", "NeoForge"}, {"forge", "Forge"},
        {"fabric-loader", "Fabric"}, {"quilt", "Quilt"}, {"fabric", "Fabric"}
    };
    for (const auto& key : keys) {
        if (!dependencies.contains(key.first)) continue;
        const QString version = dependencies.value(key.first).toVariant().toString().trimmed();
        if (!version.isEmpty()) return {key.second, version};
    }
    return {"Vanilla", {}};
}
bool copyDirectoryTree(const QString& source, const QString& destination) {
    if (!QDir().mkpath(destination)) return false;
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QFileInfo info(from);
        if (info.isSymLink()) continue;
        const QString relative = QDir(source).relativeFilePath(from);
        const QString to = QDir(destination).filePath(relative);
        if (info.isDir()) {
            if (!QDir().mkpath(to)) return false;
        } else {
            if (!QDir().mkpath(QFileInfo(to).absolutePath()) || !QFile::copy(from, to)) return false;
        }
    }
    return true;
}
}

LauncherEngine::LauncherEngine() {
    const QString appData = qEnvironmentVariable("APPDATA");
#ifdef Q_OS_WIN
    dataDir_ = QDir(appData.isEmpty() ? QDir::homePath() + "/AppData/Roaming" : appData).filePath("SoldierGamingMinecraft");
#else
    dataDir_ = QDir::homePath() + "/.local/share/SoldierGamingMinecraft";
#endif
    mcDir_ = QDir(dataDir_).filePath(".minecraft");
    versionsDir_ = QDir(mcDir_).filePath("versions");
    modsDir_ = QDir(mcDir_).filePath("mods");
    resourcePacksDir_ = QDir(mcDir_).filePath("resourcepacks");
    modpacksDir_ = QDir(dataDir_).filePath("modpacks");
    instancesDir_ = QDir(dataDir_).filePath("instances");
    skinsDir_ = QDir(dataDir_).filePath("skins");
    cacheDir_ = QDir(mcDir_).filePath("cache");
    runtimesDir_ = QDir(dataDir_).filePath("runtimes");
    configPath_ = QDir(mcDir_).filePath("launcher_config.json");
    performancePath_ = QDir(mcDir_).filePath("performance_settings.json");
    versionsCachePath_ = QDir(mcDir_).filePath("cached_versions.json");
    launcherLogPath_ = QDir(mcDir_).filePath("launcher.log");
    processLogPath_ = QDir(mcDir_).filePath("minecraft_process.log");
    ensureDirectories();
    appendLauncherLog("Tüketici Launcher başlatıldı.");
}
void LauncherEngine::appendLauncherLog(const QString& message) const {
    QFile file(launcherLogPath_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    const QString line = QString("[%1] %2\r\n").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message);
    file.write(line.toUtf8());
}
void LauncherEngine::ensureDirectories() const {
    for (const QString& path : {dataDir_, mcDir_, versionsDir_, modsDir_, resourcePacksDir_, modpacksDir_, instancesDir_, skinsDir_, cacheDir_, runtimesDir_, QDir(cacheDir_).filePath("skin_avatars"), QDir(cacheDir_).filePath("mod_icons")}) QDir().mkpath(path);
}
QString LauncherEngine::modsDirectory(const QString& gameDirectory) const { return QDir(gameDirectory.isEmpty() ? mcDir_ : gameDirectory).filePath("mods"); }
QString LauncherEngine::resourcePacksDirectory(const QString& gameDirectory) const { return QDir(gameDirectory.isEmpty() ? mcDir_ : gameDirectory).filePath("resourcepacks"); }

QByteArray LauncherEngine::request(const QString& url, const QByteArray& method, const QByteArray& body,
                                   const QMap<QByteArray, QByteArray>& headers, int timeoutMs, int* status) const {
    const QUrl parsedUrl(url);
    if (!parsedUrl.isValid() || parsedUrl.scheme().compare("https", Qt::CaseInsensitive) != 0 || parsedUrl.host().isEmpty())
        throw std::runtime_error("Güvenlik nedeniyle yalnızca HTTPS bağlantıları kullanılabilir.");
    QNetworkAccessManager manager;
    QNetworkRequest req{parsedUrl};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "TuketiciLauncher/1.0 (Qt C++)");
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) req.setRawHeader(it.key(), it.value());
    QNetworkReply* reply = nullptr;
    if (method == "POST") reply = manager.post(req, body);
    else if (method == "PUT") reply = manager.put(req, body);
    else if (method == "DELETE") reply = manager.deleteResource(req);
    else reply = manager.get(req);
    QEventLoop loop;
    QTimer timer; timer.setSingleShot(true);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(timeoutMs);
    loop.exec();
    if (status) *status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const auto error = reply->error();
    const QString errorText = reply->errorString();
    const QUrl finalUrl = reply->url();
    const int code = status ? *status : reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    delete reply;
    if (finalUrl.scheme().compare("https", Qt::CaseInsensitive) != 0)
        throw std::runtime_error("Güvenlik nedeniyle HTTPS dışına yönlendirme engellendi.");
    if (error != QNetworkReply::NoError && code == 0) throw std::runtime_error(errorText.toStdString());
    return data;
}
QJsonObject LauncherEngine::getJson(const QString& url, const QMap<QByteArray, QByteArray>& headers) const {
    int status = 0;
    const QByteArray bytes = request(url, "GET", {}, headers, 30000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("HTTP %1: %2").arg(status).arg(qjsonError(bytes)).toStdString());
    QJsonParseError parse{};
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Sunucudan geçerli JSON alınamadı.");
    return doc.object();
}
QString LauncherEngine::sha1File(const QString& path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha1);
    while (!f.atEnd()) h.addData(f.read(1024 * 1024));
    return QString::fromLatin1(h.result().toHex());
}
QString LauncherEngine::downloadUrl(const QString& url, const QString& destination,
                                    const QJsonObject& expectedHashes, Progress progress,
                                    const QStringList& trustedHosts, qint64 maximumBytes) const {
    const QUrl parsedUrl(url);
    if (!parsedUrl.isValid() || parsedUrl.scheme().compare("https", Qt::CaseInsensitive) != 0 || parsedUrl.host().isEmpty())
        throw std::runtime_error("Güvenlik nedeniyle indirme bağlantısı HTTPS olmalı.");
    if (!trustedHosts.isEmpty() && !trustedHosts.contains(parsedUrl.host(), Qt::CaseInsensitive))
        throw std::runtime_error("İndirme kaynağı güvenilen alan adları arasında değil.");
    QDir().mkpath(QFileInfo(destination).absolutePath());
    const QString temp = destination + ".part";
    QFile output(temp);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) throw std::runtime_error(output.errorString().toStdString());
    QNetworkAccessManager manager;
    QNetworkRequest req{parsedUrl};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "TuketiciLauncher/1.0 (Qt C++)");
    QNetworkReply* reply = manager.get(req);
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    qint64 total = 0, received = 0;
    QObject::connect(reply, &QNetworkReply::metaDataChanged, &loop, [&] {
        total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (maximumBytes > 0 && total > maximumBytes) reply->abort();
    });
    QObject::connect(reply, &QIODevice::readyRead, &loop, [&] {
        const QByteArray chunk = reply->readAll();
        if (maximumBytes > 0 && received + chunk.size() > maximumBytes) { reply->abort(); return; }
        if (output.write(chunk) != chunk.size()) reply->abort();
        received += chunk.size();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, &loop, [&](qint64 got, qint64 all) {
        received = got; total = all;
        if (progress && all > 0) progress(QString("İndiriliyor: %1").arg(QFileInfo(destination).fileName()), int(got * 100 / all));
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timer, &QTimer::timeout, reply, &QNetworkReply::abort);
    timer.start(5 * 60 * 1000); loop.exec();
    const QByteArray trailing = reply->readAll();
    bool tooLarge = maximumBytes > 0 && received + trailing.size() > maximumBytes;
    if (!tooLarge) {
        if (output.write(trailing) != trailing.size()) { reply->abort(); }
        received += trailing.size();
    }
    output.flush(); output.close();
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString netError = reply->errorString();
    tooLarge = tooLarge || (maximumBytes > 0 && (received > maximumBytes || total > maximumBytes));
    const QUrl finalUrl = reply->url();
    const auto err = reply->error(); delete reply;
    if (finalUrl.scheme().compare("https", Qt::CaseInsensitive) != 0 ||
        (!trustedHosts.isEmpty() && !trustedHosts.contains(finalUrl.host(), Qt::CaseInsensitive))) {
        QFile::remove(temp);
        throw std::runtime_error("İndirme HTTPS dışına veya güvenilmeyen bir alan adına yönlendirildi.");
    }
    if (tooLarge) { QFile::remove(temp); throw std::runtime_error(QString("İndirilen dosya izin verilen %1 MB sınırını aşıyor.").arg(maximumBytes / (1024 * 1024)).toStdString()); }
    if (err != QNetworkReply::NoError || code < 200 || code >= 300) { QFile::remove(temp); throw std::runtime_error(QString("İndirme başarısız (HTTP %1): %2").arg(code).arg(netError).toStdString()); }
    for (auto it = expectedHashes.begin(); it != expectedHashes.end(); ++it) {
        QCryptographicHash::Algorithm alg;
        if (it.key().compare("sha1", Qt::CaseInsensitive) == 0) alg = QCryptographicHash::Sha1;
        else if (it.key().compare("sha256", Qt::CaseInsensitive) == 0) alg = QCryptographicHash::Sha256;
        else if (it.key().compare("sha512", Qt::CaseInsensitive) == 0) alg = QCryptographicHash::Sha512;
        else if (it.key().compare("md5", Qt::CaseInsensitive) == 0) alg = QCryptographicHash::Md5;
        else continue;
        QFile file(temp); if (!file.open(QIODevice::ReadOnly)) { QFile::remove(temp); throw std::runtime_error("İndirilen dosya doğrulanamadı."); }
        QCryptographicHash h(alg); while (!file.atEnd()) h.addData(file.read(1024 * 1024));
        if (QString::fromLatin1(h.result().toHex()).compare(it.value().toString(), Qt::CaseInsensitive) != 0) {
            QFile::remove(temp); throw std::runtime_error(QString("%1 doğrulaması başarısız.").arg(it.key()).toStdString());
        }
    }
    QFile::remove(destination);
    if (!QFile::rename(temp, destination)) { QFile::remove(temp); throw std::runtime_error("İndirilen dosya hedefe taşınamadı."); }
    return destination;
}

QJsonObject LauncherEngine::loadConfig() const {
    QJsonObject defaults{
        {"username", "Steve"}, {"version", ""}, {"loader", "Vanilla"}, {"ram", 4},
        {"jvm_arguments", ""}, {"custom_java", ""}, {"gc_mode", "G1GC (Aikar - Önerilen / Düşük-Orta RAM)"},
        {"high_priority", true}, {"force_gpu", true}, {"close_behavior", "Gizle (Oyun Bitince Aç)"},
        {"only_installed", false}, {"show_snapshots", false}, {"res_w", ""}, {"res_h", ""},
        {"account_type", "offline"}, {"ms_account", QJsonObject{}}, {"accounts", QJsonArray{}}, {"active_account", ""}, {"skin_model", "Klasik (Steve - 4px)"},
        {"active_instance", "default"}, {"curseforge_api_key", ""}
    };
    QFile f(configPath_);
    if (f.open(QIODevice::ReadOnly)) {
        const auto saved = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = saved.begin(); it != saved.end(); ++it) defaults.insert(it.key(), it.value());
    }
    QFile perf(performancePath_);
    if (perf.open(QIODevice::ReadOnly)) {
        const auto saved = QJsonDocument::fromJson(perf.readAll()).object();
        for (const QString& key : {"gc_mode", "high_priority", "force_gpu", "close_behavior", "res_w", "res_h", "custom_java", "jvm_arguments"}) if (saved.contains(key)) defaults.insert(key, saved.value(key));
    }
    QJsonObject account = defaults.value("ms_account").toObject();
    if (account.contains("access_token")) account.insert("access_token", unsealText(account.value("access_token").toString()));
    if (account.contains("refresh_token")) account.insert("refresh_token", unsealText(account.value("refresh_token").toString()));
    defaults.insert("ms_account", account);
    QJsonArray accounts=defaults.value("accounts").toArray();
    for(qsizetype i=0;i<accounts.size();++i){
        QJsonObject entry=accounts.at(i).toObject();QJsonObject saved=entry.value("ms_account").toObject();
        if(saved.contains("access_token"))saved.insert("access_token",unsealText(saved.value("access_token").toString()));
        if(saved.contains("refresh_token"))saved.insert("refresh_token",unsealText(saved.value("refresh_token").toString()));
        if(!saved.isEmpty())entry.insert("ms_account",saved);accounts.replace(i,entry);
    }
    defaults.insert("accounts",accounts);
    defaults.insert("curseforge_api_key", unsealText(defaults.value("curseforge_api_key").toString()));
    return defaults;
}
bool LauncherEngine::saveConfig(const QJsonObject& config, QString* error) const {
    QJsonObject disk = config;
    QJsonObject account = disk.value("ms_account").toObject();
    if (account.contains("access_token")) account.insert("access_token", sealText(account.value("access_token").toString()));
    if (account.contains("refresh_token")) account.insert("refresh_token", sealText(account.value("refresh_token").toString()));
    disk.insert("ms_account", account);
    QJsonArray accounts=disk.value("accounts").toArray();
    for(qsizetype i=0;i<accounts.size();++i){
        QJsonObject entry=accounts.at(i).toObject();QJsonObject saved=entry.value("ms_account").toObject();
        if(saved.contains("access_token"))saved.insert("access_token",sealText(saved.value("access_token").toString()));
        if(saved.contains("refresh_token"))saved.insert("refresh_token",sealText(saved.value("refresh_token").toString()));
        if(!saved.isEmpty())entry.insert("ms_account",saved);accounts.replace(i,entry);
    }
    disk.insert("accounts",accounts);
    if (disk.contains("curseforge_api_key")) disk.insert("curseforge_api_key", sealText(disk.value("curseforge_api_key").toString()));
    QSaveFile out(configPath_);
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(disk).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) {
        if (error) *error = out.errorString(); return false;
    }
    QJsonObject perf;
    for (const QString& key : {"gc_mode", "high_priority", "force_gpu", "close_behavior", "res_w", "res_h", "custom_java", "jvm_arguments"}) if (config.contains(key)) perf.insert(key, config.value(key));
    QSaveFile perfOut(performancePath_);
    if (perfOut.open(QIODevice::WriteOnly)) { perfOut.write(QJsonDocument(perf).toJson(QJsonDocument::Indented)); perfOut.commit(); }
    return true;
}
QJsonArray LauncherEngine::readProfiles() const {
    QJsonArray profiles;
    profiles.append(QJsonObject{{"id", "default"}, {"name", "Varsayılan (.minecraft)"}, {"directory", mcDir_}, {"kind", "default"}, {"loader", "Vanilla"}, {"minecraft_version", ""}, {"launch_version", ""}, {"loader_version", ""}});
    profiles.append(QJsonObject{{"id", "titan-rust-26.3"}, {"name", "Rust 26.3"},
        {"directory", QDir(dataDir_).filePath("horizon/game-home")}, {"kind", "horizon-rust"},
        {"loader", "Vanilla"}, {"minecraft_version", "26.3"}, {"launch_version", "26.3"},
        {"loader_version", ""}});
    QDirIterator it(instancesDir_, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dir = it.next(); QFile f(QDir(dir).filePath("instance.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        QJsonObject profile = QJsonDocument::fromJson(f.readAll()).object();
        if (profile.value("id").toString().isEmpty()) continue;
        profile.insert("profile_root", dir);
        profile.insert("directory", profile.value("game_directory").toString(dir));
        profile.insert("kind", profile.value("kind").toString("modpack"));
        profile.insert("loader", profile.value("loader").toString("Vanilla")); profiles.append(profile);
    }
    return profiles;
}
bool LauncherEngine::saveProfile(const QJsonObject& profile, QString* error) const {
    const QString id = profile.value("id").toString();
    if (!safeRelativePath(id) || id.contains('/') || id.contains('\\') || id == "." || id == "..") { if (error) *error = "Oyun profili kimliği geçersiz."; return false; }
    const QString dir = QDir(instancesDir_).filePath(id);
    QDir().mkpath(dir);
    QJsonObject clean = profile; clean.remove("directory"); clean.remove("profile_root");
    QSaveFile out(QDir(dir).filePath("instance.json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(clean).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) { if (error) *error = out.errorString(); return false; }
    return true;
}
QJsonObject LauncherEngine::createInstance(const QString& name, const QString& minecraftVersion,
                                            const QString& loader, const QString& loaderVersion,
                                            const QString& launchVersion, QString* error) const {
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || minecraftVersion.trimmed().isEmpty() || launchVersion.trimmed().isEmpty()) {
        if (error) *error = "Profil adı, Minecraft sürümü ve kurulu oyun sürümü gereklidir.";
        return {};
    }
    QString id = safeFolderName(cleanName).left(48);
    if (id.isEmpty()) id = "instance";
    if (id == "default") id += "-instance";
    const QString baseId = id;
    int suffix = 2;
    while (QFileInfo::exists(QDir(instancesDir_).filePath(id))) id = baseId + "-" + QString::number(suffix++);
    const QString root = QDir(instancesDir_).filePath(id);
    const QString game = QDir(root).filePath("game");
    if (!QDir().mkpath(game)) { if (error) *error = "Profil klasörü oluşturulamadı."; return {}; }
    QJsonObject profile{{"id", id}, {"name", cleanName}, {"kind", "instance"},
        {"minecraft_version", minecraftVersion}, {"loader", loader}, {"loader_version", loaderVersion},
        {"launch_version", launchVersion}, {"game_directory", game}};
    if (!saveProfile(profile, error)) { QDir(root).removeRecursively(); return {}; }
    profile.insert("profile_root", root); profile.insert("directory", game);
    return profile;
}
bool LauncherEngine::renameProfile(const QString& id, const QString& newName, QString* error) const {
    if (id == "default") { if (error) *error = "Varsayılan profilin adı değiştirilemez."; return false; }
    const QString name = newName.trimmed();
    if (name.isEmpty()) { if (error) *error = "Profil adı boş bırakılamaz."; return false; }
    for (const auto& value : readProfiles()) {
        QJsonObject profile = value.toObject();
        if (profile.value("id").toString() != id) continue;
        profile.insert("name", name);
        return saveProfile(profile, error);
    }
    if (error) *error = "Profil bulunamadı.";
    return false;
}
QJsonObject LauncherEngine::cloneProfile(const QString& id, const QString& newName, QString* error) const {
    if (id == "default") { if (error) *error = "Varsayılan profil doğrudan kopyalanamaz; önce yeni profil oluştur."; return {}; }
    QJsonObject source;
    for (const auto& value : readProfiles()) if (value.toObject().value("id").toString() == id) { source = value.toObject(); break; }
    if (source.isEmpty()) { if (error) *error = "Kopyalanacak profil bulunamadı."; return {}; }
    const QString name = newName.trimmed();
    if (name.isEmpty()) { if (error) *error = "Yeni profil adı boş bırakılamaz."; return {}; }
    const QString sourceRoot = source.value("profile_root").toString();
    QString newId = safeFolderName(name).left(48);
    if (newId.isEmpty()) newId = "instance-copy";
    const QString baseId = newId;
    int suffix = 2;
    while (QFileInfo::exists(QDir(instancesDir_).filePath(newId))) newId = baseId + "-" + QString::number(suffix++);
    const QString destRoot = QDir(instancesDir_).filePath(newId);
    if (!copyDirectoryTree(sourceRoot, destRoot)) { QDir(destRoot).removeRecursively(); if (error) *error = "Profil dosyaları kopyalanamadı."; return {}; }
    source.insert("id", newId); source.insert("name", name);
    if (source.contains("game_directory")) source.insert("game_directory", QDir(destRoot).filePath("game"));
    if (!saveProfile(source, error)) { QDir(destRoot).removeRecursively(); return {}; }
    source.insert("profile_root", destRoot);
    source.insert("directory", source.value("game_directory").toString(destRoot));
    return source;
}
bool LauncherEngine::deleteProfile(const QString& id, QString* error) const {
    if (id == "default") { if (error) *error = "Varsayılan oyun klasörü silinemez."; return false; }
    QString root;
    for (const auto& value : readProfiles()) if (value.toObject().value("id").toString() == id) { root = value.toObject().value("profile_root").toString(); break; }
    if (root.isEmpty()) { if (error) *error = "Silinecek profil bulunamadı."; return false; }
    const QString absoluteRoot = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString parent = QDir::cleanPath(QFileInfo(instancesDir_).absoluteFilePath());
    const QString relative = QDir(parent).relativeFilePath(absoluteRoot);
    if (!safeRelativePath(relative) || relative.contains('/') || relative.contains('\\')) { if (error) *error = "Profil yolu güvenlik denetiminden geçemedi."; return false; }
    if (!QDir(absoluteRoot).removeRecursively()) { if (error) *error = "Profil klasörü silinemedi."; return false; }
    return true;
}
bool LauncherEngine::exportProfile(const QString& id, const QString& destinationZip, QString* error) const {
    if (id == "default") { if (error) *error = "Kimlik bilgileri içerebilen varsayılan .minecraft klasörü profil dışa aktarımına dahil edilmez."; return false; }
    QString root;
    for (const auto& value : readProfiles()) if (value.toObject().value("id").toString() == id) { root = value.toObject().value("profile_root").toString(); break; }
    if (root.isEmpty()) { if (error) *error = "Dışa aktarılacak profil bulunamadı."; return false; }
    return ZipArchive::createFromDirectory(root, destinationZip, error);
}
QJsonObject LauncherEngine::importModpack(const QString& archivePath, const QString& curseForgeKey,
                                           const QString& javaPath, Progress progress) {
    ZipArchive zip; QString why;
    if (!zip.open(archivePath, &why)) throw std::runtime_error(why.toStdString());
    QJsonObject hit{{"title", QFileInfo(archivePath).completeBaseName()},
                    {"project_id", "import-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)}};
    const auto names = zip.entries();
    bool modrinth = false, curseForge = false;
    for (const ZipEntry& entry : names) {
        modrinth |= entry.name == "modrinth.index.json";
        curseForge |= entry.name == "manifest.json";
    }
    if (modrinth) return installModrinthPack(archivePath, hit, javaPath, progress);
    if (curseForge) return installCurseForgePack(archivePath, hit, curseForgeKey, javaPath, progress);
    throw std::runtime_error("Bu arşiv tanınan bir Modrinth veya CurseForge modpack'i değil.");
}
QStringList LauncherEngine::worldNames(const QString& gameDirectory) const {
    QStringList result;
    const QDir saves(QDir(gameDirectory).filePath("saves"));
    for (const QString& name : saves.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (QFileInfo::exists(saves.filePath(name + "/level.dat"))) result << name;
    return result;
}
QJsonObject LauncherEngine::activeProfile(const QJsonObject& config) const {
    const QString id = config.value("active_instance").toString("default");
    for (const auto& value : readProfiles()) if (value.toObject().value("id").toString() == id) return value.toObject();
    return readProfiles().first().toObject();
}
QString LauncherEngine::activeGameDirectory(const QJsonObject& config) const { return activeProfile(config).value("directory").toString(mcDir_); }

QJsonArray LauncherEngine::localVersions() const {
    QJsonArray out; QDirIterator it(versionsDir_, QDir::Dirs | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        const QString dir = it.next(), id = QFileInfo(dir).fileName();
        const bool hasJson = QFileInfo::exists(QDir(dir).filePath(id + ".json"));
        const bool hasJar = QFileInfo::exists(QDir(dir).filePath(id + ".jar"));
        if (hasJson && hasJar) out.append(id);
        else {
            const QStringList names = QDir(dir).entryList({"*.json", "*.jar"}, QDir::Files);
            bool j = false, a = false; for (const auto& n : names) { j |= n.endsWith(".json"); a |= n.endsWith(".jar"); }
            if (j && a) out.append(id);
        }
    }
    QStringList versions = arrayToStrings(out);
    std::sort(versions.begin(), versions.end(), [](const QString& a, const QString& b) {
        const auto ka = versionSortKey(a), kb = versionSortKey(b);
        if (ka != kb) return std::lexicographical_compare(kb.cbegin(), kb.cend(), ka.cbegin(), ka.cend());
        return a > b;
    });
    out = {}; for (const auto& v : versions) out.append(v);
    return out;
}
QJsonArray LauncherEngine::remoteVersions(bool snapshots, Progress progress) const {
    QJsonObject manifest;
    try {
        manifest = getJson("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        QSaveFile cache(versionsCachePath_);
        if (cache.open(QIODevice::WriteOnly)) {
            cache.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
            cache.commit();
        }
    } catch (...) {
        QFile cache(versionsCachePath_);
        if (!cache.open(QIODevice::ReadOnly)) throw;
        manifest = QJsonDocument::fromJson(cache.readAll()).object();
        if (manifest.value("versions").toArray().isEmpty()) throw;
        if (progress) progress("Ağ erişilemiyor; önbellekteki sürüm listesi kullanılıyor", 85);
    }
    QJsonArray result;
    for (const auto& value : manifest.value("versions").toArray()) {
        const QJsonObject v = value.toObject();
        if (v.value("type").toString() == "release" || (snapshots && v.value("type").toString() == "snapshot")) result.append(v.value("id"));
    }
    if (progress) progress("Sürüm listesi hazır", 100);
    return result;
}

QString LauncherEngine::platformName() {
#ifdef Q_OS_WIN
    return "windows";
#elif defined(Q_OS_MACOS)
    return "osx";
#else
    return "linux";
#endif
}
bool LauncherEngine::rulesAllow(const QJsonArray& rules, const QString& osName) {
    if (rules.isEmpty()) return true;
    bool allowed = false;
    for (const auto& value : rules) {
        const QJsonObject rule = value.toObject();
        const QJsonObject os = rule.value("os").toObject();
        if (!os.value("name").toString().isEmpty() && os.value("name").toString() != osName) continue;
        bool featuresMatch = true;
        const QJsonObject features = rule.value("features").toObject();
        for (auto it = features.begin(); it != features.end(); ++it) {
            // Launcher context here is a normal desktop launch: no demo or quick-play flags.
            const bool actual = false;
            if (it.value().toBool() != actual) { featuresMatch = false; break; }
        }
        if (!featuresMatch) continue;
        allowed = rule.value("action").toString() == "allow";
    }
    return allowed;
}
QStringList LauncherEngine::splitCommandLine(const QString& input) {
    QStringList out; QString token; QChar quote; bool escape = false, active = false;
    for (QChar ch : input) {
        if (escape) { token += ch; escape = false; active = true; continue; }
        if (ch == '\\' && quote != '\'') { escape = true; continue; }
        if (!quote.isNull()) { if (ch == quote) quote = {}; else token += ch; active = true; continue; }
        if (ch == '\'' || ch == '"') { quote = ch; active = true; continue; }
        if (ch.isSpace()) { if (active) { out << token; token.clear(); active = false; } continue; }
        token += ch; active = true;
    }
    if (escape) token += '\\';
    if (active) out << token;
    return out;
}
bool LauncherEngine::safeRelativePath(const QString& raw) {
    QString path = raw; path.replace('\\', '/');
    if (path.isEmpty() || path.startsWith('/') || path.startsWith("//") || QRegularExpression("^[A-Za-z]:").match(path).hasMatch()) return false;
    for (const QString& part : path.split('/', Qt::KeepEmptyParts)) if (part.isEmpty() || part == "..") return false;
    return true;
}
QString LauncherEngine::safeFolderName(const QString& name) {
    QString v = name; v.replace(QRegularExpression("[^A-Za-z0-9._-]+"), "-");
    while (v.startsWith('.') || v.startsWith('-') || v.startsWith('_')) v.remove(0, 1);
    while (v.endsWith('.') || v.endsWith('-') || v.endsWith('_')) v.chop(1);
    if (v.isEmpty()) v = "modpack";
    return v.left(48);
}
QJsonObject LauncherEngine::versionJson(const QString& versionId) const {
    const QString path = QDir(versionsDir_).filePath(versionId + "/" + versionId + ".json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(QString("Sürüm JSON bulunamadı: %1").arg(versionId).toStdString());
    const QJsonObject json = QJsonDocument::fromJson(f.readAll()).object();
    if (json.isEmpty()) throw std::runtime_error("Sürüm JSON dosyası bozuk.");
    return json;
}
QString LauncherEngine::installVanilla(const QString& mcVersion, Progress progress) const {
    const QString dir = QDir(versionsDir_).filePath(mcVersion);
    QDir().mkpath(dir);
    QJsonObject version;
    const QString jsonPath = QDir(dir).filePath(mcVersion + ".json");
    const QString jarPath = QDir(dir).filePath(mcVersion + ".jar");
    QFile cached(jsonPath);
    if (cached.open(QIODevice::ReadOnly) && QFileInfo::exists(jarPath))
        version = QJsonDocument::fromJson(cached.readAll()).object();
    if (version.isEmpty()) {
        const QJsonObject manifest = getJson("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
        QJsonObject entry;
        for (const auto& v : manifest.value("versions").toArray()) if (v.toObject().value("id").toString() == mcVersion) { entry = v.toObject(); break; }
        if (entry.isEmpty()) throw std::runtime_error(QString("Minecraft sürümü bulunamadı: %1").arg(mcVersion).toStdString());
        if (progress) progress(QString("%1 sürüm bilgisi indiriliyor").arg(mcVersion), 3);
        version = getJson(entry.value("url").toString());
        QSaveFile jsonOut(jsonPath);
        if (!jsonOut.open(QIODevice::WriteOnly) || jsonOut.write(QJsonDocument(version).toJson()) < 0 || !jsonOut.commit())
            throw std::runtime_error("Sürüm JSON kaydedilemedi.");
    }
    const QJsonObject client = version.value("downloads").toObject().value("client").toObject();
    if (client.isEmpty()) throw std::runtime_error("Minecraft istemci indirme bağlantısı bulunamadı.");
    if (!QFileInfo::exists(jarPath)) {
        if (progress) progress("Minecraft istemci JAR indiriliyor…", 5);
        QJsonObject hashes; if (client.contains("sha1")) hashes.insert("sha1", client.value("sha1"));
        downloadUrl(client.value("url").toString(), jarPath, hashes, progress);
    }
    installLibraries(version, progress);
    const QJsonObject assetIndex = version.value("assetIndex").toObject();
    if (!assetIndex.isEmpty()) {
        const QString indexId = assetIndex.value("id").toString();
        const QString indexPath = QDir(mcDir_).filePath("assets/indexes/" + indexId + ".json");
        QDir().mkpath(QFileInfo(indexPath).absolutePath());
        if (!QFileInfo::exists(indexPath))
            downloadUrl(assetIndex.value("url").toString(), indexPath, QJsonObject{{"sha1", assetIndex.value("sha1")}}, {});
        QFile af(indexPath);
        if (!af.open(QIODevice::ReadOnly)) throw std::runtime_error("Minecraft asset index açılamadı.");
        const QJsonObject objects = QJsonDocument::fromJson(af.readAll()).object().value("objects").toObject();

        // Sadece eksik olanları topla
        QVector<DownloadItem> missing;
        missing.reserve(objects.size());
        for (auto it = objects.begin(); it != objects.end(); ++it) {
            const QString hash = it.value().toObject().value("hash").toString();
            if (hash.size() < 2) continue;
            const QString objectPath = QDir(mcDir_).filePath("assets/objects/" + hash.left(2) + "/" + hash);
            if (!QFileInfo::exists(objectPath))
                missing.append({
                    "https://resources.download.minecraft.net/" + hash.left(2) + "/" + hash,
                    objectPath,
                    hash
                });
        }

        if (!missing.isEmpty()) {
            if (progress) progress(QString("Oyun varlıkları indiriliyor (0/%1)").arg(missing.size()), 15);
            downloadParallel(missing, 16, progress,
                             "Oyun varlıkları indiriliyor", 15, 30);
        }
    }
    if (progress) progress("Minecraft temel sürümü hazır", 50);
    return mcVersion;
}
void LauncherEngine::installLibraries(const QJsonObject& json, Progress progress) const {
    QStringList chain;
    QJsonObject current = json;
    for (int i = 0; i < 8; ++i) {
        const QString id = current.value("id").toString();
        if (id.isEmpty() || chain.contains(id)) break;
        chain.prepend(id);
        const QString parent = current.value("inheritsFrom").toString();
        if (parent.isEmpty()) break;
        current = versionJson(parent);
    }

    // İlk geçiş: tüm kütüphane indirme görevlerini topla
    QVector<DownloadItem> libItems;
    // Natives ZIP'lerin çıkarılması için ayrı bir liste tutuyoruz
    struct NativeEntry { QString zipPath; QString nativeDir; };
    QVector<NativeEntry> nativeZips;

    for (const QString& id : chain) {
        const QJsonObject v = versionJson(id);
        const QString nativeDir = QDir(versionsDir_).filePath(id + "/" + id + "-natives");
        QDir().mkpath(nativeDir);
        const QJsonArray libraries = v.value("libraries").toArray();
        for (const auto& entry : libraries) {
            const QJsonObject lib = entry.toObject();
            if (!rulesAllow(lib.value("rules").toArray(), platformName())) continue;
            QJsonObject downloads = lib.value("downloads").toObject();
            QJsonObject artifact = downloads.value("artifact").toObject();
            if (!artifact.isEmpty()) {
                const QString rel = artifact.value("path").toString();
                const QString url = artifact.value("url").toString();
                if (!rel.isEmpty() && !url.isEmpty()) {
                    const QString path = QDir(mcDir_).filePath("libraries/" + rel);
                    if (!QFileInfo::exists(path))
                        libItems.append({ url, path, artifact.value("sha1").toString() });
                }
            }
#ifdef Q_OS_WIN
            const QString nativeKey = lib.value("natives").toObject().value("windows").toString().replace("${arch}", "64");
#else
            const QString nativeKey = lib.value("natives").toObject().value(platformName()).toString().replace("${arch}", "64");
#endif
            if (!nativeKey.isEmpty()) {
                const QJsonObject classifier = downloads.value("classifiers").toObject().value(nativeKey).toObject();
                if (!classifier.isEmpty()) {
                    const QString rel = classifier.value("path").toString();
                    const QString path = QDir(mcDir_).filePath("libraries/" + rel);
                    if (!QFileInfo::exists(path))
                        libItems.append({ classifier.value("url").toString(), path, classifier.value("sha1").toString() });
                    nativeZips.append({ path, nativeDir });
                }
            }
            // Older version manifests only contain Maven coordinates and repository URL.
            if (artifact.isEmpty() && lib.contains("name")) {
                QStringList parts = lib.value("name").toString().split(':');
                if (parts.size() >= 3) {
                    const QString group = parts[0].replace('.', '/');
                    QString rel = group + "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
                    if (parts.size() >= 4) rel += "-" + parts[3];
                    rel += ".jar";
                    const QString path = QDir(mcDir_).filePath("libraries/" + rel);
                    const QString repo = lib.value("url").toString("https://libraries.minecraft.net/");
                    if (!QFileInfo::exists(path)) libItems.append({ repo + rel, path, {} });
                }
            }
        }
    }

    // Paralel indirme
    if (!libItems.isEmpty()) {
        if (progress) progress(QString("Kütüphaneler indiriliyor (%1 dosya)…").arg(libItems.size()), 50);
        downloadParallel(libItems, 8, progress, "Kütüphaneler indiriliyor", 50, 15);
    }

    // Native ZIP'leri çıkar (sıralı — ZIP çıkarma thread-safe değil)
    for (const NativeEntry& ne : nativeZips) {
        ZipArchive zip; QString why;
        if (!zip.open(ne.zipPath, &why)) continue;
        for (const ZipEntry& ze : zip.entries()) {
            if (ze.name.endsWith('/') || ze.name.startsWith("META-INF/")) continue;
            const QString target = QDir(ne.nativeDir).filePath(ze.name);
            if (!QDir().mkpath(QFileInfo(target).absolutePath())) continue;
            QFile out(target); const QByteArray bytes = zip.read(ze.name, &why);
            if (why.isEmpty() && out.open(QIODevice::WriteOnly | QIODevice::Truncate)) out.write(bytes);
        }
    }

    if (progress) progress("Kütüphaneler hazır", 65);
}
QString LauncherEngine::installFabric(const QString& mcVersion, const QString& loaderVersion,
                                      const QString& javaPath, Progress progress) const {
    const QString existing = resolveVersionId(mcVersion, "Fabric", loaderVersion);
    if (!existing.isEmpty()) return existing;
    const QString base = installVanilla(mcVersion, progress);
    Q_UNUSED(javaPath);
    QString resolvedLoaderVersion = loaderVersion.trimmed();
    if (resolvedLoaderVersion.isEmpty() || resolvedLoaderVersion.compare("latest", Qt::CaseInsensitive) == 0) {
        const QString versionsUrl = "https://meta.fabricmc.net/v2/versions/loader/" + mcVersion;
        int versionsStatus = 0;
        const QByteArray versionsRaw = request(versionsUrl, "GET", {}, {}, 30000, &versionsStatus);
        if (versionsStatus < 200 || versionsStatus >= 300)
            throw std::runtime_error(QString("Fabric loader sürümleri alınamadı (HTTP %1). Minecraft %2 desteklenmiyor olabilir.")
                                     .arg(versionsStatus).arg(mcVersion).toStdString());

        const QJsonArray availableLoaders = QJsonDocument::fromJson(versionsRaw).array();
        QString firstAvailableVersion;
        for (const QJsonValue& value : availableLoaders) {
            const QJsonObject loader = value.toObject().value("loader").toObject();
            const QString candidate = loader.value("version").toString().trimmed();
            if (candidate.isEmpty()) continue;
            if (firstAvailableVersion.isEmpty()) firstAvailableVersion = candidate;
            if (loader.value("stable").toBool()) {
                resolvedLoaderVersion = candidate;
                break;
            }
        }
        if (resolvedLoaderVersion.isEmpty()) resolvedLoaderVersion = firstAvailableVersion;
        if (resolvedLoaderVersion.isEmpty())
            throw std::runtime_error(QString("Minecraft %1 için uyumlu Fabric loader sürümü bulunamadı.")
                                     .arg(mcVersion).toStdString());
    }

    const QString url = "https://meta.fabricmc.net/v2/versions/loader/" + mcVersion + "/" +
                        resolvedLoaderVersion + "/profile/json";
    int status = 0; const QByteArray raw = request(url, "GET", {}, {}, 30000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Fabric profili alınamadı (HTTP %1). Sürüm veya loader uyumsuz olabilir.").arg(status).toStdString());
    QJsonObject profile = QJsonDocument::fromJson(raw).object();
    if (profile.isEmpty()) throw std::runtime_error("Fabric profil JSON'u bozuk.");
    profile.insert("inheritsFrom", base);
    QString id = profile.value("id").toString();
    if (id.isEmpty()) id = "fabric-loader-" + resolvedLoaderVersion + "-" + mcVersion;
    profile.insert("id", id);
    const QString dir = QDir(versionsDir_).filePath(id); QDir().mkpath(dir);
    QSaveFile out(QDir(dir).filePath(id + ".json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(profile).toJson(QJsonDocument::Indented)) < 0 || !out.commit()) throw std::runtime_error("Fabric profili kaydedilemedi.");
    installLibraries(profile, progress);
    if (progress) progress("Fabric kuruldu", 100);
    return id;
}
QString LauncherEngine::installQuilt(const QString& mcVersion, const QString& loaderVersion,
                                     const QString& javaPath, Progress progress) const {
    const QString existing = resolveVersionId(mcVersion, "Quilt", loaderVersion);
    if (!existing.isEmpty()) return existing;
    const QString base = installVanilla(mcVersion, progress);
    Q_UNUSED(javaPath);
    QString url = "https://meta.quiltmc.org/v3/versions/loader/" + mcVersion;
    if (!loaderVersion.isEmpty()) url += "/" + loaderVersion;
    url += "/profile/json";
    int status = 0;
    const QByteArray raw = request(url, "GET", {}, {}, 30000, &status);
    if (status < 200 || status >= 300)
        throw std::runtime_error(QString("Quilt profili alınamadı (HTTP %1). Sürüm veya loader uyumsuz olabilir.").arg(status).toStdString());
    QJsonObject profile = QJsonDocument::fromJson(raw).object();
    if (profile.isEmpty()) throw std::runtime_error("Quilt profil JSON'u bozuk.");
    profile.insert("inheritsFrom", base);
    QString id = profile.value("id").toString();
    if (id.isEmpty()) id = "quilt-loader-" + (loaderVersion.isEmpty() ? "latest" : loaderVersion) + "-" + mcVersion;
    profile.insert("id", id);
    const QString dir = QDir(versionsDir_).filePath(id);
    QDir().mkpath(dir);
    QSaveFile out(QDir(dir).filePath(id + ".json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(profile).toJson(QJsonDocument::Indented)) < 0 || !out.commit())
        throw std::runtime_error("Quilt profili kaydedilemedi.");
    installLibraries(profile, progress);
    if (progress) progress("Quilt kuruldu", 100);
    return id;
}
QString LauncherEngine::installForgeLike(const QString& mcVersion, const QString& loader,
                                         const QString& loaderVersion, const QString& javaPath,
                                         Progress progress) const {
    const QString already = resolveVersionId(mcVersion, loader, loaderVersion);
    if (!already.isEmpty()) return already;
    installVanilla(mcVersion, progress);
    QString chosen = loaderVersion.trimmed();
    QString installerUrl;
    QString expectedId;
    if (loader.compare("Forge", Qt::CaseInsensitive) == 0) {
        if (chosen.startsWith(mcVersion + "-")) chosen = chosen.mid(mcVersion.size() + 1);
        if (chosen.isEmpty()) {
            const QJsonObject promos = getJson("https://files.minecraftforge.net/net/minecraftforge/forge/promotions_slim.json").value("promos").toObject();
            chosen = promos.value(mcVersion + "-recommended").toString();
            if (chosen.isEmpty()) chosen = promos.value(mcVersion + "-latest").toString();
        }
        if (chosen.isEmpty()) throw std::runtime_error(QString("Minecraft %1 için Forge sürümü bulunamadı.").arg(mcVersion).toStdString());
        const QString forgeId = mcVersion + "-" + chosen;
        expectedId = forgeId;
        installerUrl = QString("https://maven.minecraftforge.net/net/minecraftforge/forge/%1/forge-%1-installer.jar").arg(forgeId);
    } else {
        if (chosen.isEmpty()) {
            const QString metaUrl = "https://maven.neoforged.net/releases/net/neoforged/neoforge/maven-metadata.xml";
            int status = 0; const QByteArray xml = request(metaUrl, "GET", {}, {}, 30000, &status);
            if (status < 200 || status >= 300) throw std::runtime_error("NeoForge sürüm listesi alınamadı.");
            QXmlStreamReader reader(xml); QStringList versions;
            while (!reader.atEnd()) { reader.readNext(); if (reader.isStartElement() && reader.name() == "version") versions << reader.readElementText(); }
            const QString key = mcVersion.mid(2).section('.', 0, 1) + "." + mcVersion.mid(2).section('.', 1, 1) + ".";
            for (auto it = versions.crbegin(); it != versions.crend(); ++it) if (it->startsWith(key)) { chosen = *it; break; }
        }
        if (chosen.isEmpty()) throw std::runtime_error(QString("Minecraft %1 için NeoForge bulunamadı.").arg(mcVersion).toStdString());
        expectedId = "neoforge-" + chosen;
        installerUrl = QString("https://maven.neoforged.net/releases/net/neoforged/neoforge/%1/neoforge-%1-installer.jar").arg(chosen);
    }
    const QString installer = QDir(cacheDir_).filePath(loader.toLower() + "-installer-" + chosen + ".jar");
    if (!QFileInfo::exists(installer)) downloadUrl(installerUrl, installer, {}, progress);
    QString java = ensureJavaForVersion(mcVersion, javaPath, progress);
#ifdef Q_OS_WIN
    if (java.endsWith("javaw.exe", Qt::CaseInsensitive)) {
        const QString consoleJava = java.left(java.size() - QString("javaw.exe").size()) + "java.exe";
        if (QFileInfo::exists(consoleJava)) java = consoleJava;
    }
#endif
    QProcess proc; proc.setProgram(java); proc.setArguments({"-jar", installer, "--installClient", mcDir_}); proc.setWorkingDirectory(mcDir_);
    proc.start();
    if (!proc.waitForStarted(15000)) throw std::runtime_error(QString("%1 yükleyicisi başlatılamadı: %2").arg(loader, proc.errorString()).toStdString());
    if (progress) progress(loader + " kuruluyor", 85);
    if (!proc.waitForFinished(20 * 60 * 1000)) { proc.kill(); throw std::runtime_error(QString("%1 kurulumu zaman aşımına uğradı.").arg(loader).toStdString()); }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) throw std::runtime_error(QString("%1 yükleyicisi hata verdi: %2").arg(loader, QString::fromLocal8Bit(proc.readAllStandardError() + proc.readAllStandardOutput())).toStdString());
    const QString installed = resolveVersionId(mcVersion, loader, chosen);
    if (installed.isEmpty()) throw std::runtime_error(QString("%1 kuruldu ancak oyun profili bulunamadı.").arg(loader).toStdString());
    if (progress) progress(loader + " kuruldu", 100);
    Q_UNUSED(expectedId);
    return installed;
}
QString LauncherEngine::installVersion(const QString& mcVersion, const QString& loader,
                                       const QString& loaderVersion, const QString& javaPath,
                                       Progress progress) const {
    if (mcVersion.trimmed().isEmpty() || isVersionPlaceholder(mcVersion)) throw std::runtime_error("Geçerli Minecraft sürümü seçilmedi.");
    const QString normalized = loader.trimmed().toLower();
    if (normalized == "vanilla") return installVanilla(mcVersion, progress);
    if (normalized == "fabric") return installFabric(mcVersion, loaderVersion, javaPath, progress);
    if (normalized == "quilt") return installQuilt(mcVersion, loaderVersion, javaPath, progress);
    if (normalized == "forge" || normalized == "neoforge") return installForgeLike(mcVersion, normalized == "forge" ? "Forge" : "NeoForge", loaderVersion, javaPath, progress);
    throw std::runtime_error(QString("Desteklenmeyen loader: %1").arg(loader).toStdString());
}
QString LauncherEngine::resolveVersionId(const QString& mcVersion, const QString& loader, const QString& loaderVersion) const {
    QStringList candidates = arrayToStrings(localVersions());
    if (loader.compare("Fabric", Qt::CaseInsensitive) == 0 || loader.compare("Quilt", Qt::CaseInsensitive) == 0) {
        const QString prefix = loader.compare("Quilt", Qt::CaseInsensitive) == 0 ? "quilt-loader-" : "fabric-loader-";
        const QString exact = prefix + loaderVersion + "-" + mcVersion;
        if (candidates.contains(exact)) return exact;
        for (const auto& id : candidates) if (id.startsWith(prefix) && id.endsWith("-" + mcVersion)) return id;
    } else {
        const QString name = loader.toLower();
        for (const auto& id : candidates) {
            if (!id.contains(name, Qt::CaseInsensitive) || !id.contains(mcVersion)) continue;
            if (!loaderVersion.isEmpty() && id.endsWith("-" + loaderVersion)) return id;
        }
        for (auto it = candidates.crbegin(); it != candidates.crend(); ++it) if (it->contains(name, Qt::CaseInsensitive) && it->contains(mcVersion)) return *it;
    }
    return {};
}
int LauncherEngine::javaMajor(const QString& executable) {
    if (executable.isEmpty()) return 0;
    QString probe = executable;
#ifdef Q_OS_WIN
    if (probe.endsWith("javaw.exe", Qt::CaseInsensitive)) {
        const QString java = probe.left(probe.size() - QString("javaw.exe").size()) + "java.exe";
        if (QFileInfo::exists(java)) probe = java;
    }
#endif
    QProcess p; p.start(probe, {"-version"});
    if (!p.waitForStarted(3000) || !p.waitForFinished(5000)) { p.kill(); return 0; }
    const QString text = QString::fromLocal8Bit(p.readAllStandardError() + p.readAllStandardOutput());
    const auto match = QRegularExpression("version\\s+\\\"?(\\d+)(?:\\.(\\d+))?").match(text);
    if (!match.hasMatch()) return 0;
    const int first = match.captured(1).toInt();
    return first == 1 ? match.captured(2).toInt() : first;
}
QString LauncherEngine::offlineUuid(const QString& username) {
    const QByteArray name = ("OfflinePlayer:" + username).toUtf8();
    const QUuid dnsNamespace = QUuid::fromString(QStringLiteral("{6ba7b810-9dad-11d1-80b4-00c04fd430c8}"));
    return QUuid::createUuidV3(dnsNamespace, name).toString(QUuid::WithoutBraces);
}
QString LauncherEngine::resolveJava(const QString& minecraftVersion, const QString& customPath) const {
    if (!customPath.trimmed().isEmpty() && QFileInfo(customPath.trimmed()).isFile()) return customPath.trimmed();
    const int required = requiredJavaMajor(minecraftVersion);
    QStringList candidates;
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    QStringList runtimeRoots;
    if (!appData.isEmpty()) runtimeRoots << QDir(appData).filePath(".minecraft/runtime");
    runtimeRoots << QDir(mcDir_).filePath("runtime");
    runtimeRoots << runtimesDir_;
    for (const QString& root : runtimeRoots) {
        if (!QFileInfo::exists(root)) continue;
        QDirIterator it(root, {"java.exe", "javaw.exe"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) candidates << it.next();
    }
    for (const QString& base : {qEnvironmentVariable("ProgramFiles", "C:/Program Files"), qEnvironmentVariable("ProgramFiles(x86)", "C:/Program Files (x86)")}) {
        for (const QString& vendor : {"Java", "Eclipse Adoptium", "BellSoft", "Microsoft", "Zulu", "Semeru"}) {
            const QString dir = QDir(base).filePath(vendor);
            if (!QFileInfo::exists(dir)) continue;
            QDirIterator it(dir, {"javaw.exe", "java.exe"}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) candidates << it.next();
        }
    }
    const QString exe = "java.exe";
#else
    const QString exe = "java";
    candidates << QDir::homePath() + "/.minecraft/runtime";
#endif
    for (const QString& path : {QStandardPaths::findExecutable(exe), QStandardPaths::findExecutable("java")}) if (!path.isEmpty()) candidates << path;
    struct Candidate { int major; QString path; };
    QList<Candidate> found;
    for (const QString& item : candidates) {
        QFileInfo info(item);
        if (info.isDir()) continue;
        const QString path = info.absoluteFilePath();
        const int major = javaMajor(path);
        if (major > 0) found.append({major, path});
    }
    std::sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.major < b.major; });
    auto launchPath = [](QString path) {
#ifdef Q_OS_WIN
        if (path.endsWith("java.exe", Qt::CaseInsensitive)) {
            const QString javaw = path.left(path.size() - QString("java.exe").size()) + "javaw.exe";
            if (QFileInfo::exists(javaw)) return javaw;
        }
#endif
        return path;
    };
    for (const auto& c : found) if (c.major == required) return launchPath(c.path);
    for (const auto& c : found) if (c.major >= required) return launchPath(c.path);
#ifdef Q_OS_WIN
    return "javaw";
#else
    return "java";
#endif
}

int LauncherEngine::requiredJavaMajor(const QString& minecraftVersion) const {
    // Minecraft 26.3 and Horizon's prepare tool require the Java 25 toolchain.
    if (minecraftVersion == "26.3") return 25;
    QString current = minecraftVersion;
    QStringList visited;
    for (int i = 0; i < 8 && !current.isEmpty() && !visited.contains(current); ++i) {
        visited << current;
        try {
            const QJsonObject json = versionJson(current);
            const int major = json.value("javaVersion").toObject().value("majorVersion").toInt();
            if (major > 0) return major;
            current = json.value("inheritsFrom").toString();
        } catch (...) { break; }
    }
    const auto match = QRegularExpression("^1\\.(\\d+)(?:\\.(\\d+))?").match(minecraftVersion);
    if (match.hasMatch()) {
        const int minor = match.captured(1).toInt();
        const int patch = match.captured(2).toInt();
        if (minor >= 21 || (minor == 20 && patch >= 5)) return 21;
        if (minor >= 17) return 17;
        return 8;
    }
    return 21;
}

QString LauncherEngine::findBundledJava(int major) const {
    const QString root = QDir(runtimesDir_).filePath("temurin-" + QString::number(major));
    if (!QFileInfo::exists(root)) return {};
#ifdef Q_OS_WIN
    const QStringList patterns{"javaw.exe", "java.exe"};
#else
    const QStringList patterns{"java"};
#endif
    QDirIterator it(root, patterns, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (javaMajor(path) == major) {
#ifdef Q_OS_WIN
            if (path.endsWith("java.exe", Qt::CaseInsensitive)) {
                const QString javaw = path.left(path.size() - QString("java.exe").size()) + "javaw.exe";
                if (QFileInfo::exists(javaw)) return javaw;
            }
#endif
            return path;
        }
    }
    return {};
}

QString LauncherEngine::ensureJavaForVersion(const QString& minecraftVersion, const QString& customPath,
                                              Progress progress) const {
    const int required = requiredJavaMajor(minecraftVersion);
    const bool horizonJdkRequired = minecraftVersion == "26.3";
    const auto usableToolchain = [required, horizonJdkRequired](const QString& path) {
        if (path.isEmpty() || javaMajor(path) != required) return false;
        if (!horizonJdkRequired) return true;
#ifdef Q_OS_WIN
        const QString compiler = "javac.exe";
#else
        const QString compiler = "javac";
#endif
        return QFileInfo(QDir(QFileInfo(path).absolutePath()).filePath(compiler)).isFile();
    };
    if (!customPath.trimmed().isEmpty() && QFileInfo(customPath.trimmed()).isFile() && usableToolchain(customPath.trimmed()))
        return customPath.trimmed();
    const QString detected = resolveJava(minecraftVersion, {});
    if (usableToolchain(detected)) return detected;
    const QString cached = findBundledJava(required);
    if (usableToolchain(cached)) return cached;
#ifndef Q_OS_WIN
    throw std::runtime_error("Otomatik Temurin kurulumu şu anda Windows arşivleri için hazırlandı. Bu sistemde Java yolu seç veya kurulu bir Java kullan.");
#endif

#ifdef Q_OS_WIN
    const QString os = "windows";
#elif defined(Q_OS_MACOS)
    const QString os = "mac";
#else
    const QString os = "linux";
#endif
    const QString arch = QSysInfo::currentCpuArchitecture().contains("arm", Qt::CaseInsensitive) ? "aarch64" : "x64";
    QUrl api(QString("https://api.adoptium.net/v3/assets/latest/%1/hotspot").arg(required));
    QUrlQuery query;
    query.addQueryItem("architecture", arch);
    query.addQueryItem("image_type", "jdk");
    query.addQueryItem("os", os);
    query.addQueryItem("vendor", "eclipse");
    api.setQuery(query);
    if (progress) progress(QString("Java %1 için Adoptium Temurin paketi aranıyor…").arg(required), 5);
    int status = 0;
    const QByteArray metadataBytes = request(api.toString(QUrl::FullyEncoded), "GET", {},
        {{"Accept", "application/json"}}, 30000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Adoptium Java listesi alınamadı (HTTP %1).").arg(status).toStdString());
    const QJsonDocument metadataDoc = QJsonDocument::fromJson(metadataBytes);
    if (!metadataDoc.isArray() || metadataDoc.array().isEmpty()) throw std::runtime_error("Adoptium bu bilgisayar için uygun Temurin Java paketi döndürmedi.");
    const QJsonArray binaries = metadataDoc.array().first().toObject().value("binaries").toArray();
    if (binaries.isEmpty()) throw std::runtime_error("Adoptium bu bilgisayar için Java ikili paketi döndürmedi.");
    const QJsonObject binary = binaries.first().toObject();
    const QJsonObject package = binary.value("package").toObject();
    const QUrl packageUrl(package.value("link").toString());
    const QString checksum = package.value("checksum").toString().toLower();
    if (!packageUrl.isValid() || packageUrl.scheme() != "https" || packageUrl.host().compare("github.com", Qt::CaseInsensitive) != 0 ||
        !QRegularExpression("^/adoptium/temurin[0-9]+-binaries/releases/download/").match(packageUrl.path()).hasMatch() ||
        !QRegularExpression("^[0-9a-f]{64}$").match(checksum).hasMatch())
        throw std::runtime_error("Adoptium paketi güvenli kaynak veya SHA-256 doğrulamasından geçemedi.");

    const QString archive = QDir(cacheDir_).filePath(QString("temurin-%1-%2-%3.zip").arg(required).arg(os, arch));
    if (progress) progress(QString("Adoptium Temurin Java %1 indiriliyor…").arg(required), 10);
    downloadUrl(packageUrl.toString(), archive, {{"sha256", checksum}}, progress,
                {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"});
    const QString runtimeRoot = QDir(runtimesDir_).filePath("temurin-" + QString::number(required));
    QDir(runtimeRoot).removeRecursively();
    ZipArchive zip; QString why;
    if (!zip.open(archive, &why) || !zip.extractUnder(runtimeRoot, {}, &why)) {
        QFile::remove(archive); QDir(runtimeRoot).removeRecursively();
        throw std::runtime_error((why.isEmpty() ? QString("Temurin arşivi açılamadı.") : why).toStdString());
    }
    QFile::remove(archive);
    const QString installed = findBundledJava(required);
    if (installed.isEmpty()) {
        QDir(runtimeRoot).removeRecursively();
        throw std::runtime_error("Temurin arşivi doğrulandı ancak çalıştırılabilir Java bulunamadı.");
    }
    if (progress) progress(QString("Java %1 Temurin kuruldu").arg(required), 100);
    return installed;
}

LaunchSpec LauncherEngine::createLaunchSpec(const QString& targetVersion, const QString& username,
                                             const QString& uuid, const QString& accessToken,
                                             const QString& gameDirectory, const QJsonObject& config,
                                             Progress progress) const {
    QJsonObject leaf = versionJson(targetVersion);
    QStringList chainIds;
    QJsonObject cur = leaf;
    for (int i = 0; i < 8; ++i) {
        const QString id = cur.value("id").toString();
        if (id.isEmpty() || chainIds.contains(id)) break;
        chainIds.prepend(id);
        const QString parent = cur.value("inheritsFrom").toString();
        if (parent.isEmpty()) break;
        cur = versionJson(parent);
    }
    installLibraries(leaf, progress);

    QString clientJar;
    QString assetIndex;
    QString mainClass = leaf.value("mainClass").toString();
    QStringList classPath;
    for (const QString& id : chainIds) {
        const QJsonObject v = versionJson(id);
        if (v.value("mainClass").isString() && (mainClass.isEmpty() || id == targetVersion)) mainClass = v.value("mainClass").toString();
        const QJsonObject client = v.value("downloads").toObject().value("client").toObject();
        if (!client.isEmpty()) clientJar = QDir(versionsDir_).filePath(id + "/" + id + ".jar");
        if (v.contains("assetIndex")) assetIndex = v.value("assetIndex").toObject().value("id").toString();
        for (const auto& libValue : v.value("libraries").toArray()) {
            const QJsonObject lib = libValue.toObject();
            if (!rulesAllow(lib.value("rules").toArray(), platformName())) continue;
            const QString path = lib.value("downloads").toObject().value("artifact").toObject().value("path").toString();
            if (!path.isEmpty()) classPath << QDir(mcDir_).filePath("libraries/" + path);
            else {
                const QString coordinate = lib.value("name").toString();
                const QStringList parts = coordinate.split(':');
                if (parts.size() >= 3) {
                    QString rel = parts[0]; rel.replace('.', '/'); rel += "/" + parts[1] + "/" + parts[2] + "/" + parts[1] + "-" + parts[2];
                    if (parts.size() > 3) rel += "-" + parts[3];
                    classPath << QDir(mcDir_).filePath("libraries/" + rel + ".jar");
                }
            }
        }
    }
    if (clientJar.isEmpty()) clientJar = QDir(versionsDir_).filePath(targetVersion + "/" + targetVersion + ".jar");
    classPath << clientJar;
    if (mainClass.isEmpty()) throw std::runtime_error("Minecraft ana sınıfı sürüm bilgisinde bulunamadı.");
    const QString separator = platformName() == "windows" ? ";" : ":";
    const QString nativePath = QDir(versionsDir_).filePath(targetVersion + "/" + targetVersion + "-launcher-natives");
    QDir(nativePath).removeRecursively();
    if (!QDir().mkpath(nativePath)) throw std::runtime_error("Minecraft yerel kitaplık klasörü oluşturulamadı.");
    for (const QString& id : chainIds) {
        const QString sourceDir = QDir(versionsDir_).filePath(id + "/" + id + "-natives");
        if (!QFileInfo(sourceDir).isDir()) continue;
        QDirIterator files(sourceDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString sourceFile = files.next();
            const QString relativePath = QDir(sourceDir).relativeFilePath(sourceFile);
            if (!safeRelativePath(relativePath)) continue;
            const QString destination = QDir(nativePath).filePath(relativePath);
            if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
                throw std::runtime_error("Minecraft yerel kitaplık klasörü hazırlanamadı.");
            if (QFileInfo::exists(destination)) QFile::remove(destination);
            if (!QFile::copy(sourceFile, destination))
                throw std::runtime_error(QString("Minecraft yerel kitaplığı birleştirilemedi: %1").arg(QFileInfo(sourceFile).fileName()).toStdString());
        }
    }
    const QString cp = classPath.join(separator);
    const QString assetRoot = QDir(mcDir_).filePath("assets");
    const QString versionType = leaf.value("type").toString("release");
    const QString userType = accessToken == "0" || accessToken.isEmpty() ? "legacy" : "msa";
    QString tokenUuid = uuid; tokenUuid.remove('-');
    QMap<QString, QString> values{
        {"${auth_player_name}", username}, {"${version_name}", targetVersion},
        {"${game_directory}", gameDirectory}, {"${assets_root}", assetRoot},
        {"${assets_index_name}", assetIndex}, {"${auth_uuid}", tokenUuid},
        {"${auth_access_token}", accessToken.isEmpty() ? "0" : accessToken},
        {"${user_type}", userType}, {"${version_type}", versionType},
        {"${natives_directory}", nativePath}, {"${library_directory}", QDir(mcDir_).filePath("libraries")},
        {"${classpath}", cp}, {"${classpath_separator}", separator},
        {"${launcher_name}", "TuketiciLauncher"}, {"${launcher_version}", "1.0"},
        {"${user_properties}", "{}"}, {"${clientid}", ""}, {"${auth_xuid}", ""},
        {"${resolution_width}", config.value("res_w").toString("854")},
        {"${resolution_height}", config.value("res_h").toString("480")}
    };
    auto substitute = [&](QString s) { for (auto it = values.cbegin(); it != values.cend(); ++it) s.replace(it.key(), it.value()); return s; };
    auto argumentsFromArray = [&](const QJsonArray& array, QStringList& out) {
        for (const auto& item : array) {
            if (item.isString()) { out << substitute(item.toString()); continue; }
            const QJsonObject obj = item.toObject();
            if (!rulesAllow(obj.value("rules").toArray(), platformName())) continue;
            const QJsonValue val = obj.value("value");
            if (val.isString()) out << substitute(val.toString());
            else for (const auto& sub : val.toArray()) if (sub.isString()) out << substitute(sub.toString());
        }
    };
    QStringList jvmArgs, gameArgs;
    for (const QString& id : chainIds) {
        const QJsonObject v = versionJson(id);
        const QJsonObject arguments = v.value("arguments").toObject();
        argumentsFromArray(arguments.value("jvm").toArray(), jvmArgs);
        argumentsFromArray(arguments.value("game").toArray(), gameArgs);
    }
    if (gameArgs.isEmpty() && leaf.contains("minecraftArguments")) gameArgs = splitCommandLine(substitute(leaf.value("minecraftArguments").toString()));
    const QString width = config.value("res_w").toString().trimmed();
    const QString height = config.value("res_h").toString().trimmed();
    if (!width.isEmpty() && !height.isEmpty() && width.toInt() > 0 && height.toInt() > 0) {
        if (!gameArgs.contains("--width")) gameArgs << "--width" << width;
        if (!gameArgs.contains("--height")) gameArgs << "--height" << height;
    }

    QString java = ensureJavaForVersion(targetVersion, config.value("custom_java").toString(), progress);
    const int javaVer = javaMajor(java);
    QStringList finalArgs;
    const int ram = qBound(1, config.value("ram").toInt(4), 64);
    finalArgs << QString("-Xmx%1G").arg(ram) << QString("-Xms%1G").arg(ram);
    const QString gc = config.value("gc_mode").toString();
    if (gc.contains("ZGC")) {
        if (javaVer >= 21) finalArgs << "-XX:+UseZGC" << "-XX:+ZGenerational" << "-XX:+AlwaysPreTouch" << "-XX:+DisableExplicitGC";
        else if (javaVer >= 11) finalArgs << "-XX:+UnlockExperimentalVMOptions" << "-XX:+UseZGC" << "-XX:+AlwaysPreTouch" << "-XX:+DisableExplicitGC";
        else finalArgs << "-XX:+UseG1GC" << "-XX:+AlwaysPreTouch" << "-XX:+DisableExplicitGC";
    } else if (gc.contains("Shenandoah") && javaVer >= 11) {
        finalArgs << "-XX:+UseShenandoahGC" << "-XX:+UnlockExperimentalVMOptions" << "-XX:ShenandoahGCHeuristics=adaptive" << "-XX:+AlwaysPreTouch" << "-XX:+DisableExplicitGC";
    } else if (gc.contains("Aikar")) {
        finalArgs << "-XX:+UseG1GC" << "-XX:+ParallelRefProcEnabled" << "-XX:MaxGCPauseMillis=200" << "-XX:+UnlockExperimentalVMOptions" << "-XX:+DisableExplicitGC" << "-XX:+AlwaysPreTouch" << "-XX:G1NewSizePercent=30" << "-XX:G1MaxNewSizePercent=40" << "-XX:G1ReservePercent=20" << "-XX:G1HeapWastePercent=5" << "-XX:G1MixedGCCountTarget=4" << "-XX:InitiatingHeapOccupancyPercent=15" << "-XX:G1MixedGCLiveThresholdPercent=90" << "-XX:G1RSetUpdatingPauseTimePercent=5" << "-XX:SurvivorRatio=32" << "-XX:+PerfDisableSharedMem" << "-XX:MaxTenuringThreshold=1";
    }
    finalArgs << "-Dsun.net.client.defaultConnectTimeout=5000" << "-Dsun.net.client.defaultReadTimeout=5000" << "-Dsun.rmi.dgc.server.gcInterval=2147483646" << "-Dsun.rmi.dgc.client.gcInterval=2147483646";
    if (!nativePath.isEmpty() && std::none_of(jvmArgs.cbegin(), jvmArgs.cend(), [](const QString& a){ return a.startsWith("-Djava.library.path"); })) finalArgs << ("-Djava.library.path=" + nativePath);
    for (const QString& a : jvmArgs) finalArgs << a;
    if (std::none_of(finalArgs.cbegin(), finalArgs.cend(), [](const QString& a){ return a == "-cp" || a == "-classpath"; })) finalArgs << "-cp" << cp;
    finalArgs << splitCommandLine(config.value("jvm_arguments").toString());
    finalArgs << mainClass;
    finalArgs << gameArgs;
    LaunchSpec spec; spec.java = java; spec.arguments = finalArgs; spec.workingDirectory = gameDirectory; spec.versionId = targetVersion;
    if (progress) progress(QString("%1 başlatma komutu hazır").arg(targetVersion), 100);
    return spec;
}

QJsonObject LauncherEngine::requestDeviceCode(const QString& clientId) const {
    QUrlQuery form; form.addQueryItem("client_id", clientId.isEmpty() ? kClientId : clientId);
    form.addQueryItem("scope", "XboxLive.signin offline_access");
    int status = 0;
    const QByteArray body = request("https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode", "POST",
        form.query(QUrl::FullyEncoded).toUtf8(), {{"Content-Type", "application/x-www-form-urlencoded"}}, 20000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Microsoft kodu alınamadı: %1").arg(qjsonError(body)).toStdString());
    return QJsonDocument::fromJson(body).object();
}
QJsonObject LauncherEngine::pollDeviceToken(const QString& clientId, const QString& deviceCode,
                                            int interval, int expiresIn,
                                            const std::function<bool()>& cancelled, Progress progress) const {
    const QString id = clientId.isEmpty() ? kClientId : clientId;
    QElapsedTimer elapsed; elapsed.start();
    while (elapsed.elapsed() < qint64(expiresIn) * 1000) {
        if (cancelled && cancelled()) throw std::runtime_error("Giriş kullanıcı tarafından iptal edildi.");
        QThread::sleep(qMax(1, interval));
        QUrlQuery form; form.addQueryItem("client_id", id);
        form.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
        form.addQueryItem("device_code", deviceCode);
        int status = 0;
        const QByteArray body = request("https://login.microsoftonline.com/consumers/oauth2/v2.0/token", "POST",
            form.query(QUrl::FullyEncoded).toUtf8(), {{"Content-Type", "application/x-www-form-urlencoded"}}, 20000, &status);
        const QJsonObject obj = QJsonDocument::fromJson(body).object();
        if (status >= 200 && status < 300) { if (progress) progress("Microsoft oturumu alındı", 50); return obj; }
        const QString error = obj.value("error").toString();
        if (error == "authorization_pending") continue;
        if (error == "slow_down") { QThread::sleep(5); continue; }
        if (error == "expired_token") throw std::runtime_error("Doğrulama kodunun süresi doldu. Yeniden deneyin.");
        if (error == "access_denied") throw std::runtime_error("Microsoft giriş isteğini reddettiniz.");
        throw std::runtime_error(qjsonError(body).toStdString());
    }
    throw std::runtime_error("Microsoft giriş isteği zaman aşımına uğradı.");
}
QJsonObject LauncherEngine::exchangeMicrosoftToken(const QString& msToken, const QString& refreshToken) const {
    const QByteArray xblBody = QJsonDocument(QJsonObject{
        {"Properties", QJsonObject{{"AuthMethod", "RPS"}, {"SiteName", "user.auth.xboxlive.com"}, {"RpsTicket", "d=" + msToken}}},
        // Xbox expects this HTTP URI as an identity value; it is not a request or download URL.
        {"RelyingParty", "http://auth.xboxlive.com"}, {"TokenType", "JWT"}
    }).toJson(QJsonDocument::Compact);
    int status = 0;
    const QByteArray xblRaw = request("https://user.auth.xboxlive.com/user/authenticate", "POST", xblBody,
        {{"Content-Type", "application/json"}, {"Accept", "application/json"}}, 20000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Xbox girişi başarısız: %1").arg(qjsonError(xblRaw)).toStdString());
    const QJsonObject xbl = QJsonDocument::fromJson(xblRaw).object();
    const QString xblToken = xbl.value("Token").toString();
    const QString userHash = xbl.value("DisplayClaims").toObject().value("xui").toArray().first().toObject().value("uhs").toString();
    const QByteArray xstsBody = QJsonDocument(QJsonObject{
        {"Properties", QJsonObject{{"SandboxId", "RETAIL"}, {"UserTokens", QJsonArray{xblToken}}}},
        {"RelyingParty", "rp://api.minecraftservices.com/"}, {"TokenType", "JWT"}
    }).toJson(QJsonDocument::Compact);
    const QByteArray xstsRaw = request("https://xsts.auth.xboxlive.com/xsts/authorize", "POST", xstsBody,
        {{"Content-Type", "application/json"}, {"Accept", "application/json"}}, 20000, &status);
    if (status < 200 || status >= 300) {
        const QJsonObject err = QJsonDocument::fromJson(xstsRaw).object();
        const QString xerr = err.value("XErr").toVariant().toString();
        if (xerr == "2148916233") throw std::runtime_error("Bu Microsoft hesabına bağlı bir Xbox profili bulunamadı.");
        if (xerr == "2148916238") throw std::runtime_error("Bu çocuk hesabı için aile izni gerekiyor.");
        throw std::runtime_error(QString("Xbox XSTS hatası: %1").arg(qjsonError(xstsRaw)).toStdString());
    }
    const QJsonObject xsts = QJsonDocument::fromJson(xstsRaw).object();
    const QString xstsToken = xsts.value("Token").toString();
    const QJsonArray xstsClaims = xsts.value("DisplayClaims").toObject().value("xui").toArray();
    const QString xuid = xstsClaims.isEmpty() ? QString() : xstsClaims.first().toObject().value("xid").toString();
    const QByteArray mcBody = QJsonDocument(QJsonObject{{"identityToken", "XBL3.0 x=" + userHash + ";" + xstsToken}}).toJson(QJsonDocument::Compact);
    const QByteArray mcRaw = request("https://api.minecraftservices.com/authentication/login_with_xbox", "POST", mcBody,
        {{"Content-Type", "application/json"}, {"Accept", "application/json"}}, 20000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Minecraft hesabı doğrulanamadı: %1").arg(qjsonError(mcRaw)).toStdString());
    const QString mcToken = QJsonDocument::fromJson(mcRaw).object().value("access_token").toString();
    const QByteArray profileRaw = request("https://api.minecraftservices.com/minecraft/profile", "GET", {},
        {{"Authorization", "Bearer " + mcToken.toUtf8()}}, 20000, &status);
    if (status == 404) throw std::runtime_error("Bu Microsoft hesabında Minecraft: Java Edition lisansı bulunamadı.");
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Minecraft profili alınamadı: %1").arg(qjsonError(profileRaw)).toStdString());
    const QJsonObject profile = QJsonDocument::fromJson(profileRaw).object();
    QJsonObject account{{"name", profile.value("name")}, {"id", profile.value("id")}, {"access_token", mcToken}, {"xuid", xuid}};
    if (!refreshToken.isEmpty()) account.insert("refresh_token", refreshToken);
    return account;
}
QJsonObject LauncherEngine::refreshMicrosoft(const QString& clientId, const QString& refreshToken) const {
    QUrlQuery form; form.addQueryItem("client_id", clientId.isEmpty() ? kClientId : clientId);
    form.addQueryItem("grant_type", "refresh_token"); form.addQueryItem("refresh_token", refreshToken);
    form.addQueryItem("scope", "XboxLive.signin offline_access");
    int status = 0; const QByteArray body = request("https://login.microsoftonline.com/consumers/oauth2/v2.0/token", "POST",
        form.query(QUrl::FullyEncoded).toUtf8(), {{"Content-Type", "application/x-www-form-urlencoded"}}, 20000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Microsoft oturumu yenilenemedi: %1").arg(qjsonError(body)).toStdString());
    const QJsonObject tokens = QJsonDocument::fromJson(body).object();
    return exchangeMicrosoftToken(tokens.value("access_token").toString(), tokens.value("refresh_token").toString(refreshToken));
}
QJsonArray LauncherEngine::curseForgeRequest(const QString& path, const QMap<QString, QString>& query,
                                             const QString& apiKey) const {
    if (apiKey.trimmed().isEmpty()) throw std::runtime_error("CurseForge kullanmak için kendi API anahtarını Ayarlar bölümüne gir.");
    const QString url = cleanUrl("https://api.curseforge.com/v1" + path, query);
    int status = 0;
    const QByteArray body = request(url, "GET", {}, {{"Accept", "application/json"}, {"x-api-key", apiKey.trimmed().toUtf8()}}, 25000, &status);
    if (status == 401 || status == 403) throw std::runtime_error("CurseForge API anahtarı geçersiz veya yetkisiz.");
    if (status < 200 || status >= 300) throw std::runtime_error(QString("CurseForge isteği başarısız (HTTP %1): %2").arg(status).arg(qjsonError(body)).toStdString());
    const QJsonValue data = QJsonDocument::fromJson(body).object().value("data");
    return data.isArray() ? data.toArray() : QJsonArray{data};
}
QJsonObject LauncherEngine::curseForgeObject(const QString& path, const QString& apiKey) const {
    const QJsonArray a = curseForgeRequest(path, {}, apiKey);
    if (a.isEmpty() || !a.first().isObject()) throw std::runtime_error("CurseForge dosyası bulunamadı.");
    return a.first().toObject();
}
QJsonArray LauncherEngine::searchMarketplace(const QString& provider, const QString& type,
                                             const QString& query, const QString& mcVersion,
                                             const QString& loader, const QString& curseForgeKey) const {
    const QString kind = type.toLower();
    QString cleanVersion = mcVersion;
    const auto vm = QRegularExpression("1\\.\\d+(?:\\.\\d+)?").match(cleanVersion);
    if (vm.hasMatch()) cleanVersion = vm.captured(0); else if (isVersionPlaceholder(cleanVersion)) cleanVersion.clear();
    if (provider.compare("CurseForge", Qt::CaseInsensitive) == 0) {
        int classId = 6;
        if (kind == "modpack") classId = 4471;
        else if (kind == "resourcepack") classId = 12;
        else if (kind == "world") classId = 17;
        else if (kind == "shader") classId = 6552;
        else if (kind == "datapack") classId = 6945;
        else if (kind != "mod") throw std::runtime_error("CurseForge bu içerik türünü desteklemiyor.");
        QMap<QString, QString> params{{"gameId", "432"}, {"classId", QString::number(classId)},
            {"searchFilter", query.trimmed()}, {"pageSize", "25"}, {"sortField", "6"}, {"sortOrder", "desc"}};
        if (!cleanVersion.isEmpty()) params.insert("gameVersion", cleanVersion);
        const QString low = loader.toLower();
        if (kind == "mod" && (low == "forge" || low == "fabric" || low == "neoforge" || low == "quilt"))
            params.insert("modLoaderType", low == "forge" ? "1" : low == "fabric" ? "4" : low == "quilt" ? "5" : "6");
        const QJsonArray data = curseForgeRequest("/mods/search", params, curseForgeKey);
        QJsonArray out;
        for (const auto& v : data) {
            const QJsonObject item = v.toObject();
            const QJsonArray authors = item.value("authors").toArray();
            const QJsonObject logo = item.value("logo").toObject();
            out.append(QJsonObject{{"provider", "CurseForge"}, {"project_type", kind},
                {"project_id", QString::number(item.value("id").toVariant().toLongLong())},
                {"curseforge_mod_id", item.value("id")}, {"title", item.value("name")},
                {"author", authors.isEmpty() ? "Bilinmiyor" : authors.first().toObject().value("name")},
                {"description", item.value("summary")}, {"downloads", item.value("downloadCount")},
                {"icon_url", logo.value("url")}, {"gallery", item.value("screenshots")}});
        }
        return out;
    }
    QJsonArray facets{QJsonArray{"project_type:" + kind}};
    if (!cleanVersion.isEmpty()) facets.append(QJsonArray{"versions:" + cleanVersion});
    QString loaderFacet = loader.toLower();
    if (loaderFacet == "vanilla") loaderFacet = "minecraft";
    if (!loaderFacet.isEmpty() && loaderFacet != "tümü" && (kind == "mod" || kind == "modpack")) facets.append(QJsonArray{"categories:" + loaderFacet});
    const QMap<QString, QString> params{{"query", query.trimmed()}, {"limit", "25"},
        {"facets", QString::fromUtf8(QJsonDocument(facets).toJson(QJsonDocument::Compact))}};
    const QString url = cleanUrl("https://api.modrinth.com/v2/search", params);
    const QJsonObject result = getJson(url, {{"User-Agent", "TuketiciLauncher/1.0 (Qt C++)"}});
    QJsonArray hits = result.value("hits").toArray();
    QJsonArray out;
    for (const auto& v : hits) {
        QJsonObject item = v.toObject();
        item.insert("provider", "Modrinth");
        item.insert("project_type", kind);
        item.insert("project_id", item.value("project_id").toString());
        item.insert("icon_url", item.value("icon_url"));
        out.append(item);
    }
    return out;
}
QJsonObject LauncherEngine::marketplaceMedia(const QJsonObject& hit, const QString& curseForgeKey) const {
    const QString provider = hit.value("provider").toString();
    const QString id = hit.value("project_id").toString();
    QJsonObject details;
    if (provider.compare("Modrinth", Qt::CaseInsensitive) == 0) {
        const QString url = "https://api.modrinth.com/v2/project/" + QString::fromLatin1(QUrl::toPercentEncoding(id));
        details = getJson(url, {{"User-Agent", "TuketiciLauncher/1.0 (Qt C++)"}});
    } else if (provider.compare("CurseForge", Qt::CaseInsensitive) == 0) {
        const QString modId = hit.value("curseforge_mod_id").isDouble()
            ? QString::number(qint64(hit.value("curseforge_mod_id").toDouble())) : id;
        const QJsonArray response = curseForgeRequest("/mods/" + modId, {}, curseForgeKey);
        if (!response.isEmpty()) details = response.first().toObject();
    } else {
        throw std::runtime_error("Bu içerik sağlayıcısının medya bilgisi desteklenmiyor.");
    }

    QJsonArray images;
    QJsonArray gallery = details.value("gallery").toArray();
    if (gallery.isEmpty()) gallery = details.value("screenshots").toArray();
    if (gallery.isEmpty()) gallery = hit.value("gallery").toArray();
    for (const QJsonValue& entry : gallery) {
        const QJsonObject image = entry.toObject();
        const QString imageUrl = entry.isString() ? entry.toString() : image.value("url").toString();
        const QUrl parsed(imageUrl);
        if (parsed.isValid() && parsed.scheme().compare("https", Qt::CaseInsensitive) == 0 && !parsed.host().isEmpty())
            images.append(QJsonObject{{"url", imageUrl}, {"title", image.value("title").toString()},
                                      {"description", image.value("description").toString()}});
    }
    if (images.isEmpty()) {
        const QJsonObject logo = details.value("logo").toObject();
        const QString iconUrl = details.value("icon_url").toString(logo.value("url").toString(hit.value("icon_url").toString()));
        const QUrl parsed(iconUrl);
        if (parsed.isValid() && parsed.scheme().compare("https", Qt::CaseInsensitive) == 0 && !parsed.host().isEmpty())
            images.append(QJsonObject{{"url", iconUrl}, {"title", "Proje görseli"}});
    }

    QStringList textSources{details.value("body").toString(), details.value("summary").toString()};
    const QJsonObject links = details.value("links").toObject();
    for (auto it = links.begin(); it != links.end(); ++it) if (it.value().isString()) textSources << it.value().toString();
    QJsonArray videos;
    QSet<QString> seenVideos;
    const QRegularExpression urlPattern(R"(https://[^\s<>\"'()\[\]]+)", QRegularExpression::CaseInsensitiveOption);
    for (const QString& source : textSources) {
        auto match = urlPattern.globalMatch(source);
        while (match.hasNext()) {
            QString candidate = match.next().captured(0);
            while (!candidate.isEmpty() && QStringLiteral(".,;:!?}").contains(candidate.back())) candidate.chop(1);
            const QUrl videoUrl(candidate);
            if (!videoUrl.isValid() || videoUrl.scheme().compare("https", Qt::CaseInsensitive) != 0 || videoUrl.host().isEmpty()) continue;
            const QString host = videoUrl.host().toLower();
            const QString path = videoUrl.path().toLower();
            const bool videoSite = host == "youtu.be" || host == "youtube.com" || host.endsWith(".youtube.com") ||
                                   host == "vimeo.com" || host.endsWith(".vimeo.com");
            const bool videoFile = path.endsWith(".mp4") || path.endsWith(".webm") || path.endsWith(".mov") || path.endsWith(".m3u8");
            if ((videoSite || videoFile) && !seenVideos.contains(candidate)) {
                seenVideos.insert(candidate);
                videos.append(candidate);
            }
        }
    }
    return QJsonObject{{"images", images}, {"videos", videos}};
}
QString LauncherEngine::downloadModrinthFile(const QString& projectId, const QString& type,
                                             const QString& mcVersion, const QString& loader,
                                             QString* fileName, QJsonObject* metadata) const {
    QMap<QString, QString> params;
    QString version = mcVersion;
    const auto vm = QRegularExpression("1\\.\\d+(?:\\.\\d+)?").match(version);
    if (vm.hasMatch()) version = vm.captured(0); else if (isVersionPlaceholder(version)) version.clear();
    if (!version.isEmpty()) params.insert("game_versions", QString::fromUtf8(QJsonDocument(QJsonArray{version}).toJson(QJsonDocument::Compact)));
    QString apiLoader = loader.toLower(); if (apiLoader == "vanilla") apiLoader = "minecraft";
    if (!apiLoader.isEmpty() && apiLoader != "tümü" && (type == "mod" || type == "modpack")) params.insert("loaders", QString::fromUtf8(QJsonDocument(QJsonArray{apiLoader}).toJson(QJsonDocument::Compact)));
    QString url = "https://api.modrinth.com/v2/project/" + QString::fromLatin1(QUrl::toPercentEncoding(projectId)) + "/version";
    if (!params.isEmpty()) url = cleanUrl(url, params);
    int status = 0;
    const QByteArray raw = request(url, "GET", {}, {{"User-Agent", "TuketiciLauncher/1.0 (Qt C++)"}}, 25000, &status);
    if (status < 200 || status >= 300) throw std::runtime_error(QString("Modrinth sürümleri alınamadı (HTTP %1).").arg(status).toStdString());
    const QJsonArray versions = QJsonDocument::fromJson(raw).array();
    const QString ext = type == "mod" ? ".jar" : type == "modpack" ? ".mrpack" : ".zip";
    const QRegularExpression serverPackToken(R"((?:^|[\s._-])server(?:$|[\s._-]))", QRegularExpression::CaseInsensitiveOption);
    QString serverOnlyUrl, serverOnlyName;
    for (const auto& vv : versions) {
        const QJsonObject ver = vv.toObject();
        const QString versionLabel = ver.value("name").toString() + " " + ver.value("version_number").toString();
        const QJsonArray files = ver.value("files").toArray();
        for (int pass = 0; pass < 2; ++pass) {
            for (const auto& fv : files) {
                const QJsonObject f = fv.toObject();
                const QString name = f.value("filename").toString();
                if (!name.toLower().endsWith(ext) || (pass == 0 && !f.value("primary").toBool())) continue;
                if (type == "modpack" &&
                    (serverPackToken.match(versionLabel).hasMatch() || serverPackToken.match(name).hasMatch())) {
                    if (serverOnlyUrl.isEmpty()) {
                        serverOnlyUrl = f.value("url").toString();
                        serverOnlyName = name;
                    }
                    continue;
                }
                if (fileName) *fileName = safeFileName(name);
                if (metadata) *metadata = f;
                return f.value("url").toString();
            }
        }
    }
    if (type == "modpack" && !serverOnlyUrl.isEmpty())
        throw std::runtime_error(QString("Modrinth yalnızca sunucu modpack dosyası verdi (%1). İstemci sürümünü seç veya yayıncıdan istemci paketi bekle.").arg(serverOnlyName).toStdString());
    throw std::runtime_error("Seçili sürüm ve loader için indirilebilir Modrinth dosyası bulunamadı.");
}
QString LauncherEngine::curseForgeFile(const QString& modId, const QString& fileId,
                                       const QString& apiKey, QString* fileName,
                                       QJsonObject* metadata, const QString& mcVersion,
                                       const QString& loader, const QString& type) const {
    QJsonObject item;
    if (fileId.isEmpty()) {
        QMap<QString, QString> params{{"pageSize", "50"}, {"index", "0"}};
        const auto versionMatch = QRegularExpression("1\\.\\d+(?:\\.\\d+)?").match(mcVersion);
        if (versionMatch.hasMatch()) params.insert("gameVersion", versionMatch.captured(0));
        const QString lowLoader = loader.toLower();
        if (type == "mod" && lowLoader == "forge") params.insert("modLoaderType", "1");
        else if (type == "mod" && lowLoader == "fabric") params.insert("modLoaderType", "4");
        else if (type == "mod" && lowLoader == "neoforge") params.insert("modLoaderType", "6");
        const auto files = curseForgeRequest("/mods/" + modId + "/files", params, apiKey);
        if (files.isEmpty()) throw std::runtime_error("CurseForge dosyası bulunamadı.");
        item = files.first().toObject();
    } else item = curseForgeObject("/mods/" + modId + "/files/" + fileId, apiKey);
    QString name = safeFileName(item.value("fileName").toString("download.zip"));
    QString url = item.value("downloadUrl").toString();
    if (url.isEmpty()) {
        const auto data = curseForgeRequest("/mods/" + modId + "/files/" + item.value("id").toVariant().toString() + "/download-url", {}, apiKey);
        if (!data.isEmpty() && data.first().isString()) url = data.first().toString();
    }
    if (!url.startsWith("https://")) throw std::runtime_error("CurseForge bu dosya için doğrudan indirme bağlantısı vermedi.");
    if (fileName) *fileName = name;
    if (metadata) *metadata = item;
    return url;
}
QString LauncherEngine::downloadMarketplaceItem(const QJsonObject& hit, const QString& mcVersion,
                                                const QString& loader, const QString& curseForgeKey,
                                                const QString& gameDirectory, Progress progress,
                                                const QString& targetWorld) {
    const QString provider = hit.value("provider").toString("Modrinth");
    const QString type = hit.value("project_type").toString("mod");
    const QString projectId = hit.value("project_id").toString();
    QString filename; QJsonObject info; QString url;
    if (provider == "CurseForge") { const QJsonValue idValue = hit.value("curseforge_mod_id"); const QString modId = idValue.isDouble() ? QString::number(qint64(idValue.toDouble())) : idValue.toString(projectId); url = curseForgeFile(modId, {}, curseForgeKey, &filename, &info, mcVersion, loader, type); }
    else url = downloadModrinthFile(projectId, type, mcVersion, loader, &filename, &info);
    if (type == "mod" && !filename.endsWith(".jar", Qt::CaseInsensitive)) throw std::runtime_error("Mod dosyası .jar değil.");
    if (type == "modpack" && !(filename.endsWith(".zip", Qt::CaseInsensitive) || filename.endsWith(".mrpack", Qt::CaseInsensitive))) throw std::runtime_error("Modpack dosyası ZIP veya MRPACK olmalı.");
    if ((type == "resourcepack" || type == "shader" || type == "datapack" || type == "world") && !filename.endsWith(".zip", Qt::CaseInsensitive))
        throw std::runtime_error("Seçilen içerik ZIP arşivi değil.");
    QJsonObject hashes = info.value("hashes").toObject();
    if (provider == "CurseForge") {
        hashes = {};
        for (const auto& hv : info.value("hashes").toArray()) {
            const QJsonObject h = hv.toObject(); const int algo = h.value("algo").toInt();
            if (algo == 1) hashes.insert("sha1", h.value("value")); else if (algo == 2) hashes.insert("md5", h.value("value"));
        }
    }
    QString dir;
    if (type == "mod") dir = modsDirectory(gameDirectory);
    else if (type == "resourcepack") dir = resourcePacksDirectory(gameDirectory);
    else if (type == "shader") dir = QDir(gameDirectory).filePath("shaderpacks");
    else if (type == "datapack") {
        if (targetWorld.trimmed().isEmpty() || !worldNames(gameDirectory).contains(targetWorld) ||
            QFileInfo(targetWorld).fileName() != targetWorld || targetWorld.contains('/') || targetWorld.contains('\\'))
            throw std::runtime_error("Data pack kurmak için önce hedef dünyayı seç.");
        dir = QDir(QDir(QDir(gameDirectory).filePath("saves")).filePath(targetWorld)).filePath("datapacks");
    } else if (type == "world") {
        const QString archive = QDir(cacheDir_).filePath("world-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".zip");
        downloadUrl(url, archive, hashes, progress);
        const QString installed = installWorldArchive(archive, hit.value("title").toString("Yeni Dünya"), gameDirectory);
        QFile::remove(archive);
        return installed;
    } else dir = modpacksDir_;
    QDir().mkpath(dir);
    QString target = QDir(dir).filePath(filename);
    if (QFileInfo::exists(target)) {
        if (type != "modpack") throw std::runtime_error("Dosya zaten mevcut: " + filename.toStdString());
        target = QDir(dir).filePath(safeFolderName(hit.value("title").toString()) + "-" + filename);
    }
    return downloadUrl(url, target, hashes, progress);
}

QString LauncherEngine::installWorldArchive(const QString& archivePath, const QString& requestedName,
                                             const QString& gameDirectory) const {
    ZipArchive zip; QString why;
    if (!zip.open(archivePath, &why)) throw std::runtime_error(why.toStdString());
    const QString staging = QDir(cacheDir_).filePath("world-extract-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!zip.extractUnder(staging, {}, &why)) { QDir(staging).removeRecursively(); throw std::runtime_error(why.toStdString()); }
    QString worldRoot;
    QDirIterator it(staging, {"level.dat"}, QDir::Files, QDirIterator::Subdirectories);
    if (it.hasNext()) worldRoot = QFileInfo(it.next()).absolutePath();
    if (worldRoot.isEmpty()) { QDir(staging).removeRecursively(); throw std::runtime_error("Dünya ZIP'inde level.dat bulunamadı."); }
    QString folder = safeFolderName(requestedName);
    if (folder.isEmpty()) folder = "Imported World";
    const QString saves = QDir(gameDirectory).filePath("saves");
    QDir().mkpath(saves);
    const QString base = folder;
    int suffix = 2;
    while (QFileInfo::exists(QDir(saves).filePath(folder))) folder = base + " (" + QString::number(suffix++) + ")";
    const QString target = QDir(saves).filePath(folder);
    if (!copyDirectoryTree(worldRoot, target)) { QDir(target).removeRecursively(); QDir(staging).removeRecursively(); throw std::runtime_error("Minecraft dünyası saves klasörüne kurulamadı."); }
    QDir(staging).removeRecursively();
    return target;
}


QJsonObject LauncherEngine::installModrinthPack(const QString& archivePath, const QJsonObject& hit,
                                                const QString& javaPath, Progress progress) {
    ZipArchive zip; QString why;
    if (!zip.open(archivePath, &why)) throw std::runtime_error(why.toStdString());
    const QJsonObject index = QJsonDocument::fromJson(zip.read("modrinth.index.json", &why)).object();
    if (index.isEmpty()) throw std::runtime_error(why.isEmpty() ? "Modrinth modpack manifesti bozuk." : why.toStdString());
    const QJsonObject deps = index.value("dependencies").toObject();
    const QString mcVersion = deps.value("minecraft").toString();
    if (mcVersion.isEmpty()) throw std::runtime_error("Modrinth modpack içinde Minecraft sürümü belirtilmemiş.");
    const auto packLoader = modrinthPackLoader(deps);
    const QString loader = packLoader.first;
    const QString loaderVersion = packLoader.second;
    const QString title = hit.value("title").toString("Modrinth Modpack");
    const QString project = hit.value("project_id").toString("pack");
    const QString baseId = safeFolderName(title) + "-" + safeFolderName(project).left(10);
    const QJsonArray existingProfiles = readProfiles();
    QString id = baseId;
    int installNumber = 1;
    for (;;) {
        bool inUse = QFileInfo::exists(QDir(instancesDir_).filePath(id));
        for (const auto& value : existingProfiles) inUse |= value.toObject().value("id").toString() == id;
        if (!inUse) break;
        id = baseId + "-" + QString::number(++installNumber);
    }
    const QString profileName = installNumber == 1 ? title : QString("%1 (%2)").arg(title).arg(installNumber);
    const QString instanceDir = QDir(instancesDir_).filePath(id);
    if (!QDir().mkpath(instanceDir)) throw std::runtime_error("Modpack profil klasörü oluşturulamadı.");
    try {
        if (!zip.extractUnder(instanceDir, "overrides", &why)) throw std::runtime_error(why.toStdString());
        bool hasClientOverrides = false;
        for (const ZipEntry& e : zip.entries()) hasClientOverrides |= e.name.startsWith("client-overrides/");
        if (hasClientOverrides) {
            if (!zip.extractUnder(instanceDir, "client-overrides", &why)) throw std::runtime_error(why.toStdString());
        }
        const QJsonArray files = index.value("files").toArray();
        int done = 0;
        for (const auto& fv : files) {
            const QJsonObject file = fv.toObject();
            const QString path = file.value("path").toString();
            if (!safeRelativePath(path)) throw std::runtime_error(QString("Güvenli olmayan modpack yolu: %1").arg(path).toStdString());
            const QString env = file.value("env").toObject().value("client").toString("required");
            if (env == "unsupported") continue;
            const QJsonArray urls = file.value("downloads").toArray();
            if (urls.isEmpty()) throw std::runtime_error(QString("Dosya için indirme bağlantısı yok: %1").arg(path).toStdString());
            QJsonObject hashes = file.value("hashes").toObject();
            const QString dest = QDir(instanceDir).filePath(path);
            downloadUrl(urls.first().toString(), dest, hashes, {});
            ++done;
            if (progress) progress(QString("Modpack dosyaları (%1/%2)").arg(done).arg(files.size()), files.isEmpty() ? 75 : 55 + done * 20 / files.size());
        }
        const QString launchId = installVersion(mcVersion, loader, loaderVersion, javaPath, progress);
        QJsonObject profile{{"id", QFileInfo(instanceDir).fileName()}, {"name", profileName}, {"kind", "modpack"},
            {"provider", "Modrinth"}, {"project_id", project}, {"minecraft_version", mcVersion},
            {"loader", loader}, {"loader_version", loaderVersion}, {"launch_version", launchId},
            {"directory", instanceDir}, {"game_directory", instanceDir},
            {"source_archive", QFileInfo(archivePath).absoluteFilePath()}};
        if (!saveProfile(profile, &why)) throw std::runtime_error(why.toStdString());
        if (progress) progress("Modrinth modpack profili kuruldu", 100);
        return profile;
    } catch (...) { QDir(instanceDir).removeRecursively(); throw; }
}
QJsonObject LauncherEngine::repairLegacyModrinthPack(const QJsonObject& profile,
                                                      const QString& javaPath,
                                                      Progress progress) const {
    if (profile.value("kind").toString() != "modpack" ||
        profile.value("provider").toString() != "Modrinth" ||
        profile.value("loader").toString() != "Vanilla") return profile;

    const QString mcVersion = profile.value("minecraft_version").toString();
    const QString gameDirectory = profile.value("directory").toString();
    if (mcVersion.isEmpty() || gameDirectory.isEmpty() || !QFileInfo(gameDirectory).isDir()) return profile;

    QStringList archives;
    const QString savedArchive = profile.value("source_archive").toString();
    const QFileInfo savedArchiveInfo(savedArchive);
    if (savedArchiveInfo.isFile() && QDir::cleanPath(savedArchiveInfo.absolutePath()).compare(
            QDir::cleanPath(QFileInfo(modpacksDir_).absoluteFilePath()), Qt::CaseInsensitive) == 0)
        archives << savedArchiveInfo.absoluteFilePath();
    const QFileInfoList cached = QDir(modpacksDir_).entryInfoList({"*.mrpack"}, QDir::Files, QDir::Time);
    for (const QFileInfo& file : cached) {
        const QString path = file.absoluteFilePath();
        bool alreadyListed = false;
        for (const QString& listed : archives) alreadyListed |= listed.compare(path, Qt::CaseInsensitive) == 0;
        if (!alreadyListed) archives << path;
    }

    QString matchedArchive, matchedLoader, matchedLoaderVersion;
    bool ambiguous = false;
    for (const QString& archivePath : archives) {
        ZipArchive zip;
        QString error;
        if (!zip.open(archivePath, &error)) continue;
        const QJsonObject index = QJsonDocument::fromJson(zip.read("modrinth.index.json", &error)).object();
        if (index.isEmpty() || index.value("dependencies").toObject().value("minecraft").toString() != mcVersion) continue;
        const QJsonArray files = index.value("files").toArray();
        if (files.isEmpty()) continue;

        bool matches = true;
        int checkedFiles = 0;
        for (const auto& value : files) {
            const QJsonObject file = value.toObject();
            if (file.value("env").toObject().value("client").toString("required") == "unsupported") continue;
            const QString relativePath = file.value("path").toString();
            if (!safeRelativePath(relativePath)) { matches = false; break; }
            ++checkedFiles;
            if (!QFileInfo::exists(QDir(gameDirectory).filePath(relativePath))) { matches = false; break; }
        }
        if (!matches || checkedFiles == 0) continue;

        const auto loader = modrinthPackLoader(index.value("dependencies").toObject());
        if (loader.first == "Vanilla") continue;
        if (!matchedArchive.isEmpty() && (loader.first != matchedLoader || loader.second != matchedLoaderVersion)) {
            ambiguous = true;
            break;
        }
        if (matchedArchive.isEmpty()) {
            matchedArchive = archivePath;
            matchedLoader = loader.first;
            matchedLoaderVersion = loader.second;
        }
    }
    if (ambiguous || matchedArchive.isEmpty()) return profile;

    if (progress) progress("Eski modpack profili düzeltiliyor: " + matchedLoader, 12);
    const QString launchId = installVersion(mcVersion, matchedLoader, matchedLoaderVersion, javaPath, progress);
    QJsonObject repaired = profile;
    repaired.insert("loader", matchedLoader);
    repaired.insert("loader_version", matchedLoaderVersion);
    repaired.insert("launch_version", launchId);
    repaired.insert("game_directory", gameDirectory);
    repaired.insert("source_archive", matchedArchive);
    QString error;
    if (!saveProfile(repaired, &error)) throw std::runtime_error(QString("Modpack profili onarıldı ancak kaydedilemedi: %1").arg(error).toStdString());
    repaired.insert("directory", gameDirectory);
    if (progress) progress(QString("Modpack profili %1 ile hazırlandı").arg(matchedLoader), 100);
    return repaired;
}
QJsonObject LauncherEngine::installCurseForgePack(const QString& archivePath, const QJsonObject& hit,
                                                  const QString& apiKey, const QString& javaPath,
                                                  Progress progress) {
    ZipArchive zip; QString why;
    if (!zip.open(archivePath, &why)) throw std::runtime_error(why.toStdString());
    const QJsonObject manifest = QJsonDocument::fromJson(zip.read("manifest.json", &why)).object();
    if (manifest.isEmpty()) throw std::runtime_error(why.isEmpty() ? "CurseForge manifest.json bulunamadı." : why.toStdString());
    const QJsonObject minecraft = manifest.value("minecraft").toObject();
    const QString mcVersion = minecraft.value("version").toString();
    if (mcVersion.isEmpty()) throw std::runtime_error("CurseForge modpack içinde Minecraft sürümü belirtilmemiş.");
    QString loader = "Vanilla", loaderVersion;
    bool supported = minecraft.value("modLoaders").toArray().isEmpty();
    for (const auto& v : minecraft.value("modLoaders").toArray()) {
        const QString lid = v.toObject().value("id").toString();
        const QString low = lid.toLower();
        if (low.startsWith("quilt-")) { loader = "Quilt"; loaderVersion = lid.mid(6); supported = true; break; }
        if (low.startsWith("neoforge-")) { loader = "NeoForge"; loaderVersion = lid.mid(9); supported = true; break; }
        if (low.startsWith("forge-")) { loader = "Forge"; loaderVersion = lid.mid(6); supported = true; break; }
        if (low.startsWith("fabric-")) { loader = "Fabric"; loaderVersion = lid.mid(7); supported = true; break; }
    }
    if (!supported) throw std::runtime_error("Bu CurseForge modpack loader'ı desteklenmiyor.");
    const QString title = hit.value("title").toString(manifest.value("name").toString("CurseForge Modpack"));
    const QString project = hit.value("project_id").toString("pack");
    const QString id = safeFolderName(title) + "-" + safeFolderName(project).left(10);
    for (const auto& p : readProfiles()) if (p.toObject().value("id").toString() == id) throw std::runtime_error("Bu modpack zaten kurulu.");
    QString instanceDir = QDir(instancesDir_).filePath(id);
    if (QFileInfo::exists(instanceDir)) instanceDir += "-" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(6);
    if (!QDir().mkpath(instanceDir)) throw std::runtime_error("Modpack profil klasörü oluşturulamadı.");
    try {
        QString overrides = manifest.value("overrides").toString("overrides"); overrides.replace('\\', '/'); overrides = overrides.trimmed();
        if (!overrides.isEmpty() && !zip.extractUnder(instanceDir, overrides, &why)) throw std::runtime_error(why.toStdString());
        const QString modsDir = QDir(instanceDir).filePath("mods"); QDir().mkpath(modsDir);
        const QJsonArray files = manifest.value("files").toArray(); int done = 0;
        for (const auto& fv : files) {
            const QJsonObject file = fv.toObject();
            if (file.contains("required") && !file.value("required").toBool(true)) continue;
            QString filename; QJsonObject metadata;
            const QString modId = QString::number(file.value("projectID").toVariant().toLongLong());
            const QString fileId = QString::number(file.value("fileID").toVariant().toLongLong());
            const QString url = curseForgeFile(modId, fileId, apiKey, &filename, &metadata);
            if (!filename.endsWith(".jar", Qt::CaseInsensitive)) throw std::runtime_error(QString("Mod dosyası .jar değil: %1").arg(filename).toStdString());
            QJsonObject hashes;
            for (const auto& hv : metadata.value("hashes").toArray()) {
                const QJsonObject h = hv.toObject();
                if (h.value("algo").toInt() == 1) hashes.insert("sha1", h.value("value"));
                else if (h.value("algo").toInt() == 2) hashes.insert("md5", h.value("value"));
            }
            downloadUrl(url, QDir(modsDir).filePath(filename), hashes, {});
            ++done;
            if (progress) progress(QString("CurseForge modpack dosyaları (%1/%2)").arg(done).arg(files.size()), files.isEmpty() ? 75 : 40 + done * 35 / files.size());
        }
        const QString launchId = installVersion(mcVersion, loader, loaderVersion, javaPath, progress);
        QJsonObject profile{{"id", QFileInfo(instanceDir).fileName()}, {"name", title}, {"kind", "modpack"},
            {"provider", "CurseForge"}, {"project_id", project}, {"minecraft_version", mcVersion},
            {"loader", loader}, {"loader_version", loaderVersion}, {"launch_version", launchId}, {"directory", instanceDir}};
        if (!saveProfile(profile, &why)) throw std::runtime_error(why.toStdString());
        if (progress) progress("CurseForge modpack profili kuruldu", 100);
        return profile;
    } catch (...) { QDir(instanceDir).removeRecursively(); throw; }
}

QString LauncherEngine::downloadSkin(const QString& query, Progress progress) {
    const bool isUrl = query.startsWith("https://", Qt::CaseInsensitive) || query.startsWith("http://", Qt::CaseInsensitive);
    QString destination;
    if (isUrl) destination = QDir(skinsDir_).filePath("downloaded_" + QUuid::createUuid().toString(QUuid::WithoutBraces).left(12) + ".png");
    else {
        QString safe = query; safe.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
        while (safe.startsWith('.') || safe.startsWith('_')) safe.remove(0, 1);
        if (safe.isEmpty()) safe = "player";
        destination = QDir(skinsDir_).filePath("downloaded_" + safe.toLower() + ".png");
        if (QImage(destination).isNull()) QFile::remove(destination);
        else return destination;
    }
    QString url = query;
    if (!isUrl) url = "https://skinsystem.ely.by/skins/" + QString::fromLatin1(QUrl::toPercentEncoding(query)) + ".png";
    QString temp = destination + ".part";
    int status = 0; QByteArray body = request(url, "GET", {}, {{"User-Agent", "TuketiciLauncher/1.0"}}, 15000, &status);
    if ((status < 200 || status >= 300 || QImage::fromData(body).isNull()) && !isUrl) {
        const QString fallback = "https://minotar.net/skin/" + QString::fromLatin1(QUrl::toPercentEncoding(query));
        body = request(fallback, "GET", {}, {{"User-Agent", "TuketiciLauncher/1.0"}}, 15000, &status);
    }
    if (status < 200 || status >= 300 || body.isEmpty() || QImage::fromData(body).isNull()) throw std::runtime_error("Skin bulunamadı veya sunucu geçerli bir görsel döndürmedi.");
    QSaveFile file(destination); if (!file.open(QIODevice::WriteOnly) || file.write(body) != body.size() || !file.commit()) throw std::runtime_error("Skin dosyası kaydedilemedi.");
    if (progress) progress("Skin indirildi", 100);
    return destination;
}
QPair<bool, QString> LauncherEngine::uploadSkin(const QString& accessToken, const QString& pngPath, const QString& variant) const {
    QFile file(pngPath); if (!file.open(QIODevice::ReadOnly)) return {false, "Skin dosyası bulunamadı."};
    const QByteArray image = file.readAll();
    if (QImage::fromData(image).isNull()) return {false, "Seçilen PNG skin geçerli değil."};
    const QByteArray boundary = "----TitanLauncher" + QUuid::createUuid().toString(QUuid::WithoutBraces).toLatin1();
    QByteArray body;
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"variant\"\r\n\r\n" + (variant.toLower() == "slim" ? "slim" : "classic") + "\r\n";
    body += "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"skin.png\"\r\nContent-Type: image/png\r\n\r\n";
    body += image; body += "\r\n--" + boundary + "--\r\n";
    int status = 0;
    const QByteArray response = request("https://api.minecraftservices.com/minecraft/profile/skins", "POST", body,
        {{"Authorization", "Bearer " + accessToken.toUtf8()}, {"Content-Type", "multipart/form-data; boundary=" + boundary}, {"User-Agent", "TuketiciLauncher/1.0"}}, 30000, &status);
    if (status == 200 || status == 204) return {true, "Skin Mojang hesabına yüklendi."};
    return {false, QString("Skin yüklenemedi (HTTP %1): %2").arg(status).arg(qjsonError(response))};
}
QJsonObject LauncherEngine::queryServer(const QString& host) const {
    if (host.trimmed().isEmpty()) throw std::runtime_error("Sunucu adresi boş olamaz.");
    const QString url = "https://api.mcsrvstat.us/2/" + QString::fromLatin1(QUrl::toPercentEncoding(host.trimmed(), QByteArray(":.")));
    const QJsonObject data = getJson(url, {{"User-Agent", "TuketiciLauncher/1.0"}});
    QJsonObject out; out.insert("online", data.value("online"));
    if (!data.value("online").toBool()) return out;
    const QJsonObject players = data.value("players").toObject();
    out.insert("players", players.value("online")); out.insert("max", players.value("max"));
    out.insert("version", data.value("version"));
    const QJsonArray clean = data.value("motd").toObject().value("clean").toArray();
    QStringList lines; for (const auto& v : clean) lines << v.toString();
    out.insert("motd", lines.join('\n')); return out;
}
QStringList LauncherEngine::installedMods(const QString& gameDirectory, const QString& filter) const {
    const QString dir = modsDirectory(gameDirectory); QDir().mkpath(dir);
    QStringList names = QDir(dir).entryList({"*.jar", "*.jar.disabled"}, QDir::Files, QDir::Name);
    if (!filter.trimmed().isEmpty()) names.erase(std::remove_if(names.begin(), names.end(), [&](const QString& name) { return !name.contains(filter, Qt::CaseInsensitive); }), names.end());
    return names;
}
bool LauncherEngine::setModEnabled(const QString& gameDirectory, const QString& filename, bool enabled, QString* error) const {
    const QString base = QFileInfo(filename).fileName();
    QString activeName;
    if (base.endsWith(".jar.disabled", Qt::CaseInsensitive)) activeName = base.left(base.size() - QString(".disabled").size());
    else if (base.endsWith(".jar", Qt::CaseInsensitive)) activeName = base;
    else { if (error) *error = "Mod dosyası .jar veya .jar.disabled olmalı."; return false; }
    const QString dir = modsDirectory(gameDirectory);
    const QString active = QDir(dir).filePath(activeName), disabled = active + ".disabled";
    const bool hasA = QFileInfo::exists(active), hasD = QFileInfo::exists(disabled);
    bool ok = true;
    if (enabled) {
        if (hasD && hasA) ok = QFile::remove(disabled);
        else if (hasD) ok = QFile::rename(disabled, active);
        else if (!hasA) ok = false;
    } else {
        if (hasA && hasD) ok = QFile::remove(active);
        else if (hasA) ok = QFile::rename(active, disabled);
        else if (!hasD) ok = false;
    }
    if (!ok && error) *error = "Mod dosyasının durumu değiştirilemedi.";
    return ok;
}
bool LauncherEngine::deleteMod(const QString& gameDirectory, const QString& filename, QString* error) const {
    const QString base = QFileInfo(filename).fileName();
    if (!base.endsWith(".jar", Qt::CaseInsensitive) && !base.endsWith(".jar.disabled", Qt::CaseInsensitive)) { if (error) *error = "Yalnızca .jar mod dosyaları silinebilir."; return false; }
    const QString dir = modsDirectory(gameDirectory);
    const QString a = QDir(dir).filePath(base.endsWith(".disabled", Qt::CaseInsensitive) ? base.left(base.size() - 9) : base);
    const QString d = a + ".disabled";
    bool ok = true; if (QFileInfo::exists(a)) ok &= QFile::remove(a); if (QFileInfo::exists(d)) ok &= QFile::remove(d);
    if (!ok && error) *error = "Mod dosyası silinemedi.";
    return ok;
}



// ---------------------------------------------------------------------------
// Paralel indirme motoru
// ---------------------------------------------------------------------------
void LauncherEngine::downloadParallel(const QVector<DownloadItem>& items,
                                      int concurrentLimit,
                                      Progress progress,
                                      const QString& progressLabel,
                                      int progressBase,
                                      int progressRange) const
{
    if (items.isEmpty()) return;

    const int total = items.size();
    std::atomic<int> doneCount{0};
    std::atomic<bool> hasFailed{false};
    QString firstError;
    QMutex errorMutex;

    // items'i concurrentLimit boyutlu batch'lere bölerek işle.
    // Her batch'te hepsini başlat, hepsi bitince sonrakine geç.
    const int batchSize = qMax(1, concurrentLimit);

    for (int batchStart = 0; batchStart < total; batchStart += batchSize) {
        const int batchEnd = qMin(batchStart + batchSize, total);
        QVector<QFuture<void>> batch;
        batch.reserve(batchEnd - batchStart);

        for (int i = batchStart; i < batchEnd; ++i) {
            const DownloadItem item = items[i];
            batch.append(QtConcurrent::run(
                [this, item, &doneCount, &hasFailed, &firstError, &errorMutex,
                 total, progressBase, progressRange, &progress, progressLabel]() {
                    if (!hasFailed.load()) {
                        try {
                            QJsonObject hashes;
                            if (!item.sha1.isEmpty())
                                hashes.insert("sha1", item.sha1);
                            downloadUrl(item.url, item.destination, hashes);
                        } catch (const std::exception& e) {
                            QMutexLocker lk(&errorMutex);
                            if (!hasFailed.exchange(true))
                                firstError = QString::fromUtf8(e.what());
                        }
                    }
                    const int n = ++doneCount;
                    if (progress) {
                        const int pct = progressBase +
                            (total > 0 ? n * progressRange / total : progressRange);
                        progress(QString("%1 (%2/%3)")
                                 .arg(progressLabel).arg(n).arg(total), pct);
                    }
                }
            ));
        }

        // Bu batch'in tamamlanmasını bekle
        for (auto& f : batch) f.waitForFinished();

        // Hata varsa erken çık
        if (hasFailed.load()) break;
    }

    if (hasFailed.load()) {
        throw std::runtime_error(firstError.isEmpty()
            ? "Paralel indirme sirasinda hata olustu."
            : firstError.toStdString());
    }
}
