/**
 * @file EditorQtSmokeTests.cpp
 * @brief Minimal Qt Test harness (opt-in via HEPHAESTUS_BUILD_QT_TESTS).
 *
 * Avoids Vulkan and full EditorMainWindow; only validates basic widget state
 * patterns used by Asset Browser search UX.
 */

#include "UI/QtEditor/PropertyEditors.h"
#include "UI/QtEditor/EditorCommandSearch.h"
#include "UI/QtEditor/CodeEditorPanel.h"
#include "UI/QtEditor/ProjectSearchPanel.h"
#include "Assets/AssetManager.h"
#include "UI/QtEditor/EditorConsolePanel.h"
#include "Core/EngineLog.h"
#include "UI/QtEditor/EditorTheme.h"
#include "UI/QtEditor/QtViewportWindow.h"
#include "UI/QtEditor/KitEditorDialog.h"
#include "Construction/ConstructionKit.h"

#include <QtTest/QtTest>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QTableWidget>
#include <QSettings>
#include <QAbstractItemModel>
#include <QCompleter>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QToolButton>
#include <QTreeWidget>

class EditorQtSmokeTests : public QObject {
    Q_OBJECT
private slots:
    // The kit editor lists a kit's pieces, tries a wall length with them and saves edits back to the file.
    void kitEditorEditsAndSaves() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const std::filesystem::path file = std::filesystem::path(dir.filePath("Stone.hpkit").toStdWString());
        Hephaestus::Construction::KitDefinition kit;
        kit.name = "Stone";
        kit.pieces = {{"assets/Kits/Panel2m.hpprefab", "wall", 2.0f, 0.05f, 0.0f, 0.0f},
                      {"assets/Kits/Panel1m.hpprefab", "wall", 1.0f, 0.05f, 0.0f, 0.0f}};
        std::string error;
        QVERIFY(Hephaestus::Construction::SaveKit(file, kit, error));

