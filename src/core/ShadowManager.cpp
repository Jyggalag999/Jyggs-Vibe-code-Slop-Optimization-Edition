#define ShadowMapFarPlane 32768;
#include "ShadowFaceCull.h"
#include "GpuProfiler.h"

static int sLodPartFilter = -1;	// >= 0: only accumulate geometries whose hash falls in this quarter (SpreadLod)
static UInt32 sAccumObjects = 0;	// geometries accumulated since last reset (profiler count)

static bool TouchesShadowFace(NiAVObject* object, const NiPoint3* light,
                              const D3DXVECTOR3& direction) {
	NiGeometry* geometry = object->IsGeometry() ? static_cast<NiGeometry*>(object) : nullptr;
	if (geometry && geometry->skinInstance) return true;
	NiBound* bound = object->m_kWorldBound;
	if (!bound) return true;
	return ShadowSphereTouchesFace(bound->Center.x - light->x,
		bound->Center.y - light->y, bound->Center.z - light->z, bound->Radius,
		direction.x, direction.y, direction.z);
}

/*
* Initializes the Shadow Manager by grabbing the relevant settings and shaders, and setting up map sizes.
*/
void ShadowManager::Initialize() {
	
	Logger::Log("Starting the shadows manager...");
	TheShadowManager = new ShadowManager();

	// setup the shadow render passes for the shadowmaps
	TheShadowManager->geometryPass = new ShadowRenderPass();
	TheShadowManager->alphaPass = new AlphaShadowRenderPass();
	TheShadowManager->skinnedGeoPass = new SkinnedGeoShadowRenderPass();
	TheShadowManager->speedTreePass = new SpeedTreeShadowRenderPass();
	TheShadowManager->terrainLODPass = new TerrainLODPass();

	// load the shaders
	TheShadowManager->ShadowMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowMap.vso", "Shadows\\");
	TheShadowManager->ShadowMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowMap.pso", "Shadows\\");
	TheShadowManager->ShadowCubeMapVertex = (ShaderRecordVertex*)ShaderRecord::LoadShader("ShadowCubeMap.vso", "Shadows\\");
	TheShadowManager->ShadowCubeMapPixel = (ShaderRecordPixel*)ShaderRecord::LoadShader("ShadowCubeMap.pso", "Shadows\\");

    TheShadowManager->ShadowMapBlurVertex = (ShaderRecordVertex*) ShaderRecord::LoadShader("ShadowMapBlur.vso", "Shadows\\");
    TheShadowManager->ShadowMapBlurPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapBlur.pso", "Shadows\\");

	TheShadowManager->ShadowMapClearPixel = (ShaderRecordPixel*) ShaderRecord::LoadShader("ShadowMapClear.pso", "Shadows\\");

	// Make sure samplers are not reset on SetCT as that causes errors.
	// Null-safe: a missing shader file used to crash here before the check below could log it.
	ShaderRecord* shadowShaders[] = { TheShadowManager->ShadowMapVertex, TheShadowManager->ShadowMapPixel, TheShadowManager->ShadowCubeMapVertex,
		TheShadowManager->ShadowCubeMapPixel, TheShadowManager->ShadowMapBlurVertex, TheShadowManager->ShadowMapBlurPixel, TheShadowManager->ShadowMapClearPixel };
	for (ShaderRecord* shader : shadowShaders)
		if (shader) shader->ClearSamplers = false;

	TheShadowManager->ShadowShadersLoaded = true;
    if (TheShadowManager->ShadowMapVertex == nullptr || TheShadowManager->ShadowMapPixel == nullptr  || TheShadowManager->ShadowMapBlurVertex  == nullptr
        || TheShadowManager->ShadowCubeMapVertex == nullptr || TheShadowManager->ShadowCubeMapPixel == nullptr || TheShadowManager->ShadowMapBlurPixel  == nullptr ){
		TheShadowManager->ShadowShadersLoaded = false;
		Logger::Log("[ERROR]: Could not load one or more of the ShadowMap generation shaders. Reinstall the mod.");
    }

	UINT ShadowCubeMapSize = TheShaderManager->Effects.ShadowsExteriors->Settings.Interiors.ShadowCubeMapSize;
	TheShadowManager->ShadowCubeMapViewPort = { 0, 0, ShadowCubeMapSize, ShadowCubeMapSize, 0.0f, 1.0f };

	TheShadowManager->shadowMapsRenderTime = 0;
}


/*
* Returns the given object ref's NiNode if it passes the test for excluded form types, otherwise returns NULL.
*/
NiNode* ShadowManager::GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms) {
	
	if (!Ref) return NULL;
	NiNode* Node = Ref->GetNode();

	if (!Node) return NULL;
	if (Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows) return NULL;

	TESForm* Form = Ref->baseForm;
	UInt8 TypeID = Form->formType;
	switch (TypeID) {
	case TESForm::FormType::kFormType_Land:
		return NULL; // land is handled separately
		break;
	case TESForm::FormType::kFormType_Activator:
		if (!Forms->Activators) return NULL; 
		break;
	case TESForm::FormType::kFormType_Apparatus:
		if (!Forms->Apparatus) return NULL;
		break;
	case TESForm::FormType::kFormType_Book:
		if (!Forms->Books) return NULL;
		break;
	case TESForm::FormType::kFormType_Container:
		if (!Forms->Containers) return NULL;
		break;
	case TESForm::FormType::kFormType_Door:
		if (!Forms->Doors) return NULL;
		break;
	case TESForm::FormType::kFormType_Misc:
		if (!Forms->Misc) return NULL;
		break;
	case TESForm::FormType::kFormType_Tree:
		if (!Forms->Trees) return NULL;
		break;
	case TESForm::FormType::kFormType_Furniture:
		if (!Forms->Furniture) return NULL;
		break;
	case TESForm::FormType::kFormType_NPC:
	case TESForm::FormType::kFormType_Creature:
	case TESForm::FormType::kFormType_LeveledCreature:
		if (!Forms->Actors) return NULL;  // D3D9 breaks on actors for some unknown reason
		break;
	case TESForm::FormType::kFormType_Stat:
	case TESForm::FormType::kFormType_StaticCollection:
	case TESForm::FormType::kFormType_MoveableStatic:
		//return NULL;
		if (!Forms->Statics) return NULL;
		break;
	default:
		break;
	}

	ExtraRefractionProperty* RefractionExtraProperty = (ExtraRefractionProperty*)Ref->extraDataList.GetExtraData(BSExtraData::ExtraDataType::kExtraData_RefractionProperty);
	float Refraction = RefractionExtraProperty ? (1 - RefractionExtraProperty->refractionAmount) : 0.0f;
	if (Refraction >= 0.5) return NULL;

	return Node;
}


// Detech shader flags that we do not want.
bool ShadowManager::CheckShaderFlags(NiGeometry* Geometry) {
	BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(Geometry->GetProperty(NiProperty::kType_Shade));

	if (!shaderProp)
		return false;

	return !(shaderProp->GetFlag(BSSP_REFRACTION) ||
		shaderProp->GetFlag(BSSP_FIRE_REFRACTION) ||
		shaderProp->GetFlag(BSSP_DECAL) ||
		shaderProp->GetFlag(BSSP_DYNAMIC_DECAL));
}


