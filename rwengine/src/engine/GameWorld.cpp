#include <engine/GameWorld.hpp>
#include <loaders/LoaderIPL.hpp>
#include <loaders/LoaderIDE.hpp>
#include <ai/DefaultAIController.hpp>
#include <BulletCollision/CollisionDispatch/btGhostObject.h>
#include <render/Model.hpp>
#include <data/WeaponData.hpp>
#include <WorkContext.hpp>

#include <script/Opcodes3.hpp>
#include <script/ScriptMachine.hpp>

// 3 isn't enough to cause a factory.
#include <objects/CharacterObject.hpp>
#include <objects/InstanceObject.hpp>
#include <objects/VehicleObject.hpp>
#include <objects/CutsceneObject.hpp>

#include <data/CutsceneData.hpp>
#include <loaders/LoaderCutsceneDAT.hpp>

class WorldCollisionDispatcher : public btCollisionDispatcher
{
public:

	WorldCollisionDispatcher(btCollisionConfiguration* collisionConfiguration)
		: btCollisionDispatcher(collisionConfiguration)
	{}

	bool needsResponse(const btCollisionObject *obA, const btCollisionObject *obB) {
		if( !( obA->getUserPointer() && obB->getUserPointer() ) ) {
			return btCollisionDispatcher::needsResponse(obA, obB);
		}

		GameObject* a = static_cast<GameObject*>(obA->getUserPointer());
		GameObject* b = static_cast<GameObject*>(obB->getUserPointer());

		bool valA = a && a->type() == GameObject::Instance;
		bool valB = b && b->type() == GameObject::Instance;

		if( ! (valA && valB) &&	(valB || valA) ) {

			// Figure out which is the dynamic instance.
			InstanceObject* dynInst = nullptr;
			const btRigidBody* instBody = nullptr, * otherBody = nullptr;

			if( valA ) {
				dynInst = static_cast<InstanceObject*>(a);
				instBody = static_cast<const btRigidBody*>(obA);
				otherBody = static_cast<const btRigidBody*>(obB);
			}
			else {
				dynInst = static_cast<InstanceObject*>(b);
				instBody = static_cast<const btRigidBody*>(obB);
				otherBody = static_cast<const btRigidBody*>(obA);
			}

			if( dynInst->dynamics == nullptr || ! instBody->isStaticObject() ) {
				return btCollisionDispatcher::needsResponse(obA, obB);
			}

			// Attempt to determine relative velocity.
			auto dV  = (otherBody->getLinearVelocity());
			auto impulse = dV.length();

			// Ignore collision if the object is about to be uprooted.
			if(	dynInst->dynamics->uprootForce <= impulse / (otherBody->getInvMass()) ) {
				return false;
			}
		}
		return btCollisionDispatcher::needsResponse(obA, obB);
	}
};

GameWorld::GameWorld(const std::string& path)
	: gameTime(0.f), gameData(path), renderer(this), randomEngine(rand()),
	  _work( new WorkContext( this ) ), script(nullptr)
{
	gameData.engine = this;
}

GameWorld::~GameWorld()
{
	delete _work;

	for(auto o : objects) {
		delete o;
	}

	delete dynamicsWorld;
	delete solver;
	delete broadphase;
	delete collisionDispatcher;
	delete collisionConfig;

	/// @todo delete other things.
}

bool GameWorld::load()
{
	collisionConfig = new btDefaultCollisionConfiguration;
	collisionDispatcher = new WorldCollisionDispatcher(collisionConfig);
	broadphase = new btDbvtBroadphase();
	solver = new btSequentialImpulseConstraintSolver;
	dynamicsWorld = new btDiscreteDynamicsWorld(collisionDispatcher, broadphase, solver, collisionConfig);
	dynamicsWorld->setGravity(btVector3(0.f, 0.f, -9.81f));
	broadphase->getOverlappingPairCache()->setInternalGhostPairCallback(new btGhostPairCallback());
	gContactProcessedCallback = ContactProcessedCallback;
	dynamicsWorld->setInternalTickCallback(PhysicsTickCallback, this);

	gameData.load();

	return true;
}

