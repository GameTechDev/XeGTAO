//------------------------------------------------------------------------------
// Uniforms access
//------------------------------------------------------------------------------

/** @public-api */
mat4 getViewFromWorldMatrix() {
    return g_globals.View;
}

/** @public-api */
mat4 getWorldFromViewMatrix() {
    return g_globals.ViewInv;
}

/** @public-api */
mat4 getClipFromViewMatrix() {
    return g_globals.Proj;
}

/** @public-api */
mat4 getViewFromClipMatrix() {
    return g_globals.ProjInv;
}

/** @public-api */
mat4 getClipFromWorldMatrix() {
    return g_globals.ViewProj;
}

/** @public-api */
mat4 getWorldFromClipMatrix() {
    return g_globals.ViewProjInv;
}

/** @public-api */
vec4 getResolution() {
    return float4( g_globals.ViewportSize, g_globals.ViewportPixelSize );
}

/** @public-api */
vec3 getWorldCameraPosition() {
    return g_globals.CameraWorldPosition.xyz;
}

// /** @public-api */
// highp vec3 getWorldOffset() {
//     return frameUniforms.worldOffset;
// }

/** @public-api */
float getTime() {
    return g_globals.TimeFmod3600;
}

// /** @public-api */
// highp vec4 getUserTime() {
//     return frameUniforms.userTime;
// }

// /** @public-api **/
// highp float getUserTimeMod(float m) {
//     return mod(mod(frameUniforms.userTime.x, m) + mod(frameUniforms.userTime.y, m), m);
// }

// /** @public-api */
// float getExposure() {
//     return frameUniforms.exposure;
// }

// /** @public-api */
// float getEV100() {
//     return frameUniforms.ev100;
// }
