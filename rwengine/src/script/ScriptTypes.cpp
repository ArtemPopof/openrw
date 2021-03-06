#include "script/ScriptTypes.hpp"

#include <cstddef>

#include "ai/PlayerController.hpp"
#include "engine/GameData.hpp"
#include "engine/GameState.hpp"
#include "engine/GameWorld.hpp"
#include "objects/CharacterObject.hpp"
#include "objects/InstanceObject.hpp"
#include "objects/PickupObject.hpp"
#include "objects/VehicleObject.hpp"
#include "script/SCMFile.hpp"
#include "script/ScriptMachine.hpp"

GameState* ScriptArguments::getState() const {
    return getVM()->getState();
}

GameWorld* ScriptArguments::getWorld() const {
    return getVM()->getState()->world;
}

int ScriptArguments::getModel(unsigned int arg) const {
    RW_CHECK(arg < getParameters().size(), "arg out of range");
    if (arg >= getParameters().size()) {
        return 0;
    }

    int id = getParameters()[arg].integerValue();

    /// @todo verify this behaviour
    if (id < 0) {
        id = -id;
        const auto& model = getVM()->getFile().getModels()[id];
        id = getWorld()->data->findModelObject(model);
    }

    return id;
}

GameObject* ScriptArguments::getPlayerCharacter(unsigned int player) const {
    auto playerId = parameters->at(player).integerValue();
    auto controller = getWorld()->players.at(playerId);
    RW_CHECK(controller != nullptr, "No controller for player " << player);
    RW_CHECK(controller->getCharacter(), "No character for player " << player);
    return controller->getCharacter();
}

// @todo figure out original cast
template <>
ScriptGarageType ScriptArguments::getParameter<ScriptGarageType>(unsigned int arg) const {
    return static_cast<GarageType>(getParameters().at(arg).integerValue());
}

template <>
GameObject* ScriptArguments::getObject<ai::PlayerController>(
    unsigned int arg) const {
    return getPlayerCharacter(arg);
}

template <>
GameObject* ScriptArguments::getObject<CharacterObject>(
    unsigned int arg) const {
    auto gameObjectID = parameters->at(arg).integerValue();
    auto object = getWorld()->pedestrianPool.find(gameObjectID);
    RW_CHECK(object != nullptr, "No pedestrian for ID " << gameObjectID);
    return object;
}

template <>
GameObject* ScriptArguments::getObject<CutsceneObject>(unsigned int arg) const {
    auto gameObjectID = parameters->at(arg).integerValue();
    auto object = getWorld()->cutscenePool.find(gameObjectID);
    RW_CHECK(object != nullptr, "No cutscene object for ID " << gameObjectID);
    return object;
}

template <>
GameObject* ScriptArguments::getObject<InstanceObject>(unsigned int arg) const {
    auto gameObjectID = parameters->at(arg).integerValue();
    auto object = getWorld()->instancePool.find(gameObjectID);
    RW_CHECK(object != nullptr, "No instance for ID " << gameObjectID);
    return object;
}

template <>
GameObject* ScriptArguments::getObject<PickupObject>(unsigned int arg) const {
    auto gameObjectID = parameters->at(arg).integerValue();
    auto object = getWorld()->pickupPool.find(gameObjectID);
    RW_CHECK(object != nullptr, "No pickup for ID " << gameObjectID);
    return object;
}

template <>
GameObject* ScriptArguments::getObject<VehicleObject>(unsigned int arg) const {
    auto gameObjectID = parameters->at(arg).integerValue();
    auto object = getWorld()->vehiclePool.find(gameObjectID);
    RW_CHECK(object != nullptr, "No pedestrian for ID " << gameObjectID);
    return object;
}

template <>
ScriptFloat ScriptArguments::getParameter<ScriptFloat>(unsigned int arg) const {
    return getParameters().at(arg).realValue();
}

template <>
ScriptInt ScriptArguments::getParameter<ScriptInt>(unsigned int arg) const {
    return getParameters().at(arg).integerValue();
}

template <>
const char* ScriptArguments::getParameter<const char*>(unsigned int arg) const {
    return getParameters().at(arg).string;
}

