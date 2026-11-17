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

// Puts a straight flight of stairs inside a room, along its longer inside wall, from its floor up to the floor of
// the storey above, and cuts a stairwell in that floor over them. maxRise is the tallest step allowed (the player's
// step height, with a margin). Returns the stairs, or null with error when there is no storey above or the room is
// too short for the flight.
entt::entity AddStairsUp(Scene& scene, entt::entity room, float maxRise, std::string& error);

} // namespace Hephaestus::Construction
