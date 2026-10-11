#include "PhysicsLayerEditorWidget.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Hephaestus::UI {

PhysicsLayerEditorWidget::PhysicsLayerEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    m_countLabel = new QLabel(this);
    root->addWidget(m_countLabel);

    m_noteLabel = new QLabel(
        QStringLiteral("Layer changes are saved to .hephaestus/physics_layers.json and apply the next time Play starts. A running simulation is not rebuilt."),
        this);
    m_noteLabel->setWordWrap(true);
    root->addWidget(m_noteLabel);

    m_layers = new QTableWidget(this);
    m_layers->setColumnCount(3);
    m_layers->setHorizontalHeaderLabels({QStringLiteral("Id"), QStringLiteral("Name"), QStringLiteral("Kind")});
    m_layers->horizontalHeader()->setStretchLastSection(true);
    m_layers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_layers->verticalHeader()->setVisible(false);
    m_layers->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_layers->setSelectionMode(QAbstractItemView::SingleSelection);
    m_layers->setMinimumHeight(160);
    root->addWidget(m_layers);

    auto* editRow = new QHBoxLayout();
    m_newName = new QLineEdit(this);
    m_newName->setPlaceholderText(QStringLiteral("New layer name"));
    m_addButton = new QPushButton(QStringLiteral("Add Layer"), this);
    m_deleteButton = new QPushButton(QStringLiteral("Delete Layer"), this);
    editRow->addWidget(m_newName, 1);
    editRow->addWidget(m_addButton);
    editRow->addWidget(m_deleteButton);
    root->addLayout(editRow);

    auto* matrixLabel = new QLabel(QStringLiteral("Collision matrix"), this);
    root->addWidget(matrixLabel);

    m_matrix = new QTableWidget(this);
    m_matrix->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_matrix->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_matrix->setMinimumHeight(180);
    root->addWidget(m_matrix, 1);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_layers, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        OnNameEdited(item);
    });
    connect(m_addButton, &QPushButton::clicked, this, [this]() { OnAddLayer(); });
    connect(m_deleteButton, &QPushButton::clicked, this, [this]() { OnDeleteLayer(); });
    connect(m_newName, &QLineEdit::returnPressed, this, [this]() { OnAddLayer(); });
}

bool PhysicsLayerEditorWidget::Load(const std::filesystem::path& projectRoot, QString* error)
{
    m_projectRoot = projectRoot;
    const Physics::PhysicsLayerLoadResult result = Physics::PhysicsLayerConfig::LoadFromFile(
        Physics::PhysicsLayerConfig::ProjectFile(projectRoot), m_config);
    m_dirty = false;
    Rebuild();
    if (result.missingFile) {
        m_status->setText(QStringLiteral("No physics layer file yet. Saving writes .hephaestus/physics_layers.json. Until then Play uses the Default compatibility matrix."));
    } else if (!result.error.empty()) {
        m_status->setText(QString::fromStdString(result.error));
        if (error) {
            *error = m_status->text();
        }
        return false;
    }
    return true;
}

bool PhysicsLayerEditorWidget::Save(QString* error)
{
    std::string saveError;
    if (!m_config.SaveToFile(Physics::PhysicsLayerConfig::ProjectFile(m_projectRoot), saveError)) {
        const QString message = saveError.empty()
            ? QStringLiteral("Failed to save physics layers.")
            : QString::fromStdString(saveError);
        m_status->setText(message);
        if (error) {
            *error = message;
        }
        return false;
    }
    m_dirty = false;
    m_status->setText(QStringLiteral("Saved. The new matrix is used the next time Play starts."));
    return true;
}

void PhysicsLayerEditorWidget::SetChangedCallback(std::function<void()> callback)
{
    m_changed = std::move(callback);
}

void PhysicsLayerEditorWidget::MarkDirty()
{
    m_dirty = true;
    if (m_changed) {
        m_changed();
    }
}

void PhysicsLayerEditorWidget::UpdateStatus()
{
    const int count = static_cast<int>(m_config.Layers().size());
    QString text = QStringLiteral("%1 / %2 layers. Built-in layers cannot be deleted. Retired ids are not reused.")
                       .arg(count)
                       .arg(Physics::kMaxGameplayLayers);
    if (!m_config.Tombstones().empty()) {
        QStringList ids;
        for (std::uint16_t id : m_config.Tombstones()) {
            ids.push_back(QString::number(id));
        }
        text += QStringLiteral(" Retired: %1.").arg(ids.join(QStringLiteral(", ")));
    }
    m_countLabel->setText(text);
    const bool full = m_config.Layers().size() >= Physics::kMaxGameplayLayers;
    m_addButton->setEnabled(!full);
}

