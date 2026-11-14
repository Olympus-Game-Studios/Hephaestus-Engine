#if HEPHAESTUS_EDITOR_QT_UI

#include "MaterialGraphQtEditorDialog.h"
#include "EditorDockTitleBar.h"
#include "EditorTheme.h"

#include "../MaterialEditor/MaterialAssetSerializer.h"
#include "../MaterialEditor/MaterialGraphEvaluator.h"
#include "../MaterialEditor/MaterialNodeDefinitions.h"
#include "../MaterialEditor/MaterialShaderGenerator.h"
#include "../MaterialEditor/TextureDiscovery.h"
#include "QtEditorEngine.h"
#include "QtViewportWindow.h"

#include "../../Assets/AssetManager.h"
#include "../../Assets/AssetPathPolicy.h"
#include "../../Scene/Scene.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Hephaestus {
namespace QtEditor {
namespace {

using namespace MaterialEditor;

constexpr const char* DEFAULT_PREVIEW_HDRI_PATH = ".engine/hdris/studio_garden_1k.hdr";

QToolButton* MakeToolButton(QWidget* parent, const QString& icon, const QString& toolTip, const QString& text = QString()) {
    auto* button = new QToolButton(parent);
    if (!icon.isEmpty()) {
        button->setIcon(Theme::Icon(icon));
    }
    if (!text.isEmpty()) {
        button->setText(text);
        button->setToolButtonStyle(icon.isEmpty() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    }
    button->setToolTip(toolTip);
    return button;
}

QWidget* MakePanel(QWidget* parent) {
    auto* panel = new QWidget(parent);
    panel->setObjectName(QStringLiteral("EditorPanel"));
    panel->setAttribute(Qt::WA_StyledBackground, true);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return panel;
}

void SetStyleProperty(QWidget* widget, const char* name, const QString& value) {
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QString NodeTypeLabel(NodeType type) {
    return QString::fromUtf8(GetNodeDefinition(type).typeName.data(), static_cast<int>(GetNodeDefinition(type).typeName.size()));
}

QString NodeTooltip(NodeType type) {
    const auto tooltip = GetNodeDefinition(type).tooltip;
    QString text = QString::fromUtf8(tooltip.data(), static_cast<int>(tooltip.size()));
    if (!NodeAffectsRenderedMaterial(type)) {
        text += QObject::tr("\nDoes not affect the rendered material yet: its output is ignored.");
    }
    return text;
}

QString NodeMenuLabel(NodeType type, const QString& label) {
    return NodeAffectsRenderedMaterial(type) ? label : QObject::tr("%1 (no effect yet)").arg(label);
}

QString NodeCategory(NodeType type) {
    const auto category = GetNodeDefinition(type).category;
    return QString::fromUtf8(category.data(), static_cast<int>(category.size()));
}

QColor PinColor(ValueType type) {
    switch (type) {
    case ValueType::Float1: return QColor(240, 220, 100);
    case ValueType::Float3: return QColor(140, 220, 140);
    case ValueType::Sampler2D: return QColor(190, 120, 255);
    case ValueType::Float2: return QColor(140, 220, 140);
    case ValueType::Float4: return QColor(140, 220, 140);
    }
    return QColor(180, 180, 180);
}

QColor NodeTitleColor(NodeType type) {
    const QString category = NodeCategory(type);
    if (category == "Textures") return QColor(180, 100, 42);
    if (category == "Math") return QColor(58, 112, 175);
    if (category == "Inputs") return QColor(92, 92, 96);
    if (category == "Parameters") return QColor(70, 145, 88);
    if (category == "PBR") return QColor(155, 82, 66);
    return QColor(82, 82, 82);
}

bool IsTextureNode(NodeType type) {
    return GetNodeDefinition(type).textureNode;
}

bool IsScalarNode(NodeType type) {
    return type == NodeType::ConstantFloat || type == NodeType::FloatParameter || type == NodeType::BoolParameter;
}

bool IsColorNode(NodeType type) {
    return type == NodeType::ConstantVec3 || type == NodeType::ColorParameter || type == NodeType::Vector2Parameter;
}

struct NodeMenuEntry {
    QString label;
    NodeType type;
};

QString ValueTypeLabel(ValueType type) {
    return QString::fromUtf8(ValueTypeName(type).data(), static_cast<int>(ValueTypeName(type).size()));
}

bool FuzzyMatch(const QString& needle, const QString& haystack) {
    if (needle.isEmpty()) {
        return true;
    }
    int n = 0;
    const QString a = needle.toLower();
    const QString b = haystack.toLower();
    for (int h = 0; h < b.size() && n < a.size(); ++h) {
        if (a[n] == b[h]) {
            ++n;
        }
    }
    return n == a.size();
}

const std::vector<NodeMenuEntry>& NodeMenuEntries() {
    static const std::vector<NodeMenuEntry> entries = {
        {QObject::tr("MaterialOutput"), NodeType::MaterialOutput},
        {QObject::tr("Scalar"), NodeType::ConstantFloat},
        {QObject::tr("Vector3"), NodeType::ConstantVec3},
        {QObject::tr("ConstantScalar"), NodeType::ConstantFloat},
        {QObject::tr("ConstantColor"), NodeType::ConstantVec3},
        {QObject::tr("FloatParameter"), NodeType::FloatParameter},
        {QObject::tr("ColorParameter"), NodeType::ColorParameter},
        {QObject::tr("TextureParameter"), NodeType::TextureParameter},
        {QObject::tr("BoolParameter"), NodeType::BoolParameter},
        {QObject::tr("Vector2Parameter"), NodeType::Vector2Parameter},
        {QObject::tr("StaticSwitchParameter"), NodeType::StaticSwitchParameter},
        {QObject::tr("AlbedoTextureSample"), NodeType::AlbedoTextureSample},
        {QObject::tr("NormalTextureSample"), NodeType::NormalTextureSample},
        {QObject::tr("RoughnessTextureSample"), NodeType::RoughnessTextureSample},
        {QObject::tr("AOTextureSample"), NodeType::AoTextureSample},
        {QObject::tr("WorldAlignedTexture"), NodeType::WorldAlignedTexture},
        {QObject::tr("TextureSample"), NodeType::TextureSample},
        {QObject::tr("TextureObject"), NodeType::TextureObject},
        {QObject::tr("TextureSampleFromObject"), NodeType::TextureSampleFromObject},
        {QObject::tr("TextureTilingOffset"), NodeType::TextureTilingOffset},
        {QObject::tr("TriplanarTexture"), NodeType::TriplanarTexture},
        {QObject::tr("NormalUnpack"), NodeType::NormalUnpack},
        {QObject::tr("Add"), NodeType::Add},
        {QObject::tr("Subtract"), NodeType::Subtract},
        {QObject::tr("Multiply"), NodeType::Multiply},
        {QObject::tr("Divide"), NodeType::Divide},
        {QObject::tr("Clamp"), NodeType::Clamp},
        {QObject::tr("Saturate"), NodeType::Saturate},
        {QObject::tr("Power"), NodeType::Power},
        {QObject::tr("Abs"), NodeType::Abs},
        {QObject::tr("Min"), NodeType::Min},
        {QObject::tr("Max"), NodeType::Max},
        {QObject::tr("OneMinus"), NodeType::OneMinus},
        {QObject::tr("DotProduct"), NodeType::DotProduct},
        {QObject::tr("CrossProduct"), NodeType::CrossProduct},
        {QObject::tr("Normalize"), NodeType::Normalize},
        {QObject::tr("Length"), NodeType::Length},
        {QObject::tr("ComponentMask"), NodeType::ComponentMask},
        {QObject::tr("AppendVector"), NodeType::AppendVector},
        {QObject::tr("BreakVector"), NodeType::BreakVector},
        {QObject::tr("Transform"), NodeType::Transform},
        {QObject::tr("TexCoord"), NodeType::TexCoord},
        {QObject::tr("ScreenPosition"), NodeType::ScreenPosition},
        {QObject::tr("Noise"), NodeType::Noise},
        {QObject::tr("Gradient"), NodeType::Gradient},
        {QObject::tr("Fresnel"), NodeType::Fresnel},
        {QObject::tr("UVDistortion"), NodeType::UVDistortion},
        {QObject::tr("Time"), NodeType::Time},
        {QObject::tr("CameraVector"), NodeType::CameraVector},
        {QObject::tr("ViewDirection"), NodeType::ViewDirection},
        {QObject::tr("ReflectionVector"), NodeType::ReflectionVector},
        {QObject::tr("WorldPosition"), NodeType::WorldPosition},
        {QObject::tr("ObjectPosition"), NodeType::ObjectPosition},
        {QObject::tr("DistanceToCamera"), NodeType::DistanceToCamera},
        {QObject::tr("If"), NodeType::If},
        {QObject::tr("Step"), NodeType::Step},
        {QObject::tr("SmoothStep"), NodeType::SmoothStep},
        {QObject::tr("Compare"), NodeType::Compare},
        {QObject::tr("Lerp"), NodeType::Lerp},
        {QObject::tr("Overlay"), NodeType::Overlay},
        {QObject::tr("Screen"), NodeType::Screen},
        {QObject::tr("HeightBlend"), NodeType::HeightBlend}
    };
    return entries;
}

struct GraphIssue {
    QString message;
    NodeID nodeId = 0;
    enum class Severity { Error, Warning, Info } severity = Severity::Error;
};

struct GraphValidationResult {
    bool ok = true;
    std::vector<GraphIssue> issues;
};

NodeID PinToNode(const Graph& graph, PinID pinId) {
    const Pin* pin = graph.FindPin(pinId);
    return pin ? pin->node : 0;
}

GraphValidationResult ValidateGraph(const Graph& graph) {
    GraphValidationResult result;

    int outputCount = 0;
    NodeID outputNodeId = 0;
    const Node* outputNode = nullptr;
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            ++outputCount;
            outputNodeId = node.id;
            outputNode = &node;
        }
    }
    if (outputCount != 1) {
        result.ok = false;
        result.issues.push_back({QObject::tr("Graph must contain exactly one MaterialOutput node."), outputNodeId});
    }

    std::unordered_map<NodeID, std::vector<NodeID>> adjacency;
    std::unordered_map<NodeID, std::vector<NodeID>> reverseAdjacency;
    for (const Link& link : graph.GetLinks()) {
        const Pin* start = graph.FindPin(link.startPin);
        const Pin* end = graph.FindPin(link.endPin);
        if (!start || !end) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Orphan link id=%1 references missing pins.").arg(link.id), 0});
            continue;
        }
        if (start->kind != PinKind::Output || end->kind != PinKind::Input) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Link id=%1 has invalid direction (Output -> Input required).").arg(link.id), start->node});
            continue;
        }
        if (!ArePinTypesCompatible(start->valueType, end->valueType)) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Type mismatch on link id=%1.").arg(link.id), start->node});
            continue;
        }
        adjacency[start->node].push_back(end->node);
        reverseAdjacency[end->node].push_back(start->node);
    }

    if (outputNode) {
        auto inputLinked = [&](int inputIndex) {
            return inputIndex >= 0 &&
                   inputIndex < static_cast<int>(outputNode->inputs.size()) &&
                   graph.FindInputSource(outputNode->inputs[inputIndex]) != 0;
        };
        if (!inputLinked(0)) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Material Output has no Base Color input."), outputNode->id, GraphIssue::Severity::Error});
        }
        if (!inputLinked(2)) {
            result.issues.push_back({QObject::tr("Material Output has no Roughness input; default material roughness will be used."), outputNode->id, GraphIssue::Severity::Warning});
        }
        if (!inputLinked(1)) {
            result.issues.push_back({QObject::tr("Material Output has no Metallic input; default material metallic value will be used."), outputNode->id, GraphIssue::Severity::Info});
        }
    }

    for (const Node& node : graph.GetNodes()) {
        if (!NodeAffectsRenderedMaterial(node.type)) {
            result.issues.push_back({QObject::tr("%1 does not affect the rendered material yet. Its output is ignored.").arg(QString::fromStdString(node.displayName)), node.id, GraphIssue::Severity::Warning});
        }
        if (IsTextureNode(node.type) && node.resourcePath.empty()) {
            result.issues.push_back({QObject::tr("%1 has no texture assigned.").arg(QString::fromStdString(node.displayName)), node.id, GraphIssue::Severity::Warning});
        } else if (IsTextureNode(node.type) && !node.resourcePath.empty()) {
            std::filesystem::path texturePath(node.resourcePath);
            if (texturePath.is_absolute() && !std::filesystem::exists(texturePath)) {
                result.issues.push_back({QObject::tr("Texture file is missing: %1").arg(QString::fromStdString(node.resourcePath)), node.id, GraphIssue::Severity::Warning});
            }
        }
    }

    enum class Mark : uint8_t { None, Visiting, Done };
    std::unordered_map<NodeID, Mark> mark;
    std::function<bool(NodeID)> dfs = [&](NodeID nodeId) {
        mark[nodeId] = Mark::Visiting;
        auto it = adjacency.find(nodeId);
        if (it != adjacency.end()) {
            for (NodeID dst : it->second) {
                if (mark[dst] == Mark::Visiting) {
                    return true;
                }
                if (mark[dst] == Mark::None && dfs(dst)) {
                    return true;
                }
            }
        }
        mark[nodeId] = Mark::Done;
        return false;
    };

    for (const Node& node : graph.GetNodes()) {
        if (mark[node.id] == Mark::None && dfs(node.id)) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Graph contains a cycle."), node.id});
            break;
        }
    }

    if (outputNode) {
        std::unordered_set<NodeID> used;
        std::vector<NodeID> stack{outputNode->id};
        while (!stack.empty()) {
            const NodeID current = stack.back();
            stack.pop_back();
            if (!used.insert(current).second) {
                continue;
            }
            auto it = reverseAdjacency.find(current);
            if (it != reverseAdjacency.end()) {
                for (NodeID source : it->second) {
                    stack.push_back(source);
                }
            }
        }
        for (const Node& node : graph.GetNodes()) {
            if (node.type != NodeType::MaterialOutput && used.find(node.id) == used.end()) {
                result.issues.push_back({QObject::tr("%1 is not connected to the Material Output.").arg(QString::fromStdString(node.displayName)), node.id, GraphIssue::Severity::Info});
            }
        }
    }

    return result;
}

class PinItem final : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 4001 };

    PinItem(PinID pinId, PinKind kind, ValueType valueType, QString pinLabel, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), m_pinId(pinId), m_kind(kind), m_valueType(valueType), m_label(std::move(pinLabel)) {
        setAcceptHoverEvents(true);
        setToolTip(QObject::tr("%1 (%2)").arg(m_label, ValueTypeLabel(m_valueType)));
        setZValue(2.0);
    }

    int type() const override { return Type; }
    PinID pinId() const { return m_pinId; }
    PinKind kind() const { return m_kind; }
    ValueType valueType() const { return m_valueType; }
    const QString& label() const { return m_label; }
    QPointF anchorScenePos() const { return mapToScene(QPointF(0.0, 0.0)); }

    QRectF boundingRect() const override {
        return QRectF(-6.0, -6.0, 12.0, 12.0);
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(Qt::black, 1.0));
        painter->setBrush(PinColor(m_valueType));
        painter->drawEllipse(boundingRect());
    }

private:
    PinID m_pinId;
    PinKind m_kind;
    ValueType m_valueType;
    QString m_label;
};

class LinkItem final : public QGraphicsPathItem {
public:
    enum { Type = QGraphicsItem::UserType + 4002 };

    explicit LinkItem(LinkID linkId, QGraphicsItem* parent = nullptr)
        : QGraphicsPathItem(parent), m_linkId(linkId) {
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setZValue(1.0);
    }

    int type() const override { return Type; }
    LinkID linkId() const { return m_linkId; }

    void setPins(PinItem* start, PinItem* end) {
        m_start = start;
        m_end = end;
        updatePath();
    }

    void setLiveEnd(const QPointF& scenePos) {
        m_liveEnd = scenePos;
        updatePath();
    }

