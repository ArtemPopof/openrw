#include "Garage.hpp"

#ifdef _MSC_VER
#pragma warning(disable : 4305)
#endif
#include <btBulletDynamicsCommon.h>
#ifdef _MSC_VER
#pragma warning(default : 4305)
#endif

#include <glm/gtx/quaternion.hpp>

#include "ai/PlayerController.hpp"
#include "data/CollisionModel.hpp"
#include "dynamics/CollisionInstance.hpp"
#include "engine/GameState.hpp"
#include "objects/CharacterObject.hpp"
#include "objects/GameObject.hpp"
#include "objects/InstanceObject.hpp"
#include "objects/VehicleObject.hpp"

Garage::Garage(GameWorld* engine_, size_t id_, const glm::vec3& coord0,
               const glm::vec3& coord1, GarageType type_)
    : engine(engine_), id(id_), type(type_) {
    min.x = std::min(coord0.x, coord1.x);
    min.y = std::min(coord0.y, coord1.y);
    min.z = std::min(coord0.z, coord1.z);

    max.x = std::max(coord0.x, coord1.x);
    max.y = std::max(coord0.y, coord1.y);
    max.z = std::max(coord0.z, coord1.z);

    glm::vec2 midpoint;
    midpoint.x = (min.x + max.x) / 2;
    midpoint.y = (min.y + max.y) / 2;

    // Find door objects for this garage
    for (const auto& p : engine->instancePool.objects) {
        const auto inst = static_cast<InstanceObject*>(p.second.get());

        if (!inst->getModel()) {
            continue;
        }

        if (!SimpleModelInfo::isDoorModel(
                inst->getModelInfo<BaseModelInfo>()->name)) {
            continue;
        }

        const auto instPos = inst->getPosition();
        const auto xDist = std::abs(instPos.x - midpoint.x);
        const auto yDist = std::abs(instPos.y - midpoint.y);

        if (xDist < 20.f && yDist < 20.f) {
            if (!doorObject) {
                doorObject = inst;
                continue;
            } else {
                secondDoorObject = inst;
            }
        }
    }

    if (doorObject) {
        startPosition = doorObject->getPosition();

        // Setup door height based on model's bounding box
        auto doorModel = doorObject->getModelInfo<BaseModelInfo>();
        auto collision = doorModel->getCollision();
        // Original behavior - game actually subtracts 0.1f
        doorHeight =
            collision->boundingBox.max.z - collision->boundingBox.min.z - 0.1f;
    }

    if (secondDoorObject) {
        startPositionSecondDoor = secondDoorObject->getPosition();
    }

    step /= doorHeight;

    switch (type) {
        case GarageType::Mission:
        case GarageType::CollectCars1:
        case GarageType::CollectCars2:
        case GarageType::MissionForCarToComeOut:
        case GarageType::MissionKeepCar:
        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3:
        case GarageType::MissionToOpenAndClose:
        case GarageType::MissionForSpecificCar:
        case GarageType::MissionKeepCarAndRemainClosed: {
            state = GarageState::Closed;
            break;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3:
        case GarageType::Respray:
        case GarageType::Crusher: {
            state = GarageState::Opened;
            break;
        }
    }

    if (state == GarageState::Closed) {
        fraction = 0.f;
    } else {
        fraction = 1.f;
    }

    if (doorObject) {
        updateDoor();
    }
}

void Garage::makeDoorSwing() {
    // This is permanent, you can't restore it
    // back to non swing just like in original game
    // Values are from original game
    if (!swingType) {
        swingType = true;
        doorHeight /= 2.0f;
        doorHeight -= 0.1f;
    }
}

bool Garage::isTargetInsideGarage() const {
    return state == GarageState::Closed && isObjectInsideGarage(target);
}

void Garage::activate() {
    active = true;

    if (type == GarageType::MissionForCarToComeOut && state == GarageState::Closed) {
        state = GarageState::Opening;
    }
}

void Garage::deactivate() {
    active = false;
}

void Garage::open() {
    if (state == GarageState::Closed || state == GarageState::Closing) {
        state = GarageState::Opening;
    }
}

void Garage::close() {
    if (state == GarageState::Opened || state == GarageState::Opening) {
        state = GarageState::Closing;
    }
}

float Garage::getDistanceToGarage(const glm::vec3 point) {
    // Seems like original game ignores z axis
    float dx = std::max({min.x - point.x, 0.f, point.x - max.x});
    float dy = std::max({min.y - point.y, 0.f, point.y - max.y});

    return std::sqrt(dx * dx + dy * dy);
}

bool Garage::isObjectInsideGarage(GameObject* object) const {
    auto p = object->getPosition();

    // Do basic check first
    if (p.x < min.x) return false;
    if (p.y < min.y) return false;
    if (p.z < min.z) return false;
    if (p.x > max.x) return false;
    if (p.y > max.y) return false;
    if (p.z > max.z) return false;

    // Now check if all collision spheres are inside
    // garage's bounding box
    auto objectModel = object->getModelInfo<BaseModelInfo>();
    auto collision = objectModel->getCollision();
    // Peds don't have collisions currently?
    if (collision) {
        for (auto& sphere : collision->spheres) {
            auto c = p + sphere.center;
            auto r = sphere.radius;
            if (c.x + r < min.x) return false;
            if (c.y + r < min.y) return false;
            if (c.z + r < min.z) return false;
            if (c.x - r > max.x) return false;
            if (c.y - r > max.y) return false;
            if (c.z - r > max.z) return false;
        }
    }

    return true;
}

bool Garage::shouldClose() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;

    switch (type) {
        case GarageType::Mission: {
            if (!isObjectInsideGarage(static_cast<GameObject*>(plyChar)) &&
                isObjectInsideGarage(target) && !playerIsInVehicle &&
                getDistanceToGarage(playerPosition) >= 2.f) {
                return true;
            }

            return false;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3: {
            if (playerIsInVehicle) {
                if (isObjectInsideGarage(
                        static_cast<GameObject*>(playerVehicle)) &&
                    playerVehicle->isStopped()) {
                    return true;
                }
            }

            return false;
        }

        case GarageType::Respray: {
            if (playerIsInVehicle) {
                if (isObjectInsideGarage(
                        static_cast<GameObject*>(playerVehicle)) &&
                    playerVehicle->isStopped() && !resprayDone) {
                    return true;
                } else if (!isObjectInsideGarage(
                               static_cast<GameObject*>(playerVehicle)) &&
                           getDistanceToGarage(playerVehicle->getPosition()) >=
                               2.f &&
                           resprayDone) {
                    resprayDone = false;
                }
            }

            return false;
        }

        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            if (playerIsInVehicle) {
                if (isObjectInsideGarage(
                        static_cast<GameObject*>(playerVehicle))) {
                    if (playerVehicle->getLifetime() !=
                        GameObject::MissionLifetime) {
                        return true;
                    } else {
                        // @todo show message "come back when youre not busy"
                    }
                }
            }

            return false;
        }

        case GarageType::MissionForCarToComeOut: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Crusher: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            // Not sure about these values
            if ((!playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) >= 5.f) ||
                (playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) >= 10.f)) {
                return true;
            }

            return false;
        }

        case GarageType::MissionToOpenAndClose: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionForSpecificCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            // @todo unimplemented
            return false;
        }

        default: { return false; }
    }

    return false;
}

