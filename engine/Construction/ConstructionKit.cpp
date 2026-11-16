#include "ConstructionKit.h"

#include "ConstructionGeometry.h"

#include "../Scene/Json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>

namespace Hephaestus::Construction {

namespace {

constexpr const char* kKitSchema = "HephaestusKit";

bool KnownRole(const std::string& role)
{
    return role == "wall" || role == "filler" || role == "corner" || role == "door" || role == "window";
}

std::string Escape(const std::string& value)
{
    std::string out;
    for (char c : value) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

// A straight stretch of wall to dress: from start along direction for length, the room's inside to its left.
struct Face {
    Vec3 start;
    Vec3 direction;
    float length = 0.0f;
    bool cornerAtStart = false;
    bool cornerAtEnd = false;
    std::vector<std::pair<float, const OpeningParams*>> openings;  // centre along the face
};

float YawFor(const Vec3& direction)
{
    return std::atan2(-direction.z, direction.x);
}

const KitPiece* FirstOfRole(const KitDefinition& kit, const std::string& role)
{
    for (const KitPiece& piece : kit.pieces) {
        if (piece.role == role) return &piece;
    }
    return nullptr;
}

const KitPiece* FrameFor(const KitDefinition& kit, const std::string& role, float openingWidth)
{
    for (const KitPiece& piece : kit.pieces) {
        if (piece.role == role && openingWidth >= piece.openingMin - 1e-4f && openingWidth <= piece.openingMax + 1e-4f) {
            return &piece;
        }
    }
    return nullptr;
}

std::vector<Face> FacesOf(const ConstructionShapeComponent& shape, const std::vector<OpeningParams>& openings)
{
    std::vector<Face> faces;
    if (shape.recipe == Recipe::Box && shape.box.hollow) {
        // The centre line of the room's walls, anticlockwise seen from above so the inside is on the left.
        const float t = shape.box.wallThickness;
        const float hx = (shape.box.width - t) * 0.5f;
        const float hz = (shape.box.depth - t) * 0.5f;
        const struct { WallSide side; Vec3 start; Vec3 direction; float length; } sides[] = {
            {WallSide::NegZ, Vec3(-hx, 0.0f, -hz), Vec3(1.0f, 0.0f, 0.0f), 2.0f * hx},
            {WallSide::PosX, Vec3(hx, 0.0f, -hz), Vec3(0.0f, 0.0f, 1.0f), 2.0f * hz},
            {WallSide::PosZ, Vec3(hx, 0.0f, hz), Vec3(-1.0f, 0.0f, 0.0f), 2.0f * hx},
            {WallSide::NegX, Vec3(-hx, 0.0f, hz), Vec3(0.0f, 0.0f, -1.0f), 2.0f * hz},
        };
        for (const auto& side : sides) {
            Face face{side.start, side.direction, side.length, true, true, {}};
            const WallFrame frame = GetWallFrame(shape.box, side.side);
            for (const OpeningParams& opening : openings) {
                if (opening.side == side.side) {
                    face.openings.push_back({side.length * 0.5f + opening.offset * vdot(frame.u, side.direction), &opening});
                }
            }
            faces.push_back(std::move(face));
        }
        return faces;
    }
    if (shape.recipe != Recipe::Wall) {
        return faces;
    }
    const int count = WallSegmentCount(shape);
    float area = 0.0f;
    const auto& points = shape.wall.points;
    for (size_t n = 0; n < points.size(); ++n) {
        const WallPoint& a = points[n];
        const WallPoint& b = points[(n + 1) % points.size()];
        area += a.x * b.z - b.x * a.z;
    }
    // Closed paths go anticlockwise so the inside of the room is on the left of every face.
    const bool reverse = shape.wall.closed && area < 0.0f;
    for (int i = 0; i < count; ++i) {
        WallSegmentFrame frame;
        if (!GetWallSegmentFrame(shape, i, frame)) {
            continue;
        }
        const Vec3 a = vsub(frame.mid, vscale(frame.d, frame.length * 0.5f));
        const Vec3 b = vadd(frame.mid, vscale(frame.d, frame.length * 0.5f));
        Face face;
        face.start = reverse ? b : a;
        face.direction = reverse ? vscale(frame.d, -1.0f) : frame.d;
        face.length = frame.length;
        const bool firstJoint = shape.wall.closed || i > 0;
        const bool lastJoint = shape.wall.closed || i + 1 < count;
        face.cornerAtStart = reverse ? lastJoint : firstJoint;
        face.cornerAtEnd = reverse ? firstJoint : lastJoint;
        for (const OpeningParams& opening : openings) {
            if (opening.segment == i) {
                const float along = frame.length * 0.5f + opening.offset;
                face.openings.push_back({reverse ? frame.length - along : along, &opening});
            }
        }
        faces.push_back(std::move(face));
    }
    return faces;
}

} // namespace

bool LoadKit(const std::filesystem::path& file, KitDefinition& out, std::string& error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "Cannot open kit " + file.string();
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), {});
    JsonValue root;
    if (!JsonParser(text).Parse(root, error) || !root.IsObject()) {
        error = "Kit " + file.filename().string() + " is not valid JSON: " + error;
        return false;
    }
    KitDefinition kit;
    if (const JsonValue* name = root.Find("name"); name && name->IsString()) kit.name = name->AsString();
    const JsonValue* pieces = root.Find("pieces");
    if (!pieces || !pieces->IsArray()) {
        error = "Kit " + file.filename().string() + " has no pieces list.";
        return false;
    }
    auto number = [](const JsonValue& object, const char* key, float& target) {
        if (const JsonValue* value = object.Find(key); value && value->IsNumber()) {
            target = static_cast<float>(value->AsNumber());
        }
    };
    for (const JsonValue& entry : pieces->AsArray()) {
        if (!entry.IsObject()) continue;
        KitPiece piece;
        if (const JsonValue* prefab = entry.Find("prefab"); prefab && prefab->IsString()) piece.prefab = prefab->AsString();
        if (const JsonValue* role = entry.Find("role"); role && role->IsString()) piece.role = role->AsString();
        number(entry, "width", piece.width);
        number(entry, "stretch", piece.stretch);
        number(entry, "openingMin", piece.openingMin);
        number(entry, "openingMax", piece.openingMax);
        kit.pieces.push_back(std::move(piece));
    }
    out = std::move(kit);
    return true;
}