void GameWorld::logInfo(const std::string& info)
{
	log.push_back({LogEntry::Info, gameTime, info});
	std::cout << info << std::endl;
}

void GameWorld::logError(const std::string& error)
{
	log.push_back({LogEntry::Error, gameTime, error});
}

void GameWorld::logWarning(const std::string& warning)
{
	log.push_back({LogEntry::Warning, gameTime, warning});
}

bool GameWorld::defineItems(const std::string& name)
{
	auto i = gameData.ideLocations.find(name);
	std::string path = name;
	
	if( i != gameData.ideLocations.end()) {
		path = i->second;
	}
	else {
		std::cout << "IDE not pre-listed" << std::endl;
	}
	
	LoaderIDE idel;
	
	if(idel.load(path)) {
		for( size_t o = 0; o < idel.OBJSs.size(); ++o) {
			objectTypes.insert({
				idel.OBJSs[o]->ID, 
				idel.OBJSs[o]
			});
		}
		
		for( size_t v = 0; v < idel.CARSs.size(); ++v) {
			vehicleTypes.insert({
				idel.CARSs[v]->ID,
				idel.CARSs[v]
			});
		}

		for( size_t v = 0; v < idel.PEDSs.size(); ++v) {
			pedestrianTypes.insert({
				idel.PEDSs[v]->ID,
				idel.PEDSs[v]
			});
		}

		for( size_t v = 0; v < idel.HIERs.size(); ++v) {
			cutsceneObjectTypes.insert({
				idel.HIERs[v]->ID,
				idel.HIERs[v]
			});
		}

		// Load AI information.
		for( size_t a = 0; a < idel.PATHs.size(); ++a ) {
			auto pathit = objectNodes.find(idel.PATHs[a]->ID);
			if( pathit == objectNodes.end() ) {
					objectNodes.insert({
															idel.PATHs[a]->ID,
															{idel.PATHs[a]}
													});
			}
			else {
					pathit->second.push_back(idel.PATHs[a]);
			}
		}
	}
	else {
		std::cerr << "Failed to load IDE " << path << std::endl;
	}
	
	return false;
}

void GameWorld::runScript(const std::string &name)
{
	SCMFile* f = gameData.loadSCM(name);
	if( f ) {
		if( script ) delete script;

		script = new ScriptMachine(this, f, new Opcodes3);
	}
	else {
		logError("Failed to load SCM: " + name);
	}
}

bool GameWorld::placeItems(const std::string& name)
{
	auto i = gameData.iplLocations.find(name);
	std::string path = name;
	
	if(i != gameData.iplLocations.end())
	{
		path = i->second;
	}
	else
	{
		std::cout << "IPL not pre-listed" << std::endl;
	}
	
	LoaderIPL ipll;

	if(ipll.load(path))
	{
		// Find the object.
		for( size_t i = 0; i < ipll.m_instances.size(); ++i) {
			std::shared_ptr<InstanceData> inst = ipll.m_instances[i];
			if(! createInstance(inst->id, inst->pos, inst->rot)) {
				std::cerr << "No object for instance " << inst->id << " Model: " << inst->model << " (" << path << ")" << std::endl;
			}
		}
		
		// Attempt to Associate LODs.
		for(GameObject* object : objects) {
			if( object->type() == GameObject::Instance ) {
				InstanceObject* instance = static_cast<InstanceObject*>(object);
				if( !instance->object->LOD ) {
					auto lodInstit = modelInstances.find("LOD" + instance->object->modelName.substr(3));
					if( lodInstit != modelInstances.end() ) {
						instance->LODinstance = lodInstit->second;
					}
				}
			}
		}
		
		return true;
	}
	else
	{
		std::cerr << "Failed to load IPL: " << path << std::endl;
		return false;
	}
	
	return false;
}