template <>
ScriptVec2 ScriptArguments::getParameter<ScriptVec2>(unsigned int arg) const {
    RW_CHECK(getParameters().size() > arg + 1,
             "Argument list too short to get vec2");
    return glm::vec2(getParameter<float>(arg), getParameter<float>(arg + 1));
}

template <>
ScriptVec3 ScriptArguments::getParameter<ScriptVec3>(unsigned int arg) const {
    RW_CHECK(getParameters().size() > arg + 2,
             "Argument list too short to get vec3");
    return glm::vec3(getParameter<float>(arg), getParameter<float>(arg + 1),
                     getParameter<float>(arg + 2));
}

template <>
ScriptRGB ScriptArguments::getParameter<ScriptRGB>(unsigned int arg) const {
    RW_CHECK(getParameters().size() > arg + 2,
             "Argument list too short to get u8vec3");
    return ScriptRGB(getParameter<int>(arg), getParameter<int>(arg + 1),
                     getParameter<int>(arg + 2));
}

template <>
ScriptRGBA ScriptArguments::getParameter<ScriptRGBA>(unsigned int arg) const {
    RW_CHECK(getParameters().size() > arg + 3,
             "Argument list too short to get u8vec4");
    return ScriptRGBA(getParameter<int>(arg), getParameter<int>(arg + 1),
                      getParameter<int>(arg + 2), getParameter<int>(arg + 3));
}

template <>
ScriptFloat& ScriptArguments::getParameterRef<ScriptFloat>(
    unsigned int arg) const {
    return *reinterpret_cast<ScriptFloat*>(
        getParameters().at(arg).handleValue());
}

template <>
ScriptInt& ScriptArguments::getParameterRef<ScriptInt>(unsigned int arg) const {
    return *reinterpret_cast<ScriptInt*>(getParameters().at(arg).handleValue());
}

template <>
ScriptObjectType<VehicleObject> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), getObject<VehicleObject>(arg)};
}
template <>
ScriptObjectType<InstanceObject> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), getObject<InstanceObject>(arg)};
}
template <>
ScriptObjectType<CharacterObject> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), getObject<CharacterObject>(arg)};
}
template <>
ScriptObjectType<PickupObject> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), getObject<PickupObject>(arg)};
}
template <>
ScriptObjectType<ai::PlayerController> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    /// @todo suport more than one player
    auto player = getState()->playerObject;
    auto object = getWorld()->pedestrianPool.find(player);
    ai::PlayerController* ctrl = nullptr;
    if (object) {
        auto playerObject = static_cast<CharacterObject*>(object);
        ctrl = static_cast<ai::PlayerController*>(playerObject->controller);
    }
    return {param.handleValue(), ctrl};
}
template <>
ScriptObjectType<VehicleGenerator> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    VehicleGenerator* generator = nullptr;
    auto& generators = getWorld()->state->vehicleGenerators;
    if (size_t(*param.handleValue()) < generators.size()) {
        generator = &generators[*param.handleValue()];
    }
    return {param.handleValue(), generator};
}
template <>
ScriptObjectType<Garage> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    auto& garages = getWorld()->garages;
    Garage* garage = nullptr;
    if (size_t(*param.handleValue()) < garages.size()) {
        garage = garages[*param.handleValue()].get();
    }
    return {param.handleValue(), garage};
}
template <>
ScriptObjectType<Payphone> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    RW_CHECK(*param.handleValue() >= 0, "Object index is negative");
    auto index = size_t(*param.handleValue());
    auto& payphones = getWorld()->payphones;
    Payphone* payphone = nullptr;
    if (index < payphones.size()) {
        payphone = payphones[index].get();
    }
    return {param.handleValue(), payphone};
}

template <>
ScriptObjectType<Sound> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), &getWorld()->sound.getSoundRef(arg)};
}

template <>
ScriptObjectType<BlipData> ScriptArguments::getScriptObject(
    unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    auto& blips = getWorld()->state->radarBlips;
    BlipData* blip = nullptr;
    auto it = blips.find(*param.handleValue());
    if (it != blips.end()) {
        blip = &it->second;
    }
    return {param.handleValue(), blip};
}
template <>
ScriptObjectType<int> ScriptArguments::getScriptObject(unsigned int arg) const {
    auto& param = (*this)[arg];
    RW_CHECK(param.isLvalue(), "Non lvalue passed as object");
    return {param.handleValue(), static_cast<int*>(nullptr)};
}