// Detect which pass the object must be added to
void ShadowManager::AccumObject(std::stack<NiAVObject*>* containersAccum, NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand) {
	auto timelog = TimeLogger();

	NiGeometry* geo = static_cast<NiGeometry*>(NiObject);
	if (!geo->shader) return; // skip Geometry without a shader

	if (!CheckShaderFlags(geo))
		return;

	if (sLodPartFilter >= 0 && (int)((((UInt32)((uintptr_t)geo >> 4)) * 2654435761u) >> 30) != sLodPartFilter)
		return;

	sAccumObjects++;

#if defined(OBLIVION)
	if (geo->m_pcName && !memcmp(geo->m_pcName, "Torch", 5)) return; // No torch geo, it is too near the light and a bad square is rendered.
#endif

	if (skinnedGeoPass->AccumObject(geo)) {}
	else if (speedTreePass->AccumObject(geo)) {}
	else if (Forms->Lod && isLODLand && terrainLODPass->AccumObject(geo)) {}
	else if (Forms->AlphaEnabled && alphaPass->AccumObject(geo)) {}
	else geometryPass->AccumObject(geo);

	//timelog.LogTime("ShadowManager::AccumObject");
}


// go through the Object children and sort the ones that will be rendered based on their properties
void ShadowManager::AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes *arPlanes) {
	if (!NiObject) return;

	std::stack<NiAVObject*> containers;
	NiAVObject* child;
	NiAVObject* object;
	NiNode* Node;

	//list all objects contained, or sort the object if not a container
	if (!NiObject->IsGeometry())
		containers.push(NiObject);
	else
		AccumObject(&containers, NiObject, Forms, isLand && isLOD);
		

	// Gather geometry
	while (!containers.empty()) {
    	object = containers.top();
    	containers.pop();

		if (!object) continue;

		Node = object->IsNiNode();
    	if (!Node || Node->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling containers
		if (!isLand && Node->GetWorldBoundRadius() < Forms->MinRadius) continue;

		if (Node->IsKindOf<NiSwitchNode>()) {
			// NiSwitchNode - only render active children (if exists) to the shadow map.
			NiSwitchNode* SwitchNode = static_cast<NiSwitchNode*>(Node);
			if (SwitchNode->m_iIndex < 0)
				continue;

			child = Node->m_children.data[SwitchNode->m_iIndex];
			if (!child->IsGeometry())
				containers.push(child);
			else
				AccumObject(&containers, child, Forms, false);
			continue;
		}

		for (int i = 0; i < Node->m_children.end; i++) {
			child = Node->m_children.data[i];
			if (!child || child->m_flags & NiAVObject::NiFlags::APP_CULLED) continue; // culling children
			if (!isLand && child->GetWorldBoundRadius() < Forms->MinRadius) continue;

			// Frustum culling.
			if (arPlanes && (isLand || isLOD)) {
				BSMultiBoundNode* multibound = child->IsMultiBoundNode();

				if (multibound && !multibound->spMultiBound->spShape->WithinFrustum(*arPlanes)) continue;
			}
			else if (arPlanes && !child->WithinFrustum(arPlanes)) continue;

			if (child->IsFadeNode() && static_cast<BSFadeNode*>(child)->FadeAlpha < 0.75f) continue; // stop rendering fadenodes below a certain opacity
			if (!child->IsGeometry())
				containers.push(child);
			else
				AccumObject(&containers, child, Forms, isLand && isLOD);
		}
	}
}

// Go through accumulations and render found objects
void ShadowManager::RenderAccums() {
	geometryPass->RenderAccum();
	terrainLODPass->RenderAccum();
	alphaPass->RenderAccum();
	skinnedGeoPass->RenderAccum();
	speedTreePass->RenderAccum();
}


// The form type filter of GetRefNode.
bool ShadowManager::FormTypeAllowed(UInt8 TypeID, ShadowsExteriorEffect::FormsStruct* Forms) {
	switch (TypeID) {
	case TESForm::FormType::kFormType_Land: return false;
	case TESForm::FormType::kFormType_Activator: return Forms->Activators;
	case TESForm::FormType::kFormType_Apparatus: return Forms->Apparatus;
	case TESForm::FormType::kFormType_Book: return Forms->Books;
	case TESForm::FormType::kFormType_Container: return Forms->Containers;
	case TESForm::FormType::kFormType_Door: return Forms->Doors;
	case TESForm::FormType::kFormType_Misc: return Forms->Misc;
	case TESForm::FormType::kFormType_Tree: return Forms->Trees;
	case TESForm::FormType::kFormType_Furniture: return Forms->Furniture;
	case TESForm::FormType::kFormType_NPC:
	case TESForm::FormType::kFormType_Creature:
	case TESForm::FormType::kFormType_LeveledCreature: return Forms->Actors;
	case TESForm::FormType::kFormType_Stat:
	case TESForm::FormType::kFormType_StaticCollection:
	case TESForm::FormType::kFormType_MoveableStatic: return Forms->Statics;
	default: return true;
	}
}

/*
* SharedShadowWalk: does the part of GetRefNode that doesn't depend on the cascade (node, NotCastShadows, refraction)
* once per frame for every ref of every exterior cell, in the order AccumExteriorCell visits them. Each cascade then only
* applies its own form filter (FormTypeAllowed), frustum test and AccumChildren, so it accumulates exactly what it did.
*/
void ShadowManager::BuildSharedWalk() {
	sharedRefs.clear();
	sharedCells.clear();
	GridCellArray* CellArray = Tes->gridCellArray;
	UInt32 CellArraySize = CellArray->size * CellArray->size;

	for (UInt32 i = 0; i < CellArraySize; i++) {
		TESObjectCELL* Cell = CellArray->GetCell(i);
		if (!Cell || Cell->IsInterior()) continue;

		SharedCell cell = { Cell->GetChildNode(TESObjectCELL::kCellNode_Land), (UInt32)sharedRefs.size(), 0 };
		TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First;
		while (Entry) {
			TESObjectREFR* Ref = Entry->item;
			Entry = Entry->next;
			if (!Ref) continue;
			NiNode* Node = Ref->GetNode();
			if (!Node) continue;
			if (Ref->flags & TESForm::FormFlags::kFormFlags_NotCastShadows) continue;
			UInt8 TypeID = Ref->baseForm->formType;
			ExtraRefractionProperty* RefractionExtraProperty = (ExtraRefractionProperty*)Ref->extraDataList.GetExtraData(BSExtraData::ExtraDataType::kExtraData_RefractionProperty);
			float Refraction = RefractionExtraProperty ? (1 - RefractionExtraProperty->refractionAmount) : 0.0f;
			if (Refraction >= 0.5) continue;
			sharedRefs.push_back({ Node, TypeID });
		}
		cell.count = (UInt32)sharedRefs.size() - cell.first;
		sharedCells.push_back(cell);
	}
}

void ShadowManager::ReleaseLodTarget() {
	if (LodRT) { LodRT->Release(); LodRT = nullptr; }
	if (LodDepth) { LodDepth->Release(); LodDepth = nullptr; }
	lodRTSize = 0;
	lodBuildValid = false;
}

// SpreadLod buffers: one cascade-sized target and depth, multisampled like the atlas. False (and SpreadLod off until restart) if they can't be made.
bool ShadowManager::EnsureLodTarget() {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	IDirect3DDevice9* Device = TheRenderManager->device;
	const UInt32 size = (UInt32)Shadows->ShadowMaps[MapLod].ShadowMapViewPort.Width;
	const bool msaa = Shadows->ShadowAtlasSurfaceMSAA != nullptr;
	if (LodRT && LodDepth && lodRTSize == size && lodRTMsaa == msaa) return true;

	ReleaseLodTarget();
	const D3DMULTISAMPLE_TYPE samples = msaa ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE;
	if (FAILED(Device->CreateRenderTarget(size, size, Shadows->Settings.ShadowMaps.Format, samples, 0, 0, &LodRT, NULL)) ||
		FAILED(Device->CreateDepthStencilSurface(size, size, D3DFMT_D24S8, samples, 0, true, &LodDepth, NULL))) {
		ReleaseLodTarget();
		lodSpreadBroken = true;
		Logger::Log("[WARNING] Shadow maps: could not create the LOD cascade buffer, SpreadLod is off until restart.");
		return false;
	}
	lodRTSize = size;
	lodRTMsaa = msaa;
	return true;
}

/*
* SpreadLod: one frame of the LOD cascade's four-frame cycle. Part 0 sets up the light view-projection and culling planes
* for the cycle; each part draws a quarter of the objects (by pointer hash) into the LOD buffer with that fixed light
* matrix, moved by the camera's movement since part 0. Part 3 finishes the buffer: its matrix becomes the sampled one.
* Between swaps the sampled matrix follows the camera exactly like on a skipped LimitFrequency frame. Returns true when
* the buffer is complete this frame (the caller copies it into the atlas).
*/
bool ShadowManager::RenderSpreadLodPart(ShadowsExteriorEffect* Shadows, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir) {
	IDirect3DDevice9* Device = TheRenderManager->device;
	const D3DXVECTOR3 camera = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();

	// Sampled map: follow the camera.
	D3DXMATRIX translation;
	D3DXVECTOR3 difference = camera - ShadowMap->CameraTranslation;
	D3DXMatrixTranslation(&translation, difference.x, difference.y, difference.z);
	ShadowMap->ShadowCameraToLight = translation * ShadowMap->ShadowCameraToLight;
	ShadowMap->CameraTranslation = camera;

	const int part = FrameCounter % 4;
	if (part == 0) lodBuildValid = true;
	else if (!lodBuildValid || part != lodLastPart + 1) lodBuildValid = false;
	if (!lodBuildValid) {
		Shadows->Constants.ShadowBlur.y = Shadows->ShadowAtlasSurfaceMSAA ? 1.0f : 0.0f;
		return false;
	}
	lodLastPart = part;

	const D3DXMATRIX sampledMatrix = ShadowMap->ShadowCameraToLight;
	const D3DXMATRIX savedViewProj = Shadows->Constants.ShadowViewProj;
	const D3DVIEWPORT9 atlasViewport = ShadowMap->ShadowMapViewPort;

	D3DXMATRIX viewProj;
	if (part == 0) {
		viewProj = Shadows->GetCascadeViewProj(ShadowMap, SunDir);	// also sets the culling planes and CameraTranslation
		lodBuildViewProj = viewProj;
		lodBuildCam = ShadowMap->CameraTranslation;
		lodBuildPlanes = ShadowMap->ShadowMapFrustumPlanes;
		ShadowMap->CameraTranslation = camera;
	}
	else {
		D3DXVECTOR3 moved = camera - lodBuildCam;
		D3DXMatrixTranslation(&translation, moved.x, moved.y, moved.z);
		viewProj = translation * lodBuildViewProj;
		ShadowMap->ShadowMapFrustumPlanes = lodBuildPlanes;
	}

	ShadowMap->ShadowMapViewPort = { 0, 0, atlasViewport.Width, atlasViewport.Height, 0.0f, 1.0f };
	Device->SetRenderTarget(0, LodRT);
	Device->SetDepthStencilSurface(LodDepth);
	Shadows->Constants.ShadowViewProj = viewProj;
	{
		GpuScope partScope(Device, "Sun: lod part");
		sLodPartFilter = part;
		RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj, "Sun walk: lod", "Sun objects: lod", part == 0);
		sLodPartFilter = -1;
	}

	// Back to the atlas, as the other cascades leave it.
	ShadowMap->ShadowMapViewPort = atlasViewport;
	Device->SetRenderTarget(0, Shadows->ShadowAtlasSurfaceMSAA ? Shadows->ShadowAtlasSurfaceMSAA : Shadows->ShadowAtlasSurface);
	Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
	Device->SetViewport(&atlasViewport);
	Shadows->Constants.ShadowViewProj = savedViewProj;

	if (part == 3) {
		ShadowMap->CameraTranslation = camera;	// ShadowCameraToLight is the cycle's matrix moved to this camera
		return true;
	}
	ShadowMap->ShadowCameraToLight = sampledMatrix;
	Shadows->Constants.ShadowBlur.y = Shadows->ShadowAtlasSurfaceMSAA ? 1.0f : 0.0f;
	return false;
}