bool GameWorld::loadZone(const std::string& path)
{
	LoaderIPL ipll;

	if( ipll.load(path)) {
		if( ipll.zones.size() > 0) {
			for(auto& z : ipll.zones) {
				zones.insert({z.name, z});
			}
			std::cout << "Loaded " << ipll.zones.size() << " zones" << std::endl;
			return true;
		}
	}
	else {
		std::cerr << "Failed to load Zones " << path << std::endl;
	}
	
	return false;
}

InstanceObject *GameWorld::createInstance(const uint16_t id, const glm::vec3& pos, const glm::quat& rot)
{
	auto oi = objectTypes.find(id);
	if( oi != objectTypes.end()) {

		std::string modelname = oi->second->modelName;
		std::string texturename = oi->second->textureName;

		// Ensure the relevant data is loaded.
		if(! oi->second->modelName.empty()) {
			if( modelname != "null" ) {
				gameData.loadDFF(modelname + ".dff", true);
			}
		}
		if(! texturename.empty()) {
			gameData.loadTXD(texturename + ".txd", true);
		}

		ModelHandle* m = gameData.models[modelname];

		// Check for dynamic data.
		auto dyit = gameData.dynamicObjectData.find(oi->second->modelName);
		std::shared_ptr<DynamicObjectData> dydata;
		if( dyit != gameData.dynamicObjectData.end() ) {
			dydata = dyit->second;
		}

		if( modelname.empty() ) {
			logWarning("Instance with missing model: " + std::to_string(id));
		}
		
		auto instance = new InstanceObject(
			this,
			pos,
			rot,
			m,
			glm::vec3(1.f, 1.f, 1.f),
			oi->second, nullptr, dydata
		);

		objects.insert(instance);

		modelInstances.insert({
			oi->second->modelName,
			instance
		});

		return instance;
	}
	
	return nullptr;
}

#include <strings.h>
uint16_t GameWorld::findModelDefinition(const std::string model)
{
	// Dear C++ Why do I have to resort to strcasecmp this isn't C.
	auto defit = std::find_if(objectTypes.begin(), objectTypes.end(),
							  [&](const decltype(objectTypes)::value_type& d) { return strcasecmp(d.second->modelName.c_str(), model.c_str()) == 0; });
	if( defit != objectTypes.end() ) return defit->first;
	return -1;
}

#include <ai/PlayerController.hpp>
CutsceneObject *GameWorld::createCutsceneObject(const uint16_t id, const glm::vec3 &pos, const glm::quat &rot)
{
	std::string modelname;
	std::string texturename;

	/// @todo merge all object defintion types so we don't have to deal with this.
	auto ci = cutsceneObjectTypes.find(id);
	if( ci != cutsceneObjectTypes.end()) {
		modelname = state.specialModels[id];
		texturename = state.specialModels[id];
	}
	else {
		auto ii = objectTypes.find(id);
		if( ii != objectTypes.end() ) {
			modelname = ii->second->modelName;
			texturename = ii->second->textureName;
		}
		else {
			auto pi = pedestrianTypes.find(id);
			if( pi != pedestrianTypes.end() ) {
				modelname = pi->second->modelName;
				texturename = pi->second->textureName;

				static std::string specialPrefix("special");
				if(! modelname.compare(0, specialPrefix.size(), specialPrefix) ) {
					auto sid = modelname.substr(specialPrefix.size());
					unsigned short specialID = std::atoi(sid.c_str());
					modelname = state.specialCharacters[specialID];
					texturename = state.specialCharacters[specialID];
				}
			}
		}
	}

	if( id == 0 ) {
		modelname = state.player->getCharacter()->model->name;
	}

	// Ensure the relevant data is loaded.
	if( modelname.empty() ) {
		std::cerr << "Couldn't find model for id " << id << std::endl;
		return nullptr;
	}

	if( modelname != "null" ) {
		gameData.loadDFF(modelname + ".dff", false);
	}

	if(! texturename.empty()) {
		gameData.loadTXD(texturename + ".txd", true);
	}


	ModelHandle* m = gameData.models[modelname];

	auto instance = new CutsceneObject(
		this,
		pos,
		m);

	objects.insert(instance);


	return instance;
}