void PhysicsLayerEditorWidget::Rebuild()
{
    m_rebuilding = true;
    m_layers->setRowCount(0);
    const std::vector<Physics::PhysicsLayerDesc>& layers = m_config.Layers();
    m_layers->setRowCount(static_cast<int>(layers.size()));
    for (int row = 0; row < static_cast<int>(layers.size()); ++row) {
        const Physics::PhysicsLayerDesc& layer = layers[static_cast<std::size_t>(row)];
        auto* idItem = new QTableWidgetItem(QString::number(layer.id));
        idItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        idItem->setData(Qt::UserRole, static_cast<int>(layer.id));

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(layer.name));
        nameItem->setData(Qt::UserRole, static_cast<int>(layer.id));
        nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);

        auto* kindItem = new QTableWidgetItem(layer.builtin ? QStringLiteral("Built-in") : QStringLiteral("User"));
        kindItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

        m_layers->setItem(row, 0, idItem);
        m_layers->setItem(row, 1, nameItem);
        m_layers->setItem(row, 2, kindItem);
    }
    m_rebuilding = false;
    RebuildMatrix();
    UpdateStatus();
}

void PhysicsLayerEditorWidget::RebuildMatrix()
{
    m_rebuilding = true;
    m_matrixIds.clear();
    for (const Physics::PhysicsLayerDesc& layer : m_config.Layers()) {
        m_matrixIds.push_back(layer.id);
    }
    const int count = static_cast<int>(m_matrixIds.size());
    m_matrix->clear();
    m_matrix->setRowCount(count);
    m_matrix->setColumnCount(count);
    QStringList headers;
    for (std::uint16_t id : m_matrixIds) {
        const Physics::PhysicsLayerDesc* layer = m_config.FindById(id);
        headers.push_back(layer ? QString::fromStdString(layer->name) : QString::number(id));
    }
    m_matrix->setHorizontalHeaderLabels(headers);
    m_matrix->setVerticalHeaderLabels(headers);

    for (int row = 0; row < count; ++row) {
        for (int column = 0; column < count; ++column) {
            auto* box = new QCheckBox(m_matrix);
            box->setChecked(m_config.Collides(m_matrixIds[static_cast<std::size_t>(row)],
                                              m_matrixIds[static_cast<std::size_t>(column)]));
            auto* cell = new QWidget(m_matrix);
            auto* layout = new QHBoxLayout(cell);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setAlignment(Qt::AlignCenter);
            layout->addWidget(box);
            m_matrix->setCellWidget(row, column, cell);
            connect(box, &QCheckBox::toggled, this, [this, row, column](bool checked) {
                if (m_rebuilding) {
                    return;
                }
                const std::uint16_t a = m_matrixIds[static_cast<std::size_t>(row)];
                const std::uint16_t b = m_matrixIds[static_cast<std::size_t>(column)];
                m_config.SetCollides(a, b, checked);
                if (row != column) {
                    QWidget* mirrorCell = m_matrix->cellWidget(column, row);
                    auto* mirror = mirrorCell ? mirrorCell->findChild<QCheckBox*>() : nullptr;
                    if (mirror) {
                        const QSignalBlocker blocker(mirror);
                        mirror->setChecked(checked);
                    }
                }
                MarkDirty();
            });
        }
    }
    m_rebuilding = false;
}

void PhysicsLayerEditorWidget::OnNameEdited(QTableWidgetItem* item)
{
    if (m_rebuilding || !item || item->column() != 1) {
        return;
    }
    const auto id = static_cast<std::uint16_t>(item->data(Qt::UserRole).toInt());
    std::string error;
    if (!m_config.RenameLayer(id, item->text().trimmed().toStdString(), error)) {
        const QSignalBlocker blocker(m_layers);
        if (const Physics::PhysicsLayerDesc* layer = m_config.FindById(id)) {
            item->setText(QString::fromStdString(layer->name));
        }
        m_status->setText(QString::fromStdString(error));
        return;
    }
    RebuildMatrix();
    m_status->clear();
    MarkDirty();
}

void PhysicsLayerEditorWidget::OnAddLayer()
{
    std::string error;
    if (!m_config.AddUserLayer(m_newName->text().trimmed().toStdString(), error)) {
        m_status->setText(QString::fromStdString(error));
        return;
    }
    m_newName->clear();
    m_status->clear();
    Rebuild();
    MarkDirty();
}

void PhysicsLayerEditorWidget::OnDeleteLayer()
{
    const int row = m_layers->currentRow();
    if (row < 0) {
        m_status->setText(QStringLiteral("Select a user layer to delete."));
        return;
    }
    QTableWidgetItem* idItem = m_layers->item(row, 0);
    if (!idItem) {
        return;
    }
    const auto id = static_cast<std::uint16_t>(idItem->data(Qt::UserRole).toInt());
    std::string error;
    if (!m_config.DeleteUserLayer(id, error)) {
        m_status->setText(QString::fromStdString(error));
        return;
    }
    m_status->setText(QStringLiteral("Layer %1 is retired and will not be reused.").arg(id));
    Rebuild();
    MarkDirty();
}

} // namespace Hephaestus::UI