void ShadowManager::RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj, const char* WalkName, const char* CountName, bool ClearTarget) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;

	ShadowMap->ShadowCameraToLight = (*ViewProj);
	TheCameraManager->SetFrustum(&ShadowMap->ShadowMapFrustum, ViewProj);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_DEPTHBIAS, (DWORD)0.0f, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, (DWORD)0.0f, RenderStateArgs);

	const LONGLONG walkStart = WalkName ? GpuProfiler::Tick() : 0;
	sAccumObjects = 0;

	if (ShadowMap->Forms.Lod) {
		AccumChildren(BGSTerrainManager::GetRootLandLODNode(), &ShadowMap->Forms, true, true, &ShadowMap->ShadowMapFrustumPlanes);
		AccumChildren(BGSTerrainManager::GetRootObjectLODNode(), &ShadowMap->Forms, false, true, &ShadowMap->ShadowMapFrustumPlanes);
	}

	if (sharedWalkActive) {
		for (const SharedCell& cell : sharedCells) {
			if (ShadowMap->Forms.Terrain)
				AccumChildren(cell.land, &ShadowMap->Forms, true, false, &ShadowMap->ShadowMapFrustumPlanes);

			for (UInt32 r = cell.first; r < cell.first + cell.count; r++) {
				const SharedRef& ref = sharedRefs[r];
				if (!FormTypeAllowed(ref.type, &ShadowMap->Forms)) continue;
				if (ref.node->WithinFrustum(&ShadowMap->ShadowMapFrustumPlanes))
					AccumChildren(ref.node, &ShadowMap->Forms, false, false, &ShadowMap->ShadowMapFrustumPlanes);
			}
		}
	}
	else if (Player->GetWorldSpace()) {
		GridCellArray* CellArray = Tes->gridCellArray;
		UInt32 CellArraySize = CellArray->size * CellArray->size;

		for (UInt32 i = 0; i < CellArraySize; i++) {
			AccumExteriorCell(CellArray->GetCell(i), ShadowMap);
		}
	}
	else {
		AccumExteriorCell(Player->parentCell, ShadowMap);
	}

	if (WalkName) GpuProfiler::AddCpu(WalkName, walkStart);
	if (CountName) GpuProfiler::AddCount(CountName, sAccumObjects);

	Device->SetViewport(&ShadowMap->ShadowMapViewPort);
	if (ClearTarget) {
		Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

		if (ShadowMap->CustomClearRequired)
			ClearShadowCascade(&ShadowMap->ShadowMapViewPort, &ShadowMap->ClearColor);
	}

	RenderAccums();
}


void ShadowManager::AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap) {
	if (!Cell || Cell->IsInterior())
		return;
	
	if (ShadowMap->Forms.Terrain)
		AccumChildren(Cell->GetChildNode(TESObjectCELL::kCellNode_Land), &ShadowMap->Forms, true, false, &ShadowMap->ShadowMapFrustumPlanes);

	TList<TESObjectREFR>::Entry* Entry = &Cell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &ShadowMap->Forms);

		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		if (RefNode && RefNode->WithinFrustum(&ShadowMap->ShadowMapFrustumPlanes))
			AccumChildren(RefNode, &ShadowMap->Forms, false, false, &ShadowMap->ShadowMapFrustumPlanes);

		Entry = Entry->next;
	}
}


