#include "PluginAPI/PluginAPI.h"
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <array>
#include <filesystem>

using namespace Hephaestus::Plugins;

namespace {

struct Shape {
    PrimitiveKind kind;
    const char* command;
    const char* name;
    const char* icon;
    const char* toolTip;
    std::array<float, 3> size;  // metres; change it afterwards in the Inspector
};

constexpr std::array<Shape, 4> kShapes = {{
    {PrimitiveKind::Cube, "primitive.cube", "Cube", "Resources/cube.svg",
     "A construction block in front of the camera. Drag its face handles to resize it, or set Mode to Subtract in "
     "the Inspector to cut holes in blocks and rooms.", {1.0f, 1.0f, 1.0f}},
    {PrimitiveKind::Plane, "primitive.plane", "Plane", "Resources/plane.svg",
     "A flat plane in front of the camera.", {4.0f, 0.05f, 4.0f}},
    {PrimitiveKind::Cylinder, "primitive.cylinder", "Cylinder", "Resources/cylinder.svg",
     "A cylinder standing in front of the camera.", {1.0f, 2.0f, 1.0f}},
    {PrimitiveKind::Sphere, "primitive.sphere", "Sphere", "Resources/sphere.svg",
     "A sphere resting in front of the camera.", {1.0f, 1.0f, 1.0f}},
}};

} // namespace

class PrimitiveToolsPlugin final : public IHephaestusPlugin {
public:
    bool OnLoad(IPluginContext& c) override
    {
        context = &c;
        bool ok = true;
        ok &= Command(kShapes[0].command, kShapes[0].name, kShapes[0].toolTip, &CreateShape<0>);
        ok &= Command(kShapes[1].command, kShapes[1].name, kShapes[1].toolTip, &CreateShape<1>);
        ok &= Command(kShapes[2].command, kShapes[2].name, kShapes[2].toolTip, &CreateShape<2>);
        ok &= Command(kShapes[3].command, kShapes[3].name, kShapes[3].toolTip, &CreateShape<3>);
        ok &= Command("primitive.light", "Point Light", "A point light at the origin", &Light);
        for (const Shape& shape : kShapes) {
            ToolbarButtonDescriptor button;
            button.commandId = shape.command;
            button.iconPath = shape.icon;
            button.toolTip = shape.toolTip;
            ok &= c.UI().RegisterToolbarButton(button);
        }
        ok &= c.UI().RegisterContextAction({"viewport", "primitive.cube"});
        AssetTypeDescriptor asset{"primitive.testAsset", ".hptestasset", "Primitive Test Asset", "", false,
                                  &CreateAsset, &OpenAsset, this};
        ok &= c.Assets().RegisterAssetType(asset);
        ok &= c.Assets().RegisterImporter(
            {"Primitive Test Source (*.hptestsrc)", "*.hptestsrc", "primitive.testAsset", &ImportAsset, this});
        ok &= c.Settings().Register(
            {"defaultPrimitive", "Default Primitive", "Used by future tool presets.", "Cube", SettingScope::Project});
        return ok;
    }

    void OnUnload() override { context = nullptr; }

private:
    bool Command(const char* id, const char* name, const char* description, PluginCallback cb)
    {
        return context->Commands().Register({id, name, description, "", cb, this});
    }

    template <size_t Index>
    static void CreateShape(void* p)
    {
        static_cast<PrimitiveToolsPlugin*>(p)->Create(kShapes[Index]);
    }

    void Create(const Shape& shape)
    {
        PrimitiveRequest request;
        request.kind = shape.kind;
        request.size[0] = shape.size[0];
        request.size[1] = shape.size[1];
        request.size[2] = shape.size[2];
        request.collision = true;
        request.placeInView = true;
        const auto tx = context->Undo().Begin(shape.name);
        if (!tx) {
            return;
        }
        if (context->Scene().CreatePrimitiveEx(request)) {
            context->Undo().Commit(tx);
        } else {
            context->Undo().Rollback(tx);
        }
    }

    static void Light(void* p)
    {
        auto* self = static_cast<PrimitiveToolsPlugin*>(p);
        const auto tx = self->context->Undo().Begin("Point Light");
        if (!tx) return;
        if (self->context->Scene().CreatePrimitive(PrimitiveKind::PointLight, "Point Light")) {
            self->context->Undo().Commit(tx);
        } else {
            self->context->Undo().Rollback(tx);
        }
    }

    static bool CreateAsset(const char* dir, void* data)
    {
        auto* self = static_cast<PrimitiveToolsPlugin*>(data);
        bool ok = false;
        QString name = QInputDialog::getText(nullptr, "Create Primitive Test Asset", "Name:", QLineEdit::Normal,
                                             "PrimitivePreset", &ok);
        if (!ok || name.trimmed().isEmpty()) return false;
        std::filesystem::path root(self->context->Project().ProjectRoot());
        std::filesystem::path target = root / "assets" / (dir ? dir : "") / (name.trimmed().toStdString() + ".hptestasset");
        QFile file(QString::fromStdString(target.string()));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return false;
        file.write("{\n  \"version\": 1,\n  \"primitive\": \"Cube\"\n}\n");
        return true;
    }

    static bool OpenAsset(const char* path, void*)
    {
        QMessageBox::information(nullptr, "Primitive Test Asset",
                                 QString("Opened through the registered custom asset editor:\n%1").arg(QString::fromUtf8(path)));
        return true;
    }

    static bool ImportAsset(const char* source, const char* dir, void* data)
    {
        auto* self = static_cast<PrimitiveToolsPlugin*>(data);
        std::filesystem::path root(self->context->Project().ProjectRoot());
        QFileInfo info(QString::fromUtf8(source));
        std::filesystem::path target = root / "assets" / (dir ? dir : "") / (info.completeBaseName().toStdString() + ".hptestasset");
        QFile::remove(QString::fromStdString(target.string()));
        return QFile::copy(QString::fromUtf8(source), QString::fromStdString(target.string()));
    }

    IPluginContext* context = nullptr;
};

HP_DECLARE_PLUGIN(PrimitiveToolsPlugin)
