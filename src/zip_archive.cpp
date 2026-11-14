#include "zip_archive.h"
#include <QDir>
#include <QDirIterator>
#include <algorithm>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <zlib.h>
#include <limits>

static quint16 u16(const QByteArray& b, qsizetype p) {
    return quint16(quint8(b[p])) | (quint16(quint8(b[p + 1])) << 8);
}
static quint32 u32(const QByteArray& b, qsizetype p) {
    return quint32(quint8(b[p])) | (quint32(quint8(b[p + 1])) << 8) |
           (quint32(quint8(b[p + 2])) << 16) | (quint32(quint8(b[p + 3])) << 24);
}
static void append16(QByteArray& b, quint16 v) {
    b.append(char(v & 0xff)); b.append(char((v >> 8) & 0xff));
}
static void append32(QByteArray& b, quint32 v) {
    append16(b, quint16(v & 0xffff)); append16(b, quint16(v >> 16));
}

bool ZipArchive::open(const QString& fileName, QString* error) {
    entries_.clear();
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly)) { if (error) *error = f.errorString(); return false; }
    const qint64 size = f.size();
    const qint64 tailSize = qMin<qint64>(size, 22 + 65535 + 20);
    if (tailSize < 22 || !f.seek(size - tailSize)) { if (error) *error = "ZIP başlığı okunamadı."; return false; }
    const QByteArray tail = f.read(tailSize);
    const QByteArray sig("PK\x05\x06", 4);
    const qsizetype eocd = tail.lastIndexOf(sig);
    if (eocd < 0 || eocd + 22 > tail.size()) { if (error) *error = "ZIP merkezi dizini bulunamadı."; return false; }
    const quint16 disk = u16(tail, eocd + 4);
    const quint16 centralDisk = u16(tail, eocd + 6);
    const quint16 count = u16(tail, eocd + 10);
    const quint32 centralSize = u32(tail, eocd + 12);
    const quint32 centralOffset = u32(tail, eocd + 16);
    if (disk || centralDisk || count == 0xffff || centralOffset == 0xffffffffu || centralSize == 0xffffffffu) {
        if (error) *error = "Bu ZIP biçimi (multi-disk/ZIP64) desteklenmiyor.";
        return false;
    }
    if (quint64(centralOffset) + centralSize > quint64(size) || !f.seek(centralOffset)) {
        if (error) *error = "ZIP merkezi dizini geçersiz."; return false;
    }
    for (quint16 i = 0; i < count; ++i) {
        const QByteArray h = f.read(46);
        if (h.size() != 46 || h.left(4) != QByteArray("PK\x01\x02", 4)) {
            if (error) *error = "ZIP girdisi bozuk."; entries_.clear(); return false;
        }
        ZipEntry e;
        e.flags = u16(h, 8); e.method = u16(h, 10); e.crc = u32(h, 16);
        e.compressedSize = u32(h, 20); e.size = u32(h, 24);
        const quint16 nameLen = u16(h, 28), extraLen = u16(h, 30), commentLen = u16(h, 32);
        e.externalAttributes = u32(h, 38); e.localHeaderOffset = u32(h, 42);
        if (e.compressedSize == 0xffffffffu || e.size == 0xffffffffu || e.localHeaderOffset == 0xffffffffu) {
            if (error) *error = "ZIP64 girdisi desteklenmiyor."; entries_.clear(); return false;
        }
        const QByteArray name = f.read(nameLen);
        if (name.size() != nameLen || !f.seek(f.pos() + extraLen + commentLen)) {
            if (error) *error = "ZIP dosya adı okunamadı."; entries_.clear(); return false;
        }
        e.name = QString::fromUtf8(name);
        if (!(e.flags & (1u << 11))) e.name = QString::fromLocal8Bit(name);
        entries_.append(e);
    }
    fileName_ = fileName;
    return true;
}

bool ZipArchive::isSafeRelativePath(const QString& raw) {
    QString path = raw;
    path.replace('\\', '/');
    if (path.isEmpty() || path.startsWith('/') || path.startsWith("//") ||
        QRegularExpression("^[A-Za-z]:").match(path).hasMatch()) return false;
    const QStringList parts = path.split('/', Qt::KeepEmptyParts);
    for (const QString& part : parts) if (part == ".." || part.isEmpty()) return false;
    return true;
}