void ShadowManager::RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex) {
	NiSpotLight* pNiLight = Lights[LightIndex];
	if (pNiLight == NULL || !pNiLight->CastShadows) return;

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	D3DXMATRIX View, Proj;

	NiPoint3* LightPos = &pNiLight->m_worldTransform.pos;
	float Radius = pNiLight->Spec.r;

#if defined(OBLIVION)
	if (pNiLight->CanCarry)
		Radius = 256.0f;
#endif

	D3DXVECTOR3 Up = D3DXVECTOR3(0, 0, 1);
	D3DXVECTOR3 Eye = LightPos->toD3DXVEC3();
	Eye.x -= TheRenderManager->CameraPosition.x;
	Eye.y -= TheRenderManager->CameraPosition.y;
	Eye.z -= TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(pNiLight->OuterSpotAngle * 2), 1.0f, 0.1f, Radius);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, 0, RenderStateArgs);

	D3DXVECTOR3 CameraDirection = D3DXVECTOR3(pNiLight->m_worldTransform.rot.data[0][0], pNiLight->m_worldTransform.rot.data[1][0], pNiLight->m_worldTransform.rot.data[2][0]);
	D3DXVECTOR3 At = Eye + CameraDirection;

	TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
	while (Entry) {
		NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms);
		if (!RefNode) {
			Entry = Entry->next;
			continue;
		}

		// Detect if the object is in front of the light in the direction of the current face
		// TODO: improve to base on frustum
		D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
		D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

		D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
		bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
		if (inFront && RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius()) 
			AccumChildren(RefNode, &Settings->Forms, false, false);

		Entry = Entry->next;
	}

	D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
	Shadows->Constants.ShadowViewProj = View * Proj;
	TheShaderManager->SpotLightWorldToLightMatrix[LightIndex] = (Shadows->Constants.ShadowViewProj);

	Device->SetRenderTarget(0, Shadows->Textures.ShadowSpotlightSurface[LightIndex]);
	Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

	Device->SetViewport(&ShadowCubeMapViewPort);
	Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

	RenderAccums();
}


void ShadowManager::RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex) {
	if (Lights[LightIndex] == NULL) return; // No light at current index
	
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::InteriorsStruct* Settings = &Shadows->Settings.Interiors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	float Radius = 0.0f;
	float MinRadius = Settings->Forms.MinRadius;
	NiPoint3* LightPos = NULL;
	D3DXMATRIX View, Proj;
	D3DXVECTOR3 Eye, At, Up, CameraDirection;

	NiPointLight* pNiLight = Lights[LightIndex]->sourceLight;

	LightPos = &pNiLight->m_worldTransform.pos;
	Radius = pNiLight->Spec.r * Shadows->Settings.Interiors.LightRadiusMult;
	if (pNiLight->CanCarry)
		Radius = 256.0f;
	Eye.x = LightPos->x - TheRenderManager->CameraPosition.x;
	Eye.y = LightPos->y - TheRenderManager->CameraPosition.y;
	Eye.z = LightPos->z - TheRenderManager->CameraPosition.z;
	Shadows->Constants.ShadowCubeMapLightPosition.x = Eye.x;
	Shadows->Constants.ShadowCubeMapLightPosition.y = Eye.y;
	Shadows->Constants.ShadowCubeMapLightPosition.z = Eye.z;
	Shadows->Constants.ShadowCubeMapLightPosition.w = Radius;
	Shadows->Constants.Data.z = Radius;
	D3DXMatrixPerspectiveFovRH(&Proj, D3DXToRadian(90.0f), 1.0f, 0.1f, Radius);
	// the radius the lamp shadow pass uses for this lamp (TESR_ShadowLightPosition[i].w), or the drawn one if larger
	const float readRadius = max(Radius, Shadows->Constants.ShadowLightPosition[LightIndex].w);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE, RenderStateArgs);
	
	RenderState->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS, RenderStateArgs);

	// geometry of this lamp that passes the face-independent filters, built on the first face that is drawn
	static std::vector<std::pair<NiGeometry*, bool>> faceCandidates; // (geometry, IsLightingProperty)
	faceCandidates.clear();
	bool faceCandidatesBuilt = false;

	for (int Face = 0; Face < 6; Face++) {
		At = Eye;
		switch (Face) {
		case D3DCUBEMAP_FACE_POSITIVE_X:
			CameraDirection = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_X:
			CameraDirection = D3DXVECTOR3(-1.0f, 0.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_POSITIVE_Y:
			CameraDirection = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_Y:
			CameraDirection = D3DXVECTOR3(0.0f, -1.0f, 0.0f);
			Up = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
			break;
		case D3DCUBEMAP_FACE_POSITIVE_Z:
			CameraDirection = D3DXVECTOR3(0.0f, 0.0f, -1.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		case D3DCUBEMAP_FACE_NEGATIVE_Z:
			CameraDirection = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
			Up = D3DXVECTOR3(0.0f, 1.0f, 0.0f);
			break;
		}
		At += CameraDirection;

		// skip a face that no on-screen pixel can read this frame (see ShadowFaceVisibility.h)
		if (CubeFaceWedge.Valid && !ShadowCubeFaceMayBeRead(CubeFaceWedge, LightPos->x, LightPos->y, LightPos->z, readRadius,
			CameraDirection.x, CameraDirection.y, CameraDirection.z, 2.0f + readRadius * 0.001f)) {
			CubeFacesSkipped++;
			continue;
		}

		// Since this is pure geometry, getting reference data will be difficult (read: slow)
		auto iter = Lights[LightIndex]->kGeometryList.start;
		if (iter) {
			if (!faceCandidatesBuilt) {
				// checks that don't depend on the face run once per lamp
				faceCandidatesBuilt = true;
				while (iter) {
					NiGeometry* geo = iter->data;
					iter = iter->next;
					if (!geo || geo->m_flags & NiAVObject::APP_CULLED)
						continue;

					BSShaderProperty* shaderProp = static_cast<BSShaderProperty*>(geo->GetProperty(NiProperty::kType_Shade));
					NiMaterialProperty* matProp = static_cast<NiMaterialProperty*>(geo->GetProperty(NiProperty::kType_Material));

					if (!shaderProp)
						continue;

					// Skip refraction and fire refraction.
					if (!CheckShaderFlags(geo))
						continue;

					bool isFirstPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kFirstPerson);
					bool isThirdPerson = shaderProp->m_usFlags.GetBit(NiShadeProperty::kThirdPerson);

					// Skip objects if they are barely visible.
					if ((matProp && matProp->fAlpha < 0.05f))
						continue;

					// Also skip viewmodel due to issues, and render player's model only in 3rd person
					if (isFirstPerson) continue;

					if (!Player->isThirdPerson && !Settings->PlayerShadowFirstPerson && isThirdPerson)
						continue;

					if (Player->isThirdPerson && !Settings->PlayerShadowThirdPerson && isThirdPerson)
						continue;

					faceCandidates.push_back({ geo, shaderProp->IsLightingProperty() });
				}
			}

			for (const auto& candidate : faceCandidates) {
				NiGeometry* geo = candidate.first;

				if (TheSettingManager->SettingsMain.Main.CubeMapFaceCulling && candidate.second && !TouchesShadowFace(geo, LightPos, CameraDirection))
					continue;

				if (skinnedGeoPass->AccumObject(geo)) {}
				else if (speedTreePass->AccumObject(geo)) {}
				else if (Settings->Forms.AlphaEnabled && alphaPass->AccumObject(geo)) {}
				else geometryPass->AccumObject(geo);
			}
		}
		else {
			// old form based geo accumulation when the one perform by the game has not handled this light
			TList<TESObjectREFR>::Entry* Entry = &Player->parentCell->objectList.First;
			while (Entry) {
				if (NiNode* RefNode = GetRefNode(Entry->item, &Settings->Forms)) {
					// Detect if the object is in front of the light in the direction of the current face
					// TODO: improve to base on frustum
					D3DXVECTOR3 ObjectPos = RefNode->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 ObjectToLight = ObjectPos - LightPos->toD3DXVEC3();

					D3DXVec3Normalize(&ObjectToLight, &ObjectToLight);
					bool inFront = D3DXVec3Dot(&ObjectToLight, &CameraDirection) > 0;
					if (RefNode->GetDistance(LightPos) <= Radius + RefNode->GetWorldBoundRadius()) AccumChildren(RefNode, &Settings->Forms, false, false);
				}
				Entry = Entry->next;
			}
		}


		D3DXMatrixLookAtRH(&View, &Eye, &At, &Up);
		Shadows->Constants.ShadowViewProj = View * Proj;

		Device->SetRenderTarget(0, Shadows->Textures.ShadowCubeMapSurface[LightIndex][Face]);
		Device->SetDepthStencilSurface(Shadows->Textures.ShadowCubeMapDepthSurface);

		Device->SetViewport(&ShadowCubeMapViewPort);
		Device->Clear(0L, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f), 1.0f, 0L);

		RenderAccums();
	}
}


