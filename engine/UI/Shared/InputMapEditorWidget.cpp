#include "InputMapEditorWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Hephaestus::UI {
namespace {

constexpr int KIND_ROLE = Qt::UserRole;
constexpr int KIND_ACTION = 1;
constexpr int KIND_AXIS = 2;
constexpr int KIND_BINDING = 3;

std::filesystem::path ResolveProfilePath(const std::filesystem::path& projectRoot,
                                         const QString& profilePath)
{
    QString normalized = profilePath.trimmed();
    if (normalized.isEmpty() || normalized.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0) {
        normalized = QStringLiteral("assets/input/Default.hpinput");
    }
    std::filesystem::path path(normalized.toStdWString());
    return path.is_absolute() ? path : projectRoot / path;
}

QTreeWidgetItem* MakeDefinitionItem(const QString& name, int kind)
{
    auto* item = new QTreeWidgetItem();
    item->setText(0, name);
    item->setText(1, kind == KIND_ACTION ? QStringLiteral("Action") : QStringLiteral("Axis"));
    item->setData(0, KIND_ROLE, kind);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

QTreeWidgetItem* MakeBindingItem(const InputBinding& binding)
{
    auto* item = new QTreeWidgetItem();
    item->setText(1, QString::fromUtf8(ToString(binding.type)));
    item->setText(2, QString::fromStdString(binding.control));
    item->setText(3, QString::number(binding.scale, 'g', 5));
    item->setText(4, QString::number(binding.deadzone, 'g', 3));
    item->setData(0, KIND_ROLE, KIND_BINDING);
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

} // namespace

InputMapEditorWidget::InputMapEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* help = new QLabel(
        QStringLiteral("Actions are digital. Axes combine scaled bindings. "
                       "Binding types: keyboard, mouse_button, mouse_delta, "
                       "mouse_wheel, gamepad_button, gamepad_axis."),
        this);
    help->setWordWrap(true);
    root->addWidget(help);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(6);
    m_tree->setHeaderLabels({
        QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Control"),
        QStringLiteral("Scale"), QStringLiteral("Deadzone"), QStringLiteral("Clamp")
    });
    m_tree->setAlternatingRowColors(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    root->addWidget(m_tree, 1);

    auto* buttons = new QHBoxLayout();
    m_addAction = new QPushButton(QStringLiteral("Add Action"), this);
    m_addAxis = new QPushButton(QStringLiteral("Add Axis"), this);
    m_addBinding = new QPushButton(QStringLiteral("Add Binding"), this);
    m_remove = new QPushButton(QStringLiteral("Remove"), this);
    buttons->addWidget(m_addAction);
    buttons->addWidget(m_addAxis);
    buttons->addWidget(m_addBinding);
    buttons->addStretch(1);
    buttons->addWidget(m_remove);
    root->addLayout(buttons);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem*, int) {
        if (!m_rebuilding) {
            SyncFromTree();
            MarkDirty();
        }
    });
    connect(m_addAction, &QPushButton::clicked, this, [this]() {
        SyncFromTree();
        InputActionDefinition action;
        action.name = "NewAction" + std::to_string(m_map.actions.size() + 1);
        action.bindings.push_back(
            {InputBindingType::Keyboard, "SPACE", 1.0f, 0.15f,
             ResolveInputControl(InputBindingType::Keyboard, "SPACE")});
        m_map.actions.push_back(std::move(action));
        RebuildTree();
        MarkDirty();
    });
    connect(m_addAxis, &QPushButton::clicked, this, [this]() {
        SyncFromTree();
        InputAxisDefinition axis;
        axis.name = "NewAxis" + std::to_string(m_map.axes.size() + 1);
        axis.bindings.push_back(
            {InputBindingType::Keyboard, "W", 1.0f, 0.15f,
             ResolveInputControl(InputBindingType::Keyboard, "W")});
        m_map.axes.push_back(std::move(axis));
        RebuildTree();
        MarkDirty();
    });
    connect(m_addBinding, &QPushButton::clicked, this, [this]() {
        SyncFromTree();
        QTreeWidgetItem* definition = SelectedDefinition();
        if (!definition) return;
        const int kind = definition->data(0, KIND_ROLE).toInt();
        const int index = m_tree->indexOfTopLevelItem(definition);
        InputBinding binding{
            InputBindingType::Keyboard, "SPACE", 1.0f, 0.15f,
            ResolveInputControl(InputBindingType::Keyboard, "SPACE")
        };
        if (kind == KIND_ACTION && index >= 0 && index < static_cast<int>(m_map.actions.size())) {
            m_map.actions[static_cast<std::size_t>(index)].bindings.push_back(binding);
        } else if (kind == KIND_AXIS) {
            const int axisIndex = index - static_cast<int>(m_map.actions.size());
            if (axisIndex >= 0 && axisIndex < static_cast<int>(m_map.axes.size())) {
                m_map.axes[static_cast<std::size_t>(axisIndex)].bindings.push_back(binding);
            }
        }
        RebuildTree();
        MarkDirty();
    });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        SyncFromTree();
        QTreeWidgetItem* selected = m_tree->currentItem();
        if (!selected) return;
        if (selected->parent()) {
            QTreeWidgetItem* parent = selected->parent();
            const int childIndex = parent->indexOfChild(selected);
            const int topIndex = m_tree->indexOfTopLevelItem(parent);
            const int kind = parent->data(0, KIND_ROLE).toInt();
            if (kind == KIND_ACTION) {
                auto& bindings = m_map.actions[static_cast<std::size_t>(topIndex)].bindings;
                bindings.erase(bindings.begin() + childIndex);
            } else {
                const int axisIndex = topIndex - static_cast<int>(m_map.actions.size());
                auto& bindings = m_map.axes[static_cast<std::size_t>(axisIndex)].bindings;
                bindings.erase(bindings.begin() + childIndex);
            }
        } else {
            const int topIndex = m_tree->indexOfTopLevelItem(selected);
            const int kind = selected->data(0, KIND_ROLE).toInt();
            if (kind == KIND_ACTION) {
                m_map.actions.erase(m_map.actions.begin() + topIndex);
            } else {
                const int axisIndex = topIndex - static_cast<int>(m_map.actions.size());
                m_map.axes.erase(m_map.axes.begin() + axisIndex);
            }
        }
        RebuildTree();
        MarkDirty();
    });

    m_map = InputMap::CreateDefault();
    RebuildTree();
}

