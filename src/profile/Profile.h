#ifndef FIREFOXPROFILERUNNER_PROFILE_H
#define FIREFOXPROFILERUNNER_PROFILE_H

#include <KSharedConfig>
#include <QList>
#include <QtCore/QString>
#include <ostream>

class Profile
{
public:
    QString name;
    QString launchCommand;
    QString launchName;
    QString path;
    int priority;
    bool isDefault;
    bool isEdited;
    bool launchNormalWindowWithProxychains = false;
    bool extraNormalWindowProxychainsLaunchOption = false;
    int extraNormalWindowProxychainsOptionPriority = 0;

    int privateWindowPriority = 0;
    bool launchPrivateWindowWithProxychains = false;
    bool extraPrivateWindowProxychainsLaunchOption = false;
    int extraPrivateWindowProxychainsOptionPriority = 0;

    void writeSettings(KSharedConfigPtr firefoxConfig, int initialPriority = 0) const;

    void toString() const;

    void writeConfigChanges(KSharedConfigPtr firefoxConfig, const QString &forceNewInstance = "") const;
};

#endif // FIREFOXPROFILERUNNER_PROFILE_H