static void FlagShaderPropertyRecurse(NiAVObject* apObject, UInt32 auiFlags, bool abSet) {
	if (!apObject)
		return;

	if (apObject->IsGeometry()) {
		NiGeometry* pGeometry = static_cast<NiGeometry*>(apObject);
		NiShadeProperty* shaderProperty = static_cast<NiShadeProperty*>(pGeometry->GetProperty(NiProperty::kType_Shade));
		if (shaderProperty) {
			if (abSet)
				shaderProperty->m_usFlags.Set(auiFlags);
			else
				shaderProperty->m_usFlags.Clear(auiFlags);
		}

	}
	else if (apObject->IsNiNode()) {
		NiNode* pNiNode = static_cast<NiNode*>(apObject);
		for (UInt32 i = 0; i < pNiNode->m_children.end; i++) {
			FlagShaderPropertyRecurse(pNiNode->m_children.data[i], auiFlags, abSet);
		}
	}
}

static SInt32 frames = -1;
static void FlagPlayerGeometry() {
	frames++;

	// Run this function every 50 frames, or on launch
	if (frames > 50 || frames == 0) {
		if (Player->firstPersonNiNode)
			FlagShaderPropertyRecurse(Player->firstPersonNiNode, NiShadeProperty::kFirstPerson, true);

		NiNode* node = Player->GetNode();
		if (node)
			FlagShaderPropertyRecurse(node, NiShadeProperty::kThirdPerson, true);

		frames = 0;
	}
}

void ShadowManager::RecalculateBillboardVectors(D3DXVECTOR3* SunDir) {
	D3DXVECTOR3 WorldUp = D3DXVECTOR3(0.0f, 0.0f, 1.0f);

	// Calculate BillboardRight as perpendicular to SunDir and WorldUp
	D3DXVECTOR3 BillboardRightVec;
	D3DXVec3Cross(&BillboardRightVec, &WorldUp, SunDir);

	// Handle case where sun is directly above/below
	if (D3DXVec3LengthSq(&BillboardRightVec) < 0.0001f) {
		BillboardRightVec = D3DXVECTOR3(1.0f, 0.0f, 0.0f);
	}
	D3DXVec3Normalize(&BillboardRightVec, &BillboardRightVec);

	// Calculate BillboardUp perpendicular to both SunDir and BillboardRightVec
	D3DXVECTOR3 BillboardUpVec;
	D3DXVec3Cross(&BillboardUpVec, SunDir, &BillboardRightVec);
	D3DXVec3Normalize(&BillboardUpVec, &BillboardUpVec);

	// Set shader constants
	BillboardRight = NiVector4(BillboardRightVec.x, BillboardRightVec.y, BillboardRightVec.z, 0.0f);
	BillboardUp = NiVector4(BillboardUpVec.x, BillboardUpVec.y, BillboardUpVec.z, 0.0f);
}

