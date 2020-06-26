//#version 310 es
precision highp float;

//To Output Framebuffer - Use gl_FragColor
//layout(location = 0) out vec4 colour;

//From Vertex Varying
layout(location = 0)  in vec3 v_Position;
layout(location = 1)  in vec3 v_Normal;
layout(location = 2)  in vec3 v_Tangent;
layout(location = 3)  in vec3 v_Binormal;
layout(location = 4)  in mat3 v_TBN;
layout(location = 7)  in vec2 v_UV_diffuse;
layout(location = 8)  in vec2 v_UV_normal;
layout(location = 9)  in vec4 v_Color;
layout(location = 10) in vec4 v_Joint;
layout(location = 11) in vec4 v_Weights;
layout(location = 12) in vec3 v_CameraPosition;
layout(location = 13) in vec3 v_ModelSpacePosition;

#define lerp mix

//From Application SR
//Lights
const int MaxLights = 8;
struct Light //Layout conformant to GLSL std140
{
    vec4 u_Colour;
    vec3 u_Position;
    float u_Power;		 //Strength or Power of the light in Watts equilavent to Radiant Flux in Radiometry.
    vec3 u_Direction;
	float _pad3;
	mat4 u_LightSpaceTransform;
};

layout(std140, binding = 2) uniform u_LightData
{
    Light[MaxLights] u_Lights;
};

//Material
layout(std140, binding = 3) uniform u_MaterialData //Layout conformant to GLSL std140
{
    vec4 u_DiffuseOutputScalar;
    vec2 u_DiffuseTexCoordsScalar_R;
    vec2 u_DiffuseTexCoordsScalar_G;
    vec2 u_DiffuseTexCoordsScalar_B;
    vec2 u_DiffuseTexCoordsScalar_A;

    vec4 u_NormalOutputScalar;
    vec2 u_NormalTexCoordsScalar_R;
    vec2 u_NormalTexCoordsScalar_G;
    vec2 u_NormalTexCoordsScalar_B;
    vec2 u_NormalTexCoordsScalar_A;

    vec4 u_CombinedOutputScalar;
    vec2 u_CombinedTexCoordsScalar_R;
    vec2 u_CombinedTexCoordsScalar_G;
    vec2 u_CombinedTexCoordsScalar_B;
    vec2 u_CombinedTexCoordsScalar_A;

    vec4 u_EmissiveOutputScalar;
    vec2 u_EmissiveTexCoordsScalar_R;
    vec2 u_EmissiveTexCoordsScalar_G;
    vec2 u_EmissiveTexCoordsScalar_B;
    vec2 u_EmissiveTexCoordsScalar_A;

    vec3 u_SpecularColour;
    float _pad;

    float u_DiffuseTexCoordIndex;
    float u_NormalTexCoordIndex;
    float u_CombinedTexCoordIndex;
    float u_EmissiveTexCoordIndex;
    float _pad2;
};
layout(binding = 10) uniform sampler2D u_Diffuse;
layout(binding = 11) uniform sampler2D u_Normal;
layout(binding = 12) uniform sampler2D u_Combined;
layout(binding = 13) uniform sampler2D u_Emissive;

layout(binding = 14) uniform samplerCube u_DiffuseCubemap;
layout(binding = 15) uniform samplerCube u_SpecularCubemap;
layout(binding = 16) uniform samplerCube u_RoughSpecularCubemap;
layout(binding = 17) uniform samplerCube u_LightsCubemap;

layout(binding = 19) uniform sampler2D u_ShadowMap0;
layout(binding = 20) uniform sampler2D u_ShadowMap1;
layout(binding = 21) uniform sampler2D u_ShadowMap2;
layout(binding = 22) uniform sampler2D u_ShadowMap3;
layout(binding = 23) uniform sampler2D u_ShadowMap4;
layout(binding = 24) uniform sampler2D u_ShadowMap5;
layout(binding = 25) uniform sampler2D u_ShadowMap6;
layout(binding = 26) uniform sampler2D u_ShadowMap7;

//Constants
const float PI = 3.1415926535;

//Helper Functions
float saturate(float _val)
{
    return min(1.0, max(0.0, _val));
}

vec3 EnvBRDFApprox(vec3 specularColour, float roughness, float n_v)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * n_v)) * r.x + r.y;
    vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColour * AB.x + AB.y;
}

float GetRoughness(vec4 combinedLookup)
{
    return combinedLookup.r;
}

