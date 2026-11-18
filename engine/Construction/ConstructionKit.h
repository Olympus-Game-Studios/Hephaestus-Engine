#pragma once

#include "ConstructionShape.h"

#include "../Core/Math.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Hephaestus::Construction {

// A prefab piece of a kit. Pieces stand on their origin with their width along +X, centred on X, and the wall's
// thickness centred on Z.
struct KitPiece {
    std::string prefab;          // project prefab path
    std::string role = "wall";   // wall, filler, corner, door or window
    float width = 1.0f;          // nominal width along X, metres
    float stretch = 0.05f;       // how far the width may stretch or shrink, as a fraction
    float openingMin = 0.0f;     // door and window pieces: the opening widths they frame
    float openingMax = 0.0f;
    bool variant = false;        // placed only where it is swapped in by hand, never chosen by the kit
};

// A .hpkit file: the prefab pieces that dress a wall.
struct KitDefinition {
    std::string name;
    std::vector<KitPiece> pieces;
};

bool LoadKit(const std::filesystem::path& file, KitDefinition& out, std::string& error);
bool SaveKit(const std::filesystem::path& file, const KitDefinition& kit, std::string& error);
// Problems with a kit's pieces: unknown roles, sizes that cannot work, frames without an opening range.
std::vector<std::string> ValidateKit(const KitDefinition& kit);

// The pieces chosen for one straight run: their widths along it, each stretched by the same factor.
struct RunFill {
    std::vector<const KitPiece*> pieces;
    float scale = 1.0f;     // actual width over nominal, the same for every piece in the run
    bool filled = false;    // false: nothing in the kit fits this length
};

// Packs a run of the given length with wall pieces (and at most one filler), choosing the combination whose uniform
// stretch is smallest and within every chosen piece's limit, then the fewest pieces.
RunFill SolveRun(const KitDefinition& kit, float length);

// One placed piece, or a gap the kit could not fill, in the host shape's local frame.
struct KitPlacement {
    const KitPiece* piece = nullptr;  // null: an unfilled gap of the given width
    const KitPiece* kitChoice = nullptr;  // what the kit chose before any swap
    // The spot this is, kept while the shape changes size: "<face>:c" corner, "<face>:o<n>" opening frame,
    // "<face>:r<run>.<n>" the nth piece of a run between corners and frames.
    std::string slot;
    Vec3 position;                    // bottom centre
    float yaw = 0.0f;                 // radians about Y; the piece's +X runs along the wall
    float scaleX = 1.0f;
    float width = 0.0f;
};

struct KitFillResult {
    std::vector<KitPlacement> placements;
    int pieces = 0;
    int fillers = 0;
    int gaps = 0;
    float maxStretch = 0.0f;  // largest |scale - 1| of any placed piece
    int swapped = 0;          // spots showing a piece swapped in by hand
    int staleSwaps = 0;       // swaps whose spot is gone or whose piece no longer fits it
};

// Lays a kit along every wall face of a box room or a wall path: corners at the joints, frames over openings, and
// solved runs in between.
KitFillResult FillShape(const ConstructionShapeComponent& shape, const std::vector<OpeningParams>& openings,
                        const KitDefinition& kit);

// Pieces that can replace one in its spot: the same role and width, the piece itself and variants included.
std::vector<const KitPiece*> SwapChoices(const KitDefinition& kit, const KitPiece& piece);
// How the editor names a spot, such as "Wall 2, piece 3".
std::string DescribeSlot(const std::string& slot);

// The extent of a piece prefab's meshes as placed (its local frame), for filling in a piece's width.
bool MeasurePiece(const std::string& prefab, AABB& bounds, std::string& error);

} // namespace Hephaestus::Construction
