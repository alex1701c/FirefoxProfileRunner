#include "ProfileManager.h"
#include <KConfigGroup>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>

ProfileManager::ProfileManager()
{
    firefoxProfilesIniPath = QDir::homePath() + "/.mozilla/firefox/profiles.ini";
    firefoxDesktopFile = getDesktopFilePath();
    launchCommand = getLaunchCommand();
    defaultPath = getDefaultProfilePath();
}

/**
 * Synchronizes  profiles with firefox.desktop file based on forceSync and settings, returns custom profiles
 * @param forceSync
 */
QList<Profile> ProfileManager::syncAndGetCustomProfiles(KConfigGroup &grp, bool forceSync)
{
    KSharedConfigPtr firefoxConfig = KSharedConfig::openConfig(firefoxDesktopFile);
    firefoxConfig->reparseConfiguration();
    grp.config()->reparseConfiguration();
    const QString lastHash = grp.readEntry("lastHash");
    bool hasChanged = false;
    QFile file(firefoxProfilesIniPath);
    if (file.open(QFile::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        if (hash.addData(&file)) {
            const QString newHash = hash.result().toHex();
            grp.writeEntry("lastHash", newHash);
            hasChanged = lastHash != newHash;
        }
    }
    if (forceSync || hasChanged) {
        QList<Profile> firefoxProfiles = getFirefoxProfiles();
        syncDesktopFile(firefoxProfiles, firefoxConfig, grp);
    }
    return getCustomProfiles(firefoxConfig);
}

/**
 * Get raw profiles from profiles.ini file, these contain just the name, path and launch command
 */
QList<Profile> ProfileManager::getFirefoxProfiles()
{
    QList<Profile> profiles;
    const KSharedConfigPtr firefoxProfilesIni = KSharedConfig::openConfig(firefoxProfilesIniPath);
    firefoxProfilesIni->reparseConfiguration();
    const QStringList configs = firefoxProfilesIni->groupList().filter(QRegularExpression(R"(Profile.*)"));

    for (const auto &profileEntry : configs) {
        Profile profile;
        KConfigGroup profileConfig = firefoxProfilesIni->group(profileEntry);
        profile.launchCommand = launchCommand;
        profile.launchName = profileConfig.readEntry("Name");
        profile.path = profileConfig.readEntry("Path");
        profiles.append(profile);
    }
    return profiles;
}

/**
 * Read profiles from firefox.desktop file, these profiles have priority etc. configured
 * @param firefoxConfig KSharedConfigPtr of firefox.desktop file
 */
QList<Profile> ProfileManager::getCustomProfiles(KSharedConfigPtr firefoxConfig)
{
    QList<Profile> profiles;
    if (firefoxDesktopFile == "<error>") {
        return profiles;
    }
    const QStringList installedProfiles = firefoxConfig->groupList().filter(QRegularExpression("Desktop Action new-window-with-profile-.*"));
    for (const auto &profileGroupName : installedProfiles) {
        auto profileGroup = firefoxConfig->group(profileGroupName);
        if (!profileGroup.exists() || profileGroup.keyList().isEmpty()) {
            continue;
        }
        Profile profile;
        profile.name = profileGroup.readEntry("Name");
        profile.launchName = profileGroup.readEntry("LaunchName");
        profile.path = QString(profileGroupName).remove("Desktop Action new-window-with-profile-");
        if (defaultPath == "<invalid>") {
            defaultPath = profile.path;
        }
        profile.isDefault = profile.path == defaultPath;
        profile.isEdited = profileGroup.readEntry("Edited", false);
        profile.priority = profileGroup.readEntry("Priority", 0);
        profile.launchCommand = launchCommand;
        profile.privateWindowPriority = profileGroup.readEntry("PrivateWindowPriority", 0);
        profiles.append(profile);
    }
    std::sort(profiles.begin(), profiles.end(), [](const Profile &profile1, const Profile &profile2) -> bool {
        return profile1.priority > profile2.priority;
    });
#ifdef status_dev
    qInfo() << "Found profiles: " << profiles.count();
    qInfo() << "Listing data of profiles:";
    for (const auto &profile : profiles)
        profile.toString();
#endif
    return profiles;
}

/**
 * Install/Update/Delete profiles from firefox.desktop files and update the registered Desktop Actions
 * @param profiles
 * @param firefoxConfig
 */
void ProfileManager::syncDesktopFile(const QList<Profile> &profiles, KSharedConfigPtr firefoxConfig, const KConfigGroup &config)
{
    if (firefoxDesktopFile == "<error>") {
        return;
    }
    KConfigGroup generalConfig = firefoxConfig->group("Desktop Entry");
    const QStringList installedProfiles = firefoxConfig->groupList().filter(QRegularExpression("Desktop Action new-window-with-profile-.*"));

    QStringList deleted;

    // Update/mark to delete installed profiles
    for (auto &installedProfile : installedProfiles) {
        bool found = false;
        for (const auto &profile : profiles) {
            if (installedProfile == "Desktop Action new-window-with-profile-" + profile.path) {
                found = true;
                if (profile.launchName != firefoxConfig->group(installedProfile).readEntry("LaunchName")) {
#ifdef status_dev
                    qInfo() << "Update " << profile.launchName;
#endif
                    profile.writeSettings(firefoxConfig);
                }
            }
        }
        if (!found) {
#ifdef status_dev
            qInfo() << "Delete " << installedProfile;
#endif
            // Delete normal and private window Desktop Action
            const QString profileName = QString(installedProfile).remove("Desktop Action new-window-with-profile-");
            firefoxConfig->deleteGroup(installedProfile);
            firefoxConfig->deleteGroup("Desktop Action new-private-window-with-profile-" + profileName);
            firefoxConfig->deleteGroup("Desktop Action new-private-window-with-profile-" + profileName);
        }
    }
    // Add group and register action
    int idx = 1;
    for (const auto &profile : profiles) {
        if (!firefoxConfig->hasGroup("Desktop Action new-window-with-profile-" + profile.path)) {
#ifdef status_dev
            qInfo() << "Install  " << profile.launchName;
#endif
            // Write settings for normal and private window
            profile.writeSettings(firefoxConfig, idx);
            ++idx;
        }
    }
    firefoxConfig->sync();

    bool enableNormal = config.readEntry(Config::RegisterNormalWindows, true);
    bool enablePrivate = config.readEntry(Config::RegisterPrivateWindows, true);
    changeProfileRegistering(enableNormal, enablePrivate, firefoxConfig);
}

/**
 * Register/Unregister Desktop Actions
 * @param enableNormal
 * @param enablePrivate
 * @param firefoxConfig
 */
void ProfileManager::changeProfileRegistering(bool enableNormal, bool enablePrivate, KSharedConfigPtr firefoxConfig)
{
    QString registeredActions = "new-window;new-private-window;";
    if (firefoxDesktopFile.endsWith("firefox-esr.desktop")) {
        registeredActions.clear();
    }
    QStringList desktopActions = firefoxConfig->groupList().filter("Desktop Action");
    for (auto &groupName : desktopActions) {
        if (firefoxConfig->group(groupName).keyList().isEmpty()) {
            continue;
        }
        if (enableNormal && groupName.startsWith("Desktop Action new-window-with-profile")) {
            registeredActions.append(groupName.remove("Desktop Action ") + ";");
        } else if (enablePrivate && groupName.startsWith("Desktop Action new-private-window-with-profile-")) {
            registeredActions.append(groupName.remove("Desktop Action ") + ";");
        }
    }
#ifdef status_dev
    qInfo() << "Registered Desktop Actions:\n" << registeredActions;
#endif
    firefoxConfig->group("Desktop Entry").writeEntry("Actions", registeredActions);
    firefoxConfig->group("Desktop Entry").sync();
}

/**
 * Get launch command from .desktop file, this command is different in firefox-esr
 */
QString ProfileManager::getLaunchCommand() const
{
    return KSharedConfig::openConfig(firefoxDesktopFile)->group("Desktop Entry").readEntry("Exec").remove(" %u");
}

/**
 * Get the Path property of the default profile
 */
QString ProfileManager::getDefaultProfilePath() const
{
    const KSharedConfigPtr firefoxProfilesIni = KSharedConfig::openConfig(firefoxProfilesIniPath);
    const QStringList configs = firefoxProfilesIni->groupList();
    const QStringList installConfig = configs.filter(QRegularExpression(R"(Install.*)"));
    QString path;
    if (!installConfig.empty()) {
        path = firefoxProfilesIni->group(installConfig.first()).readEntry("Default");
    }
    if (!path.isEmpty()) {
        return path;
    }
    for (const auto &profileName : firefoxProfilesIni->groupList().filter(QRegularExpression(R"(Profile.*)"))) {
        const auto profile = firefoxProfilesIni->group(profileName);
        if (profile.readEntry("Default", 0) == 1) {
            return profile.readEntry("Path");
        }
    }
    return "<invalid>";
}

/**
 * Get path to firefox*.desktop file, checks both options for firefox and firefox-esr
 */
QString ProfileManager::getDesktopFilePath(bool quiet)
{
    QString file(QDir::homePath() + "/.local/share/applications/firefox.desktop");
    if (!QFile::exists(file)) {
        file = QDir::homePath() + "/.local/share/applications/firefox-esr.desktop";
        if (!QFile::exists(file)) {
            if (!quiet) {
                qWarning() << "Can not find a firefox.desktop or firefox-esr.desktop file in ~/.local/share/applications/";
            }
            return "<error>";
        }
    }
    return file;
}
