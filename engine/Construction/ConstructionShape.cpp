#include "ConstructionShape.h"

#include "../Scene/Json.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace Hephaestus::Construction {

namespace {

constexpr float kMinSize = 0.01f;

std::string Escape(const std::string& value)
{
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                std::ostringstream hex;
                hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                out += hex.str();
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string Number(float value)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(9) << value;
    return oss.str();
}

void ReadFloat(const JsonValue& object, const char* key, float& out)
{
    if (const JsonValue* value = object.Find(key); value && value->IsNumber() && std::isfinite(value->AsNumber())) {
        out = static_cast<float>(value->AsNumber());
    }
}

void ReadBool(const JsonValue& object, const char* key, bool& out)
{
    if (const JsonValue* value = object.Find(key); value && value->IsBool()) {
        out = value->AsBool();
    }
}

} // namespace

const char* SourceKindName(SourceKind kind)
{
    switch (kind) {
    case SourceKind::Recipe: return "recipe";
    case SourceKind::Asset: return "asset";
    case SourceKind::EditableMesh: return "editableMesh";
    }
    return "recipe";
}

const char* RecipeName(Recipe recipe)
{
    switch (recipe) {
    case Recipe::Opening: return "opening";
    case Recipe::Stairs: return "stairs";
    case Recipe::Plane: return "plane";
    case Recipe::Cylinder: return "cylinder";
    case Recipe::Sphere: return "sphere";
    case Recipe::Wall: return "wall";
    case Recipe::Ramp: return "ramp";
    case Recipe::Box: break;
    }
    return "box";
}

const char* WallSideName(WallSide side)
{
    switch (side) {
    case WallSide::PosX: return "+x";
    case WallSide::NegX: return "-x";
    case WallSide::PosZ: return "+z";
    case WallSide::NegZ: return "-z";
    }
    return "+z";
}

const char* OperationName(Operation operation)
{
    return operation == Operation::Subtract ? "subtract" : "add";
}

const char* CollisionModeName(CollisionMode mode)
{
    return mode == CollisionMode::None ? "none" : "solid";
}

bool ParseRecipe(const std::string& name, Recipe& out)
{
    if (name == "box") {
        out = Recipe::Box;
        return true;
    }
    if (name == "opening") {
        out = Recipe::Opening;
        return true;
    }
    for (Recipe recipe : {Recipe::Stairs, Recipe::Plane, Recipe::Cylinder, Recipe::Sphere, Recipe::Wall, Recipe::Ramp}) {
        if (name == RecipeName(recipe)) {
            out = recipe;
            return true;
        }
    }
    return false;
}

const char* StairTurnName(StairTurn turn)
{
    switch (turn) {
    case StairTurn::Left: return "left";
    case StairTurn::Right: return "right";
    case StairTurn::UTurnLeft: return "uturn-left";
    case StairTurn::UTurnRight: return "uturn-right";
    case StairTurn::Straight: break;
    }
    return "straight";
}

bool ParseStairTurn(const std::string& name, StairTurn& out)
{
    for (StairTurn turn : {StairTurn::Straight, StairTurn::Left, StairTurn::Right, StairTurn::UTurnLeft,
                           StairTurn::UTurnRight}) {
        if (name == StairTurnName(turn)) {
            out = turn;
            return true;
        }
    }
    return false;
}

bool ParseWallSide(const std::string& name, WallSide& out)
{
    for (WallSide side : {WallSide::PosX, WallSide::NegX, WallSide::PosZ, WallSide::NegZ}) {
        if (name == WallSideName(side)) {
            out = side;
            return true;
        }
    }
    return false;
}

