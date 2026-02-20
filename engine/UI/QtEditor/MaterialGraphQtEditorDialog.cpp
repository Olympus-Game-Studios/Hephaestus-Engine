#if HEPHAESTUS_EDITOR_QT_UI

#include "MaterialGraphQtEditorDialog.h"

#include "../MaterialEditor/MaterialAssetSerializer.h"
#include "../MaterialEditor/MaterialGraphEvaluator.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
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
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QScrollBar>
#include <QSplitter>
#include <QToolButton>
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

QString NodeTypeLabel(NodeType type) {
    switch (type) {
    case NodeType::MaterialOutput: return QObject::tr("MaterialOutput");
    case NodeType::ConstantFloat: return QObject::tr("ConstantScalar");
    case NodeType::ConstantVec3: return QObject::tr("ConstantColor");
    case NodeType::AlbedoTextureSample: return QObject::tr("AlbedoTextureSample");
    case NodeType::NormalTextureSample: return QObject::tr("NormalTextureSample");
    case NodeType::RoughnessTextureSample: return QObject::tr("RoughnessTextureSample");
    case NodeType::AoTextureSample: return QObject::tr("AOTextureSample");
    case NodeType::WorldAlignedTexture: return QObject::tr("WorldAlignedTexture");
    case NodeType::Add: return QObject::tr("Add");
    case NodeType::Multiply: return QObject::tr("Multiply");
    case NodeType::ColorParameter: return QObject::tr("ColorParameter");
    case NodeType::FloatParameter: return QObject::tr("FloatParameter");
    case NodeType::Unknown: return QObject::tr("Unknown Node");
    }
    return QObject::tr("Node");
}

QString NodeCategory(NodeType type) {
    switch (type) {
    case NodeType::ColorParameter:
    case NodeType::FloatParameter:
    case NodeType::ConstantFloat:
    case NodeType::ConstantVec3:
        return QStringLiteral("Inputs");
    case NodeType::AlbedoTextureSample:
    case NodeType::NormalTextureSample:
    case NodeType::RoughnessTextureSample:
    case NodeType::AoTextureSample:
    case NodeType::WorldAlignedTexture:
        return QStringLiteral("Textures");
    case NodeType::Add:
    case NodeType::Multiply:
        return QStringLiteral("Math");
    case NodeType::MaterialOutput:
        return QStringLiteral("Output");
    case NodeType::Unknown:
        return QStringLiteral("Utilities");
    }
    return QStringLiteral("Utilities");
}

QColor PinColor(ValueType type) {
    switch (type) {
    case ValueType::Float1: return QColor(240, 220, 100);
    case ValueType::Float3: return QColor(90, 170, 255);
    case ValueType::Sampler2D: return QColor(190, 120, 255);
    case ValueType::Float2: return QColor(140, 220, 140);
    case ValueType::Float4: return QColor(255, 140, 140);
    }
    return QColor(180, 180, 180);
}

QColor NodeTitleColor(NodeType type) {
    const QString category = NodeCategory(type);
    if (category == "Textures") return QColor(76, 58, 125);
    if (category == "Math") return QColor(64, 98, 120);
    if (category == "Inputs") return QColor(66, 92, 64);
    if (category == "Output") return QColor(132, 78, 64);
    return QColor(82, 82, 82);
}

bool IsTextureNode(NodeType type) {
    return type == NodeType::AlbedoTextureSample || type == NodeType::NormalTextureSample ||
           type == NodeType::RoughnessTextureSample || type == NodeType::AoTextureSample ||
           type == NodeType::WorldAlignedTexture;
}

bool IsScalarNode(NodeType type) {
    return type == NodeType::ConstantFloat || type == NodeType::FloatParameter;
}

bool IsColorNode(NodeType type) {
    return type == NodeType::ConstantVec3 || type == NodeType::ColorParameter;
}

struct NodeMenuEntry {
    QString label;
    NodeType type;
};

