#if HEPHAESTUS_EDITOR_QT_UI

#include "KitEditorDialog.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Scene/PrefabOverrideSystem.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace Hephaestus {
namespace QtEditor {
namespace {

enum Column { PieceColumn, RoleColumn, WidthColumn, StretchColumn, OpeningMinColumn, OpeningMaxColumn, ColumnCount };

const char* const kRoles[] = {"wall", "filler", "corner", "door", "window"};

QString RoleLabel(const std::string& role)
{
    if (role == "wall") return KitEditorDialog::tr("Wall panel");
    if (role == "filler") return KitEditorDialog::tr("Filler");
    if (role == "corner") return KitEditorDialog::tr("Corner");
    if (role == "door") return KitEditorDialog::tr("Door frame");
    if (role == "window") return KitEditorDialog::tr("Window frame");
    return QString::fromStdString(role);
}

bool FramesOpenings(const std::string& role)
{
    return role == "door" || role == "window";
}

QDoubleSpinBox* MakeSpin(double minValue, double maxValue, double step, int decimals, const QString& suffix)
{
    auto* spin = new QDoubleSpinBox();
    spin->setRange(minValue, maxValue);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setSuffix(suffix);
    spin->setFrame(false);
    return spin;
}

} // namespace

KitEditorDialog::KitEditorDialog(const QString& absolutePath, QWidget* parent)
    : QDialog(parent)
    , m_absolutePath(absolutePath)
{
    setAttribute(Qt::WA_DeleteOnClose);
    std::string error;
    if (!Construction::LoadKit(std::filesystem::path(absolutePath.toStdWString()), m_kit, error)) {
        QMessageBox::warning(parent, tr("Kit"), tr("Could not open the kit: %1").arg(QString::fromStdString(error)));
        m_kit = Construction::KitDefinition{};
        m_kit.name = QFileInfo(absolutePath).completeBaseName().toStdString();
    }
    setupUi();
    rebuildTable();
    setDirty(false);
}

void KitEditorDialog::Open(QWidget* parent, const QString& absolutePath)
{
    auto* dialog = new KitEditorDialog(absolutePath, parent);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void KitEditorDialog::setupUi()
{
    resize(820, 560);
    auto* root = new QVBoxLayout(this);

    auto* header = new QFormLayout();
    m_name = new QLineEdit(QString::fromStdString(m_kit.name));
    header->addRow(tr("Name"), m_name);
    root->addLayout(header);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_kit.name = text.toStdString();
        setDirty(true);
    });

    auto* hint = new QLabel(tr("Build each piece standing on its origin, with its width along X and centred on it, "
                               "and the wall's thickness centred on Z; +Z faces into the room. Stretch is how far a "
                               "piece may grow or shrink to fit."));
    hint->setWordWrap(true);
    root->addWidget(hint);

    m_table = new QTableWidget(0, ColumnCount);
    m_table->setHorizontalHeaderLabels(
        {tr("Piece"), tr("Role"), tr("Width"), tr("Stretch"), tr("Opening from"), tr("Opening to")});
    m_table->horizontalHeader()->setSectionResizeMode(PieceColumn, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_table, 1);

    auto* pieceButtons = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add Pieces…"));
    add->setToolTip(tr("Pick piece prefabs; each one's width is measured from its meshes."));
    auto* measure = new QPushButton(tr("Measure Width"));
    measure->setToolTip(tr("Set the selected pieces' widths from their meshes again."));
    auto* remove = new QPushButton(tr("Remove"));
    pieceButtons->addWidget(add);
    pieceButtons->addWidget(measure);
    pieceButtons->addWidget(remove);
    pieceButtons->addStretch(1);
    root->addLayout(pieceButtons);
    connect(add, &QPushButton::clicked, this, &KitEditorDialog::addPieces);
    connect(measure, &QPushButton::clicked, this, &KitEditorDialog::measureSelected);
    connect(remove, &QPushButton::clicked, this, &KitEditorDialog::removeSelected);