bool InputMapEditorWidget::Load(const std::filesystem::path& projectRoot,
                                const QString& profilePath,
                                QString* error)
{
    const std::filesystem::path path = ResolveProfilePath(projectRoot, profilePath);
    std::string loadError;
    if (std::filesystem::exists(path)) {
        if (!InputMap::Load(path, m_map, loadError)) {
            if (error) *error = QString::fromStdString(loadError);
            m_status->setText(QStringLiteral("Invalid input map: %1")
                                  .arg(QString::fromStdString(loadError)));
            return false;
        }
        m_status->setText(QStringLiteral("Editing %1")
                              .arg(QString::fromStdWString(path.wstring())));
    } else {
        m_map = InputMap::CreateDefault();
        m_status->setText(QStringLiteral("Profile does not exist yet; built-in defaults are shown."));
    }
    m_dirty = false;
    RebuildTree();
    return true;
}

bool InputMapEditorWidget::Save(const std::filesystem::path& projectRoot,
                                const QString& profilePath,
                                QString* error)
{
    SyncFromTree();
    const std::filesystem::path path = ResolveProfilePath(projectRoot, profilePath);
    std::string saveError;
    if (!m_map.Save(path, saveError)) {
        if (error) *error = QString::fromStdString(saveError);
        m_status->setText(QStringLiteral("Could not save: %1")
                              .arg(QString::fromStdString(saveError)));
        return false;
    }
    m_status->setText(QStringLiteral("Saved %1")
                          .arg(QString::fromStdWString(path.wstring())));
    m_dirty = false;
    return true;
}

