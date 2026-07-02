#pragma once

#include <entt/entt.hpp>
#include <string>

struct TanimEntityBinding
{
    entt::entity Entity = entt::null;
    std::string UID;
};