bool SaveKit(const std::filesystem::path& file, const KitDefinition& kit, std::string& error)
{
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "{\n  \"schema\": \"" << kKitSchema << "\",\n  \"name\": \"" << Escape(kit.name) << "\",\n  \"pieces\": [\n";
    for (size_t n = 0; n < kit.pieces.size(); ++n) {
        const KitPiece& piece = kit.pieces[n];
        oss << "    {\"prefab\": \"" << Escape(piece.prefab) << "\", \"role\": \"" << Escape(piece.role)
            << "\", \"width\": " << piece.width << ", \"stretch\": " << piece.stretch;
        if (piece.role == "door" || piece.role == "window") {
            oss << ", \"openingMin\": " << piece.openingMin << ", \"openingMax\": " << piece.openingMax;
        }
        oss << "}" << (n + 1 < kit.pieces.size() ? "," : "") << "\n";
    }
    oss << "  ]\n}\n";
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << oss.str();
    if (!out) {
        error = "Cannot write kit " + file.string();
        return false;
    }
    return true;
}

std::vector<std::string> ValidateKit(const KitDefinition& kit)
{
    std::vector<std::string> problems;
    bool hasWall = false;
    for (size_t n = 0; n < kit.pieces.size(); ++n) {
        const KitPiece& piece = kit.pieces[n];
        const std::string label = "Piece " + std::to_string(n + 1) + " (" + piece.role + ")";
        if (!KnownRole(piece.role)) {
            problems.push_back(label + ": unknown role; use wall, filler, corner, door or window.");
        }
        if (piece.prefab.empty()) {
            problems.push_back(label + ": no prefab.");
        }
        if (!(piece.width > 0.01f)) {
            problems.push_back(label + ": width must be above 0.");
        }
        if (piece.stretch < 0.0f || piece.stretch >= 1.0f) {
            problems.push_back(label + ": stretch must be from 0 to below 1.");
        }
        if ((piece.role == "door" || piece.role == "window") &&
            !(piece.openingMax > 0.0f && piece.openingMin <= piece.openingMax)) {
            problems.push_back(label + ": give the opening widths it frames (openingMin to openingMax).");
        }
        hasWall = hasWall || piece.role == "wall";
    }
    if (!hasWall) {
        problems.push_back("The kit has no wall pieces.");
    }
    return problems;
}

