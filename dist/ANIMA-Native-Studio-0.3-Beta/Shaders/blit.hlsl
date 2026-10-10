Texture2D<float4> gSource : register(t0);
SamplerState gSampler : register(s0);
struct VertexOut { float4 position : SV_Position; float2 uv : TEXCOORD0; };
VertexOut VSMain(uint id : SV_VertexID) {
    VertexOut o;
    float2 pos = (id == 0) ? float2(-1.0,-1.0) : ((id == 1) ? float2(-1.0,3.0) : float2(3.0,-1.0));
    o.position = float4(pos, 0.0, 1.0);
    o.uv = float2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);
    return o;
}
float4 PSMain(VertexOut input) : SV_Target0 { return gSource.Sample(gSampler, input.uv); }
