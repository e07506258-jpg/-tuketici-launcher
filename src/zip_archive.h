#pragma once
#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector>

struct ZipEntry {
    QString name;
    quint32 compressedSize = 0;
    quint32 size = 0;
    quint32 localHeaderOffset = 0;
    quint32 crc = 0;
    quint32 externalAttributes = 0;
    quint16 method = 0;
    quint16 flags = 0;
};

class ZipArchive {
public:
    bool open(const QString& fileName, QString* error = nullptr);
    QList<ZipEntry> entries() const { return entries_; }
    QByteArray read(const QString& entryName, QString* error = nullptr) const;
    bool extractUnder(const QString& destinationRoot, const QString& prefix = {}, QString* error = nullptr) const;
    static bool createFromDirectory(const QString& sourceDirectory, const QString& destinationZip,
                                    QString* error = nullptr);
    static bool isSafeRelativePath(const QString& path);
private:
    QString fileName_;
    QList<ZipEntry> entries_;
};