VehicleObject *GameWorld::createVehicle(const uint16_t id, const glm::vec3& pos, const glm::quat& rot)
{
	auto vti = vehicleTypes.find(id);
	if(vti != vehicleTypes.end()) {
		std::cout << "Creating Vehicle ID " << id << " (" << vti->second->gameName << ")" << std::endl;
		
		if(! vti->second->modelName.empty()) {
			gameData.loadDFF(vti->second->modelName + ".dff");
		}
		if(! vti->second->textureName.empty()) {
			gameData.loadTXD(vti->second->textureName + ".txd");
		}
		
		glm::u8vec3 prim(255), sec(128);
		auto palit = gameData.vehiclePalettes.find(vti->second->modelName); // modelname is conveniently lowercase (usually)
		if(palit != gameData.vehiclePalettes.end() && palit->second.size() > 0 ) {
			 std::uniform_int_distribution<int> uniform(0, palit->second.size()-1);
			 int set = uniform(randomEngine);
			 prim = gameData.vehicleColours[palit->second[set].first];
			 sec = gameData.vehicleColours[palit->second[set].second];
		}
		else {
			logWarning("No colour palette for vehicle " + vti->second->modelName);
		}
		
		auto wi = objectTypes.find(vti->second->wheelModelID);
		if( wi != objectTypes.end()) {
			if(! wi->second->textureName.empty()) {
				gameData.loadTXD(wi->second->textureName + ".txd");
			}
		}
		
		ModelHandle* m = gameData.models[vti->second->modelName];
		auto model = m->model;
		auto info = gameData.vehicleInfo.find(vti->second->handlingID);
		if(model && info != gameData.vehicleInfo.end()) {
			if( info->second->wheels.size() == 0 && info->second->seats.size() == 0 ) {
				for( const ModelFrame* f : model->frames ) {
					const std::string& name = f->getName();
					
					if( name.size() > 5 && name.substr(0, 5) == "wheel" ) {
						auto frameTrans = f->getMatrix();
						info->second->wheels.push_back({glm::vec3(frameTrans[3])});
					}
					if(name.size() > 3 && name.substr(0, 3) == "ped" && name.substr(name.size()-4) == "seat") {
						auto p = f->getDefaultTranslation();
						p.x = p.x * -1.f;
						info->second->seats.push_back({p});
						p.x = p.x * -1.f;
						info->second->seats.push_back({p});
					}
				}
			}
		}

		auto vehicle = new VehicleObject{ this, pos, rot, m, vti->second, info->second, prim, sec };

		objects.insert(vehicle);

		return vehicle;
	}
	return nullptr;
}

CharacterObject* GameWorld::createPedestrian(const uint16_t id, const glm::vec3 &pos, const glm::quat& rot)
{
	auto pti = pedestrianTypes.find(id);
	if( pti != pedestrianTypes.end() ) {
		auto& pt = pti->second;

		std::string modelname = pt->modelName;
		std::string texturename = pt->textureName;

		// Ensure the relevant data is loaded.
		if(! pt->modelName.empty()) {
			// Some model names have special meanings.
			/// @todo Should CharacterObjects handle this?
			static std::string specialPrefix("special");
			if(! modelname.compare(0, specialPrefix.size(), specialPrefix) ) {
				auto sid = modelname.substr(specialPrefix.size());
				unsigned short specialID = std::atoi(sid.c_str());
				modelname = state.specialCharacters[specialID];
				texturename = state.specialCharacters[specialID];
			}

			if( modelname != "null" ) {
				gameData.loadDFF(modelname + ".dff");
			}
		}
		if(! texturename.empty()) {
			gameData.loadTXD(texturename + ".txd");
		}

		ModelHandle* m = gameData.models[modelname];

		if(m && m->model) {
			auto ped = new CharacterObject( this, pos, rot, m, pt );
			objects.insert(ped);
			new DefaultAIController(ped);
			return ped;
		}
	}
	return nullptr;
}