/*
* Renders the different shadow maps: Near, Far, Ortho.
*/
void ShadowManager::RenderShadowMaps() {
	if (!TheSettingManager->SettingsMain.Main.RenderEffects) return; // cancel out if rendering effects is disabled

	// track point lights for interiors and exteriors
	ShadowSceneLight* ShadowLights[ShadowCubeMapsMax] = { NULL };
	NiPointLight* Lights[TrackedLightsMax] = { NULL };
	NiSpotLight* SpotLights[SpotLightsMax] = { NULL };

	TheShaderManager->GetNearbyLights(ShadowLights, Lights, SpotLights);

	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	ShadowsExteriorEffect::ExteriorsStruct* ShadowsExteriors = &Shadows->Settings.Exteriors;
	ShadowsExteriorEffect::InteriorsStruct* ShadowsInteriors = &Shadows->Settings.Interiors;

	bool isExterior = TheShaderManager->GameState.isExterior;// || currentCell->flags0 & TESObjectCELL::kFlags0_BehaveLikeExterior; // exterior flag currently broken
	bool ExteriorEnabled = isExterior && TheShaderManager->Effects.ShadowsExteriors->Enabled && ShadowsExteriors->Enabled;
	bool InteriorEnabled = !isExterior && TheShaderManager->Effects.ShadowsInteriors->Enabled;

	// early out in case shadow rendering is not required
	if (!ExteriorEnabled && !InteriorEnabled && !TheShaderManager->orthoRequired || !ShadowShadersLoaded) {
		return;
	}
	if (!Player->parentCell) return;

	auto timer = TimeLogger();
	GpuScope gpuScope(TheRenderManager->device, "Shadow maps");

	// prepare some pointers to the device and surfaces
	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* DepthSurface = NULL;
	IDirect3DSurface9* RenderSurface = NULL;
	D3DVIEWPORT9 viewport;

	D3DXVECTOR4* ShadowData = &TheShaderManager->Effects.ShadowsExteriors->Constants.Data;
	D3DXVECTOR4* OrthoData = &TheShaderManager->Effects.ShadowsExteriors->Constants.OrthoData;
	Device->GetDepthStencilSurface(&DepthSurface);
	Device->GetRenderTarget(0, &RenderSurface);
	Device->GetViewport(&viewport);	

	DWORD zfunc;
	Device->GetRenderState(D3DRS_ZFUNC, &zfunc); // backup in case of inverted depth

	DWORD oldStencilEnable, oldStencilRef, oldStencilFunc;
	DWORD oldAlphaRef, oldNormalizeNormals, oldPointSize;

	Device->GetRenderState(D3DRS_STENCILENABLE, &oldStencilEnable);
	Device->GetRenderState(D3DRS_STENCILREF, &oldStencilRef);
	Device->GetRenderState(D3DRS_STENCILFUNC, &oldStencilFunc);
	Device->GetRenderState(D3DRS_ALPHAREF, &oldAlphaRef);
	Device->GetRenderState(D3DRS_NORMALIZENORMALS, &oldNormalizeNormals);
	Device->GetRenderState(D3DRS_POINTSIZE, &oldPointSize);

	if (1.0 - NiDX9Renderer::GetSingleton()->m_fZClear) // inverted depth
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_GREATEREQUAL, RenderStateArgs);
	else
		RenderState->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL, RenderStateArgs);

	RenderState->SetRenderState(D3DRS_STENCILENABLE, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ALPHAREF, 0, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_NORMALIZENORMALS, 1, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_POINTSIZE, 810365505, RenderStateArgs);

	TheRenderManager->UpdateSceneCameraData();
	TheRenderManager->SetupSceneCamera();
	PrepareCubeFaceVisibility();
	
	D3DXVECTOR4 PlayerPosition = Player->pos.toD3DXVEC4();
	TESObjectCELL* currentCell = Player->parentCell;

	// Flag player geometry so we can control if it should be rendered in shadow cubemaps
	FlagPlayerGeometry();

	// Render directional shadows for Sun/Moon
	NiNode* PlayerNode = Player->GetNode();
	D3DXVECTOR3 At;
	At.x = PlayerNode->m_worldTransform.pos.x - TheRenderManager->CameraPosition.x;
	At.y = PlayerNode->m_worldTransform.pos.y - TheRenderManager->CameraPosition.y;
	At.z = PlayerNode->m_worldTransform.pos.z - TheRenderManager->CameraPosition.z;

	// Render all shadow maps
	Device->BeginScene();

	// Quantize sun direction angle to reduce shimmer by a large factor.
	D3DXVECTOR3 SunDir = Shadows->CalculateSmoothedSunDir();

	if (isExterior && (ExteriorEnabled || TheShaderManager->orthoRequired)) {

		// Update cascade depths based on current camera.
		Shadows->GetCascadeDepths();

		geometryPass->VertexShader = ShadowMapVertex;
		geometryPass->PixelShader = ShadowMapPixel;
		alphaPass->VertexShader = ShadowMapVertex;
		alphaPass->PixelShader = ShadowMapPixel;
		skinnedGeoPass->VertexShader = ShadowMapVertex;
		skinnedGeoPass->PixelShader = ShadowMapPixel;
		speedTreePass->VertexShader = ShadowMapVertex;
		speedTreePass->PixelShader = ShadowMapPixel;
		terrainLODPass->VertexShader = ShadowMapVertex;
		terrainLODPass->PixelShader = ShadowMapPixel;

		const bool atlasWasWarm = SunAtlasWarm;
		SunAtlasWarm = false;	// set again at the end of a completed sun pass

		if (ExteriorEnabled && SunDir.z > 0.0f) {
			// Recalculate billboard vectors for speedtree leaves shader.
			RecalculateBillboardVectors(&SunDir);

			ShadowData->z = 0; // set shader constant to identify other shadow maps
			GpuScope gpuSunScope(TheRenderManager->device, "Shadow maps: sun");

			if (Shadows->ShadowAtlasSurfaceMSAA)
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurfaceMSAA);
			else
				Device->SetRenderTarget(0, Shadows->ShadowAtlasSurface);

			Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);

			if (TheSettingManager->SettingsMain.Main.SharedShadowWalk && Player->GetWorldSpace()) {
				const LONGLONG buildStart = GpuProfiler::Tick();
				BuildSharedWalk();
				GpuProfiler::AddCpu("Sun walk: shared build", buildStart);
				sharedWalkActive = true;
			}

			bool lodRendered = false;
			bool lodSwap = false;
			const bool prefilterNow = Shadows->Settings.ShadowMaps.Prefilter;
			const bool spreadWanted = Shadows->Settings.ShadowMaps.LimitFrequency && Shadows->Settings.ShadowMaps.SpreadLod && !lodSpreadBroken;
			bool spreadMode = spreadWanted && atlasWasWarm && Shadows->ShadowAtlasSurface == SunAtlasSurfaceSeen &&
				Shadows->ShadowAtlasSurfaceMSAA == SunAtlasMsaaSeen && prefilterNow == SunAtlasPrefilter;
			if (spreadMode && !EnsureLodTarget()) spreadMode = false;
			if (!spreadMode) lodBuildValid = false;
			// When SpreadLod starts or stops, or the atlas can't be trusted, draw the whole LOD cascade once so its quadrant is valid.
			const bool forceLod = (spreadWanted && !spreadMode) || (lodSpreadWasActive && !spreadWanted);

			for (int i = MapNear; i < MapOrtho; i++) {
				ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[i];

				if (i == MapLod && spreadMode) {
					lodSwap = RenderSpreadLodPart(Shadows, ShadowMap, &SunDir);
				}
				else if (!Shadows->Settings.ShadowMaps.LimitFrequency || i != MapLod || !(FrameCounter % 4) || forceLod) {
					static const char* const scopeNames[] = { "Sun: near", "Sun: middle", "Sun: far", "Sun: lod" };
					static const char* const walkNames[] = { "Sun walk: near", "Sun walk: middle", "Sun walk: far", "Sun walk: lod" };
					static const char* const countNames[] = { "Sun objects: near", "Sun objects: middle", "Sun objects: far", "Sun objects: lod" };
					GpuScope cascadeScope(TheRenderManager->device, scopeNames[i]);
					Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &SunDir);
					RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj, walkNames[i], countNames[i]);
					if (i == MapLod) lodRendered = true;
				}
				else {
					// We need to update the shadowprojmatrix of MapLod by the camera translation between frames to avoid jumps in the shadows.
					D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
					D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
					D3DXMATRIX translationMatrix;
					D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
					ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
					ShadowMap->CameraTranslation = newCameraTranslation;
					
					Shadows->Constants.ShadowBlur.y = Shadows->ShadowAtlasSurfaceMSAA ? 1.0f : 0.0f; // Disable blur for last cascade if MSAA is off.
				}
			}

			sharedWalkActive = false;
			sharedRefs.clear();
			sharedCells.clear();

			/*
			* PartialShadowAtlasUpdate: the LOD cascade wasn't redrawn, so its part of the MSAA surface still holds what the last
			* update drew, and its part of the atlas holds that, resolved and blurred (the blur clamps inside each cascade's
			* quadrant, so the other cascades never leak into it). Resolving and blurring it again gives the same result, so only
			* the other three quadrants (top half and bottom-left) are done. Anything that could have changed the atlas since the
			* last completed sun pass (a skipped frame, new surfaces, Prefilter switched) does the full path.
			*/
			const bool prefilter = prefilterNow;
			const bool partial = (spreadMode || TheSettingManager->SettingsMain.Main.PartialShadowAtlasUpdate) && !lodRendered && atlasWasWarm &&
				Shadows->ShadowAtlasSurfaceMSAA && Shadows->ShadowAtlasSurface == SunAtlasSurfaceSeen &&
				Shadows->ShadowAtlasSurfaceMSAA == SunAtlasMsaaSeen && prefilter == SunAtlasPrefilter;
			bool partialDone = false;

			// Resolve MSAA.
			if (Shadows->ShadowAtlasSurfaceMSAA) {
				GpuScope resolveScope(TheRenderManager->device, "Sun: MSAA resolve");
				if (partial) {
					D3DSURFACE_DESC desc;
					Shadows->ShadowAtlasSurface->GetDesc(&desc);
					const LONG n = (LONG)desc.Width, h = n / 2;
					RECT top = { 0, 0, n, h }, left = { 0, h, h, n };
					partialDone = SUCCEEDED(Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, &top, Shadows->ShadowAtlasSurface, &top, D3DTEXF_NONE)) &&
						SUCCEEDED(Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, &left, Shadows->ShadowAtlasSurface, &left, D3DTEXF_NONE));
				}
				if (!partialDone) {
					Device->StretchRect(Shadows->ShadowAtlasSurfaceMSAA, NULL, Shadows->ShadowAtlasSurface, NULL, D3DTEXF_NONE);
					if (spreadMode) {	// the full resolve just overwrote the LOD quadrant with stale data
						lodSpreadBroken = true;
						Logger::Log("[WARNING] Shadow maps: partial MSAA resolve failed, SpreadLod is off until restart.");
					}
				}
			}

			if (lodSwap) {
				// The LOD buffer is complete: copy it into its quadrant of the atlas (resolving MSAA on the way).
				GpuScope swapScope(TheRenderManager->device, "Sun: lod swap");
				D3DSURFACE_DESC desc;
				Shadows->ShadowAtlasSurface->GetDesc(&desc);
				const LONG n = (LONG)desc.Width, h = n / 2;
				RECT quadrant = { h, h, n, n };
				if (FAILED(Device->StretchRect(LodRT, NULL, Shadows->ShadowAtlasSurface, &quadrant, D3DTEXF_NONE))) {
					lodSpreadBroken = true;
					Logger::Log("[WARNING] Shadow maps: copying the LOD cascade failed, SpreadLod is off until restart.");
				}
			}

			if (prefilter) {
				GpuScope blurScope(TheRenderManager->device, "Sun: blur");
				BlurShadowAtlas(partialDone && !lodSwap);
			}

			SunAtlasWarm = true;
			lodSpreadWasActive = spreadMode;
			SunAtlasPrefilter = prefilter;
			SunAtlasSurfaceSeen = Shadows->ShadowAtlasSurface;
			SunAtlasMsaaSeen = Shadows->ShadowAtlasSurfaceMSAA;

			if (Shadows->Settings.ShadowMaps.Mipmaps)
				Shadows->ShadowAtlasTexture->GenerateMipSubLevels();
		}

		// render ortho map if one of the effects using ortho is active
		if (TheShaderManager->orthoRequired) {
			auto shadowMapTimer = TimeLogger();
			GpuScope gpuOrthoScope(TheRenderManager->device, "Shadow maps: ortho");

			ShadowsExteriorEffect::ShadowMapSettings* ShadowMap = &Shadows->ShadowMaps[MapOrtho];

			if (!Shadows->Settings.OrthoMap.LimitFrequency || !((FrameCounter + 2) % 4)) {
				Device->SetRenderTarget(0, Shadows->ShadowMapOrthoSurface);
				Device->SetDepthStencilSurface(Shadows->ShadowMapOrthoDepthSurface);

				ShadowData->z = 1; // identify ortho map in shader constant
				D3DXVECTOR3 OrthoDir = D3DXVECTOR3(0.05f, 0.05f, 1.0f);
				Shadows->Constants.ShadowViewProj = Shadows->GetCascadeViewProj(ShadowMap, &OrthoDir);

				RenderShadowMap(ShadowMap, &Shadows->Constants.ShadowViewProj);
			}
			else {
				D3DXVECTOR3 newCameraTranslation = WorldSceneGraph->camera->m_worldTransform.pos.toD3DXVEC3();
				D3DXVECTOR3 difference = newCameraTranslation - ShadowMap->CameraTranslation;
				D3DXMATRIX translationMatrix;
				D3DXMatrixTranslation(&translationMatrix, difference.x, difference.y, difference.z);
				ShadowMap->ShadowCameraToLight = translationMatrix * ShadowMap->ShadowCameraToLight;
				ShadowMap->CameraTranslation = newCameraTranslation;
			}

			OrthoData->x = Shadows->Settings.OrthoMap.Distance * 2;
			OrthoData->y = ShadowMap->ShadowMapInverseResolution;
	
			shadowMapTimer.LogTime("ShadowManager::RenderShadowMap Ortho");
		}
	}

	// Render shadow maps for point lights
	bool usePointLights = (TheShaderManager->GameState.isDayTime > 0.5) ? ShadowsExteriors->UsePointShadowsDay : ShadowsExteriors->UsePointShadowsNight;

	AlphaEnabled = ShadowsInteriors->Forms.AlphaEnabled;
	geometryPass->VertexShader = ShadowCubeMapVertex;
	geometryPass->PixelShader = ShadowCubeMapPixel;
	alphaPass->VertexShader = ShadowCubeMapVertex;
	alphaPass->PixelShader = ShadowCubeMapPixel;
	skinnedGeoPass->VertexShader = ShadowCubeMapVertex;
	skinnedGeoPass->PixelShader = ShadowCubeMapPixel;
	speedTreePass->VertexShader = ShadowCubeMapVertex;
	speedTreePass->PixelShader = ShadowCubeMapPixel;

	auto shadowMapTimer = TimeLogger();
	GpuScope gpuLampScope(TheRenderManager->device, "Shadow maps: lamps");
	if ((isExterior && usePointLights) || (!isExterior && InteriorEnabled)) {
		const int cubeMapsDrawn = TheSettingManager->SettingsMain.Main.SkipUnusedCubeMap ?
			min(ShadowsInteriors->LightPoints, (int)ShadowCubeMapsSampled) : ShadowsInteriors->LightPoints;
		for (int i = 0; i < cubeMapsDrawn; i++) {

			// Render targets set in function due to rendering multiple faces.
			RenderShadowCubeMap(ShadowLights, i);

			std::string message = "ShadowManager::RenderShadowCubeMap ";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
	}

	if (TheShaderManager->Effects.Flashlight->Enabled && TheShaderManager->Effects.Flashlight->spotLightActive && TheShaderManager->Effects.Flashlight->Settings.renderShadows) {
		// render shadow maps for spotlights
		
		for (int i = 0; i < SpotLightsMax; i++) {
			if (!SpotLights[i] || SpotLights[i]->Spec.r == 0) continue; //bypass lights with no radius

			// Render targets set in function.
			RenderShadowSpotlight(SpotLights, i);

			std::string message = "ShadowManager::RenderShadowSpotLight";
			message += std::to_string(i);
			shadowMapTimer.LogTime(message.c_str());
		}
	}

	// reset renderer to previous state
	Device->SetDepthStencilSurface(DepthSurface);
	Device->SetRenderTarget(0, RenderSurface);
	Device->SetViewport(&viewport);
	Device->SetRenderState(D3DRS_ZFUNC, zfunc);
	Device->SetRenderState(D3DRS_STENCILENABLE, oldStencilEnable);
	Device->SetRenderState(D3DRS_STENCILREF, oldStencilRef);
	Device->SetRenderState(D3DRS_STENCILFUNC, oldStencilFunc);
	Device->SetRenderState(D3DRS_ALPHAREF, oldAlphaRef);
	Device->SetRenderState(D3DRS_NORMALIZENORMALS, oldNormalizeNormals);
	Device->SetRenderState(D3DRS_POINTSIZE, oldPointSize);

	//release smart pointers to prevent memory leak
	if (DepthSurface) DepthSurface->Release();
	if (RenderSurface) RenderSurface->Release();

	if (TheSettingManager->SettingsMain.Develop.DebugMode && !InterfaceManager->IsActive(Menu::MenuType::kMenuType_Console)) {
		if (Global->OnKeyDown(0x17)) { // TODO: setting for debug key ?
			char Filename[MAX_PATH];

			time_t CurrentTime = time(NULL);
			GetCurrentDirectoryA(MAX_PATH, Filename);
			strcat(Filename, "\\Test");
			if (GetFileAttributesA(Filename) == INVALID_FILE_ATTRIBUTES) CreateDirectoryA(Filename, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmapatlas.jpg", D3DXIFF_JPG, Shadows->ShadowAtlasSurface, NULL, NULL);
			D3DXSaveSurfaceToFileA(".\\Test\\shadowmaportho.jpg", D3DXIFF_JPG, Shadows->ShadowMapOrthoSurface, NULL, NULL);

			InterfaceManager->ShowMessage("Textures taken!");
		}
	}

	Device->EndScene();

	FrameCounter = (FrameCounter + 1) % 4;
	shadowMapsRenderTime = timer.LogTime("ShadowManager::RenderShadowMaps");
}


/*
 * Clear a part of the shadow map atlas based on the shadow mapping mode.
 * 
 * Note: Render target, view port should be set beforehand. Scene has to be already being rendered.
 */
void ShadowManager::ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;

	IDirect3DDevice9* Device = TheRenderManager->device;
	NiDX9RenderState* RenderState = TheRenderManager->renderState;
	IDirect3DSurface9* TargetShadowMap = Shadows->ShadowAtlasSurface;

	Device->SetDepthStencilSurface(NULL);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
	RenderState->SetPixelShader(ShadowMapClearPixel->ShaderHandle, false);

	Device->SetPixelShaderConstantF(0, (const float*) ClearColor, 1);

	// Draw a full-screen quad (inside the viewport)
	struct VERTEX { float x, y, z, rhw; };
	VERTEX vertices[] = {
		{ (float)ViewPort->X - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)ViewPort->Y - 0.5f, 0.5f, 1.0f },
		{ (float)ViewPort->X - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f },
		{ (float)(ViewPort->X + ViewPort->Width) - 0.5f, (float)(ViewPort->Y + ViewPort->Height) - 0.5f, 0.5f, 1.0f }
	};

	RenderState->SetFVF(D3DFVF_XYZRHW, false);

	Device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(VERTEX));

	Device->SetDepthStencilSurface(Shadows->ShadowAtlasDepthSurface);
	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
	RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}


