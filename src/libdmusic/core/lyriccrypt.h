// SPDX-FileCopyrightText: 2023 - 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LYRICCRYPT_H
#define LYRICCRYPT_H

#include <QString>
#include <QByteArray>

namespace LyricCrypt {

// MD5 hash -> hex string
QString md5(const QByteArray &data);
QString md5Hex(const QByteArray &data);

// AES-ECB with PKCS#7 padding (OpenSSL)
QByteArray aesEcbEncrypt(const QByteArray &plain, const QByteArray &key);
QByteArray aesEcbDecrypt(const QByteArray &cipher, const QByteArray &key);

// zlib decompress (auto-detect gzip/zlib header)
QByteArray zlibDecompress(const QByteArray &data);

// Base64
QByteArray base64Decode(const QString &str);
QString base64Encode(const QByteArray &data);

} // namespace LyricCrypt

#endif // LYRICCRYPT_H