std::string WriteShapeJson(const ConstructionShapeComponent& shape, const std::string& indent)
{
    const std::string in = indent + "  ";
    const std::string in2 = in + "  ";
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << indent << "\"ConstructionShape\": {\n";
    oss << in << "\"source\": \"" << SourceKindName(shape.source) << "\",\n";
    oss << in << "\"recipe\": \"" << RecipeName(shape.recipe) << "\",\n";
    oss << in << "\"role\": \"" << Escape(shape.role) << "\",\n";
    oss << in << "\"collision\": \"" << CollisionModeName(shape.collision) << "\",\n";
    oss << in << "\"operation\": \"" << OperationName(shape.operation) << "\",\n";
    oss << in << "\"version\": " << shape.evaluatorVersion << ",\n";
    oss << in << "\"params\": {\n";
    if (shape.recipe == Recipe::Box) {
        const BoxParams& b = shape.box;
        oss << in2 << "\"width\": " << Number(b.width) << ",\n";
        oss << in2 << "\"height\": " << Number(b.height) << ",\n";
        oss << in2 << "\"depth\": " << Number(b.depth) << ",\n";
        oss << in2 << "\"hollow\": " << (b.hollow ? "true" : "false") << ",\n";
        oss << in2 << "\"wallThickness\": " << Number(b.wallThickness) << ",\n";
        oss << in2 << "\"floor\": " << (b.floor ? "true" : "false") << ",\n";
        oss << in2 << "\"ceiling\": " << (b.ceiling ? "true" : "false") << "\n";
    } else if (shape.recipe == Recipe::Ramp) {
        const BoxParams& b = shape.box;
        oss << in2 << "\"width\": " << Number(b.width) << ",\n";
        oss << in2 << "\"height\": " << Number(b.height) << ",\n";
        oss << in2 << "\"depth\": " << Number(b.depth) << "\n";
    } else if (shape.recipe == Recipe::Plane || shape.recipe == Recipe::Cylinder || shape.recipe == Recipe::Sphere) {
        const BoxParams& b = shape.box;
        oss << in2 << "\"width\": " << Number(b.width) << ",\n";
        oss << in2 << "\"height\": " << Number(b.height) << ",\n";
        oss << in2 << "\"depth\": " << Number(b.depth) << ",\n";
        oss << in2 << "\"segments\": " << shape.segments << "\n";
    } else if (shape.recipe == Recipe::Wall) {
        const BoxParams& b = shape.box;
        oss << in2 << "\"height\": " << Number(b.height) << ",\n";
        oss << in2 << "\"thickness\": " << Number(b.wallThickness) << ",\n";
        oss << in2 << "\"floor\": " << (b.floor ? "true" : "false") << ",\n";
        oss << in2 << "\"closed\": " << (shape.wall.closed ? "true" : "false") << ",\n";
        oss << in2 << "\"points\": [";
        for (size_t n = 0; n < shape.wall.points.size(); ++n) {
            oss << (n ? ", " : "") << "[" << Number(shape.wall.points[n].x) << ", " << Number(shape.wall.points[n].z) << "]";
        }
        oss << "]\n";
    } else if (shape.recipe == Recipe::Stairs) {
        const BoxParams& b = shape.box;
        oss << in2 << "\"width\": " << Number(b.width) << ",\n";
        oss << in2 << "\"height\": " << Number(b.height) << ",\n";
        oss << in2 << "\"depth\": " << Number(b.depth) << ",\n";
        oss << in2 << "\"steps\": " << shape.stairs.steps << ",\n";
        oss << in2 << "\"maxRise\": " << Number(shape.stairs.maxRise) << ",\n";
        oss << in2 << "\"turn\": \"" << StairTurnName(shape.stairs.turn) << "\",\n";
        oss << in2 << "\"flightWidth\": " << Number(shape.stairs.flightWidth) << "\n";
    } else {
        const OpeningParams& o = shape.opening;
        oss << in2 << "\"side\": \"" << WallSideName(o.side) << "\",\n";
        oss << in2 << "\"offset\": " << Number(o.offset) << ",\n";
        oss << in2 << "\"sill\": " << Number(o.sill) << ",\n";
        oss << in2 << "\"width\": " << Number(o.width) << ",\n";
        oss << in2 << "\"height\": " << Number(o.height) << ",\n";
        oss << in2 << "\"segment\": " << o.segment << "\n";
    }
    oss << in << "}\n";
    oss << indent << "}";
    return oss.str();
}