/*
* Filters the Shadow Map of given index using a 2 pass gaussian blur
*/
void ShadowManager::BlurShadowAtlas(bool SkipLodQuadrant) {
	ShadowsExteriorEffect* Shadows = TheShaderManager->Effects.ShadowsExteriors;
	
	IDirect3DDevice9* Device = TheRenderManager->device;
    NiDX9RenderState* RenderState = TheRenderManager->renderState;
    IDirect3DTexture9* SourceShadowMap = Shadows->ShadowAtlasTexture;
    IDirect3DSurface9* TargetShadowMap = Shadows->ShadowAtlasSurface;

    Device->SetDepthStencilSurface(NULL);
    RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_FALSE, RenderStateArgs);
    RenderState->SetVertexShader(ShadowMapBlurVertex->ShaderHandle, false);
    RenderState->SetPixelShader(ShadowMapBlurPixel->ShaderHandle, false);
	RenderState->SetFVF(FrameFVF, false);
	Device->SetStreamSource(0, Shadows->ShadowAtlasVertexBuffer, 0, sizeof(FrameVS));
	Device->SetRenderTarget(0, TargetShadowMap);
	
	// Pass map resolution to shader as a constant
	ShadowMapBlurPixel->SetShaderConstantF(0, &Shadows->Constants.ShadowBlur, 1);
	RenderState->SetTexture(0, SourceShadowMap);

	// blur in two passes, vertically and horizontally
	D3DXVECTOR4 Blur[2] = {
		D3DXVECTOR4(1.0f, 0.0f, 0.0f, 0.0f),
		D3DXVECTOR4(0.0f, 1.0f, 0.0f, 0.0f),
	};

	// Without the LOD quadrant (bottom right): the top half and the bottom left quarter, by scissor.
	DWORD oldScissor = FALSE;
	RECT top = {}, left = {};
	if (SkipLodQuadrant) {
		D3DSURFACE_DESC desc;
		TargetShadowMap->GetDesc(&desc);
		const LONG n = (LONG)desc.Width, h = n / 2;
		top = { 0, 0, n, h };
		left = { 0, h, h, n };
		Device->GetRenderState(D3DRS_SCISSORTESTENABLE, &oldScissor);
		Device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
	}

	for (int i = 0; i < 2; i++) {
		// set blur direction shader constants
		ShadowMapBlurPixel->SetShaderConstantF(1, &Blur[i], 1);

		if (SkipLodQuadrant) {
			Device->SetScissorRect(&top);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
			Device->SetScissorRect(&left);
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2);
		}
		else
			Device->DrawPrimitive(D3DPT_TRIANGLESTRIP, 0, 2); // draw call to execute the shader
	}

	if (SkipLodQuadrant) Device->SetRenderState(D3DRS_SCISSORTESTENABLE, oldScissor);

	RenderState->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE, RenderStateArgs);
    RenderState->SetRenderState(D3DRS_ZWRITEENABLE, D3DZB_TRUE, RenderStateArgs);
}