    void updatePath() {
        if (!m_start) {
            setPath(QPainterPath());
            return;
        }
        const QPointF start = m_start->anchorScenePos();
        const QPointF end = m_end ? m_end->anchorScenePos() : m_liveEnd;
        const qreal dx = std::max<qreal>(40.0, std::abs(end.x() - start.x()) * 0.5);
        QPainterPath p(start);
        p.cubicTo(start + QPointF(dx, 0.0), end - QPointF(dx, 0.0), end);
        setPath(p);

        QPen pen(isSelected() ? Theme::Color(Theme::Token::AccentSelection) : Theme::Color(Theme::Token::TextMuted));
        pen.setWidthF(isSelected() ? 3.0 : 2.0);
        setPen(pen);
    }

private:
    LinkID m_linkId;
    PinItem* m_start = nullptr;
    PinItem* m_end = nullptr;
    QPointF m_liveEnd;
};

class NodeItem final : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 4003 };

    explicit NodeItem(const Node& node, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), m_nodeId(node.id), m_title(QString::fromStdString(node.displayName)), m_type(node.type) {
        setFlag(QGraphicsItem::ItemIsMovable, true);
        setFlag(QGraphicsItem::ItemIsSelectable, true);
        setFlag(QGraphicsItem::ItemSendsGeometryChanges, true);
        setToolTip(NodeTooltip(node.type));
        setZValue(3.0);
        if (m_type == NodeType::MaterialOutput) {
            m_width = 270.0;
        }
    }

    int type() const override { return Type; }
    NodeID nodeId() const { return m_nodeId; }
    NodeType nodeType() const { return m_type; }

    QRectF boundingRect() const override {
        return QRectF(0.0, 0.0, m_width, m_height);
    }

    void setTitle(const QString& title) {
        m_title = title;
        update();
    }

    void setMoveCommitCallback(std::function<void(NodeID, QPointF, QPointF)> callback) {
        m_moveCommit = std::move(callback);
    }

    void setMovedCallback(std::function<void(NodeID)> callback) {
        m_moved = std::move(callback);
    }

    void setIssueSeverity(std::optional<GraphIssue::Severity> severity) {
        m_issueSeverity = severity;
        update();
    }

    void setDimmed(bool dimmed) {
        m_dimmed = dimmed;
        setOpacity(dimmed ? 0.55 : 1.0);
    }

    void setPins(std::vector<PinItem*> inputs, std::vector<PinItem*> outputs) {
        m_inputs = std::move(inputs);
        m_outputs = std::move(outputs);

        const int rows = std::max<int>(static_cast<int>(m_inputs.size()), static_cast<int>(m_outputs.size()));
        m_height = 38.0 + std::max(1, rows) * 22.0 + 10.0;

        for (int i = 0; i < static_cast<int>(m_inputs.size()); ++i) {
            m_inputs[i]->setParentItem(this);
            m_inputs[i]->setPos(8.0, 46.0 + i * 22.0);
        }
        for (int i = 0; i < static_cast<int>(m_outputs.size()); ++i) {
            m_outputs[i]->setParentItem(this);
            m_outputs[i]->setPos(m_width - 8.0, 46.0 + i * 22.0);
        }
        update();
    }

    PinItem* findPin(PinID pinId) const {
        for (PinItem* p : m_inputs) {
            if (p->pinId() == pinId) return p;
        }
        for (PinItem* p : m_outputs) {
            if (p->pinId() == pinId) return p;
        }
        return nullptr;
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget*) override {
        painter->setRenderHint(QPainter::Antialiasing, true);

        QRectF rect = boundingRect();
        painter->setPen(QPen(isSelected() ? Theme::Color(Theme::Token::BorderFocus) : Theme::Color(Theme::Token::BorderDefault), isSelected() ? 2.0 : 1.0));
        painter->setBrush(Theme::Color(Theme::Token::SurfaceRaised));
        painter->drawRoundedRect(rect, m_type == NodeType::MaterialOutput ? 10.0 : 6.0, m_type == NodeType::MaterialOutput ? 10.0 : 6.0);

        QRectF titleRect(0.0, 0.0, rect.width(), 30.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(NodeTitleColor(m_type));
        painter->drawRoundedRect(titleRect, 6.0, 6.0);
        painter->setBrush(Theme::Color(Theme::Token::SurfaceRaised));
        painter->drawRect(0.0, 26.0, rect.width(), 4.0);

        painter->setPen(Theme::Color(Theme::Token::TextPrimary));
        QFont titleFont = painter->font();
        titleFont.setBold(m_type == NodeType::MaterialOutput);
        painter->setFont(titleFont);
        painter->drawText(titleRect.adjusted(8.0, 0.0, -8.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, m_title);
        titleFont.setBold(false);
        painter->setFont(titleFont);

        painter->setPen(Theme::Color(Theme::Token::TextSecondary));
        for (PinItem* p : m_inputs) {
            const QPointF lp = p->pos();
            painter->drawText(QRectF(18.0, lp.y() - 8.0, m_width * 0.5, 16.0), Qt::AlignVCenter | Qt::AlignLeft, p->label());
        }
        for (PinItem* p : m_outputs) {
            const QPointF lp = p->pos();
            painter->drawText(QRectF(m_width * 0.45, lp.y() - 8.0, m_width * 0.45, 16.0), Qt::AlignVCenter | Qt::AlignRight, p->label());
        }

        if (m_issueSeverity.has_value()) {
            QColor issueColor = Theme::Color(Theme::Token::StatusInfo);
            QString marker = QStringLiteral("i");
            if (*m_issueSeverity == GraphIssue::Severity::Error) {
                issueColor = Theme::Color(Theme::Token::StatusError);
                marker = QStringLiteral("!");
            } else if (*m_issueSeverity == GraphIssue::Severity::Warning) {
                issueColor = Theme::Color(Theme::Token::StatusWarning);
                marker = QStringLiteral("!");
            }
            const QRectF badge(rect.width() - 26.0, 6.0, 18.0, 18.0);
            painter->setPen(Qt::NoPen);
            painter->setBrush(issueColor);
            painter->drawEllipse(badge);
            painter->setPen(Theme::Color(Theme::Token::SurfaceBase));
            painter->drawText(badge, Qt::AlignCenter, marker);
        }

        Q_UNUSED(option);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
        if (change == QGraphicsItem::ItemPositionHasChanged) {
            if (m_moved) {
                m_moved(m_nodeId);
            }
        }
        return QGraphicsItem::itemChange(change, value);
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        m_dragStart = pos();
        QGraphicsItem::mousePressEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsItem::mouseReleaseEvent(event);
        if (m_moveCommit && m_dragStart != pos()) {
            m_moveCommit(m_nodeId, m_dragStart, pos());
        }
    }

private:
    NodeID m_nodeId;
    QString m_title;
    NodeType m_type;
    qreal m_width = 220.0;
    qreal m_height = 120.0;
    QPointF m_dragStart;
    bool m_dimmed = false;
    std::optional<GraphIssue::Severity> m_issueSeverity;
    std::vector<PinItem*> m_inputs;
    std::vector<PinItem*> m_outputs;
    std::function<void(NodeID, QPointF, QPointF)> m_moveCommit;
    std::function<void(NodeID)> m_moved;
};

class MaterialGraphScene;

class AddNodeCommand final : public QUndoCommand {
public:
    AddNodeCommand(MaterialGraphScene* scene, NodeType type, const QPointF& pos, std::optional<Node> source = std::nullopt);
    void undo() override;
    void redo() override;

private:
    MaterialGraphScene* m_scene;
    NodeType m_type;
    QPointF m_pos;
    std::optional<Node> m_source;
    Node m_snapshot;
    bool m_created = false;
};

class DeleteNodeCommand final : public QUndoCommand {
public:
    DeleteNodeCommand(MaterialGraphScene* scene, NodeID nodeId);
    void undo() override;
    void redo() override;

private:
    MaterialGraphScene* m_scene;
    NodeID m_nodeId;
    Node m_node;
    std::vector<Link> m_links;
    bool m_valid = false;
};

class AddLinkCommand final : public QUndoCommand {
public:
    AddLinkCommand(MaterialGraphScene* scene, PinID startPin, PinID endPin);
    void undo() override;
    void redo() override;

private:
    MaterialGraphScene* m_scene;
    PinID m_startPin;
    PinID m_endPin;
    Link m_link;
    bool m_created = false;
};

class DeleteLinkCommand final : public QUndoCommand {
public:
    DeleteLinkCommand(MaterialGraphScene* scene, LinkID linkId);
    void undo() override;
    void redo() override;

private:
    MaterialGraphScene* m_scene;
    Link m_link;
    bool m_valid = false;
};

class MoveNodeCommand final : public QUndoCommand {
public:
    MoveNodeCommand(MaterialGraphScene* scene, NodeID nodeId, const QPointF& from, const QPointF& to);
    void undo() override;
    void redo() override;

private:
    MaterialGraphScene* m_scene;
    NodeID m_nodeId;
    QPointF m_from;
    QPointF m_to;
};

class EditNodePropertyCommand final : public QUndoCommand {
public:
    EditNodePropertyCommand(std::function<void()> undoFn, std::function<void()> redoFn, QString label)
        : m_undoFn(std::move(undoFn)), m_redoFn(std::move(redoFn)) {
        setText(label);
    }

    void undo() override { if (m_undoFn) m_undoFn(); }
    void redo() override { if (m_redoFn) m_redoFn(); }

private:
    std::function<void()> m_undoFn;
    std::function<void()> m_redoFn;
};

class EditMaterialPropertyCommand final : public QUndoCommand {
public:
    EditMaterialPropertyCommand(std::function<void()> undoFn, std::function<void()> redoFn, QString label)
        : m_undoFn(std::move(undoFn)), m_redoFn(std::move(redoFn)) {
        setText(label);
    }

    void undo() override { if (m_undoFn) m_undoFn(); }
    void redo() override { if (m_redoFn) m_redoFn(); }

private:
    std::function<void()> m_undoFn;
    std::function<void()> m_redoFn;
};

class MaterialGraphScene final : public QGraphicsScene {
public:
    explicit MaterialGraphScene(Graph* graph, QUndoStack* undoStack, QObject* parent = nullptr)
        : QGraphicsScene(parent), m_graph(graph), m_undo(undoStack) {
        setBackgroundBrush(Theme::Color(Theme::Token::SurfaceViewport));
        connect(this, &QGraphicsScene::selectionChanged, this, [this]() {
            if (!m_selectionChanged) {
                return;
            }
            NodeID selectedNode = 0;
            const auto itemsSelected = selectedItems();
            for (QGraphicsItem* item : itemsSelected) {
                if (item->type() == NodeItem::Type) {
                    selectedNode = static_cast<NodeItem*>(item)->nodeId();
                    break;
                }
            }
            m_selectionChanged(selectedNode);
        });
    }

    Graph* graph() const { return m_graph; }
    QUndoStack* undoStack() const { return m_undo; }

    void setSelectionCallback(std::function<void(NodeID)> callback) {
        m_selectionChanged = std::move(callback);
    }

    void setGraphChangedCallback(std::function<void()> callback) {
        m_graphChanged = std::move(callback);
    }

    void setNodeSearchCallback(std::function<void(const QPoint&, const QPointF&)> callback) {
        m_nodeSearchRequested = std::move(callback);
    }

    void setSnapToGrid(bool enabled) {
        m_snapToGrid = enabled;
    }

    void reloadFromGraph() {
        clear();
        m_nodeItems.clear();
        m_pinItems.clear();
        m_linkItems.clear();
        m_liveDragPin = nullptr;
        m_liveWire = nullptr;

        for (const Node& node : m_graph->GetNodes()) {
            auto* nodeItem = new NodeItem(node);
            nodeItem->setPos(node.posX, node.posY);
            nodeItem->setMoveCommitCallback([this](NodeID id, QPointF from, QPointF to) {
                if (m_ignoreMoveCommit) {
                    return;
                }
                if (m_snapToGrid) {
                    to = snapPoint(to);
                    if (NodeItem* item = nodeItemFor(id)) {
                        m_ignoreMoveCommit = true;
                        item->setPos(to);
                        m_ignoreMoveCommit = false;
                    }
                }
                m_undo->push(new MoveNodeCommand(this, id, from, to));
            });
            nodeItem->setMovedCallback([this](NodeID id) {
                updateLinksForNode(id);
                updateAlignmentGuides(id);
                Node* model = m_graph->FindNode(id);
                if (model) {
                    NodeItem* item = nodeItemFor(id);
                    if (item) {
                        const QPointF p = item->pos();
                        model->posX = static_cast<float>(p.x());
                        model->posY = static_cast<float>(p.y());
                    }
                }
            });

            std::vector<PinItem*> inPins;
            std::vector<PinItem*> outPins;
            for (PinID pinId : node.inputs) {
                const Pin* pin = m_graph->FindPin(pinId);
                if (!pin) continue;
                PinItem* pinItem = new PinItem(pin->id, pin->kind, pin->valueType, QString::fromStdString(pin->name), nodeItem);
                inPins.push_back(pinItem);
                m_pinItems[pin->id] = pinItem;
            }
            for (PinID pinId : node.outputs) {
                const Pin* pin = m_graph->FindPin(pinId);
                if (!pin) continue;
                PinItem* pinItem = new PinItem(pin->id, pin->kind, pin->valueType, QString::fromStdString(pin->name), nodeItem);
                outPins.push_back(pinItem);
                m_pinItems[pin->id] = pinItem;
            }
            nodeItem->setPins(std::move(inPins), std::move(outPins));

            addItem(nodeItem);
            m_nodeItems[node.id] = nodeItem;
        }

        for (const Link& link : m_graph->GetLinks()) {
            addLinkVisual(link);
        }

        emitGraphChanged();
    }

    void applyValidation(const GraphValidationResult& validation) {
        std::unordered_map<NodeID, GraphIssue::Severity> severityByNode;
        for (const GraphIssue& issue : validation.issues) {
            if (issue.nodeId <= 0) {
                continue;
            }
            auto it = severityByNode.find(issue.nodeId);
            if (it == severityByNode.end() ||
                static_cast<int>(issue.severity) < static_cast<int>(it->second)) {
                severityByNode[issue.nodeId] = issue.severity;
            }
        }

        std::unordered_set<NodeID> connectedToOutput;
        std::vector<NodeID> stack;
        for (const Node& node : m_graph->GetNodes()) {
            if (node.type == NodeType::MaterialOutput) {
                stack.push_back(node.id);
                break;
            }
        }
        while (!stack.empty()) {
            const NodeID current = stack.back();
            stack.pop_back();
            if (!connectedToOutput.insert(current).second) {
                continue;
            }
            for (const Link& link : m_graph->GetLinks()) {
                const NodeID dst = PinToNode(*m_graph, link.endPin);
                if (dst == current) {
                    const NodeID src = PinToNode(*m_graph, link.startPin);
                    if (src > 0) {
                        stack.push_back(src);
                    }
                }
            }
        }

        for (auto& [nodeId, item] : m_nodeItems) {
            auto issueIt = severityByNode.find(nodeId);
            item->setIssueSeverity(issueIt == severityByNode.end()
                ? std::optional<GraphIssue::Severity>{}
                : std::optional<GraphIssue::Severity>{issueIt->second});
            const Node* node = m_graph->FindNode(nodeId);
            item->setDimmed(node && node->type != NodeType::MaterialOutput && connectedToOutput.find(nodeId) == connectedToOutput.end());
        }
    }

    NodeItem* nodeItemFor(NodeID id) const {
        auto it = m_nodeItems.find(id);
        return it == m_nodeItems.end() ? nullptr : it->second;
    }

    void updateNodeTitle(NodeID id, const QString& title) {
        NodeItem* item = nodeItemFor(id);
        if (item) {
            item->setTitle(title);
        }
    }

    void setNodePositionNoUndo(NodeID nodeId, const QPointF& pos) {
        Node* node = m_graph->FindNode(nodeId);
        NodeItem* item = nodeItemFor(nodeId);
        if (!node || !item) {
            return;
        }
        m_ignoreMoveCommit = true;
        item->setPos(pos);
        m_ignoreMoveCommit = false;
        node->posX = static_cast<float>(pos.x());
        node->posY = static_cast<float>(pos.y());
        updateLinksForNode(nodeId);
        emitGraphChanged();
    }

    void removeSelected() {
        std::vector<LinkID> links;
        std::vector<NodeID> nodes;
        const auto selected = selectedItems();
        for (QGraphicsItem* item : selected) {
            if (item->type() == LinkItem::Type) {
                links.push_back(static_cast<LinkItem*>(item)->linkId());
            } else if (item->type() == NodeItem::Type) {
                const NodeID nodeId = static_cast<NodeItem*>(item)->nodeId();
                const Node* node = m_graph->FindNode(nodeId);
                if (node && node->type != NodeType::MaterialOutput) {
                    nodes.push_back(nodeId);
                }
            }
        }
        for (LinkID id : links) {
            m_undo->push(new DeleteLinkCommand(this, id));
        }
        for (NodeID id : nodes) {
            m_undo->push(new DeleteNodeCommand(this, id));
        }
    }

    void duplicateSelected() {
        const auto selected = selectedItems();
        for (QGraphicsItem* item : selected) {
            if (item->type() != NodeItem::Type) {
                continue;
            }
            const NodeID nodeId = static_cast<NodeItem*>(item)->nodeId();
            const Node* source = m_graph->FindNode(nodeId);
            if (!source || source->type == NodeType::MaterialOutput) {
                continue;
            }
            const QPointF pos(source->posX + 40.0f, source->posY + 30.0f);
            m_undo->push(new AddNodeCommand(this, source->type, pos, *source));
        }
    }

    void addNode(NodeType type, const QPointF& scenePos) {
        m_undo->push(new AddNodeCommand(this, type, scenePos));
    }

    void addLink(PinID outputPin, PinID inputPin) {
        m_undo->push(new AddLinkCommand(this, outputPin, inputPin));
    }

    bool restoreNodeFromSnapshot(const Node& node) {
        Node& restored = m_graph->CreateNodeWithID(node.id, node.type, node.displayName);
        restored.serializedType = node.serializedType;
        restored.parameterName = node.parameterName;
        restored.operation = node.operation;
        restored.sourceSpace = node.sourceSpace;
        restored.targetSpace = node.targetSpace;
        restored.scalar = node.scalar;
        restored.color = node.color;
        restored.resourcePath = node.resourcePath;
        restored.worldAlignedScale = node.worldAlignedScale;
        restored.boolValue = node.boolValue;
        restored.maskR = node.maskR;
        restored.maskG = node.maskG;
        restored.maskB = node.maskB;
        restored.maskA = node.maskA;
        restored.posX = node.posX;
        restored.posY = node.posY;
        restored.unknownFields = node.unknownFields;
        m_graph->SetPinIDsForNode(restored.id, node.inputs, node.outputs);
        m_graph->UpdateNextIDs();
        reloadFromGraph();
        return true;
    }

    bool removeNode(NodeID nodeId) {
        const bool removed = m_graph->RemoveNode(nodeId);
        if (removed) {
            reloadFromGraph();
        }
        return removed;
    }

    bool restoreLinkFromSnapshot(const Link& link) {
        Link* restored = m_graph->CreateLinkWithID(link.id, link.startPin, link.endPin);
        if (!restored) {
            return false;
        }
        restored->orphan = link.orphan;
        restored->directionRepairedInMemory = link.directionRepairedInMemory;
        restored->originalStartPin = link.originalStartPin;
        restored->originalEndPin = link.originalEndPin;
        restored->unknownFields = link.unknownFields;
        m_graph->UpdateNextIDs();
        reloadFromGraph();
        return true;
    }

    bool removeLink(LinkID linkId) {
        const bool removed = m_graph->RemoveLink(linkId);
        if (removed) {
            reloadFromGraph();
        }
        return removed;
    }

    std::optional<Link> findLink(LinkID linkId) const {
        const Link* link = m_graph->FindLink(linkId);
        if (!link) {
            return std::nullopt;
        }
        return *link;
    }

    std::optional<Node> findNode(NodeID nodeId) const {
        const Node* node = m_graph->FindNode(nodeId);
        if (!node) {
            return std::nullopt;
        }
        return *node;
    }

    void highlightNode(NodeID nodeId) {
        NodeItem* item = nodeItemFor(nodeId);
        if (!item) {
            return;
        }
        clearSelection();
        item->setSelected(true);
        for (QGraphicsView* view : views()) {
            view->centerOn(item);
        }
        if (m_selectionChanged) {
            m_selectionChanged(nodeId);
        }
    }

    void focusSelection() {
        QRectF bounds;
        bool hasBounds = false;
        for (QGraphicsItem* item : selectedItems()) {
            if (item->type() == NodeItem::Type || item->type() == LinkItem::Type) {
                bounds = hasBounds ? bounds.united(item->sceneBoundingRect()) : item->sceneBoundingRect();
                hasBounds = true;
            }
        }
        if (!hasBounds) {
            bounds = itemsBoundingRect();
        }
        if (bounds.isValid() && !bounds.isEmpty()) {
            for (QGraphicsView* view : views()) {
                view->fitInView(bounds.adjusted(-120, -80, 120, 80), Qt::KeepAspectRatio);
            }
        }
    }

    void breakLinksForPin(PinID pinId) {
        std::vector<LinkID> toDelete;
        for (const Link& link : m_graph->GetLinks()) {
            if (link.startPin == pinId || link.endPin == pinId) {
                toDelete.push_back(link.id);
            }
        }
        for (LinkID id : toDelete) {
            m_undo->push(new DeleteLinkCommand(this, id));
        }
    }

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override {
        QGraphicsScene::drawBackground(painter, rect);
        constexpr qreal grid = 32.0;
        const qreal left = std::floor(rect.left() / grid) * grid;
        const qreal top = std::floor(rect.top() / grid) * grid;
        QPen minor(Theme::Color(Theme::Token::SurfaceRaised), 1.0);
        painter->setPen(minor);
        for (qreal x = left; x < rect.right(); x += grid) {
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        }
        for (qreal y = top; y < rect.bottom(); y += grid) {
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
    }

    void drawForeground(QPainter* painter, const QRectF& rect) override {
        QGraphicsScene::drawForeground(painter, rect);
        if (m_alignmentGuideLines.empty()) {
            return;
        }
        QPen guidePen(Theme::Color(Theme::Token::BorderFocus), 1.0, Qt::DashLine);
        painter->setPen(guidePen);
        for (const QLineF& line : m_alignmentGuideLines) {
            painter->drawLine(line);
        }
    }

    void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());
            if (hit && hit->type() == PinItem::Type) {
                PinItem* pin = static_cast<PinItem*>(hit);
                if (pin->kind() == PinKind::Output) {
                    beginLiveWire(pin, event->scenePos());
                    event->accept();
                    return;
                }
            }
        }
        QGraphicsScene::mousePressEvent(event);
    }

    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override {
        if (m_liveWire) {
            m_liveWire->setLiveEnd(event->scenePos());
            event->accept();
            return;
        }
        QGraphicsScene::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override {
        if (m_liveWire && event->button() == Qt::LeftButton) {
            QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());
            PinItem* target = (hit && hit->type() == PinItem::Type) ? static_cast<PinItem*>(hit) : nullptr;
            if (target && m_liveDragPin && canConnectPins(m_liveDragPin, target)) {
                addLink(m_liveDragPin->pinId(), target->pinId());
            }
            endLiveWire();
            event->accept();
            return;
        }
        QGraphicsScene::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && !m_alignmentGuideLines.empty()) {
            m_alignmentGuideLines.clear();
            update();
        }
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override {
        QMenu menu;
        QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());

        if (!hit) {
            QAction* search = menu.addAction(QObject::tr("Add Node..."));
            connect(search, &QAction::triggered, this, [this, event]() {
                if (m_nodeSearchRequested) {
                    m_nodeSearchRequested(event->screenPos(), event->scenePos());
                }
            });

            QMenu* add = menu.addMenu(QObject::tr("Add Node by Category"));

            QMenu* outputMenu = add->addMenu(QObject::tr("PBR"));
            QMenu* inputsMenu = add->addMenu(QObject::tr("Inputs"));
            QMenu* texturesMenu = add->addMenu(QObject::tr("Textures"));
            QMenu* mathMenu = add->addMenu(QObject::tr("Math"));
            QMenu* parametersMenu = add->addMenu(QObject::tr("Parameters"));
            QMenu* utilitiesMenu = add->addMenu(QObject::tr("Utility"));
            QMenu* proceduralMenu = add->addMenu(QObject::tr("Procedural"));
            QMenu* logicMenu = add->addMenu(QObject::tr("Logic"));
            QMenu* blendMenu = add->addMenu(QObject::tr("Blend"));

            auto categoryMenu = [&](NodeType type) -> QMenu* {
                const QString cat = NodeCategory(type);
                if (cat == "PBR") return outputMenu;
                if (cat == "Inputs") return inputsMenu;
                if (cat == "Textures") return texturesMenu;
                if (cat == "Math") return mathMenu;
                if (cat == "Parameters") return parametersMenu;
                if (cat == "Procedural") return proceduralMenu;
                if (cat == "Logic") return logicMenu;
                if (cat == "Blend") return blendMenu;
                return utilitiesMenu;
            };

            auto addAction = [&](QMenu* targetMenu, const QString& label, NodeType type) {
                QAction* a = targetMenu->addAction(label);
                connect(a, &QAction::triggered, this, [this, type, event]() {
                    addNode(type, event->scenePos());
                });
            };

            for (const NodeMenuEntry& entry : NodeMenuEntries()) {
                addAction(categoryMenu(entry.type), NodeMenuLabel(entry.type, entry.label), entry.type);
            }

            menu.addSeparator();
            QAction* frame = menu.addAction(QObject::tr("Frame Selection"));
            connect(frame, &QAction::triggered, this, [this]() {
                focusSelection();
            });
        } else if (hit->type() == NodeItem::Type) {
            NodeItem* nodeItem = static_cast<NodeItem*>(hit);
            QAction* duplicate = menu.addAction(QObject::tr("Duplicate"));
            QAction* remove = menu.addAction(QObject::tr("Delete"));
            QAction* preview = menu.addAction(QObject::tr("Preview Node"));
            QAction* rename = menu.addAction(QObject::tr("Rename"));
            connect(duplicate, &QAction::triggered, this, [this, nodeItem]() {
                clearSelection();
                nodeItem->setSelected(true);
                duplicateSelected();
            });
            connect(remove, &QAction::triggered, this, [this, nodeItem]() {
                clearSelection();
                nodeItem->setSelected(true);
                removeSelected();
            });
            connect(preview, &QAction::triggered, this, [this, nodeItem]() {
                highlightNode(nodeItem->nodeId());
            });
            connect(rename, &QAction::triggered, this, [this, nodeItem]() {
                Node* node = m_graph->FindNode(nodeItem->nodeId());
                if (!node) {
                    return;
                }
                bool ok = false;
                const QString current = QString::fromStdString(node->displayName);
                const QString next = QInputDialog::getText(
                    nullptr,
                    QObject::tr("Rename Node"),
                    QObject::tr("Display name:"),
                    QLineEdit::Normal,
                    current,
                    &ok);
                if (!ok) {
                    return;
                }
                const std::string oldName = node->displayName;
                const std::string newName = next.trimmed().isEmpty() ? oldName : next.trimmed().toStdString();
                if (oldName == newName) {
                    return;
                }
                const NodeID nodeId = node->id;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldName]() {
                        if (Node* n = m_graph->FindNode(nodeId)) {
                            n->displayName = oldName;
                            updateNodeTitle(nodeId, QString::fromStdString(oldName));
                        }
                    },
                    [this, nodeId, newName]() {
                        if (Node* n = m_graph->FindNode(nodeId)) {
                            n->displayName = newName;
                            updateNodeTitle(nodeId, QString::fromStdString(newName));
                        }
                    },
                    QObject::tr("Rename Node")));
            });
        } else if (hit->type() == PinItem::Type) {
            PinItem* pin = static_cast<PinItem*>(hit);
            QAction* breakLinks = menu.addAction(QObject::tr("Break Links"));
            QAction* copyValue = menu.addAction(QObject::tr("Copy Value"));
            connect(breakLinks, &QAction::triggered, this, [this, pin]() {
                breakLinksForPin(pin->pinId());
            });
            connect(copyValue, &QAction::triggered, this, [pin]() {
                QGuiApplication::clipboard()->setText(pin->label());
            });
        } else if (hit->type() == LinkItem::Type) {
            LinkItem* linkItem = static_cast<LinkItem*>(hit);
            QAction* remove = menu.addAction(QObject::tr("Delete Link"));
            connect(remove, &QAction::triggered, this, [this, linkItem]() {
                m_undo->push(new DeleteLinkCommand(this, linkItem->linkId()));
            });
        }

        menu.exec(event->screenPos());
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
        QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());
        if (hit && hit->type() == NodeItem::Type) {
            clearSelection();
            hit->setSelected(true);
            focusSelection();
            event->accept();
            return;
        }
        QGraphicsScene::mouseDoubleClickEvent(event);
    }