float GetMetallic(vec4 combinedLookup)
{
    return combinedLookup.g;
}

float GetAO(vec4 combinedLookup)
{
    return combinedLookup.b;
}

float GetSpecular(vec4 combinedLookup)
{
    return combinedLookup.a;
}

//BRDF Reflection Model to add from UE4:
//Diffuse: Burley, OrenNayar, Gotandas
//Specular: Dist.: Blinn, Bechmann, GGXaniso
//Specular: Vis.: Implicit, Neumann, Kelemen, Schlick, Smith, SmithJointAprrox.
//Specular: Fresnel: None, Fresnel.

//DIFFUSE
//Lambert
vec3 Lambertian(vec3 diffuseColour)
{
    return diffuseColour * 1.0 / PI;
}

//SPECULAR
//Fresnel-Schlick
vec3 fresnel_schlick(vec3 R0, float v_h)
{
    return R0 + (vec3(1.0,1.0,1.0) - R0) * pow((1.0 - v_h), 5.0);
}

//vec3 FresnelTerm(vec3 specularColour, float v_h)
//{
  //  vec3 fresnel = specularColour + (vec3(1.0,1.0,1.0)- specularColour) * pow((1.0 - v_h), 5.);
    //return fresnel;
//}

//SchlickGGX
float GSub(vec3 n, vec3 w, float a, bool directLight)
{
    float k = 0.0;
    if(directLight)
    k= 0.125 * pow((a + 1.0), 2.0);
    else
    k = 0.5 * pow(a, 2.0);

    float NDotV = dot(n, w);
    return NDotV / (NDotV * (1.0 - k)) + k;
}
//Smith
float G(vec3 n, vec3 wi, vec3 wo, float a2, bool directLight)
{
    return GSub(n, wi, a2, directLight) * GSub(n, wo, a2, directLight);
}
//GGX
float D(vec3 n, vec3 h, float a2)
{
    float temp = pow(saturate(dot(n, h)), 2.0) * (a2 - 1.0) + 1.0;
    return a2 / (PI * temp * temp);
}

float MipFromRoughness(float roughness, float CubemapMaxMip)
{
    return (log2(roughness * 1.2) + 3.0);
}


vec3 PBR(vec3 normal, vec3 viewDir, vec3 diffuseColour, float roughness,float metallic ,float ao) //Return a RGB value;
{
    float n_v				= saturate(dot(normal, viewDir));
    float cosLo				= saturate( dot(normal,- viewDir));
    // Constant normal incidence Fresnel factor for all dielectrics.
    vec3 Fdielectric		=vec3(0.04,0.04,0.04);
    // Fresnel reflectance at normal incidence (for metals use albedo color).
    vec3 F0					= lerp(Fdielectric, diffuseColour, metallic);
    vec3 F					= fresnel_schlick(F0, cosLo);
    vec3 kS					= F;
    vec3 kD					= lerp(vec3(1.0, 1.0, 1.0) - kS, vec3(0.0,0.0,0.0), metallic);

    float roughnessE =roughness*roughness;
    float roughnessL		= max(.01, roughnessE);

    float roughness_mip     =MipFromRoughness(roughness,5.0);

    vec3 normal_lookup      =vec3(-normal.z,normal.x,normal.y);
    vec3 refl = reflect(viewDir, normal);
    vec3 refl_lookup      =vec3(-refl.z,refl.x,refl.y);

    vec3 env_specular       =textureLod(u_SpecularCubemap, refl_lookup,roughness_mip).rgb;
    vec3 env_rough_specular =textureLod(u_RoughSpecularCubemap, refl_lookup,max(0.0,roughness_mip-3.0)).rgb;
    vec3 env_diffuse        =textureLod(u_RoughSpecularCubemap,normal_lookup,0.0).rgb;
    env_specular            =mix(env_specular,env_rough_specular,saturate(roughness_mip-2.0));
    //Environment Light Calculation
    //vec3 environment = mix(env_specular, env_diffuse, saturate((roughnessE - 0.25) / 0.75));

    //Diffuse
    vec3 diffuse			= kD*diffuseColour * env_diffuse*ao;

    //Specular

    vec3 envSpecularColour = EnvBRDFApprox(diffuseColour, roughnessE, n_v);

    vec3 specular	         =envSpecularColour * env_specular;
    specular				*=kS*saturate(pow(n_v + ao, roughnessE) - 1.0 + ao);

   // Specular += specular_light * saturate(dot(-viewDir, normal));

    //Ambient Occlusion
   // Specular *= saturate(pow(dot(normal, -viewDir) + ao, roughnessE) - 1.0 + ao);

	// factor diffuse by kD ???
    return diffuse+specular; //kS is already included in the Specular calculations.
}