JsonValue ShapeToJson(const ConstructionShapeComponent& shape)
{
    auto text = [](const std::string& value) {
        JsonValue v;
        v.type = JsonValue::Type::String;
        v.stringValue = value;
        return v;
    };
    auto number = [](double value) {
        JsonValue v;
        v.type = JsonValue::Type::Number;
        v.numberValue = value;
        return v;
    };
    auto boolean = [](bool value) {
        JsonValue v;
        v.type = JsonValue::Type::Bool;
        v.boolValue = value;
        return v;
    };
    JsonValue params;
    params.type = JsonValue::Type::Object;
    if (shape.recipe == Recipe::Box) {
        const BoxParams& b = shape.box;
        params.objectValue["width"] = number(b.width);
        params.objectValue["height"] = number(b.height);
        params.objectValue["depth"] = number(b.depth);
        params.objectValue["hollow"] = boolean(b.hollow);
        params.objectValue["wallThickness"] = number(b.wallThickness);
        params.objectValue["floor"] = boolean(b.floor);
        params.objectValue["ceiling"] = boolean(b.ceiling);
    } else if (shape.recipe == Recipe::Ramp) {
        params.objectValue["width"] = number(shape.box.width);
        params.objectValue["height"] = number(shape.box.height);
        params.objectValue["depth"] = number(shape.box.depth);
    } else if (shape.recipe == Recipe::Plane || shape.recipe == Recipe::Cylinder || shape.recipe == Recipe::Sphere) {
        params.objectValue["width"] = number(shape.box.width);
        params.objectValue["height"] = number(shape.box.height);
        params.objectValue["depth"] = number(shape.box.depth);
        params.objectValue["segments"] = number(shape.segments);
    } else if (shape.recipe == Recipe::Wall) {
        params.objectValue["height"] = number(shape.box.height);
        params.objectValue["thickness"] = number(shape.box.wallThickness);
        params.objectValue["floor"] = boolean(shape.box.floor);
        params.objectValue["closed"] = boolean(shape.wall.closed);
        JsonValue points;
        points.type = JsonValue::Type::Array;
        for (const WallPoint& point : shape.wall.points) {
            JsonValue pair;
            pair.type = JsonValue::Type::Array;
            pair.arrayValue = {number(point.x), number(point.z)};
            points.arrayValue.push_back(std::move(pair));
        }
        params.objectValue["points"] = std::move(points);
    } else if (shape.recipe == Recipe::Stairs) {
        params.objectValue["width"] = number(shape.box.width);
        params.objectValue["height"] = number(shape.box.height);
        params.objectValue["depth"] = number(shape.box.depth);
        params.objectValue["steps"] = number(shape.stairs.steps);
        params.objectValue["maxRise"] = number(shape.stairs.maxRise);
        params.objectValue["turn"] = text(StairTurnName(shape.stairs.turn));
        params.objectValue["flightWidth"] = number(shape.stairs.flightWidth);
    } else {
        const OpeningParams& o = shape.opening;
        params.objectValue["side"] = text(WallSideName(o.side));
        params.objectValue["offset"] = number(o.offset);
        params.objectValue["sill"] = number(o.sill);
        params.objectValue["width"] = number(o.width);
        params.objectValue["height"] = number(o.height);
        params.objectValue["segment"] = number(o.segment);
    }
    JsonValue out;
    out.type = JsonValue::Type::Object;
    out.objectValue["source"] = text(SourceKindName(shape.source));
    out.objectValue["recipe"] = text(RecipeName(shape.recipe));
    out.objectValue["role"] = text(shape.role);
    out.objectValue["collision"] = text(CollisionModeName(shape.collision));
    out.objectValue["operation"] = text(OperationName(shape.operation));
    out.objectValue["version"] = number(shape.evaluatorVersion);
    out.objectValue["params"] = std::move(params);
    return out;
}

