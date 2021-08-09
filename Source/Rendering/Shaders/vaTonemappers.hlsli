///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Collection of various tonemappers and related math - please see individual copyright and license (all MIT) notices
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//=================================================================================================
//
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//
//  All code licensed under the MIT license
//
//=================================================================================================

// The code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
float3 Tonemap_ACES(float3 x) 
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (x * (a * x + b)) / (x * (c * x + d) + e);
}
float3 InverseTonemap_ACES(float3 x) 
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return (-d * x + b - sqrt(-1.0127 * x*x + 1.3702 * x + 0.0009)) / (2.0 * (c*x - a));
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Uchimura 2017, "HDR theory and practice"
// Math: https://www.desmos.com/calculator/gslcdxvipg
// Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
float Tonemap_Uchimura(float x, float P, float a, float m, float l, float c, float b) {
    // Uchimura 2017, "HDR theory and practice"
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;

    float T = m * pow(x / m, c) + b;
    float S = P - (P - S1) * exp(CP * (x - S0));
    float L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}
float Tonemap_Uchimura(float x) {
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black
    const float b = 0.0;  // pedestal
    return Tonemap_Uchimura(x, P, a, m, l, c, b);
}
float3 Tonemap_Uchimura(float3 c) 
{
    return float3( Tonemap_Uchimura(c.x), Tonemap_Uchimura(c.y), Tonemap_Uchimura(c.z) );
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines" - https://gpuopen.com/wp-content/uploads/2016/03/GdcVdrLottes.pdf
float Tonemap_Lottes(float x, float contrast)  // <-added contrast but this is a hack and needs to be revisited
{
    const float a = 1.6 * contrast;
    const float d = 0.977;
    const float hdrMax = 8.0;
    const float midIn = 0.18;
    const float midOut = 0.267;

    // Can be precomputed
    const float b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const float c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);

    return pow(x, a) / (pow(x, a * d) * b + c);
}
//
float3 Tonemap_Lottes(float3 c, float contrast) 
{
#if 0 // my hacks
    const float maxVsLuma = 0.5;
    float cmax = lerp( max( max( c.x, c.y ), c.z ), CalcLuminance( c ), maxVsLuma );
    float tmcmax = Tonemap_Lottes(cmax, contrast);
    return c * (tmcmax / cmax);
#elif 0
    float luminance = CalcLuminance( c );
    float tmpl = Tonemap_Lottes( luminance, contrast );
    return c * (tmpl / luminance);
#else
    return float3( Tonemap_Lottes(c.x, contrast), Tonemap_Lottes(c.y, contrast), Tonemap_Lottes(c.z, contrast) );
#endif
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gamma test - https://www.shadertoy.com/view/llfSRM
float3 GammaTestPattern( float2 fc, float v )
{
#define mod(x, y) (x - y * floor(x / y))
    //note: 2x2 ordered dithering, ALU-based (omgthehorror)
    float2 ij = floor( mod( fc.xy, float2(2.0,2.0) ) );
    float idx = ij.x + 2.0*ij.y;
    float4 m = step( abs(float4(idx.xxxx)-float4(0.0,1.0,2.0,3.0)), float4(0.5,0.5,0.5,0.5) ) * float4(0.75,0.25,0.00,0.50);
    float d = m.x+m.y+m.z+m.w;

    float ret = step(d,v);
    return float3( ret, ret, ret );  // * bias;
#undef mod
}
//
float3 GammaTest( const float2 fragCoord, const float2 resolution )
{
    float3 outcol = float3(0.0.xxx);

    float2 uv = fragCoord.xy / resolution.xy;

    //rect
    //bool v0 = (fract(3.0 * uv.x) > 0.25) && (fract(3.0 * uv.x) < 0.75);
    //bool v1 = (uv.y > 0.3) && (uv.y < 0.6);
    //bool ref = v0 && v1;

    //circle
    float2 aspect = float2( 1.0, resolution.y / resolution.x );
    bool ref = length((float2(1.0/6.0,0.5)-uv)*aspect) < 0.11 ||
        length((float2(3.0/6.0,0.5)-uv)*aspect) < 0.11 || 
        length((float2(5.0/6.0,0.5)-uv)*aspect) < 0.11;

    if ( uv.x < 1.0/3.0 )
        outcol.rgb = ref ? float3(0.25,0.25,0.25) : GammaTestPattern(fragCoord,0.15);
    else if ( uv.x < 2.0/3.0 )
        outcol.rgb = ref ? float3(0.50,0.50,0.50) : GammaTestPattern(fragCoord,0.30);
    else
        outcol.rgb = ref ? float3(0.75,0.75,0.75) : GammaTestPattern(fragCoord,0.60);

//    float gamma = 1.5 + iMouse.x/iResolution.x;
//    outcol = pow( outcol, float4(1.0 / (1.5 + iMouse.x/iResolution.x)) );
//    //outcol = pow( outcol, float4(1.0 / 2.00) ); //dell 2410
//    //outcol = pow( outcol, float4(1.0 / 2.15) ); //NEC ps272w
//
//    //outcol.rgb = lerp( outcol.rgb, float3(0.0), PrintValue( (uv-float2(0.43,0.9))*40.0, gamma, 1.0, 2.0) );

    return outcol;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Generic tonemapping useful for averaging in tonemapped space for purposes of AA and similar.
// It doesn't have to match actually used tonemapper perfectly but should be close enough.
// Current default is simply a slightly modified reversible ACES. 
// 
static const float c_GT_ACES_Scale = 1.027;    // invertible aces tends to cut off at the high end so this compresses everything a bit in order to minimize that
//
float3 GenericTonemap( float3 linearColor )
{
    return Tonemap_ACES( linearColor ) / c_GT_ACES_Scale;
}
//
float3 GenericTonemapInverse( float3 tonemappedColor )
{
    return InverseTonemap_ACES( tonemappedColor * c_GT_ACES_Scale );
}
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