void InputMapEditorWidget::SetChangedCallback(std::function<void()> callback)
{
    m_changed = std::move(callback);
}

void InputMapEditorWidget::RebuildTree()
{
    m_rebuilding = true;
    m_tree->clear();
    for (const auto& action : m_map.actions) {
        QTreeWidgetItem* item =
            MakeDefinitionItem(QString::fromStdString(action.name), KIND_ACTION);
        for (const auto& binding : action.bindings) item->addChild(MakeBindingItem(binding));
        m_tree->addTopLevelItem(item);
        item->setExpanded(true);
    }
    for (const auto& axis : m_map.axes) {
        QTreeWidgetItem* item =
            MakeDefinitionItem(QString::fromStdString(axis.name), KIND_AXIS);
        item->setText(5, axis.clamp ? QStringLiteral("Yes") : QStringLiteral("No"));
        for (const auto& binding : axis.bindings) item->addChild(MakeBindingItem(binding));
        m_tree->addTopLevelItem(item);
        item->setExpanded(true);
    }
    for (int topIndex = 0; topIndex < m_tree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* definition = m_tree->topLevelItem(topIndex);
        for (int childIndex = 0; childIndex < definition->childCount(); ++childIndex) {
            QTreeWidgetItem* binding = definition->child(childIndex);
            auto* type = new QComboBox(m_tree);
            type->addItems({
                QStringLiteral("keyboard"), QStringLiteral("mouse_button"),
                QStringLiteral("mouse_delta"), QStringLiteral("mouse_wheel"),
                QStringLiteral("gamepad_button"), QStringLiteral("gamepad_axis")
            });
            type->setCurrentText(binding->text(1));
            m_tree->setItemWidget(binding, 1, type);
            connect(type, &QComboBox::currentTextChanged, this,
                    [this, binding](const QString& text) {
                binding->setText(1, text);
                if (!m_rebuilding) {
                    SyncFromTree();
                    MarkDirty();
                }
            });
        }
    }
    m_rebuilding = false;
}

void InputMapEditorWidget::SyncFromTree()
{
    InputMap updated;
    for (int topIndex = 0; topIndex < m_tree->topLevelItemCount(); ++topIndex) {
        QTreeWidgetItem* item = m_tree->topLevelItem(topIndex);
        const int kind = item->data(0, KIND_ROLE).toInt();
        std::vector<InputBinding> bindings;
        for (int childIndex = 0; childIndex < item->childCount(); ++childIndex) {
            QTreeWidgetItem* child = item->child(childIndex);
            InputBinding binding;
            InputBindingTypeFromString(child->text(1).toStdString(), binding.type);
            binding.control = child->text(2).trimmed().toStdString();
            binding.scale = child->text(3).toFloat();
            binding.deadzone = child->text(4).toFloat();
            binding.controlCode = ResolveInputControl(binding.type, binding.control);
            bindings.push_back(std::move(binding));
        }
        if (kind == KIND_ACTION) {
            updated.actions.push_back({item->text(0).trimmed().toStdString(), std::move(bindings)});
        } else if (kind == KIND_AXIS) {
            const QString clampText = item->text(5).trimmed();
            const bool clamp = clampText.compare(QStringLiteral("No"), Qt::CaseInsensitive) != 0 &&
                               clampText != QStringLiteral("0");
            updated.axes.push_back({
                item->text(0).trimmed().toStdString(), clamp, std::move(bindings)
            });
        }
    }
    m_map = std::move(updated);
}

void InputMapEditorWidget::MarkDirty()
{
    m_dirty = true;
    if (m_changed) m_changed();
}

QTreeWidgetItem* InputMapEditorWidget::SelectedDefinition() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item && item->parent() ? item->parent() : item;
}

} // namespace Hephaestus::UI