bool ApplyShapeJson(ConstructionShapeComponent& shape, const JsonValue& data, std::string& outError)
{
    if (!data.IsObject()) {
        outError = "ConstructionShape component is not an object.";
        return false;
    }
    if (const JsonValue* source = data.Find("source"); source && source->IsString() &&
        source->AsString() != SourceKindName(SourceKind::Recipe)) {
        outError = "ConstructionShape source '" + source->AsString() + "' is not supported by this editor.";
        return false;
    }
    if (const JsonValue* version = data.Find("version"); version && version->IsNumber()) {
        shape.evaluatorVersion = static_cast<int>(version->AsNumber());
        if (shape.evaluatorVersion > kEvaluatorVersion) {
            outError = "ConstructionShape was saved by a newer editor (version " +
                       std::to_string(shape.evaluatorVersion) + ").";
            return false;
        }
    }
    if (const JsonValue* recipe = data.Find("recipe"); recipe && recipe->IsString()) {
        if (!ParseRecipe(recipe->AsString(), shape.recipe)) {
            outError = "Unknown ConstructionShape recipe '" + recipe->AsString() + "'.";
            return false;
        }
    }
    if (const JsonValue* role = data.Find("role"); role && role->IsString()) {
        shape.role = role->AsString();
    }
    if (const JsonValue* collision = data.Find("collision"); collision && collision->IsString()) {
        shape.collision = collision->AsString() == "none" ? CollisionMode::None : CollisionMode::Solid;
    }
    if (const JsonValue* operation = data.Find("operation"); operation && operation->IsString()) {
        shape.operation = operation->AsString() == "subtract" ? Operation::Subtract : Operation::Add;
    }
    if (const JsonValue* params = data.Find("params"); params && params->IsObject()) {
        if (shape.recipe == Recipe::Box) {
            BoxParams& b = shape.box;
            ReadFloat(*params, "width", b.width);
            ReadFloat(*params, "height", b.height);
            ReadFloat(*params, "depth", b.depth);
            ReadBool(*params, "hollow", b.hollow);
            ReadFloat(*params, "wallThickness", b.wallThickness);
            ReadBool(*params, "floor", b.floor);
            ReadBool(*params, "ceiling", b.ceiling);
        } else if (shape.recipe == Recipe::Ramp) {
            ReadFloat(*params, "width", shape.box.width);
            ReadFloat(*params, "height", shape.box.height);
            ReadFloat(*params, "depth", shape.box.depth);
        } else if (shape.recipe == Recipe::Plane || shape.recipe == Recipe::Cylinder || shape.recipe == Recipe::Sphere) {
            ReadFloat(*params, "width", shape.box.width);
            ReadFloat(*params, "height", shape.box.height);
            ReadFloat(*params, "depth", shape.box.depth);
            float segments = static_cast<float>(shape.segments);
            ReadFloat(*params, "segments", segments);
            shape.segments = static_cast<int>(std::lround(segments));
        } else if (shape.recipe == Recipe::Wall) {
            ReadFloat(*params, "height", shape.box.height);
            ReadFloat(*params, "thickness", shape.box.wallThickness);
            ReadBool(*params, "floor", shape.box.floor);
            ReadBool(*params, "closed", shape.wall.closed);
            if (const JsonValue* points = params->Find("points"); points && points->IsArray()) {
                std::vector<WallPoint> read;
                for (const JsonValue& pair : points->AsArray()) {
                    if (!pair.IsArray() || pair.AsArray().size() != 2 || !pair.AsArray()[0].IsNumber() ||
                        !pair.AsArray()[1].IsNumber()) {
                        outError = "Wall points must be [x, z] number pairs.";
                        return false;
                    }
                    read.push_back(WallPoint{static_cast<float>(pair.AsArray()[0].AsNumber()),
                                             static_cast<float>(pair.AsArray()[1].AsNumber())});
                }
                shape.wall.points = std::move(read);
            }
        } else if (shape.recipe == Recipe::Stairs) {
            ReadFloat(*params, "width", shape.box.width);
            ReadFloat(*params, "height", shape.box.height);
            ReadFloat(*params, "depth", shape.box.depth);
            float steps = static_cast<float>(shape.stairs.steps);
            ReadFloat(*params, "steps", steps);
            shape.stairs.steps = static_cast<int>(std::lround(steps));
            ReadFloat(*params, "maxRise", shape.stairs.maxRise);
            ReadFloat(*params, "flightWidth", shape.stairs.flightWidth);
            if (const JsonValue* turn = params->Find("turn"); turn && turn->IsString() &&
                !ParseStairTurn(turn->AsString(), shape.stairs.turn)) {
                outError = "Unknown stairs turn '" + turn->AsString() + "'.";
                return false;
            }
        } else {
            OpeningParams& o = shape.opening;
            if (const JsonValue* side = params->Find("side"); side && side->IsString()) {
                if (!ParseWallSide(side->AsString(), o.side)) {
                    outError = "Unknown opening side '" + side->AsString() + "'.";
                    return false;
                }
            }
            ReadFloat(*params, "offset", o.offset);
            ReadFloat(*params, "sill", o.sill);
            ReadFloat(*params, "width", o.width);
            ReadFloat(*params, "height", o.height);
            float segment = static_cast<float>(o.segment);
            ReadFloat(*params, "segment", segment);
            o.segment = static_cast<int>(std::lround(segment));
        }
    }
    shape.evaluatorVersion = kEvaluatorVersion;
    Sanitize(shape);
    return true;
}