        auto* dialog = new Hephaestus::QtEditor::KitEditorDialog(dir.filePath("Stone.hpkit"));
        auto* table = dialog->findChild<QTableWidget*>();
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 2);
        bool trialShown = false;
        for (QLabel* label : dialog->findChildren<QLabel*>()) {
            trialShown = trialShown || label->text() == QStringLiteral("Panel2m x3, no stretch.");
        }
        QVERIFY2(trialShown, "a 6 m wall is tried with three 2 m panels");
        QVERIFY(!dialog->windowTitle().endsWith('*'));

        auto* width = qobject_cast<QDoubleSpinBox*>(table->cellWidget(1, 2));
        QVERIFY(width);
        width->setValue(1.5);
        QVERIFY(dialog->windowTitle().endsWith('*'));
        auto* save = dialog->findChild<QPushButton*>();
        for (QPushButton* button : dialog->findChildren<QPushButton*>()) {
            if (button->text().remove('&') == QStringLiteral("Save")) save = button;
        }
        QVERIFY(save && save->isEnabled());
        save->click();
        QVERIFY(!dialog->windowTitle().endsWith('*'));
        delete dialog;

        Hephaestus::Construction::KitDefinition saved;
        QVERIFY(Hephaestus::Construction::LoadKit(file, saved, error));
        QCOMPARE(saved.pieces.size(), size_t(2));
        QCOMPARE(saved.pieces[1].width, 1.5f);
    }

    // Crouch (Ctrl) plus D or S must reach the game in Play, not the editor's Duplicate and Save.
    void runningGameClaimsEditorShortcutsInTheViewport() {
        Hephaestus::QtEditor::QtViewportWindow viewport;
        for (const QKeySequence& key : {QKeySequence(Qt::CTRL | Qt::Key_D), QKeySequence(Qt::CTRL | Qt::Key_S),
                                        QKeySequence(Qt::Key_H), QKeySequence(Qt::Key_F5)}) {
            const QKeyCombination combo = key[0];
            QKeyEvent editing(QEvent::ShortcutOverride, combo.key(), combo.keyboardModifiers());
            editing.ignore();
            QCoreApplication::sendEvent(&viewport, &editing);
            QVERIFY2(!editing.isAccepted(), "editor shortcuts work in the viewport outside Play");
            viewport.SetGameOwnsKeyboard(true);
            QKeyEvent playing(QEvent::ShortcutOverride, combo.key(), combo.keyboardModifiers());
            playing.ignore();
            QCoreApplication::sendEvent(&viewport, &playing);
            QVERIFY2(playing.isAccepted(), "the running game keeps the key from editor shortcuts");
            viewport.SetGameOwnsKeyboard(false);
        }
    }

    // The same through Qt's shortcut handling: a window-wide Ctrl+D action does not fire while the game plays.
    void runningGameStopsWindowShortcutsFiring() {
        QMainWindow window;
        int duplicates = 0;
        auto* duplicate = new QAction(&window);
        duplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        window.addAction(duplicate);
        connect(duplicate, &QAction::triggered, &window, [&duplicates]() { ++duplicates; });
        auto* viewport = new Hephaestus::QtEditor::QtViewportWindow();
        QList<int> gameKeys;
        viewport->SetInputEventCallback([&gameKeys](const Hephaestus::QtEditor::InputEvent& event) {
            if (event.type == Hephaestus::QtEditor::InputEvent::Type::KeyPress) gameKeys << event.key;
        });
        window.setCentralWidget(QWidget::createWindowContainer(viewport, &window));
        window.show();
        window.activateWindow();
        viewport->requestActivate();
        if (!QTest::qWaitForWindowActive(viewport)) QSKIP("the platform did not activate the test window");
        QTest::keyClick(viewport, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(duplicates, 1);
        viewport->SetGameOwnsKeyboard(true);
        gameKeys.clear();
        QTest::keyClick(viewport, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(duplicates, 1);
        QTest::keyClick(viewport, Qt::Key_Escape);
        QVERIFY2(gameKeys.contains(Qt::Key_D) && gameKeys.contains(Qt::Key_Escape),
                 "the game receives Ctrl+D as D, and Escape still arrives to stop Play");
        viewport->SetGameOwnsKeyboard(false);
        QTest::keyClick(viewport, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(duplicates, 2);
    }

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

    // The Console lists new engine log entries and filters them by level.
    void consolePanelListsAndFiltersLogEntries() {
        Hephaestus::QtEditor::EditorConsolePanel panel;
        auto* list = panel.findChild<QTreeWidget*>(QStringLiteral("ConsoleList"));
        QVERIFY(list);
        const int before = list->topLevelItemCount();
        Hephaestus::EngineLog::RecordOnly(Hephaestus::EngineLogLevel::Warning, "console test warning", "tests");
        QTRY_COMPARE(list->topLevelItemCount(), before + 1);
        QTreeWidgetItem* row = list->topLevelItem(list->topLevelItemCount() - 1);
        QCOMPARE(row->text(1), QStringLiteral("TESTS"));
        QCOMPARE(row->text(2), QStringLiteral("console test warning"));
        QToolButton* warnings = nullptr;
        for (auto* button : panel.findChildren<QToolButton*>(QStringLiteral("ConsoleLevel")))
            if (button->property("level").toString() == QLatin1String("warning")) warnings = button;
        QVERIFY(warnings && warnings->text().startsWith(QStringLiteral("Warnings")));
        warnings->setChecked(false);
        QVERIFY(row->isHidden());
        warnings->setChecked(true);
        QVERIFY(!row->isHidden());
    }

    // Script errors that name "file:line" report the location, and Lua's shortened "...tail" form is kept.
    void consolePanelReportsErrorLocations() {
        Hephaestus::QtEditor::EditorConsolePanel panel;
        QString location;
        int line = 0;
        panel.onErrorLocation = [&](const QString& file, int at, const QString&) {
            location = file;
            line = at;
        };
        Hephaestus::EngineLog::RecordOnly(Hephaestus::EngineLogLevel::Error,
            "Error while executing ...cripts/door_trigger.lua:18: attempt to index a nil value", "Script");
        QTRY_COMPARE(line, 18);
        QCOMPARE(location, QStringLiteral("...cripts/door_trigger.lua"));
    }

    void codeEditorEditsAndSearches() {
        using namespace Hephaestus::QtEditor;
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("door.lua"));
        auto write = [&](const QByteArray& bytes) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(bytes);
        };
        write("local open = false\nif open then\n    print(open)\nend\n");

        CodeEditorPanel panel;
        QString status;
        panel.onStatusChanged = [&](const QString& text) { status = text; };
        panel.resize(900, 600);
        panel.show();
        QVERIFY(panel.OpenFile(path));
        auto* editor = panel.findChild<QPlainTextEdit*>(QStringLiteral("CodeEditor"));
        QVERIFY(editor);
        QVERIFY(status.startsWith(QStringLiteral("Ln 1, Col 1")) && status.endsWith(QStringLiteral("Lua")));

        // Ctrl+/ comments the line with Lua's "--" and uncomments it again.
        QTest::keyClick(editor, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(editor->document()->firstBlock().text(), QStringLiteral("-- local open = false"));
        QTest::keyClick(editor, Qt::Key_Slash, Qt::ControlModifier);
        QCOMPARE(editor->document()->firstBlock().text(), QStringLiteral("local open = false"));

        // Brackets close themselves, and typing the closer steps over it.
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, QStringLiteral("f("));
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("f()")));
        QTest::keyClick(editor, Qt::Key_ParenRight);
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("f()")));

        // Alt+Up moves the line above its neighbour.
        QTest::keyClick(editor, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(editor->document()->findBlockByNumber(3).text(), QStringLiteral("f()"));

        // Ctrl+H, find "open", replace all with "shut".
        QTest::keyClick(editor, Qt::Key_H, Qt::ControlModifier);
        auto* find = panel.findChild<QWidget*>(QStringLiteral("CodeFindBar"));
        QVERIFY(find && find->isVisible());
        const auto fields = find->findChildren<QLineEdit*>();
        QLineEdit* findField = nullptr;
        QLineEdit* replaceField = nullptr;
        for (QLineEdit* field : fields) {
            if (field->placeholderText() == QStringLiteral("Find")) findField = field;
            if (field->placeholderText() == QStringLiteral("Replace")) replaceField = field;
        }
        QVERIFY(findField && replaceField);
        findField->setText(QStringLiteral("open"));
        replaceField->setText(QStringLiteral("shut"));
        for (QPushButton* button : find->findChildren<QPushButton*>())
            if (button->text() == QStringLiteral("Replace all")) button->click();
        QVERIFY(!editor->toPlainText().contains(QStringLiteral("open")));
        QCOMPARE(editor->toPlainText().count(QStringLiteral("shut")), 3);

        // An unmodified tab reloads when the file changes on disk.
        QTest::keyClick(editor, Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!editor->document()->isModified());
        write("print('changed')\n");
        QTRY_COMPARE(editor->toPlainText(), QStringLiteral("print('changed')\n"));

        // Errors mark the line until the file is saved.
        panel.MarkError(path, 1, QStringLiteral("door.lua:1: boom"));
        QVERIFY(editor->document()->firstBlock().userData());
        QCOMPARE(CodeEditorPanel::ResolveLocation(path), QFileInfo(path).canonicalFilePath());
    }

    // Breakpoints toggle from the gutter and come back with the project's tabs; Lua completion pops up.
    void codeEditorBreakpointsSessionAndCompletion() {
        using namespace Hephaestus::QtEditor;
        QTemporaryDir project;
        Hephaestus::AssetManager::GetInstance().SetProjectRoot(project.path().toStdString());
        const QString path = project.filePath(QStringLiteral("door.lua"));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("local speed = 1\nprint(speed)\n");
        }

        CodeEditorPanel panel;
        panel.resize(900, 600);
        panel.show();
        QVERIFY(panel.OpenFile(path));
        auto* editor = panel.findChild<QPlainTextEdit*>(QStringLiteral("CodeEditor"));
        auto* gutter = editor->findChild<QWidget*>(QStringLiteral("CodeGutter"));
        QVERIFY(gutter);
        const int lineHeight = editor->fontMetrics().lineSpacing();
        QTest::mouseClick(gutter, Qt::LeftButton, {}, QPoint(10, editor->contentsMargins().top() + lineHeight + lineHeight / 2));
        QVERIFY(editor->document()->findBlockByNumber(1).userData());

        panel.SaveSession();
        QVERIFY(panel.CloseAll());
        QTRY_VERIFY(!panel.findChild<QPlainTextEdit*>(QStringLiteral("CodeEditor")));
        panel.RestoreSession();
        QTRY_VERIFY(panel.findChild<QPlainTextEdit*>(QStringLiteral("CodeEditor")));
        editor = panel.findChild<QPlainTextEdit*>(QStringLiteral("CodeEditor"));
        QVERIFY(editor->document()->findBlockByNumber(1).userData());
        QVERIFY(!editor->document()->findBlockByNumber(0).userData());

        editor->setFocus();
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, QStringLiteral("spe"));
        QAbstractItemView* popup = nullptr;
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->objectName() == QStringLiteral("CodeCompletionPopup")) popup = qobject_cast<QAbstractItemView*>(widget);
        }
        QVERIFY(popup && popup->isVisible());
        QCOMPARE(popup->currentIndex().data().toString(), QStringLiteral("speed"));
        QTest::keyClick(popup, Qt::Key_Return);
        QVERIFY(editor->toPlainText().endsWith(QStringLiteral("speed")));
        QSettings(QStringLiteral("Hephaestus"), QStringLiteral("Editor"))
            .remove(QStringLiteral("codeEditor/") + QString::fromLatin1(QCryptographicHash::hash(
                QByteArray::fromStdString(Hephaestus::AssetManager::GetInstance().GetProjectRoot()),
                QCryptographicHash::Md5).toHex()));
    }

    void projectSearchFindsMatchesAcrossFiles() {
        using namespace Hephaestus::QtEditor;
        QTemporaryDir project;
        Hephaestus::AssetManager::GetInstance().SetProjectRoot(project.path().toStdString());
        QDir(project.path()).mkpath(QStringLiteral("assets/scripts"));
        for (const QString& name : { QStringLiteral("assets/scripts/a.lua"), QStringLiteral("b.txt") }) {
            QFile file(project.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("first line\nfind the Needle here\n");
        }
        ProjectSearchPanel panel;
        QString openedPath;
        int openedLine = 0;
        panel.onOpen = [&](const QString& path, int line, int) {
            openedPath = path;
            openedLine = line;
        };
        panel.FocusSearch(QStringLiteral("needle"));
        panel.Search();
        auto* results = panel.findChild<QTreeWidget*>(QStringLiteral("SearchResults"));
        QVERIFY(results);
        QCOMPARE(results->topLevelItemCount(), 2);
        QTreeWidgetItem* hit = results->topLevelItem(0)->child(0);
        QVERIFY(hit);
        emit results->itemActivated(hit, 0);
        QCOMPARE(openedLine, 2);
        QVERIFY(openedPath.endsWith(QStringLiteral(".lua")) || openedPath.endsWith(QStringLiteral(".txt")));
    }
};

QTEST_MAIN(EditorQtSmokeTests)
#include "EditorQtSmokeTests.moc"
