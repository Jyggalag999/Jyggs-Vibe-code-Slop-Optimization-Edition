#pragma once
#include "ShadowFaceVisibility.h"


class ShadowManager { // Never disposed
public:
	static void Initialize();
	
	enum ShadowMapTypeEnum {
		MapNear = 0,
		MapMiddle = 1,
		MapFar = 2,
		MapLod = 3,
		MapOrtho = 4,
	};


	NiNode*					GetRefNode(TESObjectREFR* Ref, ShadowsExteriorEffect::FormsStruct* Forms);
	void					AccumChildren(NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLand, bool isLOD, NiFrustumPlanes* arPlanes = nullptr);
	void					AccumObject(std::stack<NiAVObject*>* containersAccum, NiAVObject* NiObject, ShadowsExteriorEffect::FormsStruct* Forms, bool isLODLand);
	void					RenderAccums();
	void					RenderShadowMap(ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXMATRIX* ViewProj, const char* WalkName = nullptr, const char* CountName = nullptr, bool ClearTarget = true);
	void					AccumExteriorCell(TESObjectCELL* Cell, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap);
	void					RenderShadowCubeMap(ShadowSceneLight** Lights, UInt32 LightIndex);
	void					RenderShadowSpotlight(NiSpotLight** Lights, UInt32 LightIndex);
	void					RenderShadowMaps();
	void					PrepareCubeFaceVisibility();
	void					CheckCubeFaceVisibilityCamera();
	void					ClearShadowCascade(D3DVIEWPORT9* ViewPort, D3DXVECTOR4* ClearColor);
	void                    BlurShadowAtlas(bool SkipLodQuadrant = false);
	void					BuildSharedWalk();
	bool					EnsureLodTarget();
	void					ReleaseLodTarget();
	bool					RenderSpreadLodPart(ShadowsExteriorEffect* Shadows, ShadowsExteriorEffect::ShadowMapSettings* ShadowMap, D3DXVECTOR3* SunDir);
	static bool				FormTypeAllowed(UInt8 TypeID, ShadowsExteriorEffect::FormsStruct* Forms);

	ShadowRenderPass*				geometryPass;
	AlphaShadowRenderPass*			alphaPass;
	SkinnedGeoShadowRenderPass*		skinnedGeoPass;
	SpeedTreeShadowRenderPass*		speedTreePass;
	TerrainLODPass*					terrainLODPass;

	NiVector4				BillboardRight;
	NiVector4				BillboardUp;
	ShaderRecordVertex*		ShadowMapVertex;
	ShaderRecordPixel*		ShadowMapPixel;
	ShaderRecordVertex*		ShadowCubeMapVertex;
	ShaderRecordPixel*		ShadowCubeMapPixel;
	ShaderRecordVertex*		ShadowMapBlurVertex;
	ShaderRecordPixel*		ShadowMapBlurPixel;
	ShaderRecordPixel*		ShadowMapClearPixel;
	D3DVIEWPORT9			ShadowCubeMapViewPort;
	ShaderRecordVertex*		CurrentVertex;
	ShaderRecordPixel*		CurrentPixel;
	bool					AlphaEnabled;
	int						PointLightsNum;
	float					shadowMapsRenderTime;
	bool					ShadowShadersLoaded;
	int						FrameCounter;
	ShadowCameraWedge		CubeFaceWedge;
	float					CubeFaceCamera[14] = {};
	int						CubeFacesSkipped = 0;
	bool					CubeFaceSnapshotPending = false;
	bool					CubeFaceVisibilityOff = false;

	// PartialShadowAtlasUpdate: the sun atlas holds a final (resolved and blurred) LOD cascade from an earlier frame.
	bool					SunAtlasWarm = false;
	bool					SunAtlasPrefilter = false;
	IDirect3DSurface9*		SunAtlasSurfaceSeen = nullptr;
	IDirect3DSurface9*		SunAtlasMsaaSeen = nullptr;

	// SharedShadowWalk: per frame list of the exterior cells' land nodes and ref nodes, shared by the sun cascades.
	struct SharedRef { NiNode* node; UInt8 type; };
	struct SharedCell { NiNode* land; UInt32 first; UInt32 count; };
	std::vector<SharedRef>	sharedRefs;
	std::vector<SharedCell>	sharedCells;
	bool					sharedWalkActive = false;

	// SpreadLod: the LOD cascade is drawn a quarter of its objects per frame into its own buffer, swapped in when complete.
	IDirect3DSurface9*		LodRT = nullptr;
	IDirect3DSurface9*		LodDepth = nullptr;
	UInt32					lodRTSize = 0;
	bool					lodRTMsaa = false;
	bool					lodSpreadBroken = false;	// a buffer or copy failed: off until restart
	bool					lodSpreadWasActive = false;
	bool					lodBuildValid = false;
	int						lodLastPart = -1;
	D3DXMATRIX				lodBuildViewProj;
	D3DXVECTOR3				lodBuildCam;
	NiFrustumPlanes			lodBuildPlanes;

private:
	bool					CheckShaderFlags(NiGeometry* Geometry);
	void					RecalculateBillboardVectors(D3DXVECTOR3* SunDir);
};