private:
    QPointF snapPoint(const QPointF& pos) const {
        constexpr qreal grid = 32.0;
        return QPointF(std::round(pos.x() / grid) * grid, std::round(pos.y() / grid) * grid);
    }

    bool canConnectPins(PinItem* output, PinItem* input) const {
        if (!output || !input) return false;
        if (output->kind() != PinKind::Output || input->kind() != PinKind::Input) return false;
        return ArePinTypesCompatible(output->valueType(), input->valueType());
    }

    void beginLiveWire(PinItem* pin, const QPointF& startPos) {
        endLiveWire();
        m_liveDragPin = pin;
        m_liveWire = new LinkItem(0);
        m_liveWire->setPins(pin, nullptr);
        m_liveWire->setLiveEnd(startPos);
        addItem(m_liveWire);
    }

    void endLiveWire() {
        if (m_liveWire) {
            removeItem(m_liveWire);
            delete m_liveWire;
            m_liveWire = nullptr;
        }
        m_liveDragPin = nullptr;
    }

    void addLinkVisual(const Link& link) {
        PinItem* startPin = pinItem(link.startPin);
        PinItem* endPin = pinItem(link.endPin);
        if (!startPin || !endPin || link.orphan) {
            return;
        }
        auto* item = new LinkItem(link.id);
        item->setPins(startPin, endPin);
        addItem(item);
        m_linkItems[link.id] = item;
    }

    PinItem* pinItem(PinID id) const {
        auto it = m_pinItems.find(id);
        return it == m_pinItems.end() ? nullptr : it->second;
    }

    void updateLinksForNode(NodeID nodeId) {
        const Node* node = m_graph->FindNode(nodeId);
        if (!node) return;
        std::unordered_set<LinkID> affected;
        for (PinID pinId : node->inputs) {
            for (const Link& link : m_graph->GetLinks()) {
                if (link.startPin == pinId || link.endPin == pinId) affected.insert(link.id);
            }
        }
        for (PinID pinId : node->outputs) {
            for (const Link& link : m_graph->GetLinks()) {
                if (link.startPin == pinId || link.endPin == pinId) affected.insert(link.id);
            }
        }
        for (LinkID id : affected) {
            auto it = m_linkItems.find(id);
            if (it != m_linkItems.end()) {
                it->second->updatePath();
            }
        }
    }

    void updateAlignmentGuides(NodeID movingNodeId) {
        m_alignmentGuideLines.clear();
        NodeItem* moving = nodeItemFor(movingNodeId);
        if (!moving) {
            return;
        }
        const QRectF movingRect = moving->sceneBoundingRect();
        const QPointF movingCenter = movingRect.center();
        constexpr qreal threshold = 6.0;
        const QRectF sceneBounds = itemsBoundingRect().adjusted(-200, -200, 200, 200);

        for (const auto& [nodeId, other] : m_nodeItems) {
            if (nodeId == movingNodeId || !other) {
                continue;
            }
            const QRectF otherRect = other->sceneBoundingRect();
            const QPointF otherCenter = otherRect.center();
            if (std::abs(otherCenter.x() - movingCenter.x()) <= threshold) {
                m_alignmentGuideLines.push_back(QLineF(otherCenter.x(), sceneBounds.top(), otherCenter.x(), sceneBounds.bottom()));
            }
            if (std::abs(otherCenter.y() - movingCenter.y()) <= threshold) {
                m_alignmentGuideLines.push_back(QLineF(sceneBounds.left(), otherCenter.y(), sceneBounds.right(), otherCenter.y()));
            }
            if (m_alignmentGuideLines.size() >= 2) {
                break;
            }
        }
        update();
    }

    void emitGraphChanged() {
        if (m_graphChanged) {
            m_graphChanged();
        }
    }

