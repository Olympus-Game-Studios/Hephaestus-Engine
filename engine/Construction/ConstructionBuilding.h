#pragma once

#include <entt/entt.hpp>

#include <string>

class Scene;

namespace Hephaestus::Construction {

// The room directly above a room, added by AddStorey (a child whose floor sits on top of its walls), or null.
entt::entity StoreyAbove(Scene& scene, entt::entity room);

// Stacks a copy of a room on top of it: same footprint, walls, kit and look, with its floor on the lower room's
// walls. The lower room's ceiling is removed, since the new floor replaces it. Returns the new room, or null with
// error for shapes that are not rooms.
entt::entity AddStorey(Scene& scene, entt::entity room, std::string& error);

// Puts a flight of stairs inside a room, along a free straight inside wall (turning back on itself when a straight
// flight does not fit), from its floor up to the floor of the storey above, and cuts a stairwell in that floor over
// them. maxRise is the tallest step allowed (the player's step height, with a margin). Returns the stairs, or null
// with error when there is no storey above or no room for the flight.
entt::entity AddStairsUp(Scene& scene, entt::entity room, float maxRise, std::string& error);

enum class RoofStyle { Flat, Pitched };

// Puts a roof on a room with no storey above it: a flat slab over its whole outline (any room), or two slopes meeting
// at a ridge along its longer side (box rooms), both reaching overhang metres past the walls (flat roofs on rooms
// drawn with walls have none). The room's ceiling is removed. Returns the roof (a child of the room), or null with
// error.
entt::entity AddRoof(Scene& scene, entt::entity room, RoofStyle style, float overhang, std::string& error);

// Builds a balcony outside a doorway: a slab level with the room's floor, depth metres out and as wide as the door
// plus a metre each side, with a railing along its three open edges. Returns the slab (a child of the room), or null
// with error for openings that are not doorways or that face into the room's own floor.
entt::entity AddBalcony(Scene& scene, entt::entity opening, float depth, std::string& error);

} // namespace Hephaestus::Construction