void Sanitize(ConstructionShapeComponent& shape)
{
    BoxParams& b = shape.box;
    b.width = std::max(b.width, kMinSize);
    b.height = std::max(b.height, kMinSize);
    b.depth = std::max(b.depth, kMinSize);
    const float thinnest = shape.recipe == Recipe::Wall ? 20.0f : std::min({b.width, b.height, b.depth});
    b.wallThickness = std::clamp(b.wallThickness, kMinSize, std::max(kMinSize, thinnest * 0.5f - kMinSize));
    OpeningParams& o = shape.opening;
    o.width = std::max(o.width, kMinSize);
    o.height = std::max(o.height, kMinSize);
    o.sill = std::max(o.sill, 0.0f);
    o.segment = std::max(o.segment, 0);
    shape.wall.closed = shape.wall.closed && shape.wall.points.size() >= 3;
    shape.stairs.steps = std::clamp(shape.stairs.steps, 0, 500);
    shape.stairs.maxRise = std::max(shape.stairs.maxRise, 0.05f);
    const bool uTurn = shape.stairs.turn == StairTurn::UTurnLeft || shape.stairs.turn == StairTurn::UTurnRight;
    shape.stairs.flightWidth = std::clamp(shape.stairs.flightWidth, kMinSize,
                                          std::max(kMinSize, std::min(uTurn ? b.width * 0.5f : b.width, b.depth * 0.9f)));
    shape.segments = std::clamp(shape.segments, 3, 128);
}

const char* DisplayName(const ConstructionShapeComponent& shape)
{
    switch (shape.recipe) {
    case Recipe::Opening: return "Opening";
    case Recipe::Stairs: return "Stairs";
    case Recipe::Plane: return "Plane";
    case Recipe::Cylinder: return "Cylinder";
    case Recipe::Sphere: return "Sphere";
    case Recipe::Wall: return shape.wall.closed ? "Room" : "Wall";
    case Recipe::Ramp: return "Ramp";
    case Recipe::Box: break;
    }
    return shape.operation == Operation::Subtract ? "Subtract" : shape.box.hollow ? "Room" : "Block";
}

bool IsCutter(const ConstructionShapeComponent& shape)
{
    return shape.source == SourceKind::Recipe && shape.recipe == Recipe::Box && shape.operation == Operation::Subtract;
}

bool CanHostOpenings(const ConstructionShapeComponent& shape)
{
    return shape.source == SourceKind::Recipe &&
           ((shape.recipe == Recipe::Box && !IsCutter(shape)) || shape.recipe == Recipe::Wall);
}

int WallSegmentCount(const ConstructionShapeComponent& shape)
{
    const int points = static_cast<int>(shape.wall.points.size());
    if (shape.recipe != Recipe::Wall || points < 2) {
        return 0;
    }
    return shape.wall.closed ? points : points - 1;
}

bool HasOwnGeometry(const ConstructionShapeComponent& shape)
{
    return shape.source == SourceKind::Recipe && shape.recipe != Recipe::Opening;
}

int StairCount(const ConstructionShapeComponent& shape)
{
    if (shape.stairs.steps > 0) {
        return shape.stairs.steps;
    }
    return std::max(1, static_cast<int>(std::ceil(shape.box.height / std::max(shape.stairs.maxRise, 0.05f) - 1e-4f)));
}

} // namespace Hephaestus::Construction
