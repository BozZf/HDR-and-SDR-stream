// WGC FP16 scRGB: linear Rec.709 primaries, 1.0 = 80 cd/m^2.
Texture2D<float4> source : register(t0);
SamplerState linearSampler : register(s0);
cbuffer Settings : register(b0) {
    float exposureEV;
    float referenceWhiteNits;
    float shoulderStart;
};
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
// The processed SDR frame is already gamma encoded. Present it unchanged.
float4 DisplayPS(Vertex v) : SV_TARGET {
    return float4(source.Sample(linearSampler, v.uv).rgb, 1);
}
Vertex VS(uint id : SV_VertexID) {
    Vertex v;
    v.uv = float2((id << 1) & 2, id & 2);
    v.position = float4(v.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return v;
}
float3 encodeSRGB(float3 c) {
    return float3(c.r <= 0.0031308 ? 12.92*c.r : 1.055*pow(c.r, 1.0/2.4)-0.055,
                  c.g <= 0.0031308 ? 12.92*c.g : 1.055*pow(c.g, 1.0/2.4)-0.055,
                  c.b <= 0.0031308 ? 12.92*c.b : 1.055*pow(c.b, 1.0/2.4)-0.055);
}
float4 PS(Vertex v) : SV_TARGET {
    float3 captured = source.Sample(linearSampler, v.uv).rgb;
    float3 rgb = max(captured, 0) * (80.0 / referenceWhiteNits) * exp2(exposureEV);
    float y = dot(rgb, float3(0.2126, 0.7152, 0.0722));
    float k = shoulderStart;
    float mapped = y <= k ? y : k + (1-k) * (1-exp(-(y-k)/(1-k)));
    rgb *= mapped / max(y, 1e-8);
    // Compress out-of-gamut saturation toward the mapped neutral, rather than
    // independently clipping channels and changing hue in saturated highlights.
    float peak = max(rgb.r, max(rgb.g, rgb.b));
    float saturation = peak > 1 ? saturate((1-mapped) / max(peak-mapped, 1e-8)) : 1;
    rgb = lerp(mapped.xxx, rgb, saturation);
    return float4(encodeSRGB(saturate(rgb)), 1);
}
