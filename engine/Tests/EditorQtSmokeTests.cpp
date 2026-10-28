/**
 * @file EditorQtSmokeTests.cpp
 * @brief Minimal Qt Test harness (opt-in via HEPHAESTUS_BUILD_QT_TESTS).
 *
 * Avoids Vulkan and full EditorMainWindow; only validates basic widget state
 * patterns used by Asset Browser search UX.
 */

#include "UI/QtEditor/PropertyEditors.h"

#include <QtTest/QtTest>
#include <QCheckBox>
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

    // Inspector checkboxes filled from one value must turn back on with a single click.
    void unmixedInspectorCheckboxTogglesBothWays() {
        using namespace Hephaestus::QtEditor;
        QCheckBox box;
        Hephaestus::MixedBool off;
        off.present = true;
        off.value = false;
        SetCheckFromMixed(&box, off);
        box.click();
        bool value = false;
        QVERIFY(TryReadCheck(&box, value));
        QVERIFY(value);
        box.click();
        QVERIFY(TryReadCheck(&box, value));
        QVERIFY(!value);
    }

    void mixedInspectorCheckboxResolvesOnClick() {
        using namespace Hephaestus::QtEditor;
        QCheckBox box;
        Hephaestus::MixedBool mixed;
        mixed.present = true;
        mixed.mixed = true;
        SetCheckFromMixed(&box, mixed);
        bool value = false;
        QVERIFY(!TryReadCheck(&box, value));
        box.click();
        QVERIFY(TryReadCheck(&box, value));
        QVERIFY(value);
    }
};

QTEST_MAIN(EditorQtSmokeTests)
#include "EditorQtSmokeTests.moc"
