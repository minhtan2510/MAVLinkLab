#pragma once

#include <QString>
#include <QStringList>
#include <tuple>

#ifndef MAVLINK_LAB_VERSION
#define MAVLINK_LAB_VERSION "1.2.0"
#endif

namespace Version
{
constexpr const char *kProjectName = "MAVLinkLab";
constexpr const char *kCurrentVersion = MAVLINK_LAB_VERSION;
constexpr const char *kGithubRepo = "minhtan2510/MAVLinkLab";
constexpr const char *kLatestReleaseApiUrl =
    "https://api.github.com/repos/minhtan2510/MAVLinkLab/releases/latest";
constexpr const char *kReleasesPageUrl =
    "https://github.com/minhtan2510/MAVLinkLab/releases";

/**
 * @brief Parse a semantic version string into major, minor, patch integers.
 * @param version String representation such as "v1.1.1" or "0.1.0".
 * @return std::tuple<int, int, int> for straightforward lexicographical comparison.
 */
inline std::tuple<int, int, int> parseSemVer(const QString &version)
{
    QString clean = version.trimmed();
    if (clean.startsWith('v', Qt::CaseInsensitive))
    {
        clean.remove(0, 1);
    }

    const int dashIdx = clean.indexOf('-');
    if (dashIdx >= 0)
    {
        clean = clean.left(dashIdx);
    }
    const int plusIdx = clean.indexOf('+');
    if (plusIdx >= 0)
    {
        clean = clean.left(plusIdx);
    }

    const QStringList parts = clean.split('.');
    const int major = parts.value(0).toInt();
    const int minor = parts.value(1).toInt();
    const int patch = parts.value(2).toInt();
    return {major, minor, patch};
}

/**
 * @brief Check if a remote version is strictly newer than current version.
 * @param remoteVersion Version tag from GitHub (e.g. "v1.2.0").
 * @param currentVersion Current application version (e.g. "1.1.1").
 * @return True if remoteVersion > currentVersion.
 */
inline bool isNewerVersion(const QString &remoteVersion, const QString &currentVersion)
{
    return parseSemVer(remoteVersion) > parseSemVer(currentVersion);
}

} // namespace Version