vec4 Gamma(vec4 a)
{
    return pow(a,vec4(.45,.45,.45,1.0));
}

void Opaque()
{
    //Debug light
	Light d_Light;
	d_Light.u_Colour = vec4(1, 1, 1 ,1);
	d_Light.u_Position = vec3(1.3, 1.8, -7.6);
	d_Light.u_Power = 100.0;
	d_Light.u_Direction = vec3(0.0, -0.391, -0.921);

	vec3 Lo;				//Exitance Radiance from the surface in the direction of the camera.
    vec3 Le = vec3(0.0);	//Emissive Radiance from the surface in the direction of the camera, if any.

    vec4 combinedLookup = texture(u_Combined, v_UV_diffuse * u_CombinedTexCoordsScalar_R) * u_CombinedOutputScalar;

    //Primary non-light dependent
    float roughness = GetRoughness(combinedLookup);
    float roughnessE =roughness*roughness;
    float roughnessL = max(0.01, roughnessE);

    vec3 normalLookup = texture(u_Normal, v_UV_normal * u_NormalTexCoordsScalar_R).rgb;
    vec3 tangentSpaceNormalMap = 2.0 * (normalLookup.rgb - vec3(0.5, 0.5, 0.5));// * u_NormalOutputScalar.rgb;
    vec3 normal = normalize(v_TBN * tangentSpaceNormalMap);

    vec3 viewDir = normalize(v_Position-v_CameraPosition);
	vec3 diffuse_light = vec3(0, 0, 0);
	vec3 specular_light = vec3(0, 0, 0);
    float metallic = GetMetallic(combinedLookup);

    vec3 diffuseColour = texture(u_Diffuse, v_UV_diffuse * u_DiffuseTexCoordsScalar_R).rgb * u_DiffuseOutputScalar.rgb;

    //Loop over lights to calculate Lo (accumulation of L in the direction Wo)
    for(int i = 0; i < 1/*MaxLights*/; i++)
    {
        if(d_Light.u_Power == 0.0)
			continue;

        //Primary light dependent
        vec3 Wi = normalize(-v_Position + d_Light.u_Position);

        vec3 R0 = mix(diffuseColour, u_SpecularColour, metallic); //Mix R0 based on metallic look up.
        vec3 H = normalize(-viewDir + Wi);
        float D = D(normal, H, roughnessL);
        vec3 F = fresnel_schlick(R0, saturate(dot(H, Wi)));
        vec3 kS = F;
        vec3 kD = vec3(1.0, 1.0, 1.0) - kS;
        kD *= 1.0 - metallic; //Metallic materials will have no diffuse output.
        float G = G(normal, Wi, -viewDir, roughnessL, true);
       // specular_light += specular_light * D * F * G * (1.0 / 4.0 * saturate(dot(Wi, normal)));

        //Calucate irradance from the light over the sphere of directions.
        float distanceToLight   = length(-v_Position + d_Light.u_Position);
        vec3 SPD                = d_Light.u_Colour.xyz * d_Light.u_Power;
        vec3 irradiance         = SPD / (4.0 * PI * pow(distanceToLight, 2.0));

        //Because the radiance is only non-zero in the direction Wi,
        //We can replace radiance with irradiance;
        vec3 radiance           = irradiance;

		diffuse_light += Le ;
    }

    float ao = GetAO(combinedLookup);
	vec3 output_radiance = PBR(normal, viewDir, diffuseColour, roughness, metallic, ao);
   // output_radiance*=0.0001;
   // output_radiance+=combinedLookup.rgb;

   vec3 emissive = texture(u_Emissive, v_UV_diffuse * u_EmissiveTexCoordsScalar_R).rgb;
   emissive *= u_EmissiveOutputScalar.rgb;

    gl_FragColor = Gamma(vec4(output_radiance + emissive, 1.0));
}

void OpaqueAlbedo()
{
    vec3 diffuseColour = texture(u_Diffuse, v_UV_diffuse * u_DiffuseTexCoordsScalar_R).rgb * u_DiffuseOutputScalar.rgb;
    gl_FragColor = Gamma(vec4(diffuseColour, 1.0));
}