bool Garage::shouldOpen() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;

    switch (type) {
        case GarageType::Mission: {
            // Not sure about these values
            if (playerIsInVehicle &&
                getDistanceToGarage(playerPosition) < 8.f &&
                playerVehicle == target) {
                return true;
            }

            return false;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3:
        case GarageType::Respray: {
            if (garageTimer < engine->getGameTime()) {
                return true;
            }

            return false;
        }

        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionForCarToComeOut: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Crusher: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            // Not sure about these values
            if ((!playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) < 5.f) ||
                (playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) < 10.f)) {
                return true;
            }

            return false;
        }

        case GarageType::MissionToOpenAndClose: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionForSpecificCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            // @todo unimplemented
            return false;
        }

        default: { return false; }
    }

    return false;
}

bool Garage::shouldStopClosing() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;

    switch (type) {
        case GarageType::Mission:
        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3:
        case GarageType::Respray:
        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            return false;
        }

        case GarageType::MissionForCarToComeOut: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Crusher: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            // Not sure about these values
            if ((!playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) < 5.f) ||
                (playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) < 10.f)) {
                return true;
            }

            return false;
        }

        case GarageType::MissionToOpenAndClose: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionForSpecificCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            // @todo unimplemented
            return false;
        }

        default: { return false; }
    }

    return false;
}

bool Garage::shouldStopOpening() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;

    switch (type) {
        case GarageType::Mission:
        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3:
        case GarageType::Respray:
        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            return false;
        }

        case GarageType::MissionForCarToComeOut: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Crusher: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            // Not sure about these values
            if ((!playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) >= 5.f) ||
                (playerIsInVehicle &&
                 getDistanceToGarage(playerPosition) >= 10.f)) {
                return true;
            }

            return false;
        }

        case GarageType::MissionToOpenAndClose: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionForSpecificCar: {
            // @todo unimplemented
            return false;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            // @todo unimplemented
            return false;
        }

        default: { return false; }
    }

    return false;
}

void Garage::doOnOpenEvent() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;
    RW_UNUSED(playerPosition);
    RW_UNUSED(playerIsInVehicle);

    switch (type) {
        case GarageType::Mission: {
            break;
        }

        case GarageType::BombShop1: {
            break;
        }

        case GarageType::BombShop2: {
            break;
        }

        case GarageType::BombShop3: {
            break;
        }

        case GarageType::Respray: {
            break;
        }

        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            break;
        }

        case GarageType::MissionForCarToComeOut: {
            break;
        }

        case GarageType::Crusher: {
            break;
        }

        case GarageType::MissionKeepCar: {
            break;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            break;
        }

        case GarageType::MissionToOpenAndClose: {
            break;
        }

        case GarageType::MissionForSpecificCar: {
            break;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            break;
        }

        default: { break; }
    }
}