/*
* Camera wedge for skipping lamp cube map faces that no on-screen pixel can read (CubeMapCameraCulling).
* Built from the same camera data the lamp shadow pass reconstructs its world positions from.
*/
static void ReadShadowCamera(float camera[14]) {
	const D3DXMATRIX& view = TheRenderManager->viewMatrix;
	const D3DXMATRIX& proj = TheRenderManager->projMatrix;
	const float values[14] = {
		TheRenderManager->CameraPosition.x, TheRenderManager->CameraPosition.y, TheRenderManager->CameraPosition.z,
		view._11, view._21, view._31,	// right (TESR_ViewTransform column 0)
		view._12, view._22, view._32,	// up (column 1)
		view._13, view._23, view._33,	// forward (column 2)
		proj._11, proj._22 };
	memcpy(camera, values, sizeof(values));
}

void ShadowManager::PrepareCubeFaceVisibility() {
	CubeFaceWedge.Valid = false;
	CubeFacesSkipped = 0;
	CubeFaceSnapshotPending = false;
	if (CubeFaceVisibilityOff || !TheSettingManager->SettingsMain.Main.CubeMapCameraCulling) return;

	ReadShadowCamera(CubeFaceCamera);
	const float* c = CubeFaceCamera;
	if (c[12] == 0.0f || c[13] == 0.0f) return;
	BuildShadowCameraWedge(CubeFaceWedge, &c[0], &c[3], &c[6], &c[9], 1.0f / c[12], 1.0f / c[13]);
	CubeFaceSnapshotPending = CubeFaceWedge.Valid;
}

/*
* Called right before the lamp shadow pass. Faces were skipped for the camera of RenderShadowMaps; if the camera is not
* the same now, the skipping is switched off for the rest of the session so the picture can never depend on it.
*/
void ShadowManager::CheckCubeFaceVisibilityCamera() {
	if (!CubeFaceSnapshotPending) return;
	CubeFaceSnapshotPending = false;

	float now[14];
	ReadShadowCamera(now);
	bool same = true;
	for (int i = 0; i < 14 && same; i++) {
		const float tolerance = i < 3 ? 0.01f : 1.0e-4f * max(1.0f, fabsf(CubeFaceCamera[i]));
		if (!(fabsf(now[i] - CubeFaceCamera[i]) <= tolerance)) same = false;
	}
	if (same) {
		static bool reported = false;
		if (!reported && CubeFacesSkipped > 0) {
			Logger::Log("Lamp shadow faces: faces no on-screen pixel can read are skipped (%i this frame).", CubeFacesSkipped);
			reported = true;
		}
		return;
	}
	CubeFaceVisibilityOff = true;
	CubeFaceWedge.Valid = false;
	Logger::Log("[WARNING] Lamp shadow faces: the camera changed between the shadow maps and the lamp shadow pass; CubeMapCameraCulling is off until restart.");
}
