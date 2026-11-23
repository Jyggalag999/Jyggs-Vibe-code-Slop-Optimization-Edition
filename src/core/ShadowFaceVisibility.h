#pragma once
#include <cmath>

// Can any on-screen pixel read this face of a lamp's shadow cube map this frame?
//
// The lamp shadow pass (Effects/PointShadows*.fx.hlsl) is the only reader of the lamp cube maps. It reads them for
// on-screen pixels only, at the world positions CameraPosition + t * (Forward + a * halfWidth * Right + b * halfHeight * Up)
// with t >= 0 and |a|, |b| <= 1 (toWorld() in Effects/Includes/Depth.hlsl), and only where the pixel is within the
// lamp's radius (the stock result is multiplied by exactly 0 beyond it). A face is read for directions inside its
// 90-degree pyramid. So if that pyramid, cut at the lamp radius, lies wholly outside one side plane of the camera
// wedge above, no pixel can read the face and drawing it can be skipped without changing the image.

struct ShadowCameraWedge {
	float	Origin[3];
	float	Normal[4][3];	// unit inward normals of the four side planes through Origin
	bool	Valid = false;
};

inline bool ShadowVisibilityFinite(float v) {
	return v > -1.0e30f && v < 1.0e30f;
}

inline void BuildShadowCameraWedge(ShadowCameraWedge& wedge, const float origin[3], const float right[3], const float up[3],
                                   const float forward[3], float halfWidth, float halfHeight) {
	wedge.Valid = false;
	if (!(halfWidth > 1.0e-4f && halfWidth < 1.0e4f) || !(halfHeight > 1.0e-4f && halfHeight < 1.0e4f)) return;

	// inside the wedge: dot(P - Origin, n) >= 0 for n = halfWidth*Forward -/+ Right and halfHeight*Forward -/+ Up
	const float scale[4] = { halfWidth, halfWidth, halfHeight, halfHeight };
	const float sign[4] = { -1.0f, 1.0f, -1.0f, 1.0f };
	const float* side[4] = { right, right, up, up };
	for (int p = 0; p < 4; p++) {
		float n[3];
		float length = 0.0f;
		for (int k = 0; k < 3; k++) {
			n[k] = scale[p] * forward[k] + sign[p] * side[p][k];
			length += n[k] * n[k];
		}
		if (!(length > 1.0e-12f && length < 1.0e12f)) return;
		const float inverse = 1.0f / sqrtf(length);
		for (int k = 0; k < 3; k++) {
			wedge.Normal[p][k] = n[k] * inverse;
			if (!ShadowVisibilityFinite(wedge.Normal[p][k])) return;
		}
	}
	for (int k = 0; k < 3; k++) {
		if (!ShadowVisibilityFinite(origin[k])) return;
		wedge.Origin[k] = origin[k];
	}
	wedge.Valid = true;
}

// (dx, dy, dz) is the face's axis-aligned view direction, exactly as the face is rendered (CameraDirection).
inline bool ShadowCubeFaceMayBeRead(const ShadowCameraWedge& wedge, float lx, float ly, float lz, float radius,
                                    float dx, float dy, float dz, float margin) {
	if (!wedge.Valid) return true;
	if (!(radius > 0.0f && radius < 1.0e20f) || !ShadowVisibilityFinite(lx) || !ShadowVisibilityFinite(ly) ||
		!ShadowVisibilityFinite(lz)) return true;

	// the two world axes across the face
	float u[3] = { 1.0f, 0.0f, 0.0f };
	float v[3] = { 0.0f, 1.0f, 0.0f };
	if (dx != 0.0f) { u[0] = 0.0f; u[1] = 1.0f; v[1] = 0.0f; v[2] = 1.0f; }
	else if (dy != 0.0f) { v[1] = 0.0f; v[2] = 1.0f; }

	// Points within the radius and inside the face pyramid satisfy forward <= radius and |across| <= forward, so they
	// lie in the pyramid with apex at the lamp and a square base at forward = radius. The base is widened by 5% because
	// filtering near a face edge may read the neighbouring face (seamless cube filtering, e.g. under DXVK).
	const float spread = 1.05f;
	float vertex[5][3];
	vertex[0][0] = lx; vertex[0][1] = ly; vertex[0][2] = lz;
	int index = 1;
	for (int a = -1; a <= 1; a += 2) {
		for (int b = -1; b <= 1; b += 2) {
			vertex[index][0] = lx + radius * (dx + spread * (a * u[0] + b * v[0]));
			vertex[index][1] = ly + radius * (dy + spread * (a * u[1] + b * v[1]));
			vertex[index][2] = lz + radius * (dz + spread * (a * u[2] + b * v[2]));
			index++;
		}
	}

	for (int p = 0; p < 4; p++) {
		bool allOutside = true;
		for (int i = 0; i < 5 && allOutside; i++) {
			const float distance =
				(vertex[i][0] - wedge.Origin[0]) * wedge.Normal[p][0] +
				(vertex[i][1] - wedge.Origin[1]) * wedge.Normal[p][1] +
				(vertex[i][2] - wedge.Origin[2]) * wedge.Normal[p][2];
			if (!(distance < -margin)) allOutside = false;
		}
		if (allOutside) return false;
	}
	return true;
}
