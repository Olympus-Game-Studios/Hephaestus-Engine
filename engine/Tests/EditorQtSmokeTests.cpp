/**
 * @file EditorQtSmokeTests.cpp
 * @brief Minimal Qt Test harness (opt-in via HEPHAESTUS_BUILD_QT_TESTS).
 *
 * Avoids Vulkan and full EditorMainWindow; only validates basic widget state
 * patterns used by Asset Browser search UX.
 */

#include <QtTest/QtTest>
#include <QLineEdit>

class EditorQtSmokeTests : public QObject {
    Q_OBJECT
private slots:
    void searchBoxClearRestoresEmptyQuery() {
        QLineEdit box;
        box.setClearButtonEnabled(true);
        box.setText(QStringLiteral("Capsule"));
        QCOMPARE(box.text(), QStringLiteral("Capsule"));
        box.clear();
        QVERIFY(box.text().isEmpty());
    }
};

QTEST_MAIN(EditorQtSmokeTests)
#include "EditorQtSmokeTests.moc"