    auto* trialRow = new QHBoxLayout();
    trialRow->addWidget(new QLabel(tr("Try a wall")));
    m_trialLength = MakeSpin(0.1, 1000.0, 0.25, 2, QStringLiteral(" m"));
    m_trialLength->setFrame(true);
    m_trialLength->setValue(6.0);
    trialRow->addWidget(m_trialLength);
    m_trial = new QLabel();
    m_trial->setWordWrap(true);
    trialRow->addWidget(m_trial, 1);
    root->addLayout(trialRow);
    connect(m_trialLength, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this]() { refreshChecks(); });

    m_problems = new QLabel();
    m_problems->setWordWrap(true);
    root->addWidget(m_problems);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close);
    m_save = buttons->button(QDialogButtonBox::Save);
    root->addWidget(buttons);
    connect(m_save, &QPushButton::clicked, this, [this]() { save(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void KitEditorDialog::rebuildTable()
{
    m_updating = true;
    m_table->setRowCount(static_cast<int>(m_kit.pieces.size()));
    for (int row = 0; row < static_cast<int>(m_kit.pieces.size()); ++row) {
        Construction::KitPiece& piece = m_kit.pieces[static_cast<size_t>(row)];
        auto* name = new QTableWidgetItem(QFileInfo(QString::fromStdString(piece.prefab)).completeBaseName());
        name->setToolTip(QString::fromStdString(piece.prefab));
        m_table->setItem(row, PieceColumn, name);

        auto* role = new QComboBox();
        role->setFrame(false);
        for (const char* value : kRoles) {
            role->addItem(RoleLabel(value), QString::fromLatin1(value));
        }
        const int roleIndex = role->findData(QString::fromStdString(piece.role));
        if (roleIndex < 0) {
            role->addItem(RoleLabel(piece.role), QString::fromStdString(piece.role));
        }
        role->setCurrentIndex(roleIndex < 0 ? role->count() - 1 : roleIndex);
        m_table->setCellWidget(row, RoleColumn, role);

        auto* width = MakeSpin(0.01, 100.0, 0.05, 3, QStringLiteral(" m"));
        width->setValue(piece.width);
        m_table->setCellWidget(row, WidthColumn, width);
        auto* stretch = MakeSpin(0.0, 100.0, 1.0, 1, QStringLiteral(" %"));
        stretch->setValue(piece.stretch * 100.0);
        m_table->setCellWidget(row, StretchColumn, stretch);
        auto* openingMin = MakeSpin(0.0, 100.0, 0.05, 2, QStringLiteral(" m"));
        openingMin->setValue(piece.openingMin);
        auto* openingMax = MakeSpin(0.0, 100.0, 0.05, 2, QStringLiteral(" m"));
        openingMax->setValue(piece.openingMax);
        openingMin->setEnabled(FramesOpenings(piece.role));
        openingMax->setEnabled(FramesOpenings(piece.role));
        m_table->setCellWidget(row, OpeningMinColumn, openingMin);
        m_table->setCellWidget(row, OpeningMaxColumn, openingMax);

        const size_t index = static_cast<size_t>(row);
        connect(role, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, role, index, openingMin, openingMax]() {
            if (m_updating || index >= m_kit.pieces.size()) return;
            m_kit.pieces[index].role = role->currentData().toString().toStdString();
            openingMin->setEnabled(FramesOpenings(m_kit.pieces[index].role));
            openingMax->setEnabled(FramesOpenings(m_kit.pieces[index].role));
            setDirty(true);
        });
        auto bind = [this, index](QDoubleSpinBox* spin, float Construction::KitPiece::*field, double scale) {
            connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this, index, field, scale](double value) {
                if (m_updating || index >= m_kit.pieces.size()) return;
                m_kit.pieces[index].*field = static_cast<float>(value / scale);
                setDirty(true);
            });
        };
        bind(width, &Construction::KitPiece::width, 1.0);
        bind(stretch, &Construction::KitPiece::stretch, 100.0);
        bind(openingMin, &Construction::KitPiece::openingMin, 1.0);
        bind(openingMax, &Construction::KitPiece::openingMax, 1.0);
    }
    m_updating = false;
    refreshChecks();
}