void GameWorld::destroyObject(GameObject* object)
{
	auto iterator = objects.find(object);
	if( iterator != objects.end() ) {
		delete object;
		objects.erase(iterator);
	}
}

void GameWorld::destroyObjectQueued(GameObject *object)
{
	deletionQueue.push(object);
}

void GameWorld::destroyQueuedObjects()
{
	while( !deletionQueue.empty() ) {
		destroyObject( deletionQueue.front() );
		deletionQueue.pop();
	}
}

void GameWorld::doWeaponScan(const WeaponScan &scan)
{
	if( scan.type == WeaponScan::RADIUS ) {
		// TODO
		// Requires custom ConvexResultCallback
	}
	else if( scan.type == WeaponScan::HITSCAN ) {
		btVector3 from(scan.center.x, scan.center.y, scan.center.z),
				to(scan.end.x, scan.end.y, scan.end.z);
		glm::vec3 hitEnd = scan.end;
		btCollisionWorld::ClosestRayResultCallback cb(from, to);
		cb.m_collisionFilterGroup = btBroadphaseProxy::AllFilter;
		dynamicsWorld->rayTest(from, to, cb);
		// TODO: did any weapons penetrate?

		if( cb.hasHit() ) {
			GameObject* go = static_cast<GameObject*>(cb.m_collisionObject->getUserPointer());
			GameObject::DamageInfo di;
			hitEnd = di.damageLocation = glm::vec3(cb.m_hitPointWorld.x(),
								  cb.m_hitPointWorld.y(),
								  cb.m_hitPointWorld.z() );
			di.damageSource = scan.center;
			di.type = GameObject::DamageInfo::Bullet;
			di.hitpoints = scan.damage;
			go->takeDamage(di);
		}
	}
}

int GameWorld::getHour()
{
	return state.hour;
}

int GameWorld::getMinute()
{
	return state.minute;
}

glm::vec3 GameWorld::getGroundAtPosition(const glm::vec3 &pos) const
{
	btVector3 rayFrom(pos.x, pos.y, 100.f);
	btVector3 rayTo(pos.x, pos.y, -100.f);

	btDynamicsWorld::ClosestRayResultCallback rr(rayFrom, rayTo);

	dynamicsWorld->rayTest( rayFrom, rayTo, rr );

	if(rr.hasHit()) {
		auto& ws = rr.m_hitPointWorld;
		return { ws.x(), ws.y(), ws.z() };
	}

	return pos;
}

void handleVehicleResponse(GameObject* object, btManifoldPoint& mp, bool isA)
{
	bool isVehicle = object->type() == GameObject::Vehicle;
	if(! isVehicle) return;
	if( mp.getAppliedImpulse() <= 100.f ) return;

	btVector3 src, dmg;
	if(isA) {
		src = mp.getPositionWorldOnB();
		dmg = mp.getPositionWorldOnA();
	}
	else {
		src = mp.getPositionWorldOnA();
		dmg = mp.getPositionWorldOnB();
	}

	object->takeDamage({
							{dmg.x(), dmg.y(), dmg.z()},
							{src.x(), src.y(), src.z()},
							0.f,
							GameObject::DamageInfo::Physics,
							mp.getAppliedImpulse()
						});
}

