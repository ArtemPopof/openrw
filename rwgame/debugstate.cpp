#include "debugstate.hpp"
#include "game.hpp"
#include <objects/GTACharacter.hpp>

DebugState::DebugState()
{
	Menu *m = new Menu(getFont());
	m->offset = glm::vec2(50.f, 100.f);
	m->addEntry(Menu::lambda("Create Vehicle", [] {
		auto ch = getPlayerCharacter();
		if(! ch) return;

		auto fwd = ch->rotation * glm::vec3(0.f, 1.f, 0.f);

		glm::vec3 hit, normal;
		if(hitWorldRay({ch->position + fwd * 5.f}, {0.f, 0.f, -2.f}, hit, normal)) {
			// Pick random vehicle.
			auto it = getWorld()->vehicleTypes.begin();
			std::uniform_int_distribution<int> uniform(0, 3);
			for(size_t i = 0, n = uniform(getWorld()->randomEngine); i != n; i++) {
				it++;
			}

			auto spawnpos = hit + normal;
			getWorld()->createVehicle(it->first, spawnpos, glm::quat());
		}
	}));
	this->enterMenu(m);
}

void DebugState::enter()
{

}

void DebugState::exit()
{

}

void DebugState::tick(float dt)
{
	/*if(debugObject) {
		auto p = debugObject->getPosition();
		ss << "Position: " << p.x << " " << p.y << " " << p.z << std::endl;
		ss << "Health: " << debugObject->mHealth << std::endl;
		if(debugObject->model) {
			auto m = debugObject->model;
			ss << "Textures: " << std::endl;
			for(auto it = m->geometries.begin(); it != m->geometries.end();
				++it )
			{
				auto g = *it;
				for(auto itt = g->materials.begin(); itt != g->materials.end();
					++itt)
				{
					for(auto tit = itt->textures.begin(); tit != itt->textures.end();
						++tit)
					{
						ss << " " << tit->name << std::endl;
					}
				}
			}
		}
		if(debugObject->type() == GameObject::Vehicle) {
			GTAVehicle* vehicle = static_cast<GTAVehicle*>(debugObject);
			ss << "ID: " << vehicle->info->handling.ID << std::endl;
		}
	}*/
}

void DebugState::handleEvent(const sf::Event &e)
{
	switch(e.type) {
		case sf::Event::KeyPressed:
			switch(e.key.code) {
				case sf::Keyboard::Escape:
					StateManager::get().exit();
					break;
				default: break;
			}
			break;
		default: break;
	}
	State::handleEvent(e);
}