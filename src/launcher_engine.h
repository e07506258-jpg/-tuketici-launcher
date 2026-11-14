#pragma once
#include <QJsonArray>
#include <QMap>
#include <QPair>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <atomic>

struct LaunchSpec {
    QString java;
    QStringList arguments;
    QString workingDirectory;
    QString versionId;
};

class LauncherEngine {
public:
    using Progress = std::function<void(const QString&, int)>;
    LauncherEngine();

    QString dataDirectory() const { return dataDir_; }
    QString minecraftDirectory() const { return mcDir_; }
    QString versionsDirectory() const { return versionsDir_; }
    QString modsDirectory(const QString& gameDirectory = {}) const;
    QString resourcePacksDirectory(const QString& gameDirectory = {}) const;
    QString skinsDirectory() const { return skinsDir_; }
    QString modpacksDirectory() const { return modpacksDir_; }
    QString instancesDirectory() const { return instancesDir_; }
    QString configPath() const { return configPath_; }
    QString processLogPath() const { return processLogPath_; }
    QString launcherLogPath() const { return launcherLogPath_; }
    void appendLauncherLog(const QString& message) const;

    QJsonObject loadConfig() const;
    bool saveConfig(const QJsonObject& config, QString* error = nullptr) const;
    QJsonArray readProfiles() const;
    bool saveProfile(const QJsonObject& profile, QString* error = nullptr) const;
    QJsonObject createInstance(const QString& name, const QString& minecraftVersion,
                               const QString& loader, const QString& loaderVersion,
                               const QString& launchVersion, QString* error = nullptr) const;
    QJsonObject cloneProfile(const QString& id, const QString& newName, QString* error = nullptr) const;
    bool renameProfile(const QString& id, const QString& newName, QString* error = nullptr) const;
    bool deleteProfile(const QString& id, QString* error = nullptr) const;
    bool exportProfile(const QString& id, const QString& destinationZip, QString* error = nullptr) const;
    QJsonObject importModpack(const QString& archivePath, const QString& curseForgeKey,
                              const QString& javaPath, Progress progress = {});
    QStringList worldNames(const QString& gameDirectory) const;
    QJsonObject activeProfile(const QJsonObject& config) const;
    QString activeGameDirectory(const QJsonObject& config) const;

    QJsonArray localVersions() const;
    QJsonArray remoteVersions(bool snapshots, Progress progress = {}) const;
    QString installVersion(const QString& mcVersion, const QString& loader,
                           const QString& loaderVersion, const QString& javaPath,
                           Progress progress = {}) const;
    LaunchSpec createLaunchSpec(const QString& targetVersion, const QString& username,
                                const QString& uuid, const QString& accessToken,
                                const QString& gameDirectory, const QJsonObject& config,
                                Progress progress = {}) const;
    static QString offlineUuid(const QString& username);
    QString resolveJava(const QString& minecraftVersion, const QString& customPath = {}) const;
    QString ensureJavaForVersion(const QString& minecraftVersion, const QString& customPath = {},
                                 Progress progress = {}) const;
    static int javaMajor(const QString& executable);

    QJsonObject requestDeviceCode(const QString& clientId) const;
    QJsonObject pollDeviceToken(const QString& clientId, const QString& deviceCode,
                                int interval, int expiresIn,
                                const std::function<bool()>& cancelled,
                                Progress progress = {}) const;
    QJsonObject exchangeMicrosoftToken(const QString& msToken, const QString& refreshToken = {}) const;
    QJsonObject refreshMicrosoft(const QString& clientId, const QString& refreshToken) const;

    QJsonArray searchMarketplace(const QString& provider, const QString& type,
                                 const QString& query, const QString& mcVersion,
                                 const QString& loader, const QString& curseForgeKey) const;
    QJsonObject marketplaceMedia(const QJsonObject& hit, const QString& curseForgeKey) const;
    QString downloadMarketplaceItem(const QJsonObject& hit, const QString& mcVersion,
                                   const QString& loader, const QString& curseForgeKey,
                                   const QString& gameDirectory, Progress progress = {},
                                   const QString& targetWorld = {});
    QJsonObject installModrinthPack(const QString& archivePath, const QJsonObject& hit,
                                   const QString& javaPath, Progress progress = {});
    QJsonObject repairLegacyModrinthPack(const QJsonObject& profile, const QString& javaPath,
                                        Progress progress = {}) const;
    QJsonObject installCurseForgePack(const QString& archivePath, const QJsonObject& hit,
                                     const QString& apiKey, const QString& javaPath,
                                     Progress progress = {});

