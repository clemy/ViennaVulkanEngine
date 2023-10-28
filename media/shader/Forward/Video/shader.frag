#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_ARB_shading_language_420pack : enable
#extension GL_EXT_nonuniform_qualifier : enable

#include "../../common_defines.glsl"
#include "../../light.glsl"

layout(early_fragment_tests) in;

layout(location = 0) in vec3 fragPosW;
layout(location = 1) in vec3 fragNormalW;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform cameraUBO_t {
    cameraData_t data;
} cameraUBO;

layout(set = 1, binding = 0) uniform lightUBO_t {
    lightData_t data;
} lightUBO;

layout(set = 2, binding = 0) uniform sampler2D shadowMap[NUM_SHADOW_CASCADE];

layout(set = 3, binding = 0) uniform objectUBO_t {
    objectData_t data;
} objectUBO;

layout(set = 4, binding = 0) uniform sampler2D texVideo[RESOURCEARRAYLENGTH];


void main() {

    //parameters
    int  lightType  = lightUBO.data.itype[0];
    vec3 camPosW    = cameraUBO.data.camModel[3].xyz;
    vec3 lightPosW  = lightUBO.data.lightModel[3].xyz;
    vec3 lightDirW  = normalize(lightUBO.data.lightModel[2].xyz);
    vec4 lightParam = lightUBO.data.param;
    vec4 texParam   = objectUBO.data.param;
    vec2 texCoord   = (fragTexCoord + texParam.zw)*texParam.xy;
    vec3 normalW    = fragNormalW;//to be consistent with DN
    ivec4 iparam    = objectUBO.data.iparam;
    uint resIdx     = iparam.x % RESOURCEARRAYLENGTH;
    //colors
    vec3 ambcol  = lightUBO.data.col_ambient.xyz;
    vec3 diffcol = lightUBO.data.col_diffuse.xyz;
    vec3 speccol = lightUBO.data.col_specular.xyz;
    vec3 fragColor;

    // workaround as dynamic indexing does not work for yCbCrConversionSampler (at least on my NVidia)
    if (resIdx == 0) fragColor = texture(texVideo[0], texCoord).xyz;
    else if (resIdx == 1) fragColor = texture(texVideo[1], texCoord).xyz;
    else if (resIdx == 2) fragColor = texture(texVideo[2], texCoord).xyz;
    else if (resIdx == 3) fragColor = texture(texVideo[3], texCoord).xyz;
    else if (resIdx == 4) fragColor = texture(texVideo[4], texCoord).xyz;
    else if (resIdx == 5) fragColor = texture(texVideo[5], texCoord).xyz;
    else if (resIdx == 6) fragColor = texture(texVideo[6], texCoord).xyz;
    else if (resIdx == 7) fragColor = texture(texVideo[7], texCoord).xyz;
    else if (resIdx == 8) fragColor = texture(texVideo[8], texCoord).xyz;
    else if (resIdx == 9) fragColor = texture(texVideo[9], texCoord).xyz;
    else if (resIdx == 10) fragColor = texture(texVideo[10], texCoord).xyz;
    else if (resIdx == 11) fragColor = texture(texVideo[11], texCoord).xyz;
    else if (resIdx == 12) fragColor = texture(texVideo[12], texCoord).xyz;
    else if (resIdx == 13) fragColor = texture(texVideo[13], texCoord).xyz;
    else if (resIdx == 14) fragColor = texture(texVideo[14], texCoord).xyz;
    else if (resIdx == 15) fragColor = texture(texVideo[15], texCoord).xyz;
    
    vec3 result = vec3(0, 0, 0);
    int sIdx = 0;
    cameraData_t s = lightUBO.data.shadowCameras[0];
    float shadowFactor = 1.0;

    if (lightType == LIGHT_DIR) {
        sIdx = shadowIdxDirectional(cameraUBO.data.param,
        gl_FragCoord,
        lightUBO.data.shadowCameras[0].param[3],
        lightUBO.data.shadowCameras[1].param[3],
        lightUBO.data.shadowCameras[2].param[3]);

        s = lightUBO.data.shadowCameras[sIdx];
        shadowFactor = shadowFunc(fragPosW, s.camView, s.camProj, shadowMap[sIdx]);

        result +=   dirlight(lightType, camPosW,
        lightDirW, lightParam, shadowFactor,
        ambcol, diffcol, speccol,
        fragPosW, normalW, fragColor);
    }


    if (lightType == LIGHT_POINT) {

        sIdx = shadowIdxPoint(lightPosW, fragPosW);
        s = lightUBO.data.shadowCameras[sIdx];
        shadowFactor = shadowFunc(fragPosW, s.camView, s.camProj, shadowMap[sIdx]);

        result +=   pointlight(lightType, camPosW,
        lightPosW, lightParam, shadowFactor,
        ambcol, diffcol, speccol,
        fragPosW, normalW, fragColor);
    }

    if (lightType == LIGHT_SPOT) {

        shadowFactor = shadowFunc(fragPosW, s.camView, s.camProj, shadowMap[sIdx]);

        result +=  spotlight(lightType, camPosW,
        lightPosW, lightDirW, lightParam, shadowFactor,
        ambcol, diffcol, speccol,
        fragPosW, normalW, fragColor);
    }

    if (lightType == LIGHT_AMBIENT) {
        result += fragColor * ambcol;
    }

    outColor = vec4(result, 1.0);
}
