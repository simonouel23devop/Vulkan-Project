#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 worldPos;
layout(location = 2) in vec3 normal;

layout(location = 0) out vec4 outColor;

// Simple directional lighting + ambient
const vec3 lightDir = normalize(vec3(-0.5, -1.0, -0.3));
const vec3 lightColor = vec3(1.0);
const float ambientStrength = 0.25;
// Helper: perceived luminance
float luminance(vec3 c) {
	return dot(c, vec3(0.299, 0.587, 0.114));
}

// Helper: approximate color match
bool approxColor(vec3 c, vec3 target, float tol) {
	return distance(c, target) <= tol;
}

void main() {
	vec3 baseColor = fragColor;

	// Quick room-culling: discard fragments that belong to the large surrounding room
	// (large room geometry is far from origin in X/Z and sits above the ground).
	// This removes the grey ceiling/walls without changing geometry.
	float roomCullThreshold = 6.0; // world-space distance threshold
	if (max(abs(worldPos.x), abs(worldPos.z)) > roomCullThreshold && worldPos.y > 0.1) {
		discard;
	}

	// Highlight/grey the floor area beneath the vertical cube
	// Adjust these extents to match the cube footprint in world space.
	float cubeFloorHalfX = 1.5; // half-width along X of the vertical cube footprint
	float cubeFloorHalfZ = 1.5; // half-depth along Z of the vertical cube footprint
	// Move the grey band lower on the Y axis (world-space) so it appears beneath the floor
	// move the grey band further down (lower Y) to place it below the cube
	if (abs(worldPos.x) < cubeFloorHalfX && abs(worldPos.z) < cubeFloorHalfZ && worldPos.y > -3.0 && worldPos.y < -2.0) {
		// set a neutral grey for the floor under the cube
		baseColor = vec3(0.75);
	}

	// Also allow color-key desaturation: if color is vivid purple/blue/yellow/cyan, make it a darker grey first
	float tol = 0.35;
	bool isPurple = approxColor(baseColor, vec3(1.0, 0.0, 1.0), tol);
	bool isBlue   = approxColor(baseColor, vec3(0.0, 0.0, 1.0), tol);
	bool isYellow = approxColor(baseColor, vec3(1.0, 1.0, 0.0), tol);
	bool isCyan   = approxColor(baseColor, vec3(0.0, 1.0, 1.0), tol);
	if (isPurple || isBlue || isYellow || isCyan) {
		float lum = luminance(baseColor);
		baseColor = vec3(lum * 0.5);
	}

	// Lambertian diffuse
	float diff = max(dot(normalize(normal), -lightDir), 0.0);
	vec3 diffuse = diff * lightColor;

	// ambient term
	vec3 ambient = ambientStrength * baseColor;

	vec3 lit = (ambient + diffuse * baseColor);

	// clamp
	lit = clamp(lit, 0.0, 1.0);

	outColor = vec4(lit, 1.0);
}