void Garage::doOnCloseEvent() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;
    RW_UNUSED(playerPosition);
    RW_UNUSED(playerIsInVehicle);

    switch (type) {
        case GarageType::Mission: {
            player->setInputEnabled(true);
            break;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3: {
            // Find out real value
            garageTimer = engine->getGameTime() + 1.5f;

            break;
        }

        case GarageType::Respray: {
            // Find out real value
            garageTimer = engine->getGameTime() + 2.f;
            playerVehicle->setHealth(1000.f);

            break;
        }

        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            break;
        }

        case GarageType::MissionForCarToComeOut: {
            break;
        }

        case GarageType::Crusher: {
            break;
        }

        case GarageType::MissionKeepCar: {
            break;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            break;
        }

        case GarageType::MissionToOpenAndClose: {
            break;
        }

        case GarageType::MissionForSpecificCar: {
            break;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            break;
        }

        default: { break; }
    }
}

void Garage::doOnStartOpeningEvent() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;
    RW_UNUSED(playerPosition);
    RW_UNUSED(playerIsInVehicle);

    switch (type) {
        case GarageType::Mission: {
            break;
        }

        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            player->setInputEnabled(true);
            break;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3: {
            player->setInputEnabled(true);
            playerVehicle->setHandbraking(false);
            break;
        }

        case GarageType::Respray: {
            player->setInputEnabled(true);
            playerVehicle->setHandbraking(false);
            resprayDone = true;
            break;
        }

        case GarageType::MissionForCarToComeOut: {
            break;
        }

        case GarageType::Crusher: {
            break;
        }

        case GarageType::MissionKeepCar: {
            break;
        }

        case GarageType::Hideout1:
        case GarageType::Hideout2:
        case GarageType::Hideout3: {
            break;
        }

        case GarageType::MissionToOpenAndClose: {
            break;
        }

        case GarageType::MissionForSpecificCar: {
            break;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            break;
        }

        default: { break; }
    }
}

void Garage::doOnStartClosingEvent() {
    auto player = engine->getPlayer();
    auto plyChar = player->getCharacter();
    auto playerPosition = plyChar->getPosition();
    auto playerVehicle = plyChar->getCurrentVehicle();
    bool playerIsInVehicle = playerVehicle != nullptr;
    RW_UNUSED(playerPosition);
    RW_UNUSED(playerIsInVehicle);

    switch (type) {
        case GarageType::Mission:
        case GarageType::CollectCars1:
        case GarageType::CollectCars2: {
            player->setInputEnabled(false);
            break;
        }

        case GarageType::BombShop1:
        case GarageType::BombShop2:
        case GarageType::BombShop3:
        case GarageType::Respray: {
            player->setInputEnabled(false);
            playerVehicle->setHandbraking(true);
            break;
        }

        case GarageType::MissionForCarToComeOut: {
            break;
        }

        case GarageType::Crusher: {
            break;
        }

        case GarageType::MissionKeepCar: {
            break;
        }

        case GarageType::MissionToOpenAndClose: {
            break;
        }

        case GarageType::MissionForSpecificCar: {
            break;
        }

        case GarageType::MissionKeepCarAndRemainClosed: {
            break;
        }

        default: { break; }
    }
}

void Garage::tick(float dt) {
    if (!doorObject) {
        return;
    }
    if (!active) {
        return;
    }

    needsToUpdate = false;

    switch (state) {
        case GarageState::Opened: {
            if (shouldClose()) {
                state = GarageState::Closing;
                doOnStartClosingEvent();
            }

            break;
        }

        case GarageState::Closed: {
            if (shouldOpen()) {
                state = GarageState::Opening;
                doOnStartOpeningEvent();
            }

            break;
        }

        case GarageState::Opening: {
            if (shouldStopOpening()) {
                state = GarageState::Closing;
            } else {
                fraction += dt * step;

                if (fraction >= 1.0f) {
                    state = GarageState::Opened;
                    fraction = 1.f;
                    doOnOpenEvent();
                }

                needsToUpdate = true;
            }

            break;
        }

        case GarageState::Closing: {
            if (shouldStopClosing()) {
                state = GarageState::Opening;
            } else {
                fraction -= dt * step;

                if (fraction <= 0.f) {
                    state = GarageState::Closed;
                    fraction = 0.f;
                    doOnCloseEvent();
                }

                needsToUpdate = true;
            }

            break;
        }

        default: { break; }
    }

    if (needsToUpdate) {
        updateDoor();
    }
}

void Garage::updateDoor() {
    if (swingType) {
        doorObject->setRotation(glm::angleAxis(fraction * glm::radians(90.f),
                                               glm::vec3(0.f, 1.f, 0.f)));

        if (secondDoorObject) {
            secondDoorObject->setRotation(glm::angleAxis(
                fraction * glm::radians(90.f), glm::vec3(0.f, 1.f, 0.f)));
        }
    }

    doorObject->setPosition(startPosition +
                            glm::vec3(0.f, 0.f, fraction * doorHeight));

    if (secondDoorObject) {
        secondDoorObject->setPosition(
            startPositionSecondDoor +
            glm::vec3(0.f, 0.f, fraction * doorHeight));
    }
}