void KitEditorDialog::refreshChecks()
{
    const std::vector<std::string> problems = Construction::ValidateKit(m_kit);
    QStringList lines;
    for (const std::string& problem : problems) {
        lines << QString::fromStdString(problem);
    }
    m_problems->setText(lines.isEmpty() ? QString() : lines.join(QStringLiteral("\n")));
    m_problems->setVisible(!lines.isEmpty());

    const Construction::RunFill run = Construction::SolveRun(m_kit, static_cast<float>(m_trialLength->value()));
    if (!run.filled) {
        m_trial->setText(tr("The kit can't fill this length; it would show as a gap."));
        return;
    }
    std::map<const Construction::KitPiece*, int> counts;
    std::vector<const Construction::KitPiece*> order;
    for (const Construction::KitPiece* piece : run.pieces) {
        if (counts[piece]++ == 0) order.push_back(piece);
    }
    QStringList parts;
    for (const Construction::KitPiece* piece : order) {
        const QString name = QFileInfo(QString::fromStdString(piece->prefab)).completeBaseName();
        parts << (counts[piece] > 1 ? tr("%1 x%2").arg(name).arg(counts[piece]) : name);
    }
    const float stretch = (run.scale - 1.0f) * 100.0f;
    m_trial->setText(std::fabs(stretch) < 0.05f ? tr("%1, no stretch.").arg(parts.join(QStringLiteral(", ")))
                     : tr("%1, %2 by %3%.").arg(parts.join(QStringLiteral(", ")),
                                                 stretch > 0.0f ? tr("stretched") : tr("squashed"))
                           .arg(std::fabs(stretch), 0, 'f', 1));
}

void KitEditorDialog::addPieces()
{
    const std::filesystem::path projectRoot(AssetManager::GetInstance().GetProjectRoot());
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Add Kit Pieces"), QString::fromStdString((projectRoot / "assets").string()),
        tr("Prefabs (*.hpprefab)"));
    QStringList skipped;
    for (const QString& file : files) {
        const std::string key = AssetPathPolicy::ToPortableKey(projectRoot, file.toStdString());
        if (key.empty() || key.rfind("assets/", 0) != 0) {
            skipped << QFileInfo(file).fileName();
            continue;
        }
        Construction::KitPiece piece;
        piece.prefab = key;
        AABB bounds;
        std::string error;
        if (Construction::MeasurePiece(key, bounds, error)) {
            piece.width = std::max(0.01f, bounds.max.x - bounds.min.x);
        }
        m_kit.pieces.push_back(std::move(piece));
    }
    if (!skipped.isEmpty()) {
        QMessageBox::warning(this, tr("Add Kit Pieces"),
                             tr("Pieces have to be prefabs in this project's assets folder: %1")
                                 .arg(skipped.join(QStringLiteral(", "))));
    }
    if (!files.isEmpty()) {
        rebuildTable();
        setDirty(true);
    }
}

void KitEditorDialog::measureSelected()
{
    QStringList failed;
    std::set<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    for (int row : rows) {
        Construction::KitPiece& piece = m_kit.pieces[static_cast<size_t>(row)];
        AABB bounds;
        std::string error;
        if (Construction::MeasurePiece(piece.prefab, bounds, error)) {
            piece.width = std::max(0.01f, bounds.max.x - bounds.min.x);
        } else {
            failed << QString::fromStdString(error);
        }
    }
    if (!failed.isEmpty()) {
        QMessageBox::warning(this, tr("Measure Width"), failed.join(QStringLiteral("\n")));
    }
    if (!rows.empty()) {
        rebuildTable();
        setDirty(true);
    }
}

void KitEditorDialog::removeSelected()
{
    std::set<int> rows;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        rows.insert(index.row());
    }
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        m_kit.pieces.erase(m_kit.pieces.begin() + *it);
    }
    if (!rows.empty()) {
        rebuildTable();
        setDirty(true);
    }
}

bool KitEditorDialog::save()
{
    std::string error;
    if (!Construction::SaveKit(std::filesystem::path(m_absolutePath.toStdWString()), m_kit, error)) {
        QMessageBox::critical(this, tr("Save Kit"), QString::fromStdString(error));
        return false;
    }
    setDirty(false);
    return true;
}

void KitEditorDialog::setDirty(bool dirty)
{
    m_dirty = dirty;
    m_save->setEnabled(dirty);
    setWindowTitle(tr("Kit: %1%2").arg(QFileInfo(m_absolutePath).fileName(), dirty ? QStringLiteral(" *") : QString()));
    refreshChecks();
}

void KitEditorDialog::done(int result)
{
    if (m_dirty) {
        const auto answer = QMessageBox::question(this, tr("Kit"), tr("Save changes to %1?").arg(QFileInfo(m_absolutePath).fileName()),
                                                  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            return;
        }
    }
    QDialog::done(result);
}

} // namespace QtEditor
} // namespace Hephaestus

#endif