    QString downloadSkin(const QString& query, Progress progress = {});
    QPair<bool, QString> uploadSkin(const QString& accessToken, const QString& pngPath,
                                   const QString& variant) const;
    QJsonObject queryServer(const QString& host) const;

    QString downloadUrl(const QString& url, const QString& destination,
                        const QJsonObject& expectedHashes = {}, Progress progress = {},
                        const QStringList& trustedHosts = {}, qint64 maximumBytes = 0) const;
    QStringList installedMods(const QString& gameDirectory, const QString& filter = {}) const;
    bool setModEnabled(const QString& gameDirectory, const QString& filename, bool enabled,
                       QString* error = nullptr) const;
    bool deleteMod(const QString& gameDirectory, const QString& filename, QString* error = nullptr) const;
    static bool safeRelativePath(const QString& path);
    static QString safeFolderName(const QString& name);

private:
    QString dataDir_, mcDir_, versionsDir_, modsDir_, resourcePacksDir_, modpacksDir_, instancesDir_, skinsDir_, cacheDir_, runtimesDir_;
    QString configPath_, performancePath_, versionsCachePath_, launcherLogPath_, processLogPath_;
    void ensureDirectories() const;
    QJsonObject getJson(const QString& url, const QMap<QByteArray, QByteArray>& headers = {}) const;
    QByteArray request(const QString& url, const QByteArray& method = "GET",
                       const QByteArray& body = {}, const QMap<QByteArray, QByteArray>& headers = {},
                       int timeoutMs = 30000, int* status = nullptr) const;
    QString installVanilla(const QString& mcVersion, Progress progress) const;
    QString installFabric(const QString& mcVersion, const QString& loaderVersion,
                          const QString& javaPath, Progress progress) const;
    QString installQuilt(const QString& mcVersion, const QString& loaderVersion,
                         const QString& javaPath, Progress progress) const;
    QString installForgeLike(const QString& mcVersion, const QString& loader,
                             const QString& loaderVersion, const QString& javaPath,
                             Progress progress) const;
    QJsonObject versionJson(const QString& versionId) const;
    void installLibraries(const QJsonObject& json, Progress progress) const;
    QString resolveVersionId(const QString& mcVersion, const QString& loader,
                             const QString& loaderVersion) const;
    QString downloadModrinthFile(const QString& projectId, const QString& type,
                                 const QString& mcVersion, const QString& loader,
                                 QString* fileName, QJsonObject* metadata) const;
    QString curseForgeFile(const QString& modId, const QString& fileId,
                           const QString& apiKey, QString* fileName,
                           QJsonObject* metadata, const QString& mcVersion = {},
                           const QString& loader = {}, const QString& type = "mod") const;
    QJsonArray curseForgeRequest(const QString& path, const QMap<QString, QString>& query,
                                 const QString& apiKey) const;
    QJsonObject curseForgeObject(const QString& path, const QString& apiKey) const;
    static bool rulesAllow(const QJsonArray& rules, const QString& osName);
    static QStringList splitCommandLine(const QString& value);
    static QString platformName();
    static QString sha1File(const QString& path);
    int requiredJavaMajor(const QString& minecraftVersion) const;
    QString findBundledJava(int major) const;
    QString installWorldArchive(const QString& archivePath, const QString& requestedName,
                                const QString& gameDirectory) const;

    // Paralel indirme: items listesindeki (url, destination, sha1) üçlülerini
    // concurrentLimit kadar iş parçacığında eş zamanlı indirir.
    // Herhangi bir hata oluşursa std::runtime_error fırlatır.
    struct DownloadItem {
        QString url;
        QString destination;
        QString sha1; // boş bırakılabilir
    };
    void downloadParallel(const QVector<DownloadItem>& items,
                          int concurrentLimit,
                          Progress progress,
                          const QString& progressLabel,
                          int progressBase,
                          int progressRange) const;
};