bool GameWorld::ContactProcessedCallback(btManifoldPoint &mp, void *body0, void *body1)
{
	auto obA = static_cast<btCollisionObject*>(body0);
	auto obB = static_cast<btCollisionObject*>(body1);

	if( !( obA->getUserPointer() && obB->getUserPointer() ) ) {
		return false;
	}

	GameObject* a = static_cast<GameObject*>(obA->getUserPointer());
	GameObject* b = static_cast<GameObject*>(obB->getUserPointer());

	bool valA = a && a->type() == GameObject::Instance;
	bool valB = b && b->type() == GameObject::Instance;

	if( ! (valA && valB) &&	(valB || valA) ) {

		// Figure out which is the dynamic instance.
		InstanceObject* dynInst = nullptr;
		const btRigidBody* instBody = nullptr, * otherBody = nullptr;

		btVector3 src, dmg;

		if( valA ) {
			dynInst = static_cast<InstanceObject*>(a);
			instBody = static_cast<const btRigidBody*>(obA);
			otherBody = static_cast<const btRigidBody*>(obB);
			src = mp.getPositionWorldOnB();
			dmg = mp.getPositionWorldOnA();
		}
		else {
			dynInst = static_cast<InstanceObject*>(b);
			instBody = static_cast<const btRigidBody*>(obB);
			otherBody = static_cast<const btRigidBody*>(obA);
			src = mp.getPositionWorldOnA();
			dmg = mp.getPositionWorldOnB();
		}

		if( dynInst->dynamics != nullptr && instBody->isStaticObject() ) {
			// Attempt to determine relative velocity.
			auto dV  = (otherBody->getLinearVelocity());
			auto impulse = dV.length()/ (otherBody->getInvMass());

			if( dynInst->dynamics->uprootForce <= impulse ) {
				dynInst->takeDamage({
										{dmg.x(), dmg.y(), dmg.z()},
										{src.x(), src.y(), src.z()},
										0.f,
										GameObject::DamageInfo::Physics,
										impulse
									});
			}
		}
	}

	// Handle vehicles
	if(a) handleVehicleResponse(a, mp, true);
	if(b) handleVehicleResponse(b, mp, false);

	return true;
}

void GameWorld::PhysicsTickCallback(btDynamicsWorld *physWorld, btScalar timeStep)
{
	GameWorld* world = static_cast<GameWorld*>(physWorld->getWorldUserInfo());

	for( GameObject* object : world->objects ) {
		if( object->type() == GameObject::Vehicle ) {
			static_cast<VehicleObject*>(object)->tickPhysics(timeStep);
		}
	}
}

void GameWorld::loadCutscene(const std::string &name)
{
	std::string lowerName(name);
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

	auto datfile = gameData.openFile2(lowerName + ".dat");

	CutsceneData* cutscene = new CutsceneData;

	if( datfile ) {
		LoaderCutsceneDAT loaderdat;
		loaderdat.load(cutscene->tracks, datfile);
	}

	gameData.loadIFP(lowerName + ".ifp");

	cutsceneAudioLoaded = gameData.loadAudio(fgAudio, name+".mp3");

	if( state.currentCutscene ) {
		delete state.currentCutscene;
	}
	state.currentCutscene = cutscene;
	state.currentCutscene->meta.name = name;
	std::cout << "Loaded cutscene: " << name << std::endl;
}

void GameWorld::startCutscene()
{
	state.cutsceneStartTime = gameTime;
	if( cutsceneAudioLoaded ) {
		fgAudio.play();
	}
}

void GameWorld::clearCutscene()
{
	/// @todo replace with the queued deletion from the projectile branch
	for(auto o : objects) {
		if( o->type() == GameObject::Cutscene ) {
			destroyObjectQueued(o);
		}
	}

	fgAudio.stop();

	delete state.currentCutscene;
	state.currentCutscene = nullptr;
	state.cutsceneStartTime = -1.f;
}

void GameWorld::loadSpecialCharacter(const unsigned short index, const std::string &name)
{
	std::string lowerName(name);
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
	/// @todo a bit more smarter than this
	state.specialCharacters[index] = lowerName;
}

void GameWorld::loadSpecialModel(const unsigned short index, const std::string &name)
{
	std::string lowerName(name);
	std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
	/// @todo a bit more smarter than this
	state.specialModels[index] = lowerName;
}

