#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 worldPos;
layout(location = 2) out vec3 normal;

layout(binding = 0) uniform UBO {
	mat4 view;
	mat4 proj;
} ubo;

layout(push_constant) uniform Push {
	mat4 model;
} pushConst;

void main() {
	mat4 modelMat = pushConst.model;
	vec4 worldPosition = modelMat * vec4(inPos, 1.0);
	gl_Position = ubo.proj * ubo.view * worldPosition;
	fragColor = inColor;
	worldPos = worldPosition.xyz;
	// approximate normal for flat-shaded triangles: transform position-normalized vector
	// For simple solid colors, compute normal as normalized transformed position (sufficient for pyramid/cube)
	normal = normalize((modelMat * vec4(normalize(inPos), 0.0)).xyz);
}