const std::vector<NodeMenuEntry>& NodeMenuEntries() {
    static const std::vector<NodeMenuEntry> entries = {
        {QObject::tr("MaterialOutput"), NodeType::MaterialOutput},
        {QObject::tr("ConstantScalar"), NodeType::ConstantFloat},
        {QObject::tr("ConstantColor"), NodeType::ConstantVec3},
        {QObject::tr("AlbedoTextureSample"), NodeType::AlbedoTextureSample},
        {QObject::tr("NormalTextureSample"), NodeType::NormalTextureSample},
        {QObject::tr("RoughnessTextureSample"), NodeType::RoughnessTextureSample},
        {QObject::tr("AOTextureSample"), NodeType::AoTextureSample},
        {QObject::tr("WorldAlignedTexture"), NodeType::WorldAlignedTexture},
        {QObject::tr("Add"), NodeType::Add},
        {QObject::tr("Multiply"), NodeType::Multiply},
        {QObject::tr("Lerp"), NodeType::Lerp}
    };
    return entries;
}

struct GraphIssue {
    QString message;
    NodeID nodeId = 0;
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
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            ++outputCount;
            outputNodeId = node.id;
        }
    }
    if (outputCount != 1) {
        result.ok = false;
        result.issues.push_back({QObject::tr("Graph must contain exactly one MaterialOutput node."), outputNodeId});
    }

    std::unordered_map<NodeID, std::vector<NodeID>> adjacency;
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
        if (start->valueType != end->valueType) {
            result.ok = false;
            result.issues.push_back({QObject::tr("Type mismatch on link id=%1.").arg(link.id), start->node});
            continue;
        }
        adjacency[start->node].push_back(end->node);
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

    return result;
}

struct SyncResult {
    bool representable = true;
    QString warning;
};

