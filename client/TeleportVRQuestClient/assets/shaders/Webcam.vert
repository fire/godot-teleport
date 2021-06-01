precision highp float;

layout(location = 0) in vec3 position;

//layout(location = 0) out vec3 vSampleVec;
//layout(location = 5) out vec3 vEyeOffset;
layout(location = 1) out vec2 vTexCoords;

layout(binding = 0) uniform sampler2D videoTexture;
layout(std140, binding = 1) uniform webcamUB
{
    uvec2 sourceTexSize;
    // Offset of webcam texture in video texture
    ivec2 sourceOffset;
    uvec2 camTexSize;
} webcam;

void main()
{
    // Offsets from sourceOffset which is at top left of webcam texture
    ivec2 vOffsets[4];
    vOffsets[0] = ivec2(0, -1); // Bottom Left
    vOffsets[1] = ivec2(0, 0); // Top Left
    vOffsets[2] = ivec2(1, 0); // Top Right
    vOffsets[3] = ivec2(1, -1); // Bottom Right
    ivec2 vOffset = vOffsets[gl_VertexID];
    ivec2 pos = webcam.sourceOffset + (vOffset *  ivec2(webcam.camTexSize));

    vTexCoords = vec2(pos) / vec2(webcam.sourceTexSize);

    //vec2 uvs[4];
    //uvs[0] = vec2(0.0, 0.0);
    //uvs[1] = vec2(0.0, 1.0);
    //uvs[2] = vec2(1.0, 1.0);
    //uvs[3] = vec2(1.0, 0.0);
    //vTexCoords = uvs[gl_VertexID];

    vec4 p = vec4(position, 1.0);
    p = ModelMatrix * p;
    gl_Position = vec4(p.xyz, 1.0);
}