private:
    Graph* m_graph;
    QUndoStack* m_undo;
    std::unordered_map<NodeID, NodeItem*> m_nodeItems;
    std::unordered_map<PinID, PinItem*> m_pinItems;
    std::unordered_map<LinkID, LinkItem*> m_linkItems;
    std::vector<QLineF> m_alignmentGuideLines;
    PinItem* m_liveDragPin = nullptr;
    LinkItem* m_liveWire = nullptr;
    bool m_ignoreMoveCommit = false;
    bool m_snapToGrid = false;
    std::function<void(NodeID)> m_selectionChanged;
    std::function<void()> m_graphChanged;
    std::function<void(const QPoint&, const QPointF&)> m_nodeSearchRequested;
};

AddNodeCommand::AddNodeCommand(MaterialGraphScene* scene, NodeType type, const QPointF& pos, std::optional<Node> source)
    : m_scene(scene), m_type(type), m_pos(pos), m_source(std::move(source)) {
    setText(QObject::tr("Add Node"));
}

void AddNodeCommand::redo() {
    Graph* graph = m_scene->graph();
    if (!m_created) {
        Node& node = graph->CreateNode(m_type, m_source ? m_source->displayName : std::string{});
        if (m_source) {
            node.scalar = m_source->scalar;
            node.color = m_source->color;
            node.resourcePath = m_source->resourcePath;
            node.worldAlignedScale = m_source->worldAlignedScale;
            node.parameterName = m_source->parameterName;
            node.operation = m_source->operation;
            node.sourceSpace = m_source->sourceSpace;
            node.targetSpace = m_source->targetSpace;
            node.boolValue = m_source->boolValue;
            node.maskR = m_source->maskR;
            node.maskG = m_source->maskG;
            node.maskB = m_source->maskB;
            node.maskA = m_source->maskA;
            node.serializedType = m_source->serializedType;
        }
        node.posX = static_cast<float>(m_pos.x());
        node.posY = static_cast<float>(m_pos.y());
        m_snapshot = node;
        m_created = true;
    } else {
        m_scene->restoreNodeFromSnapshot(m_snapshot);
    }
    graph->UpdateNextIDs();
    m_scene->reloadFromGraph();
    m_scene->highlightNode(m_snapshot.id);
}

void AddNodeCommand::undo() {
    m_scene->removeNode(m_snapshot.id);
}

DeleteNodeCommand::DeleteNodeCommand(MaterialGraphScene* scene, NodeID nodeId)
    : m_scene(scene), m_nodeId(nodeId) {
    setText(QObject::tr("Delete Node"));
    if (std::optional<Node> node = m_scene->findNode(nodeId)) {
        m_node = *node;
        m_valid = true;
        for (const Link& link : m_scene->graph()->GetLinks()) {
            const NodeID startNode = PinToNode(*m_scene->graph(), link.startPin);
            const NodeID endNode = PinToNode(*m_scene->graph(), link.endPin);
            if (startNode == nodeId || endNode == nodeId) {
                m_links.push_back(link);
            }
        }
    }
}

void DeleteNodeCommand::redo() {
    if (!m_valid) return;
    m_scene->removeNode(m_nodeId);
}

void DeleteNodeCommand::undo() {
    if (!m_valid) return;
    m_scene->restoreNodeFromSnapshot(m_node);
    for (const Link& link : m_links) {
        m_scene->restoreLinkFromSnapshot(link);
    }
}

AddLinkCommand::AddLinkCommand(MaterialGraphScene* scene, PinID startPin, PinID endPin)
    : m_scene(scene), m_startPin(startPin), m_endPin(endPin) {
    setText(QObject::tr("Add Link"));
}

void AddLinkCommand::redo() {
    Graph* graph = m_scene->graph();
    if (!m_created) {
        Link* link = graph->CreateLink(m_startPin, m_endPin);
        if (!link) return;
        m_link = *link;
        m_created = true;
    } else {
        m_scene->restoreLinkFromSnapshot(m_link);
        return;
    }
    graph->UpdateNextIDs();
    m_scene->reloadFromGraph();
}

void AddLinkCommand::undo() {
    if (!m_created) return;
    m_scene->removeLink(m_link.id);
}

DeleteLinkCommand::DeleteLinkCommand(MaterialGraphScene* scene, LinkID linkId)
    : m_scene(scene) {
    setText(QObject::tr("Delete Link"));
    if (std::optional<Link> link = m_scene->findLink(linkId)) {
        m_link = *link;
        m_valid = true;
    }
}

void DeleteLinkCommand::redo() {
    if (!m_valid) return;
    m_scene->removeLink(m_link.id);
}

void DeleteLinkCommand::undo() {
    if (!m_valid) return;
    m_scene->restoreLinkFromSnapshot(m_link);
}

MoveNodeCommand::MoveNodeCommand(MaterialGraphScene* scene, NodeID nodeId, const QPointF& from, const QPointF& to)
    : m_scene(scene), m_nodeId(nodeId), m_from(from), m_to(to) {
    setText(QObject::tr("Move Node"));
}

void MoveNodeCommand::undo() {
    m_scene->setNodePositionNoUndo(m_nodeId, m_from);
}

void MoveNodeCommand::redo() {
    m_scene->setNodePositionNoUndo(m_nodeId, m_to);
}

class MaterialGraphView final : public QGraphicsView {
public:
    explicit MaterialGraphView(QWidget* parent = nullptr)
        : QGraphicsView(parent) {
        setRenderHint(QPainter::Antialiasing, true);
        setRenderHint(QPainter::TextAntialiasing, true);
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setCacheMode(QGraphicsView::CacheBackground);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const qreal factor = (event->angleDelta().y() > 0) ? 1.12 : 0.89;
        const qreal nextScale = transform().m11() * factor;
        if (nextScale < 0.25 || nextScale > 2.75) {
            event->accept();
            return;
        }
        scale(factor, factor);
        event->accept();
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton) {
            m_panning = true;
            m_lastPan = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        QGraphicsView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (m_panning) {
            const QPoint delta = event->pos() - m_lastPan;
            m_lastPan = event->pos();
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
            verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
            event->accept();
            return;
        }
        QGraphicsView::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::MiddleButton && m_panning) {
            m_panning = false;
            setCursor(Qt::ArrowCursor);
            event->accept();
            return;
        }
        QGraphicsView::mouseReleaseEvent(event);
    }

private:
    bool m_panning = false;
    QPoint m_lastPan;
};

class MaterialGraphEditorDialog final : public QDialog {
public:
    MaterialGraphEditorDialog(QWidget* parent, const QString& absolutePath)
        : QDialog(parent), m_absolutePath(absolutePath), m_undo(new QUndoStack(this)) {
        setAttribute(Qt::WA_DeleteOnClose);
        setWindowTitle(QObject::tr("Material Editor - %1").arg(QFileInfo(absolutePath).fileName()));
        resize(1280, 760);

        if (!loadMaterial()) {
            QMessageBox::warning(parent, QObject::tr("Cannot Open Material"),
                                 QObject::tr("Failed to load material:\n%1").arg(QString::fromStdString(m_lastError)));
            return;
        }

        buildUi();
        rebuildGraphScene();
        rebuildInspector(0);
        refreshStatus(QObject::tr("Ready"), true);
        m_undo->setClean();
        setDirty(false);
    }