QByteArray ZipArchive::read(const QString& entryName, QString* error) const {
    auto it = std::find_if(entries_.cbegin(), entries_.cend(), [&](const ZipEntry& e) { return e.name == entryName; });
    if (it == entries_.cend()) { if (error) *error = "ZIP içinde dosya bulunamadı: " + entryName; return {}; }
    const ZipEntry& e = *it;
    if (e.flags & 1u) { if (error) *error = "Şifreli ZIP girdileri desteklenmiyor."; return {}; }
    if (e.size > quint32(std::numeric_limits<int>::max()) || e.compressedSize > quint32(std::numeric_limits<int>::max())) {
        if (error) *error = "ZIP girdisi tek parçada işlenemeyecek kadar büyük."; return {};
    }
    QFile f(fileName_);
    if (!f.open(QIODevice::ReadOnly) || !f.seek(e.localHeaderOffset)) { if (error) *error = "ZIP girdisi açılamadı."; return {}; }
    const QByteArray local = f.read(30);
    if (local.size() != 30 || local.left(4) != QByteArray("PK\x03\x04", 4)) { if (error) *error = "ZIP yerel başlığı bozuk."; return {}; }
    const quint16 nameLen = u16(local, 26), extraLen = u16(local, 28);
    if (!f.seek(f.pos() + nameLen + extraLen)) { if (error) *error = "ZIP girdisi konumu bozuk."; return {}; }
    const QByteArray compressed = f.read(e.compressedSize);
    if (compressed.size() != int(e.compressedSize)) { if (error) *error = "ZIP verisi eksik."; return {}; }
    QByteArray out;
    out.resize(int(e.size));
    if (e.method == 0) {
        if (compressed.size() != out.size()) { if (error) *error = "ZIP stored girdisinin boyutu eşleşmiyor."; return {}; }
        out = compressed;
    } else if (e.method == 8) {
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
        stream.avail_in = uInt(compressed.size());
        stream.next_out = reinterpret_cast<Bytef*>(out.data());
        stream.avail_out = uInt(out.size());
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) { if (error) *error = "Deflate açıcı başlatılamadı."; return {}; }
        const int rc = inflate(&stream, Z_FINISH);
        inflateEnd(&stream);
        if (rc != Z_STREAM_END || stream.total_out != e.size) { if (error) *error = "ZIP deflate girdisi açılamadı."; return {}; }
    } else { if (error) *error = "ZIP sıkıştırma yöntemi desteklenmiyor."; return {}; }
    const uLong crc = crc32(0, reinterpret_cast<const Bytef*>(out.constData()), uInt(out.size()));
    if (quint32(crc) != e.crc) { if (error) *error = "ZIP CRC doğrulaması başarısız."; return {}; }
    return out;
}

bool ZipArchive::extractUnder(const QString& destinationRoot, const QString& prefix, QString* error) const {
    // Keep both sides of the containment check in Qt's '/' form. On Windows,
    // absolutePath() uses '/', while QDir::separator() is '\\'; mixing them
    // made every legitimate archive entry look like a path traversal.
    const QString root = QDir::cleanPath(
        QDir::fromNativeSeparators(QDir(destinationRoot).absolutePath()));
    QString rootPrefix = root;
    if (!rootPrefix.endsWith('/')) rootPrefix += '/';
    QDir().mkpath(root);
    QString normalizedPrefix = prefix;
    normalizedPrefix.replace('\\', '/');
    if (!normalizedPrefix.isEmpty() && !normalizedPrefix.endsWith('/')) normalizedPrefix += '/';
    for (const ZipEntry& e : entries_) {
        QString name = e.name; name.replace('\\', '/');
        if (!normalizedPrefix.isEmpty()) {
            if (!name.startsWith(normalizedPrefix)) continue;
            name = name.mid(normalizedPrefix.size());
        }
        if (name.isEmpty() || name.endsWith('/')) continue;
        // POSIX symlink entries are deliberately skipped; no archive link may escape the profile.
        const quint32 unixMode = e.externalAttributes >> 16;
        if ((unixMode & 0170000u) == 0120000u) continue;
        if (!isSafeRelativePath(name) || (e.flags & 1u)) { if (error) *error = "Arşivde güvenli olmayan dosya yolu: " + name; return false; }
        const QString target = QDir(root).absoluteFilePath(name);
        const QString cleanTarget = QDir::fromNativeSeparators(QDir::cleanPath(target));
        if (!cleanTarget.startsWith(rootPrefix, Qt::CaseInsensitive)) {
            if (error) *error = "Arşiv dosyası profil klasörü dışına çıkmaya çalıştı."; return false;
        }
        QString why;
        const QByteArray bytes = read(e.name, &why);
        if (!why.isEmpty()) { if (error) *error = why; return false; }
        if (!QDir().mkpath(QFileInfo(cleanTarget).absolutePath())) { if (error) *error = "Klasör oluşturulamadı."; return false; }
        QFile out(cleanTarget);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(bytes) != bytes.size()) {
            if (error) *error = "Dosya çıkarılamadı: " + name; return false;
        }
    }
    return true;
}

