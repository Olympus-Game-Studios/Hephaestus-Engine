#include "Entity.h"
#include "Scene.h"

Entity::Entity(entt::entity handle, Scene* scene)
    : m_EntityHandle(handle), m_Scene(scene)
{
}