    bool isInitialized() const {
        return m_initialized;
    }

protected:
    void closeEvent(QCloseEvent* event) override {
        if (m_dirty) {
            const QMessageBox::StandardButton result = QMessageBox::question(
                this,
                QObject::tr("Unsaved Material"),
                QObject::tr("This material has unsaved changes. Save before closing?"),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            if (result == QMessageBox::Cancel) {
                event->ignore();
                return;
            }
            if (result == QMessageBox::Save) {
                saveMaterial();
                if (m_dirty) {
                    event->ignore();
                    return;
                }
            }
        }
        shutdownPreview();
        QDialog::closeEvent(event);
    }

private:
    bool loadMaterial() {
        namespace fs = std::filesystem;
        if (!LoadMaterialAssetCompat(fs::path(m_absolutePath.toStdString()), m_material, m_displayName, m_graph, m_lastError)) {
            return false;
        }
        if (!m_graph.HasSourceGraph()) {
            Graph migrated = m_graph;
            BuildMinimalGraphFromLegacyPbr(m_material, migrated);
            MaterialComponent evaluated = m_material;
            std::string evalError;
            if (EvaluateGraph(migrated, evaluated, evalError) &&
                DescribeRenderedDifferences(m_material, evaluated).empty()) {
                m_graph = std::move(migrated);
            }
        }

        if (m_graph.HasSourceGraph() && !m_graph.IsLegacyMode()) {
            MaterialComponent evaluated = m_material;
            std::string evalError;
            if (EvaluateGraph(m_graph, evaluated, evalError)) {
                m_material = evaluated;
            }
        }
        m_initialized = true;
        return true;
    }

    void buildUi() {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* toolbar = new QToolBar(this);
        toolbar->setIconSize(QSize(16, 16));
        m_saveButton = MakeToolButton(toolbar, QStringLiteral("save"), QObject::tr("Save (Ctrl+S)"));
        m_applyButton = MakeToolButton(toolbar, QString(), QObject::tr("Compile and apply to the scene"), QObject::tr("Apply"));
        auto* undoButton = MakeToolButton(toolbar, QStringLiteral("undo"), QObject::tr("Undo (Ctrl+Z)"));
        auto* redoButton = MakeToolButton(toolbar, QStringLiteral("redo"), QObject::tr("Redo (Ctrl+Y)"));
        m_addNodeButton = new QPushButton(Theme::Icon(QStringLiteral("plus"), Theme::IconTone::Primary), QObject::tr("Add node"), toolbar);
        m_addNodeButton->setToolTip(QObject::tr("Search nodes to add (Tab)"));
        m_compileButton = MakeToolButton(toolbar, QStringLiteral("build"), QObject::tr("Compile the graph"), QObject::tr("Compile"));
        m_statusLabel = new QLabel(toolbar);
        m_graphBanner = new QLabel(toolbar);
        for (QLabel* badge : {m_statusLabel, m_graphBanner}) {
            badge->setObjectName(QStringLiteral("StatusBadge"));
        }
        m_snapCheck = MakeToolButton(toolbar, QStringLiteral("snap"), QObject::tr("Snap nodes to the grid"));
        m_snapCheck->setCheckable(true);
        m_viewButton = MakeToolButton(toolbar, QStringLiteral("maximize"), QObject::tr("Frame selection (F)"));
        auto* spacer = new QWidget(toolbar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        toolbar->addWidget(m_saveButton);
        toolbar->addWidget(m_applyButton);
        toolbar->addSeparator();
        toolbar->addWidget(undoButton);
        toolbar->addWidget(redoButton);
        toolbar->addSeparator();
        toolbar->addWidget(m_addNodeButton);
        toolbar->addSeparator();
        toolbar->addWidget(m_compileButton);
        toolbar->addWidget(m_statusLabel);
        toolbar->addWidget(m_graphBanner);
        toolbar->addWidget(spacer);
        toolbar->addWidget(m_snapCheck);
        toolbar->addWidget(m_viewButton);
        root->addWidget(toolbar);

        m_graphScene = new MaterialGraphScene(&m_graph, m_undo, this);
        m_graphView = new MaterialGraphView(this);
        m_graphView->setScene(m_graphScene);
        m_graphView->setFrameShape(QFrame::NoFrame);

        auto* horizontalSplitter = new QSplitter(Qt::Horizontal, this);
        horizontalSplitter->setChildrenCollapsible(false);

        auto* paletteHost = MakePanel(this);
        auto* paletteLayout = static_cast<QVBoxLayout*>(paletteHost->layout());
        paletteLayout->addWidget(CreatePanelHeader(QObject::tr("Node library"), paletteHost));
        m_paletteSearchEdit = new QLineEdit(paletteHost);
        m_paletteSearchEdit->setPlaceholderText(QObject::tr("Search nodes"));
        m_paletteSearchEdit->addAction(Theme::Icon(QStringLiteral("search"), Theme::IconTone::Muted), QLineEdit::LeadingPosition);
        m_paletteSearchEdit->setClearButtonEnabled(true);
        auto* searchRow = new QWidget(paletteHost);
        auto* searchLayout = new QVBoxLayout(searchRow);
        searchLayout->setContentsMargins(8, 8, 8, 4);
        searchLayout->addWidget(m_paletteSearchEdit);
        paletteLayout->addWidget(searchRow);
        m_nodePalette = new QTreeWidget(paletteHost);
        m_nodePalette->setHeaderHidden(true);
        m_nodePalette->setRootIsDecorated(true);
        m_nodePalette->setUniformRowHeights(true);
        m_nodePalette->setFrameShape(QFrame::NoFrame);
        m_nodePalette->setToolTip(QObject::tr("Double-click a node to add it to the graph."));
        paletteLayout->addWidget(m_nodePalette, 1);
        horizontalSplitter->addWidget(paletteHost);

        auto* centerSplitter = new QSplitter(Qt::Vertical, this);
        centerSplitter->setChildrenCollapsible(false);
        centerSplitter->addWidget(m_graphView);

        auto* statusFrame = MakePanel(this);
        auto* statusLayout = static_cast<QVBoxLayout*>(statusFrame->layout());
        m_issueCountLabel = new QLabel(statusFrame);
        m_issueCountLabel->setProperty("role", QStringLiteral("muted"));
        statusLayout->addWidget(CreatePanelHeader(QObject::tr("Validation"), statusFrame, {m_issueCountLabel}));
        m_errorList = new QListWidget(statusFrame);
        m_errorList->setSelectionMode(QAbstractItemView::SingleSelection);
        m_errorList->setFrameShape(QFrame::NoFrame);
        m_errorList->setToolTip(QObject::tr("Click an issue to select its node."));
        statusLayout->addWidget(m_errorList, 1);
        centerSplitter->addWidget(statusFrame);
        centerSplitter->setStretchFactor(0, 5);
        centerSplitter->setStretchFactor(1, 1);
        centerSplitter->setSizes({600, 170});
        horizontalSplitter->addWidget(centerSplitter);

        auto* rightSplitter = new QSplitter(Qt::Vertical, this);
        rightSplitter->setChildrenCollapsible(false);

        auto* previewHost = MakePanel(this);
        auto* previewLayout = static_cast<QVBoxLayout*>(previewHost->layout());
        previewLayout->addWidget(CreatePanelHeader(QObject::tr("Preview"), previewHost));
        m_previewViewportHost = new QWidget(previewHost);
        m_previewViewportHost->setObjectName(QStringLiteral("ViewportHost"));
        m_previewViewportHost->setAttribute(Qt::WA_StyledBackground, true);
        m_previewViewportHost->setMinimumHeight(220);
        auto* previewViewportLayout = new QVBoxLayout(m_previewViewportHost);
        previewViewportLayout->setContentsMargins(0, 0, 0, 0);
        m_previewPlaceholderLabel = new QLabel(QObject::tr("Preview initializing..."), m_previewViewportHost);
        m_previewPlaceholderLabel->setAlignment(Qt::AlignCenter);
        m_previewPlaceholderLabel->setProperty("role", QStringLiteral("muted"));
        previewViewportLayout->addWidget(m_previewPlaceholderLabel);
        previewLayout->addWidget(m_previewViewportHost, 1);
        auto* previewStrip = new QWidget(previewHost);
        auto* previewControls = new QHBoxLayout(previewStrip);
        previewControls->setContentsMargins(8, 8, 8, 4);
        previewControls->setSpacing(6);
        m_previewMeshCombo = new QComboBox(previewStrip);
        m_previewMeshCombo->addItems({QObject::tr("Sphere"), QObject::tr("Cube"), QObject::tr("Plane")});
        m_previewMeshCombo->setToolTip(QObject::tr("Preview mesh"));
        m_previewHdriEdit = new QLineEdit(QString::fromUtf8(DEFAULT_PREVIEW_HDRI_PATH), previewStrip);
        m_previewHdriEdit->setReadOnly(true);
        m_previewHdriEdit->setToolTip(QObject::tr("Environment map used only by this material preview."));
        m_previewHdriChooseButton = MakeToolButton(previewStrip, QStringLiteral("folder"), QObject::tr("Choose preview HDRI..."));
        m_previewHdriDefaultButton = MakeToolButton(previewStrip, QStringLiteral("reset"), QObject::tr("Use the default HDRI"));
        previewControls->addWidget(m_previewMeshCombo);
        previewControls->addWidget(m_previewHdriEdit, 1);
        previewControls->addWidget(m_previewHdriChooseButton);
        previewControls->addWidget(m_previewHdriDefaultButton);
        previewLayout->addWidget(previewStrip);
        m_previewStatusLabel = new QLabel(previewLightingStatus(), previewHost);
        m_previewStatusLabel->setWordWrap(true);
        m_previewStatusLabel->setProperty("role", QStringLiteral("caption"));
        m_previewStatusLabel->setContentsMargins(10, 0, 10, 8);
        previewLayout->addWidget(m_previewStatusLabel);
        rightSplitter->addWidget(previewHost);

        auto* inspectorHost = MakePanel(this);
        auto* inspectorLayout = static_cast<QVBoxLayout*>(inspectorHost->layout());
        inspectorLayout->addWidget(CreatePanelHeader(QObject::tr("Details"), inspectorHost));
        auto* materialSection = new QWidget(inspectorHost);
        auto* materialForm = new QFormLayout(materialSection);
        materialForm->setContentsMargins(12, 10, 12, 6);
        materialForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_nameEdit = new QLineEdit(QString::fromStdString(m_displayName), materialSection);
        materialForm->addRow(QObject::tr("Name"), m_nameEdit);
        m_blendModeCombo = new QComboBox(materialSection);
        m_blendModeCombo->addItem(QObject::tr("Opaque"), static_cast<int>(MaterialBlendMode::Opaque));
        m_blendModeCombo->addItem(QObject::tr("Masked"), static_cast<int>(MaterialBlendMode::Masked));
        m_blendModeCombo->addItem(QObject::tr("Translucent"), static_cast<int>(MaterialBlendMode::AlphaBlend));
        m_blendModeCombo->addItem(QObject::tr("Additive"), static_cast<int>(MaterialBlendMode::Additive));
        m_blendModeCombo->setItemData(0, QObject::tr("Solid surface. Writes depth and casts shadows."), Qt::ToolTipRole);
        m_blendModeCombo->setItemData(1, QObject::tr("Cut out where Opacity times the base color texture's alpha is below Opacity Mask Clip. Casts cut-out shadows; use for foliage, fences and grates."), Qt::ToolTipRole);
        m_blendModeCombo->setItemData(2, QObject::tr("Blended by Opacity times the base color texture's alpha, drawn after solid surfaces, furthest first. Casts no shadows; use for glass and decals."), Qt::ToolTipRole);
        m_blendModeCombo->setItemData(3, QObject::tr("Unlit, added to the image. Casts no shadows; use for flashes, sparks and glows."), Qt::ToolTipRole);
        m_blendModeCombo->setCurrentIndex(m_blendModeCombo->findData(static_cast<int>(m_material.blendMode)));
        materialForm->addRow(QObject::tr("Blend Mode"), m_blendModeCombo);
        inspectorLayout->addWidget(materialSection);

        auto* scroll = new QScrollArea(inspectorHost);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_inspectorContent = new QWidget(scroll);
        m_inspectorForm = new QFormLayout(m_inspectorContent);
        m_inspectorForm->setContentsMargins(12, 6, 12, 8);
        m_inspectorForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        scroll->setWidget(m_inspectorContent);
        inspectorLayout->addWidget(scroll, 1);

        m_convertButton = new QPushButton(QObject::tr("Convert to graph"), inspectorHost);
        m_convertButton->setProperty("variant", QStringLiteral("primary"));
        auto* convertRow = new QWidget(inspectorHost);
        auto* convertLayout = new QVBoxLayout(convertRow);
        convertLayout->setContentsMargins(12, 4, 12, 12);
        convertLayout->addWidget(m_convertButton);
        inspectorLayout->addWidget(convertRow);
        rightSplitter->addWidget(inspectorHost);
        rightSplitter->setStretchFactor(0, 2);
        rightSplitter->setStretchFactor(1, 3);
        horizontalSplitter->addWidget(rightSplitter);
        horizontalSplitter->setStretchFactor(0, 1);
        horizontalSplitter->setStretchFactor(1, 5);
        horizontalSplitter->setStretchFactor(2, 2);
        horizontalSplitter->setSizes({260, 900, 380});

        root->addWidget(horizontalSplitter, 1);

        auto* statusBar = new QStatusBar(this);
        statusBar->setObjectName(QStringLiteral("EditorStatusBar"));
        statusBar->setSizeGripEnabled(false);
        m_dirtyLabel = new QLabel(QObject::tr("Saved"), statusBar);
        m_dirtyLabel->setToolTip(QObject::tr("Shows whether this material has unsaved editor changes."));
        statusBar->addWidget(m_dirtyLabel);
        root->addWidget(statusBar);

        connect(m_applyButton, &QToolButton::clicked, this, [this]() { compileAndApply(); });
        connect(m_compileButton, &QToolButton::clicked, this, [this]() { compileAndApply(); });
        connect(m_saveButton, &QToolButton::clicked, this, [this]() { saveMaterial(); });
        connect(m_convertButton, &QPushButton::clicked, this, [this]() { convertLegacyToGraph(); });
        connect(m_addNodeButton, &QPushButton::clicked, this, [this]() {
            showNodeSearchPopup(m_addNodeButton->mapToGlobal(QPoint(0, m_addNodeButton->height())), graphViewCenter());
        });
        connect(m_viewButton, &QToolButton::clicked, this, [this]() { m_graphScene->focusSelection(); });
        connect(m_snapCheck, &QToolButton::toggled, this, [this](bool checked) {
            m_graphScene->setSnapToGrid(checked);
        });
        connect(m_paletteSearchEdit, &QLineEdit::textChanged, this, [this]() { populateNodePalette(); });
        connect(m_nodePalette, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
            if (!item || item->data(0, Qt::UserRole).isNull()) {
                return;
            }
            const NodeType type = static_cast<NodeType>(item->data(0, Qt::UserRole).toInt());
            addNodeFromUi(type, graphViewCenter());
        });
        connect(m_previewMeshCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
            updatePreviewMesh();
        });
        connect(m_previewHdriChooseButton, &QToolButton::clicked, this, [this]() { choosePreviewHDRI(); });
        connect(m_previewHdriDefaultButton, &QToolButton::clicked, this, [this]() {
            setPreviewHDRI(QString::fromUtf8(DEFAULT_PREVIEW_HDRI_PATH));
        });

        connect(m_graphScene, &QGraphicsScene::selectionChanged, this, [this]() {
            NodeID nodeId = 0;
            for (QGraphicsItem* item : m_graphScene->selectedItems()) {
                if (item->type() == NodeItem::Type) {
                    nodeId = static_cast<NodeItem*>(item)->nodeId();
                    break;
                }
            }
            rebuildInspector(nodeId);
        });
        m_graphScene->setGraphChangedCallback([this]() {
            refreshGraphBanner();
            markDirty();
            schedulePreviewUpdate();
        });
        m_graphScene->setNodeSearchCallback([this](const QPoint& globalPos, const QPointF& scenePos) {
            showNodeSearchPopup(globalPos, scenePos);
        });

        connect(m_errorList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const NodeID nodeId = item->data(Qt::UserRole).toInt();
            if (nodeId > 0) {
                m_graphScene->highlightNode(nodeId);
            }
        });

        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this, [this]() { m_undo->undo(); });
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this, [this]() { m_undo->redo(); });
        connect(undoButton, &QToolButton::clicked, this, [this]() { m_undo->undo(); });
        connect(m_undo, &QUndoStack::canUndoChanged, undoButton, &QToolButton::setEnabled);
        undoButton->setEnabled(m_undo->canUndo());
        connect(redoButton, &QToolButton::clicked, this, [this]() { m_undo->redo(); });
        connect(m_undo, &QUndoStack::canRedoChanged, redoButton, &QToolButton::setEnabled);
        redoButton->setEnabled(m_undo->canRedo());
        new QShortcut(QKeySequence::Save, this, [this]() { saveMaterial(); });
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() { m_graphScene->duplicateSelected(); });
        new QShortcut(QKeySequence(Qt::Key_Delete), this, [this]() { m_graphScene->removeSelected(); });
        new QShortcut(QKeySequence(Qt::Key_F), this, [this]() { m_graphScene->focusSelection(); });
        new QShortcut(QKeySequence(Qt::Key_Tab), this, [this]() {
            showNodeSearchPopup(m_graphView->mapToGlobal(m_graphView->viewport()->rect().center()), graphViewCenter());
        });

        connect(m_blendModeCombo, QOverload<int>::of(&QComboBox::activated), this, [this](int index) {
            const auto oldMode = m_material.blendMode;
            const auto newMode = static_cast<MaterialBlendMode>(m_blendModeCombo->itemData(index).toInt());
            if (oldMode == newMode) {
                return;
            }
            auto apply = [this](MaterialBlendMode mode) {
                m_material.blendMode = mode;
                const QSignalBlocker blocker(m_blendModeCombo);
                m_blendModeCombo->setCurrentIndex(m_blendModeCombo->findData(static_cast<int>(mode)));
                markDirty();
                schedulePreviewUpdate();
            };
            m_undo->push(new EditMaterialPropertyCommand([apply, oldMode]() { apply(oldMode); },
                                                         [apply, newMode]() { apply(newMode); },
                                                         QObject::tr("Change Blend Mode")));
        });

        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
            m_displayName = m_nameEdit->text().trimmed().isEmpty()
                ? QFileInfo(m_absolutePath).completeBaseName().toStdString()
                : m_nameEdit->text().toStdString();
            markDirty();
        });

        connect(m_undo, &QUndoStack::cleanChanged, this, [this](bool clean) {
            setDirty(!clean);
        });

        m_previewUpdateTimer = new QTimer(this);
        m_previewUpdateTimer->setSingleShot(true);
        m_previewUpdateTimer->setInterval(180);
        connect(m_previewUpdateTimer, &QTimer::timeout, this, [this]() { updatePreviewMaterial(); });

        populateNodePalette();
        initPreview();
        refreshGraphBanner();
        setDirty(false);
    }

    void refreshGraphBanner() {
        if (m_graph.IsLegacyMode()) {
            m_graphBanner->setText(QObject::tr("Legacy PBR"));
            m_graphBanner->setToolTip(QObject::tr("The graph is missing or invalid, or a graph cannot yet express every setting of this material. Use Convert to graph to enter graph mode."));
            SetStyleProperty(m_graphBanner, "tone", QStringLiteral("warning"));
            m_convertButton->setVisible(true);
        } else if (m_graph.HasSourceGraph()) {
            m_graphBanner->setText(QObject::tr("Graph mode"));
            m_graphBanner->setToolTip(QObject::tr("The node graph is editable. Legacy fields are preserved for backwards compatibility."));
            SetStyleProperty(m_graphBanner, "tone", QStringLiteral("action"));
            m_convertButton->setVisible(false);
        } else {
            m_graphBanner->setText(QObject::tr("PBR fields"));
            m_graphBanner->setToolTip(QObject::tr("This material uses PBR fields. Use Convert to graph to edit it as a node graph."));
            SetStyleProperty(m_graphBanner, "tone", QStringLiteral("neutral"));
            m_convertButton->setVisible(true);
        }
    }

    void rebuildGraphScene() {
        m_graphScene->reloadFromGraph();
        m_graphView->setSceneRect(m_graphScene->itemsBoundingRect().adjusted(-300, -200, 300, 200));
    }

    QPointF graphViewCenter() const {
        const QPoint viewCenter = m_graphView->viewport()->rect().center();
        return m_graphView->mapToScene(viewCenter);
    }

    void populateNodePalette() {
        if (!m_nodePalette) {
            return;
        }
        m_nodePalette->clear();
        const QString filter = m_paletteSearchEdit ? m_paletteSearchEdit->text().trimmed() : QString();
        std::unordered_map<std::string, QTreeWidgetItem*> categoryItems;

        auto getCategoryItem = [&](const QString& category) {
            const std::string key = category.toStdString();
            auto it = categoryItems.find(key);
            if (it != categoryItems.end()) {
                return it->second;
            }
            auto* item = new QTreeWidgetItem(m_nodePalette);
            item->setText(0, category);
            item->setFlags(Qt::ItemIsEnabled);
            categoryItems[key] = item;
            return item;
        };

        for (NodeType recent : m_recentNodeTypes) {
            const QString label = NodeTypeLabel(recent);
            const QString searchable = QObject::tr("%1 %2 %3")
                .arg(label, NodeCategory(recent), QString::fromUtf8(GetNodeDefinition(recent).tooltip.data(), static_cast<int>(GetNodeDefinition(recent).tooltip.size())));
            if (!FuzzyMatch(filter, searchable)) {
                continue;
            }
            auto* child = new QTreeWidgetItem(getCategoryItem(QObject::tr("Recently Used")));
            child->setText(0, NodeMenuLabel(recent, label));
            child->setData(0, Qt::UserRole, static_cast<int>(recent));
            child->setToolTip(0, NodeTooltip(recent));
        }

        for (const NodeMenuEntry& entry : NodeMenuEntries()) {
            const auto tooltip = GetNodeDefinition(entry.type).tooltip;
            const QString searchable = QObject::tr("%1 %2 %3")
                .arg(entry.label, NodeCategory(entry.type), QString::fromUtf8(tooltip.data(), static_cast<int>(tooltip.size())));
            if (!FuzzyMatch(filter, searchable)) {
                continue;
            }
            auto* child = new QTreeWidgetItem(getCategoryItem(NodeCategory(entry.type)));
            child->setText(0, NodeMenuLabel(entry.type, entry.label));
            child->setData(0, Qt::UserRole, static_cast<int>(entry.type));
            child->setToolTip(0, NodeTooltip(entry.type));
        }
        m_nodePalette->expandAll();
    }

    void addNodeFromUi(NodeType type, const QPointF& scenePos) {
        m_graphScene->addNode(type, scenePos);
        m_recentNodeTypes.erase(std::remove(m_recentNodeTypes.begin(), m_recentNodeTypes.end(), type), m_recentNodeTypes.end());
        m_recentNodeTypes.insert(m_recentNodeTypes.begin(), type);
        if (m_recentNodeTypes.size() > 5) {
            m_recentNodeTypes.resize(5);
        }
        populateNodePalette();
    }

    void showNodeSearchPopup(const QPoint& globalPos, const QPointF& scenePos) {
        QDialog popup(this);
        popup.setWindowTitle(QObject::tr("Add Node"));
        popup.setWindowFlags(Qt::Popup);
        auto* layout = new QVBoxLayout(&popup);
        layout->setContentsMargins(8, 8, 8, 8);
        auto* search = new QLineEdit(&popup);
        search->setPlaceholderText(QObject::tr("Search nodes..."));
        auto* results = new QListWidget(&popup);
        results->setMinimumWidth(340);
        results->setMinimumHeight(280);
        layout->addWidget(search);
        layout->addWidget(results);

        auto rebuildResults = [&]() {
            results->clear();
            const QString filter = search->text().trimmed();
            auto addEntry = [&](const QString& prefix, NodeType type, const QString& label) {
                const auto tooltip = GetNodeDefinition(type).tooltip;
                const QString searchable = QObject::tr("%1 %2 %3")
                    .arg(label, prefix, QString::fromUtf8(tooltip.data(), static_cast<int>(tooltip.size())));
                if (!FuzzyMatch(filter, searchable)) {
                    return;
                }
                const QString shown = NodeMenuLabel(type, label);
                auto* item = new QListWidgetItem(prefix.isEmpty() ? shown : QObject::tr("%1  -  %2").arg(prefix, shown), results);
                item->setData(Qt::UserRole, static_cast<int>(type));
                item->setToolTip(NodeTooltip(type));
            };
            for (NodeType recent : m_recentNodeTypes) {
                addEntry(QObject::tr("Recent"), recent, NodeTypeLabel(recent));
            }
            for (const NodeMenuEntry& entry : NodeMenuEntries()) {
                addEntry(NodeCategory(entry.type), entry.type, entry.label);
            }
            if (results->count() > 0) {
                results->setCurrentRow(0);
            }
        };

        connect(search, &QLineEdit::textChanged, &popup, rebuildResults);
        connect(results, &QListWidget::itemActivated, &popup, [&](QListWidgetItem* item) {
            if (!item) {
                return;
            }
            addNodeFromUi(static_cast<NodeType>(item->data(Qt::UserRole).toInt()), scenePos);
            popup.accept();
        });
        connect(search, &QLineEdit::returnPressed, &popup, [&]() {
            if (auto* item = results->currentItem()) {
                addNodeFromUi(static_cast<NodeType>(item->data(Qt::UserRole).toInt()), scenePos);
                popup.accept();
            }
        });

        rebuildResults();
        popup.move(globalPos);
        search->setFocus();
        popup.exec();
    }

    void markDirty() {
        setDirty(true);
    }

    void setDirty(bool dirty) {
        m_dirty = dirty;
        const QString baseTitle = QObject::tr("Material Editor - %1").arg(QFileInfo(m_absolutePath).fileName());
        setWindowTitle(dirty ? baseTitle + QStringLiteral(" *") : baseTitle);
        if (m_dirtyLabel) {
            m_dirtyLabel->setText(dirty ? QObject::tr("Unsaved changes") : QObject::tr("Saved"));
            SetStyleProperty(m_dirtyLabel, "state", dirty ? QStringLiteral("dirty") : QStringLiteral("saved"));
        }
    }

    void schedulePreviewUpdate() {
        if (m_previewUpdateTimer) {
            m_previewUpdateTimer->start();
        }
    }

    QString resolvePreviewHDRIPath(const QString& rawPath) const {
        QString path = QDir::fromNativeSeparators(rawPath.trimmed());
        if (path.isEmpty() || QFileInfo(path).isAbsolute()) {
            return path;
        }

        const QString projectRoot = QDir::fromNativeSeparators(QString::fromStdString(AssetManager::GetInstance().GetProjectRoot()));
        if (!projectRoot.isEmpty()) {
            const QString projectCandidate = QDir(projectRoot).filePath(path);
            if (QFileInfo(projectCandidate).exists()) {
                return QDir::fromNativeSeparators(projectCandidate);
            }

            const QString projectAssetsCandidate = QDir(projectRoot).filePath(QStringLiteral("assets/") + path);
            if (QFileInfo(projectAssetsCandidate).exists()) {
                return QDir::fromNativeSeparators(projectAssetsCandidate);
            }
        }

        const QString assetRoot = QDir::fromNativeSeparators(QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()));
        if (!assetRoot.isEmpty()) {
            const QString assetCandidate = QDir(assetRoot).filePath(path);
            if (QFileInfo(assetCandidate).exists()) {
                return QDir::fromNativeSeparators(assetCandidate);
            }
        }

        return path;
    }

    QString normalizePreviewHDRIPath(const QString& filePath) const {
        const QString cleaned = QDir::fromNativeSeparators(filePath.trimmed());
        if (cleaned.isEmpty()) {
            return QString();
        }

        const QFileInfo fileInfo(cleaned);
        if (!fileInfo.isAbsolute()) {
            return cleaned;
        }

        const QString projectRoot = QDir::fromNativeSeparators(QString::fromStdString(AssetManager::GetInstance().GetProjectRoot()));
        if (!projectRoot.isEmpty()) {
            const QString relative = QDir::fromNativeSeparators(QDir(projectRoot).relativeFilePath(fileInfo.absoluteFilePath()));
            if (!relative.isEmpty() && relative != QStringLiteral("..") && !relative.startsWith(QStringLiteral("../"))) {
                return relative;
            }
        }

        return QDir::fromNativeSeparators(fileInfo.absoluteFilePath());
    }

    QString previewHDRIStartDir() const {
        const QString resolved = resolvePreviewHDRIPath(m_previewHdriPath);
        if (!resolved.isEmpty()) {
            const QFileInfo info(resolved);
            if (info.exists()) {
                return info.absolutePath();
            }
        }

        const QString projectRoot = QDir::fromNativeSeparators(QString::fromStdString(AssetManager::GetInstance().GetProjectRoot()));
        if (!projectRoot.isEmpty()) {
            const QString hdris = QDir(projectRoot).filePath(QStringLiteral("assets/textures/hdris"));
            if (QFileInfo(hdris).exists()) {
                return hdris;
            }
            const QString textures = QDir(projectRoot).filePath(QStringLiteral("assets/textures"));
            if (QFileInfo(textures).exists()) {
                return textures;
            }
        }

        return QFileInfo(m_absolutePath).absolutePath();
    }

    QString previewLightingStatus() const {
        return QObject::tr("Lighting: HDRI (%1)").arg(m_previewHdriPath);
    }

    void setPreviewHDRI(const QString& hdriPath) {
        m_previewHdriPath = normalizePreviewHDRIPath(hdriPath);
        if (m_previewHdriPath.isEmpty()) {
            m_previewHdriPath = QString::fromUtf8(DEFAULT_PREVIEW_HDRI_PATH);
        }
        if (m_previewHdriEdit) {
            m_previewHdriEdit->setText(m_previewHdriPath);
        }
        applyPreviewEnvironment();
        if (m_previewStatusLabel) {
            m_previewStatusLabel->setText(QObject::tr("Preview mesh: %1. %2.").arg(m_previewMeshCombo->currentText(), previewLightingStatus()));
        }
    }

    void choosePreviewHDRI() {
        const QString file = QFileDialog::getOpenFileName(
            this,
            QObject::tr("Choose Preview HDRI"),
            previewHDRIStartDir(),
            QObject::tr("HDR Images (*.hdr *.exr);;All Files (*.*)"));
        if (file.isEmpty()) {
            return;
        }
        setPreviewHDRI(file);
    }

    void applyPreviewEnvironment() {
        if (!m_previewScene || m_previewEnvironmentEntity == entt::null) {
            return;
        }

        auto& registry = m_previewScene->GetRegistry();
        if (!registry.valid(m_previewEnvironmentEntity)) {
            return;
        }

        EnvironmentComponent* env = registry.try_get<EnvironmentComponent>(m_previewEnvironmentEntity);
        if (!env) {
            env = &registry.emplace<EnvironmentComponent>(m_previewEnvironmentEntity);
        }

        env->type = EnvironmentComponent::BackgroundType::HDRI;
        env->hdriPath = m_previewHdriPath.toStdString();
        env->hdriIntensity = 1.0f;
        env->rotationYaw = 0.0f;
        env->useAsIBL = true;
    }

    void initPreview() {
        if (!m_previewViewportHost || m_previewEngine) {
            return;
        }

        m_previewScene = std::make_unique<Scene>("Material Preview");
        Entity previewEntity = m_previewScene->CreateEntity("Preview Material");
        previewEntity.AddComponent<TransformComponent>();
        auto& renderer = previewEntity.AddComponent<MeshRendererComponent>();
        renderer.meshAssetPath = previewMeshKey();
        renderer.submeshCount = 1;
        renderer.appliedMaterialPaths = {""};
        renderer.materialIndices = {-1};
        previewEntity.AddComponent<MaterialComponent>(m_material);
        m_previewEntity = static_cast<entt::entity>(previewEntity);

        Entity environmentEntity = m_previewScene->CreateEntity("Environment");
        environmentEntity.AddComponent<EnvironmentComponent>();
        m_previewEnvironmentEntity = static_cast<entt::entity>(environmentEntity);
        applyPreviewEnvironment();

        m_previewViewportWindow = new QtViewportWindow();
        m_previewViewportContainer = QWidget::createWindowContainer(m_previewViewportWindow, m_previewViewportHost);
        m_previewViewportContainer->setMinimumHeight(180);
        if (auto* layout = m_previewViewportHost->layout()) {
            if (m_previewPlaceholderLabel) {
                m_previewPlaceholderLabel->hide();
            }
            layout->addWidget(m_previewViewportContainer);
        }

        m_previewEngine = std::make_unique<QtEditorEngine>(this);
        connect(m_previewEngine.get(), &QtEditorEngine::engineError, this, [this](const QString& message) {
            if (m_previewStatusLabel) {
                m_previewStatusLabel->setText(QObject::tr("Preview error: %1").arg(message));
            }
        });
        connect(m_previewViewportWindow, &QtViewportWindow::viewportReady, this, [this]() {
            if (!m_previewEngine || !m_previewViewportWindow) {
                return;
            }
            if (!m_previewEngine->AttachToViewport(m_previewViewportWindow)) {
                if (m_previewStatusLabel) {
                    m_previewStatusLabel->setText(QObject::tr("Preview failed to attach renderer."));
                }
                return;
            }

            auto* ctx = m_previewEngine->GetEditorContext();
            ctx->SetExternalScene(m_previewScene.get());
            auto& settings = ctx->GetViewportSettings();
            settings.showGrid = false;
            settings.showGizmo = false;
            settings.showSelectionOutline = false;
            settings.showShadows = true;
            settings.showSkybox = true;
            settings.showLightBillboards = false;

            const std::string projectRoot = AssetManager::GetInstance().GetProjectRoot();
            if (!projectRoot.empty()) {
                m_previewEngine->SetProjectRoot(projectRoot);
                if (auto* renderer = m_previewEngine->GetFrameRenderer()) {
                    renderer->SetProjectRoot(projectRoot);
                }
            }

            m_previewEngine->StartRenderLoop();
            m_previewEngine->RebuildSceneMeshes();
            updatePreviewMaterial();
            if (m_previewStatusLabel) {
                m_previewStatusLabel->setText(QObject::tr("Rendered preview: %1. %2.").arg(m_previewMeshCombo->currentText(), previewLightingStatus()));
            }
        });

        if (!m_previewEngine->InitializeVulkan()) {
            if (m_previewStatusLabel) {
                m_previewStatusLabel->setText(QObject::tr("Preview renderer failed to initialize Vulkan."));
            }
        }
        updatePreviewMaterial();
    }

    void shutdownPreview() {
        if (m_previewUpdateTimer) {
            m_previewUpdateTimer->stop();
        }
        if (m_previewEngine) {
            if (m_previewViewportWindow) {
                m_previewViewportWindow->SetInputEventCallback(nullptr);
            }
            if (auto* ctx = m_previewEngine->GetEditorContext()) {
                ctx->SetSelectionChangedCallback(nullptr);
                ctx->SetSceneChangedCallback(nullptr);
                ctx->ClearSelection();
                ctx->SetExternalScene(nullptr);
            }
            m_previewEngine->Shutdown();
            m_previewEngine.reset();
        }
        m_previewScene.reset();
    }

    void updatePreviewMesh() {
        if (m_previewScene && m_previewEntity != entt::null) {
            auto& registry = m_previewScene->GetRegistry();
            if (registry.valid(m_previewEntity) && registry.all_of<MeshRendererComponent>(m_previewEntity)) {
                auto& renderer = registry.get<MeshRendererComponent>(m_previewEntity);
                renderer.meshAssetPath = previewMeshKey();
                renderer.meshHandle = MeshRendererComponent::InvalidMesh;
                renderer.submeshCount = 1;
                renderer.appliedMaterialPaths = {""};
                renderer.materialIndices = {-1};
                if (m_previewEngine) {
                    m_previewEngine->RebuildSceneMeshes();
                }
            }
        }
        if (m_previewStatusLabel) {
            m_previewStatusLabel->setText(QObject::tr("Preview mesh: %1. %2.").arg(m_previewMeshCombo->currentText(), previewLightingStatus()));
        }
        schedulePreviewUpdate();
    }

    void updatePreviewMaterial() {
        MaterialComponent evaluated = m_material;
        std::string evalError;
        if (m_graph.HasSourceGraph() && !m_graph.IsLegacyMode() && !EvaluateGraph(m_graph, evaluated, evalError)) {
            if (m_previewPlaceholderLabel) {
                m_previewPlaceholderLabel->setText(QObject::tr("Preview unavailable\n%1").arg(QString::fromStdString(evalError)));
            }
            return;
        }
        m_previewMaterial = evaluated;
        if (m_previewScene && m_previewEntity != entt::null) {
            auto& registry = m_previewScene->GetRegistry();
            if (registry.valid(m_previewEntity) && registry.all_of<MaterialComponent>(m_previewEntity)) {
                registry.replace<MaterialComponent>(m_previewEntity, m_previewMaterial);
            }
        }
        if (m_previewPlaceholderLabel && m_previewPlaceholderLabel->isVisible()) {
            m_previewPlaceholderLabel->setText(QObject::tr("Preview initializing..."));
        }
    }

    std::string previewMeshKey() const {
        if (!m_previewMeshCombo) {
            return "builtin://sphere";
        }
        const QString text = m_previewMeshCombo->currentText().toLower();
        if (text.contains("cube")) {
            return "builtin://cube";
        }
        if (text.contains("plane")) {
            return "builtin://plane";
        }
        return "builtin://sphere";
    }

    void clearInspectorForm() {
        while (m_inspectorForm->rowCount() > 0) {
            m_inspectorForm->removeRow(0);
        }
    }

    void addMaterialInspector() {
        auto makeSpin = [this](double min, double max, double step, double value) {
            auto* spin = new QDoubleSpinBox(m_inspectorContent);
            spin->setRange(min, max);
            spin->setSingleStep(step);
            spin->setDecimals(4);
            spin->setValue(value);
            return spin;
        };

        auto addUndoSpin = [this](QDoubleSpinBox* spin, const QString& label, std::function<double()> getter, std::function<void(double)> setter) {
            spin->setProperty("lastCommitted", getter());
            connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, setter](double value) {
                setter(value);
                markDirty();
                schedulePreviewUpdate();
            });
            connect(spin, &QDoubleSpinBox::editingFinished, this, [this, spin, label, getter, setter]() {
                const double oldValue = spin->property("lastCommitted").toDouble();
                const double newValue = spin->value();
                if (std::abs(oldValue - newValue) < 1e-9) {
                    return;
                }
                m_undo->push(new EditMaterialPropertyCommand(
                    [setter, oldValue]() { setter(oldValue); },
                    [setter, newValue]() { setter(newValue); },
                    label));
                spin->setProperty("lastCommitted", newValue);
            });
        };

        auto* reflectionStrength = makeSpin(0.0, 10.0, 0.01, m_material.reflectionStrength);
        auto* emissiveStrength = makeSpin(0.0, 100.0, 0.01, m_material.emissiveStrength);
        m_inspectorForm->addRow(QObject::tr("Reflection Strength"), reflectionStrength);
        m_inspectorForm->addRow(QObject::tr("Emissive Strength"), emissiveStrength);
        addUndoSpin(reflectionStrength, QObject::tr("Edit Reflection Strength"), [this]() { return m_material.reflectionStrength; }, [this](double v) { m_material.reflectionStrength = static_cast<float>(v); });
        addUndoSpin(emissiveStrength, QObject::tr("Edit Emissive Strength"), [this]() { return m_material.emissiveStrength; }, [this](double v) { m_material.emissiveStrength = static_cast<float>(v); });
        if (m_graph.HasSourceGraph() && !m_graph.IsLegacyMode()) {
            return;
        }

        auto* albedoX = makeSpin(0.0, 10.0, 0.01, m_material.albedo.x);
        auto* albedoY = makeSpin(0.0, 10.0, 0.01, m_material.albedo.y);
        auto* albedoZ = makeSpin(0.0, 10.0, 0.01, m_material.albedo.z);
        auto* albedoRow = new QWidget(m_inspectorContent);
        auto* albedoLayout = new QHBoxLayout(albedoRow);
        albedoLayout->setContentsMargins(0, 0, 0, 0);
        albedoLayout->addWidget(albedoX);
        albedoLayout->addWidget(albedoY);
        albedoLayout->addWidget(albedoZ);
        m_inspectorForm->addRow(QObject::tr("Albedo (RGB)"), albedoRow);
        addUndoSpin(albedoX, QObject::tr("Edit Albedo X"), [this]() { return m_material.albedo.x; }, [this](double v) { m_material.albedo.x = static_cast<float>(v); });
        addUndoSpin(albedoY, QObject::tr("Edit Albedo Y"), [this]() { return m_material.albedo.y; }, [this](double v) { m_material.albedo.y = static_cast<float>(v); });
        addUndoSpin(albedoZ, QObject::tr("Edit Albedo Z"), [this]() { return m_material.albedo.z; }, [this](double v) { m_material.albedo.z = static_cast<float>(v); });

        auto* metallic = makeSpin(0.0, 1.0, 0.01, m_material.metallic);
        auto* roughness = makeSpin(0.0, 1.0, 0.01, m_material.roughness);
        auto* ao = makeSpin(0.0, 1.0, 0.01, m_material.ao);
        auto* specular = makeSpin(0.0, 1.0, 0.01, m_material.specular);
        auto* opacity = makeSpin(0.0, 1.0, 0.01, m_material.opacity);
        auto* opacityMask = makeSpin(0.0, 1.0, 0.01, m_material.opacityMask);
        auto* refraction = makeSpin(0.0, 2.0, 0.01, m_material.refraction);

        m_inspectorForm->addRow(QObject::tr("Metallic"), metallic);
        m_inspectorForm->addRow(QObject::tr("Roughness"), roughness);
        m_inspectorForm->addRow(QObject::tr("AO"), ao);
        m_inspectorForm->addRow(QObject::tr("Specular"), specular);
        m_inspectorForm->addRow(QObject::tr("Opacity"), opacity);
        m_inspectorForm->addRow(QObject::tr("Opacity Mask Clip"), opacityMask);
        m_inspectorForm->addRow(QObject::tr("Refraction"), refraction);

        addUndoSpin(metallic, QObject::tr("Edit Metallic"), [this]() { return m_material.metallic; }, [this](double v) { m_material.metallic = static_cast<float>(v); });
        addUndoSpin(roughness, QObject::tr("Edit Roughness"), [this]() { return m_material.roughness; }, [this](double v) { m_material.roughness = static_cast<float>(v); });
        addUndoSpin(ao, QObject::tr("Edit AO"), [this]() { return m_material.ao; }, [this](double v) { m_material.ao = static_cast<float>(v); });
        addUndoSpin(specular, QObject::tr("Edit Specular"), [this]() { return m_material.specular; }, [this](double v) { m_material.specular = static_cast<float>(v); });
        addUndoSpin(opacity, QObject::tr("Edit Opacity"), [this]() { return m_material.opacity; }, [this](double v) { m_material.opacity = static_cast<float>(v); });
        addUndoSpin(opacityMask, QObject::tr("Edit Opacity Mask Clip"), [this]() { return m_material.opacityMask; }, [this](double v) { m_material.opacityMask = static_cast<float>(v); });
        addUndoSpin(refraction, QObject::tr("Edit Refraction"), [this]() { return m_material.refraction; }, [this](double v) { m_material.refraction = static_cast<float>(v); });
    }

    void addNodeInspector(Node& node) {
        auto* display = new QLineEdit(QString::fromStdString(node.displayName), m_inspectorContent);
        m_inspectorForm->addRow(QObject::tr("Display Name"), display);
        connect(display, &QLineEdit::editingFinished, this, [this, &node, display]() {
            const QString newText = display->text().trimmed();
            const std::string oldName = node.displayName;
            const std::string newName = newText.isEmpty() ? oldName : newText.toStdString();
            if (oldName == newName) {
                return;
            }
            const NodeID nodeId = node.id;
            m_undo->push(new EditNodePropertyCommand(
                [this, nodeId, oldName]() {
                    if (Node* n = m_graph.FindNode(nodeId)) {
                        n->displayName = oldName;
                        m_graphScene->updateNodeTitle(nodeId, QString::fromStdString(oldName));
                    }
                },
                [this, nodeId, newName]() {
                    if (Node* n = m_graph.FindNode(nodeId)) {
                        n->displayName = newName;
                        m_graphScene->updateNodeTitle(nodeId, QString::fromStdString(newName));
                    }
                },
                QObject::tr("Rename Node")));
        });

        if (GetNodeDefinition(node.type).parameterNode) {
            auto* parameter = new QLineEdit(QString::fromStdString(node.parameterName), m_inspectorContent);
            parameter->setPlaceholderText(QObject::tr("Material parameter name"));
            parameter->setProperty("lastCommitted", parameter->text());
            m_inspectorForm->addRow(QObject::tr("Parameter"), parameter);
            connect(parameter, &QLineEdit::textChanged, this, [this, nodeId = node.id](const QString& value) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->parameterName = value.toStdString();
                    markDirty();
                }
            });
            connect(parameter, &QLineEdit::editingFinished, this, [this, parameter, nodeId = node.id]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const std::string oldValue = parameter->property("lastCommitted").toString().toStdString();
                const std::string newValue = parameter->text().toStdString();
                if (oldValue == newValue) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->parameterName = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->parameterName = newValue; },
                    QObject::tr("Edit Parameter Name")));
                parameter->setProperty("lastCommitted", parameter->text());
            });
        }

        if (node.type == NodeType::Compare || node.type == NodeType::Gradient) {
            auto* operation = new QComboBox(m_inspectorContent);
            const QStringList values = node.type == NodeType::Compare
                ? QStringList{QStringLiteral(">"), QStringLiteral("<"), QStringLiteral("==")}
                : QStringList{QObject::tr("Linear"), QObject::tr("Radial")};
            operation->addItems(values);
            const int current = operation->findText(QString::fromStdString(node.operation));
            operation->setCurrentIndex(current >= 0 ? current : 0);
            m_inspectorForm->addRow(node.type == NodeType::Compare ? QObject::tr("Compare") : QObject::tr("Mode"), operation);
            connect(operation, &QComboBox::currentTextChanged, this, [this, nodeId = node.id](const QString& value) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->operation = value.toStdString();
                    markDirty();
                    schedulePreviewUpdate();
                }
            });
        }

        if (node.type == NodeType::ComponentMask) {
            auto* row = new QWidget(m_inspectorContent);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto addMask = [&](const QString& label, bool checked, auto setter) {
                auto* check = new QCheckBox(label, row);
                check->setChecked(checked);
                layout->addWidget(check);
                connect(check, &QCheckBox::toggled, this, [this, nodeId = node.id, setter](bool value) {
                    if (Node* n = m_graph.FindNode(nodeId)) {
                        setter(*n, value);
                        markDirty();
                        schedulePreviewUpdate();
                    }
                });
            };
            addMask(QObject::tr("R"), node.maskR, [](Node& n, bool v) { n.maskR = v; });
            addMask(QObject::tr("G"), node.maskG, [](Node& n, bool v) { n.maskG = v; });
            addMask(QObject::tr("B"), node.maskB, [](Node& n, bool v) { n.maskB = v; });
            addMask(QObject::tr("A"), node.maskA, [](Node& n, bool v) { n.maskA = v; });
            m_inspectorForm->addRow(QObject::tr("Mask"), row);
        }

        if (node.type == NodeType::BoolParameter || node.type == NodeType::StaticSwitchParameter) {
            auto* value = new QCheckBox(QObject::tr("Enabled"), m_inspectorContent);
            value->setChecked(node.boolValue);
            m_inspectorForm->addRow(QObject::tr("Bool Value"), value);
            connect(value, &QCheckBox::toggled, this, [this, nodeId = node.id](bool checked) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->boolValue = checked;
                    n->scalar = checked ? 1.0f : 0.0f;
                    markDirty();
                    schedulePreviewUpdate();
                }
            });
        }

        if (IsScalarNode(node.type)) {
            auto* row = new QWidget(m_inspectorContent);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto* scalar = new QDoubleSpinBox(m_inspectorContent);
            scalar->setRange(-10000.0, 10000.0);
            scalar->setDecimals(4);
            scalar->setValue(node.scalar);
            auto* slider = new QSlider(Qt::Horizontal, row);
            slider->setRange(0, 1000);
            slider->setValue(static_cast<int>(std::clamp(node.scalar, 0.0f, 1.0f) * 1000.0f));
            slider->setToolTip(QObject::tr("Normalized 0-1 edit range for common scalar values."));
            auto* reset = new QPushButton(QObject::tr("Reset"), row);
            reset->setToolTip(QObject::tr("Reset to the default value for this scalar node."));
            rowLayout->addWidget(scalar);
            rowLayout->addWidget(slider, 1);
            rowLayout->addWidget(reset);
            m_inspectorForm->addRow(QObject::tr("Scalar"), row);
            scalar->setProperty("lastCommitted", node.scalar);
            connect(slider, &QSlider::valueChanged, this, [scalar](int v) {
                scalar->setValue(static_cast<double>(v) / 1000.0);
            });
            connect(scalar, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, slider, nodeId = node.id](double value) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->scalar = static_cast<float>(value);
                    slider->blockSignals(true);
                    slider->setValue(static_cast<int>(std::clamp(value, 0.0, 1.0) * 1000.0));
                    slider->blockSignals(false);
                    markDirty();
                    schedulePreviewUpdate();
                }
            });
            connect(scalar, &QDoubleSpinBox::editingFinished, this, [this, scalar, nodeId = node.id]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const float oldValue = scalar->property("lastCommitted").toFloat();
                const float newValue = static_cast<float>(scalar->value());
                if (std::abs(oldValue - newValue) < 1e-6f) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = newValue; },
                    QObject::tr("Edit Scalar")));
                scalar->setProperty("lastCommitted", newValue);
            });
            connect(reset, &QPushButton::clicked, this, [this, scalar, nodeId = node.id]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const float oldValue = n->scalar;
                const float newValue = 0.5f;
                scalar->setValue(newValue);
                if (std::abs(oldValue - newValue) < 1e-6f) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = newValue; },
                    QObject::tr("Reset Scalar")));
            });
        }

        if (IsColorNode(node.type)) {
            auto* row = new QWidget(m_inspectorContent);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);

            auto* preview = new QLabel(row);
            preview->setFixedSize(32, 18);
            preview->setFrameShape(QFrame::Box);

            auto* picker = new QPushButton(QObject::tr("Select RGBA..."), row);
            picker->setToolTip(QObject::tr("Opens RGBA picker. Alpha is editor-only for now; RGB is saved to .hematerial color[3]."));
            auto* reset = new QPushButton(QObject::tr("Reset"), row);
            reset->setToolTip(QObject::tr("Reset to default white."));

            auto updatePreview = [preview](const Float3& c, float alpha = 1.0f) {
                const QColor q = QColor::fromRgbF(std::clamp(static_cast<double>(c.x), 0.0, 1.0),
                                                  std::clamp(static_cast<double>(c.y), 0.0, 1.0),
                                                  std::clamp(static_cast<double>(c.z), 0.0, 1.0),
                                                  std::clamp(static_cast<double>(alpha), 0.0, 1.0));
                preview->setStyleSheet(QString("background-color: rgba(%1, %2, %3, %4);")
                    .arg(q.red()).arg(q.green()).arg(q.blue()).arg(q.alpha()));
            };
            updatePreview(node.color, 1.0f);

            rowLayout->addWidget(preview);
            rowLayout->addWidget(picker, 1);
            rowLayout->addWidget(reset);
            m_inspectorForm->addRow(QObject::tr("Color"), row);

            connect(picker, &QPushButton::clicked, this, [this, nodeId = node.id, updatePreview]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) {
                    return;
                }

                QColorDialog dlg(QColor::fromRgbF(
                    std::clamp(static_cast<double>(n->color.x), 0.0, 1.0),
                    std::clamp(static_cast<double>(n->color.y), 0.0, 1.0),
                    std::clamp(static_cast<double>(n->color.z), 0.0, 1.0),
                    1.0),
                    this);
                dlg.setOption(QColorDialog::ShowAlphaChannel, true);
                dlg.setWindowTitle(QObject::tr("Select Node Color"));

                if (dlg.exec() != QDialog::Accepted) {
                    return;
                }

                const QColor picked = dlg.selectedColor();
                if (!picked.isValid()) {
                    return;
                }

                const Float3 oldColor = n->color;
                const Float3 newColor{static_cast<float>(picked.redF()), static_cast<float>(picked.greenF()), static_cast<float>(picked.blueF())};
                if (oldColor.x == newColor.x && oldColor.y == newColor.y && oldColor.z == newColor.z) {
                    updatePreview(newColor, static_cast<float>(picked.alphaF()));
                    return;
                }

                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldColor, updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = oldColor;
                            updatePreview(oldColor, 1.0f);
                        }
                    },
                    [this, nodeId, newColor, alpha = static_cast<float>(picked.alphaF()), updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = newColor;
                            updatePreview(newColor, alpha);
                        }
                    },
                    QObject::tr("Edit Color")));
            });
            connect(reset, &QPushButton::clicked, this, [this, nodeId = node.id, updatePreview]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const Float3 oldColor = n->color;
                const Float3 newColor{1.0f, 1.0f, 1.0f};
                updatePreview(newColor, 1.0f);
                if (oldColor.x == newColor.x && oldColor.y == newColor.y && oldColor.z == newColor.z) {
                    return;
                }
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldColor, updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = oldColor;
                            updatePreview(oldColor, 1.0f);
                        }
                    },
                    [this, nodeId, newColor, updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = newColor;
                            updatePreview(newColor, 1.0f);
                        }
                    },
                    QObject::tr("Reset Color")));
            });

            auto* vectorRow = new QWidget(m_inspectorContent);
            auto* vectorLayout = new QHBoxLayout(vectorRow);
            vectorLayout->setContentsMargins(0, 0, 0, 0);
            auto makeComponentSpin = [this](float value) {
                auto* spin = new QDoubleSpinBox(m_inspectorContent);
                spin->setRange(-10000.0, 10000.0);
                spin->setDecimals(4);
                spin->setSingleStep(0.01);
                spin->setValue(value);
                return spin;
            };
            auto* xSpin = makeComponentSpin(node.color.x);
            auto* ySpin = makeComponentSpin(node.color.y);
            auto* zSpin = makeComponentSpin(node.color.z);
            xSpin->setProperty("lastCommitted", node.color.x);
            ySpin->setProperty("lastCommitted", node.color.y);
            zSpin->setProperty("lastCommitted", node.color.z);
            xSpin->setPrefix(QObject::tr("X "));
            ySpin->setPrefix(QObject::tr("Y "));
            zSpin->setPrefix(QObject::tr("Z "));
            vectorLayout->addWidget(xSpin);
            vectorLayout->addWidget(ySpin);
            vectorLayout->addWidget(zSpin);
            m_inspectorForm->addRow(QObject::tr("Vector3"), vectorRow);

            auto updateVectorLive = [this, nodeId = node.id, xSpin, ySpin, zSpin, updatePreview]() {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->color = Float3{
                        static_cast<float>(xSpin->value()),
                        static_cast<float>(ySpin->value()),
                        static_cast<float>(zSpin->value())};
                    updatePreview(n->color, 1.0f);
                    markDirty();
                    schedulePreviewUpdate();
                }
            };
            connect(xSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateVectorLive);
            connect(ySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateVectorLive);
            connect(zSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, updateVectorLive);

            auto commitVector = [this, nodeId = node.id, xSpin, ySpin, zSpin, updatePreview]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const Float3 oldValue{
                    xSpin->property("lastCommitted").toFloat(),
                    ySpin->property("lastCommitted").toFloat(),
                    zSpin->property("lastCommitted").toFloat()};
                const Float3 newValue{
                    static_cast<float>(xSpin->value()),
                    static_cast<float>(ySpin->value()),
                    static_cast<float>(zSpin->value())};
                if (oldValue.x == newValue.x && oldValue.y == newValue.y && oldValue.z == newValue.z) {
                    return;
                }
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue, updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = oldValue;
                            updatePreview(oldValue, 1.0f);
                        }
                    },
                    [this, nodeId, newValue, updatePreview]() {
                        if (Node* node = m_graph.FindNode(nodeId)) {
                            node->color = newValue;
                            updatePreview(newValue, 1.0f);
                        }
                    },
                    QObject::tr("Edit Vector3")));
                xSpin->setProperty("lastCommitted", newValue.x);
                ySpin->setProperty("lastCommitted", newValue.y);
                zSpin->setProperty("lastCommitted", newValue.z);
            };
            connect(xSpin, &QDoubleSpinBox::editingFinished, this, commitVector);
            connect(ySpin, &QDoubleSpinBox::editingFinished, this, commitVector);
            connect(zSpin, &QDoubleSpinBox::editingFinished, this, commitVector);
        }

        if (IsTextureNode(node.type)) {
            auto* texturePreviewRow = new QWidget(m_inspectorContent);
            auto* texturePreviewLayout = new QHBoxLayout(texturePreviewRow);
            texturePreviewLayout->setContentsMargins(0, 0, 0, 0);
            auto* thumbnail = new QLabel(texturePreviewRow);
            thumbnail->setFixedSize(64, 64);
            thumbnail->setFrameShape(QFrame::Box);
            thumbnail->setAlignment(Qt::AlignCenter);
            auto* textureInfo = new QLabel(texturePreviewRow);
            textureInfo->setWordWrap(true);
            texturePreviewLayout->addWidget(thumbnail);
            texturePreviewLayout->addWidget(textureInfo, 1);
            m_inspectorForm->addRow(QObject::tr("Texture Preview"), texturePreviewRow);

            auto* pathRow = new QWidget(m_inspectorContent);
            auto* pathLayout = new QHBoxLayout(pathRow);
            pathLayout->setContentsMargins(0, 0, 0, 0);
            auto* path = new QLineEdit(QString::fromStdString(node.resourcePath), pathRow);
            path->setPlaceholderText(QObject::tr("Texture asset path"));
            auto* browse = new QPushButton(QObject::tr("..."), pathRow);
            browse->setMaximumWidth(28);
            auto* reset = new QPushButton(QObject::tr("Clear"), pathRow);
            pathLayout->addWidget(path, 1);
            pathLayout->addWidget(browse);
            pathLayout->addWidget(reset);
            m_inspectorForm->addRow(QObject::tr("Texture Path"), pathRow);
            if (node.type == NodeType::AoTextureSample || node.type == NodeType::RoughnessTextureSample) {
                textureInfo->setToolTip(QObject::tr("Packed ORM maps usually use R=AO, G=Roughness, B=Metallic. This node samples the channel relevant to its input."));
            }
            auto updateTexturePreview = [this, thumbnail, textureInfo](const QString& rawPath) {
                QString resolved = rawPath;
                resolved.replace('\\', '/');
                if (!resolved.isEmpty() && !QFileInfo(resolved).isAbsolute()) {
                    const QString assetRoot = QString::fromStdString(AssetManager::GetInstance().GetAssetRoot()).replace('\\', '/');
                    const QString projectRoot = QString::fromStdString(AssetManager::GetInstance().GetProjectRoot()).replace('\\', '/');
                    if (!assetRoot.isEmpty() && QFileInfo(assetRoot + "/" + resolved).exists()) {
                        resolved = assetRoot + "/" + resolved;
                    } else if (!projectRoot.isEmpty() && QFileInfo(projectRoot + "/" + resolved).exists()) {
                        resolved = projectRoot + "/" + resolved;
                    }
                }

                QImage image(resolved);
                if (rawPath.trimmed().isEmpty()) {
                    thumbnail->setText(QObject::tr("None"));
                    thumbnail->setPixmap(QPixmap());
                    textureInfo->setText(QObject::tr("No texture assigned."));
                } else if (image.isNull()) {
                    thumbnail->setText(QObject::tr("Missing"));
                    thumbnail->setPixmap(QPixmap());
                    textureInfo->setText(QObject::tr("Missing or unsupported texture:\n%1").arg(rawPath));
                } else {
                    thumbnail->setText(QString());
                    thumbnail->setPixmap(QPixmap::fromImage(image).scaled(thumbnail->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                    textureInfo->setText(QObject::tr("%1 x %2\n%3").arg(image.width()).arg(image.height()).arg(rawPath));
                }
            };
            updateTexturePreview(path->text());
            path->setProperty("lastCommitted", path->text());
            auto commitPath = [this, nodeId = node.id, path]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const std::string oldPath = path->property("lastCommitted").toString().toStdString();
                const std::string newPath = path->text().toStdString();
                if (oldPath == newPath) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldPath]() { if (Node* node = m_graph.FindNode(nodeId)) node->resourcePath = oldPath; },
                    [this, nodeId, newPath]() { if (Node* node = m_graph.FindNode(nodeId)) node->resourcePath = newPath; },
                    QObject::tr("Edit Texture Path")));
                path->setProperty("lastCommitted", path->text());
            };
            connect(path, &QLineEdit::textChanged, this, updateTexturePreview);
            connect(path, &QLineEdit::textChanged, this, [this, nodeId = node.id](const QString& value) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->resourcePath = value.toStdString();
                    markDirty();
                    schedulePreviewUpdate();
                }
            });
            connect(path, &QLineEdit::editingFinished, this, commitPath);
            connect(browse, &QPushButton::clicked, this, [this, path, commitPath]() {
                const QString file = QFileDialog::getOpenFileName(this, QObject::tr("Choose Texture"),
                    QFileInfo(m_absolutePath).absolutePath(), QObject::tr("Textures (*.png *.jpg *.jpeg *.tga *.dds *.ktx *.bmp);;All Files (*.*)"));
                if (file.isEmpty()) return;
                std::string portable = file.toStdString();
                const std::string projectRoot = Hephaestus::AssetManager::GetInstance().GetProjectRoot();
                if (!projectRoot.empty()) {
                    portable = MaterialEditor::GetRelativeTexturePath(
                        std::filesystem::path(projectRoot),
                        std::filesystem::path(file.toStdString()));
                } else {
                    portable = Hephaestus::AssetPathPolicy::ToPortableKey({}, file.toStdString());
                }
                path->setText(QString::fromStdString(portable));
                commitPath();
            });
            connect(reset, &QPushButton::clicked, this, [path, commitPath]() {
                path->clear();
                commitPath();
            });
        }

        if (node.type == NodeType::WorldAlignedTexture) {
            auto* scale = new QDoubleSpinBox(m_inspectorContent);
            scale->setRange(0.001, 10000.0);
            scale->setDecimals(4);
            scale->setValue(node.worldAlignedScale);
            scale->setProperty("lastCommitted", node.worldAlignedScale);
            m_inspectorForm->addRow(QObject::tr("World Aligned Scale"), scale);
            connect(scale, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, nodeId = node.id](double value) {
                if (Node* n = m_graph.FindNode(nodeId)) {
                    n->worldAlignedScale = static_cast<float>(value);
                    markDirty();
                    schedulePreviewUpdate();
                }
            });
            connect(scale, &QDoubleSpinBox::editingFinished, this, [this, nodeId = node.id, scale]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const float oldValue = scale->property("lastCommitted").toFloat();
                const float newValue = static_cast<float>(scale->value());
                if (std::abs(oldValue - newValue) < 1e-6f) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->worldAlignedScale = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->worldAlignedScale = newValue; },
                    QObject::tr("Edit World Aligned Scale")));
                scale->setProperty("lastCommitted", newValue);
            });
        }
    }

    void rebuildInspector(NodeID selectedNodeId) {
        clearInspectorForm();

        if (selectedNodeId != 0) {
            if (Node* node = m_graph.FindNode(selectedNodeId)) {
                addNodeInspector(*node);
                if (node->type == NodeType::MaterialOutput) {
                    addMaterialInspector();
                }
                return;
            }
        }

        addMaterialInspector();
    }

    void showAddNodeMenu() {
        showNodeSearchPopup(m_addNodeButton->mapToGlobal(QPoint(0, m_addNodeButton->height())), graphViewCenter());
    }

    void compileAndApply() {
        GraphValidationResult validation = ValidateGraph(m_graph);
        m_errorList->clear();
        for (const GraphIssue& issue : validation.issues) {
            addIssueItem(issue.message, issue.nodeId, issue.severity);
        }
        m_graphScene->applyValidation(validation);

        if (!validation.ok) {
            refreshStatus(QObject::tr("Compile error"), false);
            if (m_previewPlaceholderLabel) {
                m_previewPlaceholderLabel->setText(QObject::tr("Preview blocked by graph errors."));
            }
            return;
        }

        if (m_graph.HasSourceGraph() && !m_graph.IsLegacyMode()) {
            MaterialShaderGenerationResult shaderResult = GenerateMaterialShader(m_graph);
            for (const std::string& diagnostic : shaderResult.diagnostics) {
                addIssueItem(QObject::tr("Shader: %1").arg(QString::fromStdString(diagnostic)), 0, GraphIssue::Severity::Error);
            }
            MaterialComponent evaluated = m_material;
            std::string evalError;
            if (!EvaluateGraph(m_graph, evaluated, evalError)) {
                refreshStatus(QObject::tr("Compile error: %1").arg(QString::fromStdString(evalError)), false);
                addIssueItem(QString::fromStdString(evalError), 0, GraphIssue::Severity::Error);
                return;
            }
            m_material = evaluated;
        }

        refreshStatus(QObject::tr("Compiled"), true);
        schedulePreviewUpdate();
    }

    void convertLegacyToGraph() {
        BuildMinimalGraphFromLegacyPbr(m_material, m_graph);
        m_graph.SetGraphEditingEnabled(true);
        m_graph.SetLegacyMode(false);
        m_graph.SetHasSourceGraph(true);
        rebuildGraphScene();
        rebuildInspector(0);
        markDirty();
        refreshStatus(QObject::tr("Converted legacy material to graph."), true);
    }

    void saveMaterial() {
        compileAndApply();

        const MaterialComponent& toSave = m_material;

        const std::string saveName = m_nameEdit->text().trimmed().isEmpty()
            ? QFileInfo(m_absolutePath).completeBaseName().toStdString()
            : m_nameEdit->text().toStdString();

        std::string error;
        namespace fs = std::filesystem;
        if (!SaveMaterialAssetCompat(fs::path(m_absolutePath.toStdString()), toSave, saveName, m_graph, {}, error)) {
            QMessageBox::warning(this, QObject::tr("Save Failed"),
                                 QObject::tr("Failed to save material:\n%1").arg(QString::fromStdString(error)));
            refreshStatus(QObject::tr("Save failed"), false);
            return;
        }

        m_undo->setClean();
        setDirty(false);
        refreshStatus(QObject::tr("Saved"), true);
    }

    void refreshStatus(const QString& text, bool ok) {
        m_statusLabel->setText(text);
        SetStyleProperty(m_statusLabel, "tone", ok ? QStringLiteral("success") : QStringLiteral("error"));
        refreshIssueCount();
    }

    void addIssueItem(const QString& text, NodeID nodeId, GraphIssue::Severity severity) {
        QString icon = QStringLiteral("error");
        Theme::IconTone tone = Theme::IconTone::Error;
        if (severity == GraphIssue::Severity::Warning) {
            icon = QStringLiteral("warning");
            tone = Theme::IconTone::Warning;
        } else if (severity == GraphIssue::Severity::Info) {
            icon = QStringLiteral("info");
            tone = Theme::IconTone::Info;
        }
        auto* item = new QListWidgetItem(Theme::Icon(icon, tone), text, m_errorList);
        item->setData(Qt::UserRole, nodeId);
    }

    void refreshIssueCount() {
        if (!m_issueCountLabel) {
            return;
        }
        const int count = m_errorList->count();
        m_issueCountLabel->setText(count == 0 ? QObject::tr("No issues") : QObject::tr("%n issue(s)", nullptr, count));
    }