bool ZipArchive::createFromDirectory(const QString& sourceDirectory, const QString& destinationZip,
                                     QString* error) {
    struct FileRecord { QString absolute; QByteArray name; quint32 size = 0; quint32 crc = 0; quint32 offset = 0; };
    const QString source = QDir::cleanPath(QFileInfo(sourceDirectory).absoluteFilePath());
    if (!QFileInfo(source).isDir()) { if (error) *error = "Dışa aktarılacak profil klasörü bulunamadı."; return false; }
    QList<FileRecord> records;
    QDirIterator it(source, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString absolute = it.next();
        const QFileInfo info(absolute);
        if (info.isSymLink()) continue;
        const QString relative = QDir(source).relativeFilePath(absolute).replace('\\', '/');
        if (!isSafeRelativePath(relative) || relative.toUtf8().size() > 65535 || info.size() > qint64(0xffffffffu)) {
            if (error) *error = "Profilde ZIP dışa aktarımına uygun olmayan bir dosya var.";
            return false;
        }
        QFile file(absolute);
        if (!file.open(QIODevice::ReadOnly)) { if (error) *error = file.errorString(); return false; }
        uLong crc = crc32(0L, Z_NULL, 0);
        quint64 size = 0;
        while (!file.atEnd()) {
            const QByteArray chunk = file.read(1024 * 1024);
            if (chunk.isEmpty() && file.error() != QFileDevice::NoError) { if (error) *error = file.errorString(); return false; }
            crc = crc32(crc, reinterpret_cast<const Bytef*>(chunk.constData()), uInt(chunk.size()));
            size += quint64(chunk.size());
            if (size > 0xffffffffu) { if (error) *error = "4 GB üzerindeki dosyalar dışa aktarılamıyor."; return false; }
        }
        records.append({absolute, relative.toUtf8(), quint32(size), quint32(crc), 0});
        if (records.size() >= 65535) { if (error) *error = "Profil ZIP içinde çok fazla dosya içeriyor."; return false; }
    }
    const QString temporary = destinationZip + ".part";
    QFile::remove(temporary);
    QFile output(temporary);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { if (error) *error = output.errorString(); return false; }
    QByteArray central;
    for (FileRecord& record : records) {
        if (output.pos() > qint64(0xffffffffu)) { output.close(); QFile::remove(temporary); if (error) *error = "4 GB üzerindeki profiller ZIP dışa aktarımında desteklenmiyor."; return false; }
        record.offset = quint32(output.pos());
        QByteArray local;
        append32(local, 0x04034b50); append16(local, 20); append16(local, 1u << 11); append16(local, 0);
        append16(local, 0); append16(local, 0); append32(local, record.crc); append32(local, record.size);
        append32(local, record.size); append16(local, quint16(record.name.size())); append16(local, 0);
        local += record.name;
        if (output.write(local) != local.size()) { output.close(); QFile::remove(temporary); if (error) *error = output.errorString(); return false; }
        QFile input(record.absolute);
        if (!input.open(QIODevice::ReadOnly)) { output.close(); QFile::remove(temporary); if (error) *error = input.errorString(); return false; }
        while (!input.atEnd()) {
            const QByteArray chunk = input.read(1024 * 1024);
            if (chunk.isEmpty() && input.error() != QFileDevice::NoError) { output.close(); QFile::remove(temporary); if (error) *error = input.errorString(); return false; }
            if (output.write(chunk) != chunk.size()) { output.close(); QFile::remove(temporary); if (error) *error = output.errorString(); return false; }
        }
        QByteArray entry;
        append32(entry, 0x02014b50); append16(entry, 20); append16(entry, 20); append16(entry, 1u << 11); append16(entry, 0);
        append16(entry, 0); append16(entry, 0); append32(entry, record.crc); append32(entry, record.size);
        append32(entry, record.size); append16(entry, quint16(record.name.size())); append16(entry, 0); append16(entry, 0);
        append16(entry, 0); append16(entry, 0); append32(entry, 0x81a40000u); append32(entry, record.offset);
        entry += record.name;
        central += entry;
        if (central.size() > qint64(0xffffffffu)) { output.close(); QFile::remove(temporary); if (error) *error = "ZIP dizini çok büyük."; return false; }
    }
    const quint32 centralOffset = quint32(output.pos());
    if (output.write(central) != central.size()) { output.close(); QFile::remove(temporary); if (error) *error = output.errorString(); return false; }
    QByteArray end;
    append32(end, 0x06054b50); append16(end, 0); append16(end, 0);
    append16(end, quint16(records.size())); append16(end, quint16(records.size()));
    append32(end, quint32(central.size())); append32(end, centralOffset); append16(end, 0);
    if (output.write(end) != end.size() || !output.flush()) { output.close(); QFile::remove(temporary); if (error) *error = output.errorString(); return false; }
    output.close();
    QFile::remove(destinationZip);
    if (!QFile::rename(temporary, destinationZip)) { QFile::remove(temporary); if (error) *error = "Dışa aktarılan ZIP hedefe taşınamadı."; return false; }
    return true;
}

