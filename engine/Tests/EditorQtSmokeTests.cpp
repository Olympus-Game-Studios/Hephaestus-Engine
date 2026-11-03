/**
 * @file EditorQtSmokeTests.cpp
 * @brief Minimal Qt Test harness (opt-in via HEPHAESTUS_BUILD_QT_TESTS).
 *
 * Avoids Vulkan and full EditorMainWindow; only validates basic widget state
 * patterns used by Asset Browser search UX.
 */

#include "UI/QtEditor/PropertyEditors.h"
#include "UI/QtEditor/EditorCommandSearch.h"
#include "UI/QtEditor/EditorTheme.h"

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QAbstractItemModel>
#include <QCompleter>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>

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

    // Every colour token and icon the stylesheet names must resolve.
    void editorThemeResolvesStylesheet() {
        using namespace Hephaestus::QtEditor;
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Editor theme|not in the icon set")));
        Theme::Apply(*qApp);
        QVERIFY(!qApp->styleSheet().isEmpty());
        QVERIFY(!qApp->styleSheet().contains(QLatin1Char('@')));
        QVERIFY(!Theme::Icon(QStringLiteral("cube")).isNull());
    }

    // The command search lists enabled menu commands by path and runs the one picked.
    void commandSearchRunsMatchingMenuAction() {
        QMenuBar bar;
        QMenu* file = bar.addMenu(QStringLiteral("&File"));
        QAction* save = file->addAction(QStringLiteral("&Save Level"));
        QAction* disabled = file->addAction(QStringLiteral("Save Disabled"));
        disabled->setEnabled(false);
        QMenu* recent = file->addMenu(QStringLiteral("Recent"));
        recent->addAction(QStringLiteral("Open Sample"));

        Hephaestus::QtEditor::EditorCommandSearch search(&bar, nullptr);
        QFocusEvent focusIn(QEvent::FocusIn);
        QApplication::sendEvent(&search, &focusIn);

        QCompleter* completer = search.completer();
        completer->setCompletionPrefix(QStringLiteral("save"));
        QCOMPARE(completer->completionCount(), 1);
        completer->setCompletionPrefix(QStringLiteral("sample"));
        QCOMPARE(completer->completionCount(), 1);
        QVERIFY(completer->completionModel()->index(0, 0).data().toString().startsWith(QStringLiteral("File")));

        completer->setCompletionPrefix(QStringLiteral("save level"));
        QSignalSpy triggered(save, &QAction::triggered);
        emit completer->activated(completer->completionModel()->index(0, 0));
        QCOMPARE(triggered.count(), 1);
        QVERIFY(search.text().isEmpty());
    }
};

QTEST_MAIN(EditorQtSmokeTests)
#include "EditorQtSmokeTests.moc"