private:
    QString m_absolutePath;
    bool m_initialized = false;

    MaterialComponent m_material;
    Graph m_graph;
    std::string m_displayName;
    std::string m_lastError;

    QUndoStack* m_undo = nullptr;
    MaterialGraphScene* m_graphScene = nullptr;
    MaterialGraphView* m_graphView = nullptr;

    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_graphBanner = nullptr;
    QPushButton* m_convertButton = nullptr;
    QToolButton* m_applyButton = nullptr;
    QToolButton* m_compileButton = nullptr;
    QToolButton* m_saveButton = nullptr;
    QPushButton* m_addNodeButton = nullptr;
    QToolButton* m_viewButton = nullptr;
    QToolButton* m_snapCheck = nullptr;
    QComboBox* m_blendModeCombo = nullptr;
    QLabel* m_dirtyLabel = nullptr;
    bool m_dirty = false;

    QLineEdit* m_paletteSearchEdit = nullptr;
    QTreeWidget* m_nodePalette = nullptr;
    std::vector<NodeType> m_recentNodeTypes;
    QWidget* m_inspectorContent = nullptr;
    QFormLayout* m_inspectorForm = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_issueCountLabel = nullptr;
    QListWidget* m_errorList = nullptr;
    QComboBox* m_previewMeshCombo = nullptr;
    QLineEdit* m_previewHdriEdit = nullptr;
    QToolButton* m_previewHdriChooseButton = nullptr;
    QToolButton* m_previewHdriDefaultButton = nullptr;
    QString m_previewHdriPath = QString::fromUtf8(DEFAULT_PREVIEW_HDRI_PATH);
    QWidget* m_previewViewportHost = nullptr;
    QLabel* m_previewPlaceholderLabel = nullptr;
    QLabel* m_previewStatusLabel = nullptr;
    QTimer* m_previewUpdateTimer = nullptr;
    MaterialComponent m_previewMaterial;
    std::unique_ptr<Scene> m_previewScene;
    entt::entity m_previewEntity = entt::null;
    entt::entity m_previewEnvironmentEntity = entt::null;
    QtViewportWindow* m_previewViewportWindow = nullptr;
    QWidget* m_previewViewportContainer = nullptr;
    std::unique_ptr<QtEditorEngine> m_previewEngine;
};

} // namespace

bool OpenMaterialGraphEditorDialog(QWidget* parent, const QString& absolutePath) {
    auto* dialog = new MaterialGraphEditorDialog(parent, absolutePath);
    if (!dialog->isInitialized()) {
        dialog->deleteLater();
        return false;
    }
    dialog->show();
    return true;
}

} // namespace QtEditor
} // namespace Hephaestus

#endif // HEPHAESTUS_EDITOR_QT_UI