SyncResult SyncLegacyFieldsFromGraph(const Graph& graph, MaterialComponent& ioMaterial) {
    SyncResult sync;

    const Node* output = nullptr;
    for (const Node& node : graph.GetNodes()) {
        if (node.type == NodeType::MaterialOutput) {
            output = &node;
            break;
        }
    }
    if (!output) {
        sync.representable = false;
        sync.warning = QObject::tr("Missing MaterialOutput node.");
        return sync;
    }

    auto findDirectNode = [&](int inputIndex) -> const Node* {
        if (inputIndex < 0 || inputIndex >= static_cast<int>(output->inputs.size())) {
            return nullptr;
        }
        const PinID sourcePin = graph.FindInputSource(output->inputs[inputIndex]);
        if (sourcePin == 0) {
            return nullptr;
        }
        const Pin* pin = graph.FindPin(sourcePin);
        if (!pin) {
            return nullptr;
        }
        return graph.FindNode(pin->node);
    };

    auto markComplex = [&]() {
        sync.representable = false;
        if (sync.warning.isEmpty()) {
            sync.warning = QObject::tr("Graph uses features not representable in legacy fields. Legacy values preserved.");
        }
    };

    if (const Node* n = findDirectNode(0)) {
        if (n->type == NodeType::ConstantVec3 || n->type == NodeType::ColorParameter) {
            ioMaterial.albedo = Vec3(n->color.x, n->color.y, n->color.z);
            ioMaterial.useAlbedoTexture = false;
            ioMaterial.albedoTexturePath.clear();
        } else if (n->type == NodeType::AlbedoTextureSample || n->type == NodeType::WorldAlignedTexture) {
            ioMaterial.useAlbedoTexture = true;
            ioMaterial.albedoTexturePath = n->resourcePath;
            ioMaterial.useWorldAlignedTexture = (n->type == NodeType::WorldAlignedTexture);
            ioMaterial.worldTextureScale = n->worldAlignedScale;
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(1)) {
        if (n->type == NodeType::ConstantFloat || n->type == NodeType::FloatParameter) {
            ioMaterial.metallic = n->scalar;
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(2)) {
        if (n->type == NodeType::ConstantFloat || n->type == NodeType::FloatParameter) {
            ioMaterial.roughness = n->scalar;
            ioMaterial.useRoughnessTexture = false;
            ioMaterial.roughnessTexturePath.clear();
        } else if (n->type == NodeType::RoughnessTextureSample || n->type == NodeType::WorldAlignedTexture) {
            ioMaterial.useRoughnessTexture = true;
            ioMaterial.roughnessTexturePath = n->resourcePath;
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(3)) {
        if (n->type == NodeType::ConstantVec3 || n->type == NodeType::ColorParameter) {
            ioMaterial.emissive = Vec3(n->color.x, n->color.y, n->color.z);
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(4)) {
        if (n->type == NodeType::NormalTextureSample || n->type == NodeType::WorldAlignedTexture) {
            ioMaterial.useNormalTexture = true;
            ioMaterial.normalTexturePath = n->resourcePath;
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(5)) {
        if (n->type == NodeType::RoughnessTextureSample || n->type == NodeType::WorldAlignedTexture) {
            ioMaterial.useRoughnessTexture = true;
            ioMaterial.roughnessTexturePath = n->resourcePath;
        } else if (n->type == NodeType::ConstantFloat || n->type == NodeType::FloatParameter) {
            ioMaterial.roughness = n->scalar;
            ioMaterial.useRoughnessTexture = false;
            ioMaterial.roughnessTexturePath.clear();
        } else {
            markComplex();
        }
    }

    if (const Node* n = findDirectNode(6)) {
        if (n->type == NodeType::AoTextureSample || n->type == NodeType::WorldAlignedTexture) {
            ioMaterial.useAoTexture = true;
            ioMaterial.aoTexturePath = n->resourcePath;
        } else if (n->type == NodeType::ConstantFloat || n->type == NodeType::FloatParameter) {
            ioMaterial.ao = n->scalar;
            ioMaterial.useAoTexture = false;
            ioMaterial.aoTexturePath.clear();
        } else {
            markComplex();
        }
    }

    return sync;
}

class PinItem final : public QGraphicsItem {
public:
    enum { Type = QGraphicsItem::UserType + 4001 };

    PinItem(PinID pinId, PinKind kind, ValueType valueType, QString pinLabel, QGraphicsItem* parent = nullptr)
        : QGraphicsItem(parent), m_pinId(pinId), m_kind(kind), m_valueType(valueType), m_label(std::move(pinLabel)) {
        setAcceptHoverEvents(true);
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

        QPen pen(isSelected() ? QColor(255, 230, 130) : QColor(200, 200, 200));
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
        setZValue(3.0);
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
        painter->setPen(QPen(isSelected() ? QColor(250, 230, 140) : QColor(40, 40, 40), isSelected() ? 2.0 : 1.0));
        painter->setBrush(QColor(36, 36, 40));
        painter->drawRoundedRect(rect, 6.0, 6.0);

        QRectF titleRect(0.0, 0.0, rect.width(), 30.0);
        painter->setPen(Qt::NoPen);
        painter->setBrush(NodeTitleColor(m_type));
        painter->drawRoundedRect(titleRect, 6.0, 6.0);
        painter->setBrush(QColor(36, 36, 40));
        painter->drawRect(0.0, 26.0, rect.width(), 4.0);

        painter->setPen(Qt::white);
        painter->drawText(titleRect.adjusted(8.0, 0.0, -8.0, 0.0), Qt::AlignVCenter | Qt::AlignLeft, m_title);

        painter->setPen(QColor(210, 210, 210));
        for (PinItem* p : m_inputs) {
            const QPointF lp = p->pos();
            painter->drawText(QRectF(18.0, lp.y() - 8.0, m_width * 0.5, 16.0), Qt::AlignVCenter | Qt::AlignLeft, p->label());
        }
        for (PinItem* p : m_outputs) {
            const QPointF lp = p->pos();
            painter->drawText(QRectF(m_width * 0.45, lp.y() - 8.0, m_width * 0.45, 16.0), Qt::AlignVCenter | Qt::AlignRight, p->label());
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
        setBackgroundBrush(QColor(30, 30, 34));
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
                m_undo->push(new MoveNodeCommand(this, id, from, to));
            });
            nodeItem->setMovedCallback([this](NodeID id) {
                updateLinksForNode(id);
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
        restored.scalar = node.scalar;
        restored.color = node.color;
        restored.resourcePath = node.resourcePath;
        restored.worldAlignedScale = node.worldAlignedScale;
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
        if (m_selectionChanged) {
            m_selectionChanged(nodeId);
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
    }

    void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override {
        QMenu menu;
        QGraphicsItem* hit = itemAt(event->scenePos(), QTransform());

        if (!hit) {
            QMenu* add = menu.addMenu(QObject::tr("Add Node"));

            QMenu* outputMenu = add->addMenu(QObject::tr("Output"));
            QMenu* inputsMenu = add->addMenu(QObject::tr("Inputs"));
            QMenu* texturesMenu = add->addMenu(QObject::tr("Textures"));
            QMenu* mathMenu = add->addMenu(QObject::tr("Math"));
            QMenu* utilitiesMenu = add->addMenu(QObject::tr("Utilities"));

            auto categoryMenu = [&](NodeType type) -> QMenu* {
                const QString cat = NodeCategory(type);
                if (cat == "Output") return outputMenu;
                if (cat == "Inputs") return inputsMenu;
                if (cat == "Textures") return texturesMenu;
                if (cat == "Math") return mathMenu;
                return utilitiesMenu;
            };

            auto addAction = [&](QMenu* targetMenu, const QString& label, NodeType type) {
                QAction* a = targetMenu->addAction(label);
                connect(a, &QAction::triggered, this, [this, type, event]() {
                    addNode(type, event->scenePos());
                });
            };

            for (const NodeMenuEntry& entry : NodeMenuEntries()) {
                addAction(categoryMenu(entry.type), entry.label, entry.type);
            }

            menu.addSeparator();
            QAction* frame = menu.addAction(QObject::tr("Frame Selection"));
            connect(frame, &QAction::triggered, this, [this]() {
                if (!views().isEmpty()) {
                    views().front()->fitInView(itemsBoundingRect(), Qt::KeepAspectRatio);
                }
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
                highlightNode(nodeItem->nodeId());
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

private:
    bool canConnectPins(PinItem* output, PinItem* input) const {
        if (!output || !input) return false;
        if (output->kind() != PinKind::Output || input->kind() != PinKind::Input) return false;
        return output->valueType() == input->valueType();
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
    PinItem* m_liveDragPin = nullptr;
    LinkItem* m_liveWire = nullptr;
    bool m_ignoreMoveCommit = false;
    std::function<void(NodeID)> m_selectionChanged;
    std::function<void()> m_graphChanged;
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
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setDragMode(QGraphicsView::RubberBandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
    }

protected:
    void wheelEvent(QWheelEvent* event) override {
        const qreal factor = (event->angleDelta().y() > 0) ? 1.12 : 0.89;
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
    }

    bool isInitialized() const {
        return m_initialized;
    }

private:
    bool loadMaterial() {
        namespace fs = std::filesystem;
        if (!LoadMaterialAssetCompat(fs::path(m_absolutePath.toStdString()), m_material, m_displayName, m_graph, m_lastError)) {
            return false;
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

        auto* toolbar = new QHBoxLayout();
        m_applyButton = new QPushButton(QObject::tr("Compile/Apply"), this);
        m_saveButton = new QPushButton(QObject::tr("Save"), this);
        m_addNodeButton = new QPushButton(QObject::tr("Add Node"), this);
        m_searchEdit = new QLineEdit(this);
        m_searchEdit->setPlaceholderText(QObject::tr("Search nodes..."));
        m_viewButton = new QToolButton(this);
        m_viewButton->setText(QObject::tr("View"));
        m_syncLegacyCheck = new QCheckBox(QObject::tr("Sync legacy fields from graph"), this);
        m_syncLegacyCheck->setChecked(true);
        toolbar->addWidget(m_applyButton);
        toolbar->addWidget(m_saveButton);
        toolbar->addWidget(m_addNodeButton);
        toolbar->addWidget(m_searchEdit, 1);
        toolbar->addWidget(m_viewButton);
        toolbar->addWidget(m_syncLegacyCheck);
        root->addLayout(toolbar);

        auto* splitter = new QSplitter(Qt::Horizontal, this);

        m_graphScene = new MaterialGraphScene(&m_graph, m_undo, this);
        m_graphView = new MaterialGraphView(this);
        m_graphView->setScene(m_graphScene);
        splitter->addWidget(m_graphView);

        auto* inspectorHost = new QWidget(this);
        auto* inspectorLayout = new QVBoxLayout(inspectorHost);
        m_nameEdit = new QLineEdit(QString::fromStdString(m_displayName), inspectorHost);
        inspectorLayout->addWidget(new QLabel(QObject::tr("Material Name"), inspectorHost));
        inspectorLayout->addWidget(m_nameEdit);

        m_graphBanner = new QLabel(inspectorHost);
        m_graphBanner->setWordWrap(true);
        inspectorLayout->addWidget(m_graphBanner);

        auto* scroll = new QScrollArea(inspectorHost);
        scroll->setWidgetResizable(true);
        m_inspectorContent = new QWidget(scroll);
        m_inspectorForm = new QFormLayout(m_inspectorContent);
        m_inspectorForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
        scroll->setWidget(m_inspectorContent);
        inspectorLayout->addWidget(scroll, 1);

        m_convertButton = new QPushButton(QObject::tr("Convert to Graph"), inspectorHost);
        inspectorLayout->addWidget(m_convertButton);

        splitter->addWidget(inspectorHost);
        splitter->setStretchFactor(0, 4);
        splitter->setStretchFactor(1, 2);

        root->addWidget(splitter, 1);

        auto* statusFrame = new QFrame(this);
        statusFrame->setFrameShape(QFrame::StyledPanel);
        auto* statusLayout = new QVBoxLayout(statusFrame);
        m_statusLabel = new QLabel(QObject::tr("Status: Ready"), statusFrame);
        statusLayout->addWidget(m_statusLabel);
        m_errorList = new QListWidget(statusFrame);
        m_errorList->setSelectionMode(QAbstractItemView::SingleSelection);
        statusLayout->addWidget(m_errorList);
        root->addWidget(statusFrame);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        root->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        connect(m_applyButton, &QPushButton::clicked, this, [this]() { compileAndApply(); });
        connect(m_saveButton, &QPushButton::clicked, this, [this]() { saveMaterial(); });
        connect(m_convertButton, &QPushButton::clicked, this, [this]() { convertLegacyToGraph(); });
        connect(m_addNodeButton, &QPushButton::clicked, this, [this]() { showAddNodeMenu(); });
        connect(m_viewButton, &QToolButton::clicked, this, [this]() { m_graphView->fitInView(m_graphScene->itemsBoundingRect(), Qt::KeepAspectRatio); });

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
        });

        connect(m_errorList, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            const NodeID nodeId = item->data(Qt::UserRole).toInt();
            if (nodeId > 0) {
                m_graphScene->highlightNode(nodeId);
            }
        });

        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this, [this]() { m_undo->undo(); });
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this, [this]() { m_undo->redo(); });
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() { m_graphScene->duplicateSelected(); });
        new QShortcut(QKeySequence(Qt::Key_Delete), this, [this]() { m_graphScene->removeSelected(); });

        connect(m_nameEdit, &QLineEdit::editingFinished, this, [this]() {
            m_displayName = m_nameEdit->text().trimmed().isEmpty()
                ? QFileInfo(m_absolutePath).completeBaseName().toStdString()
                : m_nameEdit->text().toStdString();
        });

        refreshGraphBanner();
    }

    void refreshGraphBanner() {
        if (m_graph.IsLegacyMode()) {
            m_graphBanner->setText(QObject::tr("Legacy PBR mode: graph is missing/invalid. Use Convert to Graph to enter Graph Mode."));
            m_convertButton->setVisible(true);
        } else if (m_graph.HasSourceGraph()) {
            m_graphBanner->setText(QObject::tr("Graph Mode: node graph is editable. Legacy fields are preserved for backwards compatibility."));
            m_convertButton->setVisible(false);
        } else {
            m_graphBanner->setText(QObject::tr("PBR material fields."));
            m_convertButton->setVisible(true);
        }
    }

    void rebuildGraphScene() {
        m_graphScene->reloadFromGraph();
        m_graphView->setSceneRect(m_graphScene->itemsBoundingRect().adjusted(-300, -200, 300, 200));
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
        auto* emissiveStrength = makeSpin(0.0, 100.0, 0.01, m_material.emissiveStrength);

        m_inspectorForm->addRow(QObject::tr("Metallic"), metallic);
        m_inspectorForm->addRow(QObject::tr("Roughness"), roughness);
        m_inspectorForm->addRow(QObject::tr("AO"), ao);
        m_inspectorForm->addRow(QObject::tr("Emissive Strength"), emissiveStrength);

        addUndoSpin(metallic, QObject::tr("Edit Metallic"), [this]() { return m_material.metallic; }, [this](double v) { m_material.metallic = static_cast<float>(v); });
        addUndoSpin(roughness, QObject::tr("Edit Roughness"), [this]() { return m_material.roughness; }, [this](double v) { m_material.roughness = static_cast<float>(v); });
        addUndoSpin(ao, QObject::tr("Edit AO"), [this]() { return m_material.ao; }, [this](double v) { m_material.ao = static_cast<float>(v); });
        addUndoSpin(emissiveStrength, QObject::tr("Edit Emissive Strength"), [this]() { return m_material.emissiveStrength; }, [this](double v) { m_material.emissiveStrength = static_cast<float>(v); });
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

        if (IsScalarNode(node.type)) {
            auto* scalar = new QDoubleSpinBox(m_inspectorContent);
            scalar->setRange(-10000.0, 10000.0);
            scalar->setDecimals(4);
            scalar->setValue(node.scalar);
            m_inspectorForm->addRow(QObject::tr("Scalar"), scalar);
            connect(scalar, &QDoubleSpinBox::editingFinished, this, [this, scalar, nodeId = node.id]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const float oldValue = n->scalar;
                const float newValue = static_cast<float>(scalar->value());
                if (std::abs(oldValue - newValue) < 1e-6f) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->scalar = newValue; },
                    QObject::tr("Edit Scalar")));
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
        }

        if (IsTextureNode(node.type)) {
            auto* pathRow = new QWidget(m_inspectorContent);
            auto* pathLayout = new QHBoxLayout(pathRow);
            pathLayout->setContentsMargins(0, 0, 0, 0);
            auto* path = new QLineEdit(QString::fromStdString(node.resourcePath), pathRow);
            auto* browse = new QPushButton(QObject::tr("..."), pathRow);
            browse->setMaximumWidth(28);
            pathLayout->addWidget(path, 1);
            pathLayout->addWidget(browse);
            m_inspectorForm->addRow(QObject::tr("Texture Path"), pathRow);
            auto commitPath = [this, nodeId = node.id, path]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const std::string oldPath = n->resourcePath;
                const std::string newPath = path->text().toStdString();
                if (oldPath == newPath) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldPath]() { if (Node* node = m_graph.FindNode(nodeId)) node->resourcePath = oldPath; },
                    [this, nodeId, newPath]() { if (Node* node = m_graph.FindNode(nodeId)) node->resourcePath = newPath; },
                    QObject::tr("Edit Texture Path")));
            };
            connect(path, &QLineEdit::editingFinished, this, commitPath);
            connect(browse, &QPushButton::clicked, this, [this, path, commitPath]() {
                const QString file = QFileDialog::getOpenFileName(this, QObject::tr("Choose Texture"),
                    QFileInfo(m_absolutePath).absolutePath(), QObject::tr("Textures (*.png *.jpg *.jpeg *.tga *.dds *.ktx *.bmp);;All Files (*.*)"));
                if (file.isEmpty()) return;
                path->setText(file);
                commitPath();
            });
        }

        if (node.type == NodeType::WorldAlignedTexture) {
            auto* scale = new QDoubleSpinBox(m_inspectorContent);
            scale->setRange(0.001, 10000.0);
            scale->setDecimals(4);
            scale->setValue(node.worldAlignedScale);
            m_inspectorForm->addRow(QObject::tr("World Aligned Scale"), scale);
            connect(scale, &QDoubleSpinBox::editingFinished, this, [this, nodeId = node.id, scale]() {
                Node* n = m_graph.FindNode(nodeId);
                if (!n) return;
                const float oldValue = n->worldAlignedScale;
                const float newValue = static_cast<float>(scale->value());
                if (std::abs(oldValue - newValue) < 1e-6f) return;
                m_undo->push(new EditNodePropertyCommand(
                    [this, nodeId, oldValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->worldAlignedScale = oldValue; },
                    [this, nodeId, newValue]() { if (Node* node = m_graph.FindNode(nodeId)) node->worldAlignedScale = newValue; },
                    QObject::tr("Edit World Aligned Scale")));
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
        QMenu menu(this);
        const QString filter = m_searchEdit->text().trimmed().toLower();

        QMenu* outputMenu = menu.addMenu(QObject::tr("Output"));
        QMenu* inputsMenu = menu.addMenu(QObject::tr("Inputs"));
        QMenu* texturesMenu = menu.addMenu(QObject::tr("Textures"));
        QMenu* mathMenu = menu.addMenu(QObject::tr("Math"));
        QMenu* utilitiesMenu = menu.addMenu(QObject::tr("Utilities"));

        auto categoryMenu = [&](NodeType type) -> QMenu* {
            const QString cat = NodeCategory(type);
            if (cat == "Output") return outputMenu;
            if (cat == "Inputs") return inputsMenu;
            if (cat == "Textures") return texturesMenu;
            if (cat == "Math") return mathMenu;
            return utilitiesMenu;
        };

        for (const NodeMenuEntry& entry : NodeMenuEntries()) {
            if (!filter.isEmpty() && !entry.label.toLower().contains(filter)) {
                continue;
            }
            QAction* action = categoryMenu(entry.type)->addAction(entry.label);
            connect(action, &QAction::triggered, this, [this, type = entry.type]() {
                const QPoint viewCenter = m_graphView->viewport()->rect().center();
                const QPointF scenePos = m_graphView->mapToScene(viewCenter);
                m_graphScene->addNode(type, scenePos);
            });
        }

        outputMenu->menuAction()->setVisible(!outputMenu->actions().isEmpty());
        inputsMenu->menuAction()->setVisible(!inputsMenu->actions().isEmpty());
        texturesMenu->menuAction()->setVisible(!texturesMenu->actions().isEmpty());
        mathMenu->menuAction()->setVisible(!mathMenu->actions().isEmpty());
        utilitiesMenu->menuAction()->setVisible(!utilitiesMenu->actions().isEmpty());

        menu.exec(m_addNodeButton->mapToGlobal(QPoint(0, m_addNodeButton->height())));
    }

    void compileAndApply() {
        GraphValidationResult validation = ValidateGraph(m_graph);
        m_errorList->clear();
        for (const GraphIssue& issue : validation.issues) {
            auto* item = new QListWidgetItem(issue.message, m_errorList);
            item->setData(Qt::UserRole, issue.nodeId);
        }

        if (!validation.ok) {
            refreshStatus(QObject::tr("Compile error"), false);
            return;
        }

        if (m_graph.HasSourceGraph() && !m_graph.IsLegacyMode()) {
            MaterialComponent evaluated = m_material;
            std::string evalError;
            if (!EvaluateGraph(m_graph, evaluated, evalError)) {
                refreshStatus(QObject::tr("Compile error: %1").arg(QString::fromStdString(evalError)), false);
                auto* item = new QListWidgetItem(QString::fromStdString(evalError), m_errorList);
                item->setData(Qt::UserRole, 0);
                return;
            }
            m_material = evaluated;
        }

        refreshStatus(QObject::tr("Compiled"), true);
    }

    void convertLegacyToGraph() {
        BuildMinimalGraphFromLegacyPbr(m_material, m_graph);
        m_graph.SetGraphEditingEnabled(true);
        m_graph.SetLegacyMode(false);
        m_graph.SetHasSourceGraph(true);
        rebuildGraphScene();
        rebuildInspector(0);
        refreshStatus(QObject::tr("Converted legacy material to graph."), true);
    }

    void saveMaterial() {
        compileAndApply();

        MaterialComponent toSave = m_material;
        if (m_syncLegacyCheck->isChecked() && m_graph.HasSourceGraph() && !m_graph.IsLegacyMode()) {
            SyncResult sync = SyncLegacyFieldsFromGraph(m_graph, toSave);
            if (!sync.representable && !sync.warning.isEmpty()) {
                auto* item = new QListWidgetItem(sync.warning, m_errorList);
                item->setData(Qt::UserRole, 0);
            }
        }

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

        refreshStatus(QObject::tr("Saved"), true);
    }

    void refreshStatus(const QString& text, bool ok) {
        m_statusLabel->setText(QObject::tr("Status: %1").arg(text));
        QPalette pal = m_statusLabel->palette();
        pal.setColor(QPalette::WindowText, ok ? QColor(140, 230, 140) : QColor(255, 120, 120));
        m_statusLabel->setPalette(pal);
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
    QPushButton* m_applyButton = nullptr;
    QPushButton* m_saveButton = nullptr;
    QPushButton* m_addNodeButton = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QToolButton* m_viewButton = nullptr;
    QCheckBox* m_syncLegacyCheck = nullptr;

    QWidget* m_inspectorContent = nullptr;
    QFormLayout* m_inspectorForm = nullptr;
    QLabel* m_statusLabel = nullptr;
    QListWidget* m_errorList = nullptr;
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