RunFill SolveRun(const KitDefinition& kit, float length)
{
    RunFill best;
    if (!(length > 1e-3f)) {
        best.filled = true;
        return best;
    }
    std::vector<const KitPiece*> walls;
    std::vector<const KitPiece*> fillers{nullptr};
    for (const KitPiece& piece : kit.pieces) {
        if (piece.role == "wall") walls.push_back(&piece);
        if (piece.role == "filler") fillers.push_back(&piece);
    }
    if (walls.empty() && fillers.size() == 1) {
        return best;
    }
    // Reachable nominal totals of wall pieces in centimetres, keeping the fewest pieces for each total.
    float widest = 0.0f;
    for (const KitPiece* piece : walls) widest = std::max(widest, piece->stretch);
    const int limit = static_cast<int>(std::ceil(length * 100.0f / std::max(0.05f, 1.0f - std::min(widest, 0.95f)))) + 1;
    struct Cell {
        int count = -1;  // -1: unreachable
        int from = -1;
        const KitPiece* piece = nullptr;
        float stretch = 1.0f;  // smallest stretch limit among the pieces used
    };
    std::vector<Cell> cells(static_cast<size_t>(limit) + 1);
    cells[0].count = 0;
    for (int total = 0; total <= limit; ++total) {
        if (cells[static_cast<size_t>(total)].count < 0) continue;
        for (const KitPiece* piece : walls) {
            const int next = total + std::max(1, static_cast<int>(std::lround(piece->width * 100.0f)));
            if (next > limit) continue;
            Cell& cell = cells[static_cast<size_t>(next)];
            const float stretch = std::min(cells[static_cast<size_t>(total)].stretch, piece->stretch);
            const int count = cells[static_cast<size_t>(total)].count + 1;
            if (cell.count < 0 || count < cell.count || (count == cell.count && stretch > cell.stretch)) {
                cell = {count, total, piece, stretch};
            }
        }
    }
    float bestError = 0.0f;
    int bestCount = 0;
    int bestTotal = -1;
    const KitPiece* bestFiller = nullptr;
    for (const KitPiece* filler : fillers) {
        const int fillerWidth = filler ? std::max(1, static_cast<int>(std::lround(filler->width * 100.0f))) : 0;
        for (int total = 0; total <= limit; ++total) {
            const Cell& cell = cells[static_cast<size_t>(total)];
            if (cell.count < 0 || (total == 0 && !filler)) continue;
            const float nominal = static_cast<float>(total + fillerWidth) / 100.0f;
            const float scale = length / nominal;
            const float allowed = filler ? std::min(cell.count > 0 ? cell.stretch : 1.0f, filler->stretch) : cell.stretch;
            const float error = std::fabs(scale - 1.0f);
            if (error > allowed + 1e-4f) continue;
            const int count = cell.count + (filler ? 1 : 0);
            if (bestTotal < 0 || error < bestError - 1e-5f || (std::fabs(error - bestError) <= 1e-5f && count < bestCount)) {
                bestError = error;
                bestCount = count;
                bestTotal = total;
                bestFiller = filler;
                best.scale = scale;
            }
        }
    }
    if (bestTotal < 0) {
        return best;
    }
    for (int total = bestTotal; total > 0; total = cells[static_cast<size_t>(total)].from) {
        best.pieces.push_back(cells[static_cast<size_t>(total)].piece);
    }
    // Widest first along the run, with the filler in the middle so it reads as a deliberate break.
    std::sort(best.pieces.begin(), best.pieces.end(), [](const KitPiece* a, const KitPiece* b) { return a->width > b->width; });
    if (bestFiller) {
        best.pieces.insert(best.pieces.begin() + static_cast<std::ptrdiff_t>(best.pieces.size() / 2), bestFiller);
    }
    best.filled = true;
    return best;
}

KitFillResult FillShape(const ConstructionShapeComponent& shape, const std::vector<OpeningParams>& openings,
                        const KitDefinition& kit)
{
    KitFillResult result;
    const KitPiece* corner = FirstOfRole(kit, "corner");
    const float inset = corner ? corner->width * 0.5f : 0.0f;
    const float floorTop = HostFloorTop(shape);
    auto place = [&](const KitPiece* piece, const Face& face, float along, float width, float scale) {
        KitPlacement placement;
        placement.piece = piece;
        placement.position = vadd(face.start, vscale(face.direction, along));
        placement.yaw = YawFor(face.direction);
        placement.scaleX = scale;
        placement.width = width;
        result.placements.push_back(placement);
        if (!piece) {
            ++result.gaps;
            return;
        }
        ++result.pieces;
        result.fillers += piece->role == "filler" ? 1 : 0;
        result.maxStretch = std::max(result.maxStretch, std::fabs(scale - 1.0f));
    };
    std::vector<Face> faces = FacesOf(shape, openings);
    for (const Face& face : faces) {
        if (corner && face.cornerAtStart) {
            place(corner, face, 0.0f, corner->width, 1.0f);
        }
        // The stretches between corners and opening frames.
        std::vector<std::pair<float, float>> cuts;
        auto sorted = face.openings;
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [centre, opening] : sorted) {
            const bool door = opening->sill <= floorTop + 0.05f;
            const KitPiece* frame = FrameFor(kit, door ? "door" : "window", opening->width);
            const float width = frame ? frame->width : opening->width;
            place(frame, face, centre, width, 1.0f);
            cuts.push_back({centre - width * 0.5f, centre + width * 0.5f});
        }
        float from = face.cornerAtStart ? inset : 0.0f;
        const float to = face.length - (face.cornerAtEnd ? inset : 0.0f);
        cuts.push_back({to, to});
        for (const auto& [cutStart, cutEnd] : cuts) {
            const float runEnd = std::min(cutStart, to);
            if (runEnd - from > 1e-3f) {
                const RunFill fill = SolveRun(kit, runEnd - from);
                if (fill.filled) {
                    float at = from;
                    for (const KitPiece* piece : fill.pieces) {
                        const float width = piece->width * fill.scale;
                        place(piece, face, at + width * 0.5f, width, fill.scale);
                        at += width;
                    }
                } else {
                    place(nullptr, face, (from + runEnd) * 0.5f, runEnd - from, 1.0f);
                }
            }
            from = std::max(from, cutEnd);
        }
    }
    return result;
}

} // namespace Hephaestus::Construction
