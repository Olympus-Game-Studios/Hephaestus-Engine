#include "UI/Launcher/ProjectCardModel.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>

#include <iostream>
#include <string>

namespace {

bool Check(const QString& actual, const QString& expected, const std::string& message)
{
    if (actual != expected) {
        std::cerr << "[FAIL] " << message << ": got '" << actual.toStdString() << "', expected '"
                  << expected.toStdString() << "'\n";
        return false;
    }
    std::cout << "[PASS] " << message << "\n";
    return true;
}

bool TestRelativeTimeText()
{
    using Hephaestus::Launcher::RelativeTimeText;
    const QDateTime now(QDate(2026, 11, 3), QTime(15, 0));

    bool pass = true;
    pass &= Check(RelativeTimeText(QDateTime(), now), QString(), "invalid date is blank");
    pass &= Check(RelativeTimeText(now.addSecs(30), now), QStringLiteral("Just now"), "future time");
    pass &= Check(RelativeTimeText(now.addSecs(-20), now), QStringLiteral("Just now"), "seconds ago");
    pass &= Check(RelativeTimeText(now.addSecs(-60), now), QStringLiteral("1 minute ago"), "one minute");
    pass &= Check(RelativeTimeText(now.addSecs(-45 * 60), now), QStringLiteral("45 minutes ago"), "minutes");
    pass &= Check(RelativeTimeText(now.addSecs(-3600), now), QStringLiteral("1 hour ago"), "one hour");
    pass &= Check(RelativeTimeText(now.addSecs(-2 * 3600), now), QStringLiteral("2 hours ago"), "hours");
    pass &= Check(RelativeTimeText(QDateTime(QDate(2026, 11, 2), QTime(23, 30)), now), QStringLiteral("Yesterday"),
                  "previous calendar day is yesterday even within 24 hours");
    pass &= Check(RelativeTimeText(now.addDays(-3), now), QStringLiteral("3 days ago"), "days");
    pass &= Check(RelativeTimeText(now.addDays(-7), now), QStringLiteral("Last week"), "seven days");
    pass &= Check(RelativeTimeText(now.addDays(-13), now), QStringLiteral("Last week"), "thirteen days");
    const QDateTime older(QDate(2026, 10, 12), QTime(9, 0));
    pass &= Check(RelativeTimeText(older, now), QLocale().toString(older.date(), QStringLiteral("d MMM yyyy")),
                  "older dates show the date");
    return pass;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (!TestRelativeTimeText()) {
        std::cerr << "LauncherModelTests FAILED\n";
        return 1;
    }
    std::cout << "LauncherModelTests PASSED\n";
    return 0;
}
