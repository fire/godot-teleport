//  Copyright (c) 2019 Simul Software Ltd. All rights reserved.
#ifndef PBR_CONSTANTS_SL
#define PBR_CONSTANTS_SL

SIMUL_CONSTANT_BUFFER(PbrConstants,13)
	vec3 albedo;
	float roughness;
	vec4 depthToLinFadeDistParams;
	vec4 fullResToLowResTransformXYWH;
	vec3 lightIrradiance;
	float metal;
	vec3 lightDir;
	int reverseDepth;

	vec4 diffuseOutputScalar;
	vec2 diffuseTexCoordsScalar_R;
	vec2 diffuseTexCoordsScalar_G;
	vec2 diffuseTexCoordsScalar_B;
	vec2 diffuseTexCoordsScalar_A;

	vec4 normalOutputScalar;
	vec2 normalTexCoordsScalar_R;
	vec2 normalTexCoordsScalar_G;
	vec2 normalTexCoordsScalar_B;
	vec2 normalTexCoordsScalar_A;

	vec4 combinedOutputScalar;
	vec2 combinedTexCoordsScalar_R;
	vec2 combinedTexCoordsScalar_G;
	vec2 combinedTexCoordsScalar_B;
	vec2 combinedTexCoordsScalar_A;

	vec4 emissiveOutputScalar;
	vec2 emissiveTexCoordsScalar_R;
	vec2 emissiveTexCoordsScalar_G;
	vec2 emissiveTexCoordsScalar_B;
	vec2 emissiveTexCoordsScalar_A;

	vec3 u_SpecularColour;
	float _pad;

	float u_DiffuseTexCoordIndex;
	float u_NormalTexCoordIndex;
	float u_CombinedTexCoordIndex;
	float u_EmissiveTexCoordIndex;
	float _pad2;
SIMUL_CONSTANT_BUFFER_END

#endif
