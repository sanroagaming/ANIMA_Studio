RWTexture2D<float4> gOutput : register(u0);
cbuffer FrameConstants : register(b0) {
    uint gWidth;
    uint gHeight;
    float gTime;
    uint gRayTracing;
};

struct SceneHit { float distance; float material; };

float SdBox(float3 p, float3 b) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}
float SdSphere(float3 p, float r) { return length(p) - r; }

void Choose(inout SceneHit result, float d, float material) {
    if (d < result.distance) { result.distance = d; result.material = material; }
}

SceneHit SceneMap(float3 p) {
    SceneHit h; h.distance = 1.0e6; h.material = 0.0;
    // Interior shell: floor, side walls and ceiling.
    Choose(h, p.y + 1.48, 1.0);        // damp concrete / timber floor
    Choose(h, p.x + 4.25, 2.0);        // left plaster wall
    Choose(h, 4.25 - p.x, 2.0);        // right plaster wall
    Choose(h, 3.25 - p.y, 2.0);        // ceiling

    // Back wall leaves a tall central window opening.
    Choose(h, SdBox(p - float3(-2.45, 0.85, 4.95), float3(1.80, 2.10, 0.14)), 2.0);
    Choose(h, SdBox(p - float3( 2.45, 0.85, 4.95), float3(1.80, 2.10, 0.14)), 2.0);
    Choose(h, SdBox(p - float3(0.0, 2.90, 4.95), float3(0.70, 0.35, 0.14)), 2.0);
    Choose(h, SdBox(p - float3(0.0,-0.95, 4.95), float3(0.70, 0.53, 0.14)), 2.0);

    // Old dark timber window frame and crossbar.
    Choose(h, SdBox(p - float3(-0.72, 0.95, 4.72), float3(0.055, 1.43, 0.10)), 3.0);
    Choose(h, SdBox(p - float3( 0.72, 0.95, 4.72), float3(0.055, 1.43, 0.10)), 3.0);
    Choose(h, SdBox(p - float3(0.0, 2.36, 4.72), float3(0.78, 0.055, 0.10)), 3.0);
    Choose(h, SdBox(p - float3(0.0,-0.42, 4.72), float3(0.78, 0.055, 0.10)), 3.0);
    Choose(h, SdBox(p - float3(0.0, 1.10, 4.72), float3(0.025, 1.20, 0.10)), 3.0);
    Choose(h, SdBox(p - float3(0.0, 1.45, 4.72), float3(0.70, 0.025, 0.10)), 3.0);

    // Worn couch with separate back, seat, and arms.
    Choose(h, SdBox(p - float3(-1.60,-0.93, 1.10), float3(1.02, 0.28, 0.63)), 4.0);
    Choose(h, SdBox(p - float3(-1.60,-0.48, 1.62), float3(1.02, 0.58, 0.18)), 4.0);
    Choose(h, SdBox(p - float3(-2.53,-0.71, 1.11), float3(0.13, 0.48, 0.66)), 4.0);
    Choose(h, SdBox(p - float3(-0.67,-0.71, 1.11), float3(0.13, 0.48, 0.66)), 4.0);
    // Low table and legs.
    Choose(h, SdBox(p - float3(0.95,-0.69, 0.05), float3(0.90, 0.08, 0.62)), 5.0);
    Choose(h, SdBox(p - float3(0.23,-1.06,-0.36), float3(0.055, 0.37, 0.055)), 5.0);
    Choose(h, SdBox(p - float3(1.67,-1.06,-0.36), float3(0.055, 0.37, 0.055)), 5.0);
    Choose(h, SdBox(p - float3(0.23,-1.06, 0.46), float3(0.055, 0.37, 0.055)), 5.0);
    Choose(h, SdBox(p - float3(1.67,-1.06, 0.46), float3(0.055, 0.37, 0.055)), 5.0);

    // Green growth just outside the glassless opening.
    Choose(h, SdSphere(p - float3(-0.45, 1.75, 5.95), 0.38), 6.0);
    Choose(h, SdSphere(p - float3( 0.17, 1.95, 6.10), 0.43), 6.0);
    Choose(h, SdSphere(p - float3( 0.52, 1.52, 5.85), 0.36), 6.0);
    Choose(h, SdSphere(p - float3(-0.05, 0.95, 6.18), 0.32), 6.0);
    // A broken plant / climbing vine near the window frame.
    Choose(h, SdSphere(p - float3(-0.86, 0.22 + 0.04 * sin(gTime), 3.95), 0.16), 6.0);
    return h;
}

float3 SceneNormal(float3 p) {
    const float e = 0.0025;
    float dx = SceneMap(p + float3(e,0,0)).distance - SceneMap(p - float3(e,0,0)).distance;
    float dy = SceneMap(p + float3(0,e,0)).distance - SceneMap(p - float3(0,e,0)).distance;
    float dz = SceneMap(p + float3(0,0,e)).distance - SceneMap(p - float3(0,0,e)).distance;
    return normalize(float3(dx,dy,dz));
}

bool Trace(float3 ro, float3 rd, float maxDistance, out float travel, out float material) {
    travel = 0.0; material = 0.0;
    [loop] for (int step = 0; step < 100; ++step) {
        SceneHit h = SceneMap(ro + rd * travel);
        if (h.distance < 0.0035) { material = h.material; return true; }
        travel += max(h.distance * 0.82, 0.0015);
        if (travel > maxDistance) break;
    }
    return false;
}

float3 Albedo(float material) {
    if (material < 1.5) return float3(0.25, 0.25, 0.20); // wet floor
    if (material < 2.5) return float3(0.25, 0.27, 0.22); // plaster
    if (material < 3.5) return float3(0.15, 0.095, 0.050); // old wood
    if (material < 4.5) return float3(0.18, 0.22, 0.15); // couch
    if (material < 5.5) return float3(0.30, 0.205, 0.11); // table
    return float3(0.12, 0.28, 0.11); // leaves
}

float3 Environment(float3 rd) {
    float sky = saturate(rd.y * 0.5 + 0.52);
    float3 c = lerp(float3(0.045,0.060,0.045), float3(0.38,0.48,0.37), sky);
    float leafNoise = sin(rd.x * 37.0 + rd.y * 13.0) * sin(rd.y * 29.0 - rd.x * 11.0);
    if (rd.z > 0.65 && leafNoise > 0.35) c = lerp(c, float3(0.11,0.22,0.08), 0.6);
    return c;
}

float3 EstimateVolume(float3 ro, float3 rd, float endT) {
    float3 beamA = float3(0.0, 1.85, 4.62);
    float3 beamB = float3(0.55,-1.42,-0.65);
    float3 ba = beamB - beamA;
    float ba2 = max(dot(ba,ba), 0.001);
    float densitySum = 0.0;
    [loop] for (int i = 0; i < 26; ++i) {
        float t = endT * ((float)i + 0.5) / 26.0;
        float3 p = ro + rd * t;
        float h = saturate(dot(p - beamA, ba) / ba2);
        float d = length(p - (beamA + h * ba));
        float dust = 0.65 + 0.35 * sin(p.x * 8.0 + p.y * 3.0 + gTime * 0.28);
        densitySum += exp(-d * 2.9) * dust;
    }
    return float3(1.0,0.77,0.43) * densitySum * (endT / 26.0) * 0.090;
}

float3 ShadeHit(float3 ro, float3 rd, float t, float material, uint2 pixel) {
    float3 p = ro + rd * t;
    float3 n = SceneNormal(p);
    float3 albedo = Albedo(material);
    float3 lightDir = normalize(float3(-0.18, 0.40, 0.90)); // window light entering the room
    float ndl = saturate(dot(n, lightDir));
    float shadow = 1.0;
    if (gRayTracing != 0) {
        float shadowT; float shadowMat;
        if (Trace(p + n * 0.012, lightDir, 8.5, shadowT, shadowMat)) shadow = 0.20;
    }
    float3 color = albedo * (0.10 + ndl * shadow * 1.45);
    color += albedo * float3(0.10,0.12,0.08) * saturate(n.y * 0.5 + 0.5);

    if (gRayTracing != 0) {
        // One inexpensive secondary diffuse sample gives a soft indirect tint.
        float2 seed = (float2(pixel) + float2(0.13,0.71)) * float2(0.0671,0.0058);
        float3 randomDir = normalize(float3(frac(sin(dot(seed,float2(12.9898,78.233)))*43758.5453)-0.5,
                                            0.55 + frac(sin(dot(seed,float2(39.3468,11.135)))*24634.6345),
                                            frac(sin(dot(seed,float2(73.156,52.235)))*12345.6789)-0.5));
        float3 bounceDir = normalize(n + randomDir);
        float bounceT; float bounceMat;
        if (Trace(p + n * 0.018, bounceDir, 6.0, bounceT, bounceMat))
            color += albedo * Albedo(bounceMat) * 0.18;

        // Wet floor/table receive a secondary reflection ray.
        if (material < 1.5 || material > 4.5 && material < 5.5) {
            float reflT; float reflMat;
            float3 reflDir = reflect(rd, n);
            if (Trace(p + n * 0.025, reflDir, 12.0, reflT, reflMat))
                color = lerp(color, Albedo(reflMat) * (0.18 + saturate(dot(SceneNormal(p + reflDir * reflT), lightDir)) * 0.55), material < 1.5 ? 0.26 : 0.15);
            else color = lerp(color, Environment(reflDir), 0.18);
        }
        color += EstimateVolume(ro, rd, max(t, 0.2));
    }
    float fog = 1.0 - exp(-t * 0.014);
    return lerp(color, float3(0.085,0.105,0.078), fog * 0.42);
}

uint GlyphRow(uint ch, uint row) {
    // Compact 5x7 bitmap glyphs used for native in-scene ON/OFF controls.
    if (ch == 82) { if(row==0)return 14;if(row==1)return 17;if(row==2)return 17;if(row==3)return 30;if(row==4)return 20;if(row==5)return 18;return 17; } // R
    if (ch == 84) { if(row==0)return 31;return 4; } // T
    if (ch == 79) { if(row==0||row==6)return 14;return 17; } // O
    if (ch == 78) { if(row==0)return 17;if(row==1)return 25;if(row==2)return 21;if(row==3)return 19;return 17; } // N
    if (ch == 70) { if(row==0)return 31;if(row==1||row==2||row==4||row==5||row==6)return 16;return 30; } // F
    return 0;
}
bool TextPixel(uint2 pixel, uint2 origin, uint ch, uint scale) {
    if (pixel.x < origin.x || pixel.y < origin.y) return false;
    uint x = (pixel.x - origin.x) / scale;
    uint y = (pixel.y - origin.y) / scale;
    if (x >= 5 || y >= 7) return false;
    return ((GlyphRow(ch,y) >> (4-x)) & 1u) != 0;
}
bool TextSequence(uint2 p, uint2 origin, uint scale, bool offButton) {
    if (TextPixel(p, origin, 82, scale)) return true;                         // R
    if (TextPixel(p, origin + uint2(6 * scale,0), 84, scale)) return true;   // T
    if (TextPixel(p, origin + uint2(18 * scale,0), 79, scale)) return true;  // O
    if (!offButton) return TextPixel(p, origin + uint2(24 * scale,0), 78, scale); // N
    if (TextPixel(p, origin + uint2(24 * scale,0), 70, scale)) return true;  // F
    return TextPixel(p, origin + uint2(30 * scale,0), 70, scale);            // F
}
float3 ApplyOverlay(uint2 pixel, float3 color) {
    bool inOn = pixel.x >= 16 && pixel.x < 108 && pixel.y >= 16 && pixel.y < 48;
    bool inOff = pixel.x >= 116 && pixel.x < 224 && pixel.y >= 16 && pixel.y < 48;
    if (inOn || inOff) {
        bool active = (gRayTracing != 0 && inOn) || (gRayTracing == 0 && inOff);
        float3 panel = active ? float3(0.73,0.84,0.61) : float3(0.065,0.078,0.064);
        color = lerp(color, panel, active ? 0.98 : 0.92);
        if (TextSequence(pixel, uint2(inOn?25:126, 27), 2, inOff))
            color = active ? float3(0.055,0.075,0.045) : float3(0.78,0.84,0.75);
        // Tiny status pip.
        if (pixel.y >= 27 && pixel.y <= 35 && pixel.x >= (inOn?18:118) && pixel.x <= (inOn?23:123))
            color = active ? float3(0.16,0.44,0.08) : float3(0.34,0.40,0.31);
    }
    return color;
}

[numthreads(8,8,1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID) {
    if (dispatchId.x >= gWidth || dispatchId.y >= gHeight) return;
    uint2 pixel = dispatchId.xy;
    float2 uv = ((float2(pixel) + 0.5) / float2(gWidth,gHeight)) * 2.0 - 1.0;
    uv.x *= (float)gWidth / (float)gHeight;
    uv.y = -uv.y;
    float3 ro = float3(0.0, 0.65, -6.1);
    float3 rd = normalize(float3(uv.x * 0.90, uv.y * 0.62, 1.45));
    // A slow camera drift makes the render feel alive without invalidating the scene.
    ro.x += sin(gTime * 0.12) * 0.025;
    float t; float material;
    float3 color;
    if (Trace(ro, rd, 28.0, t, material)) color = ShadeHit(ro, rd, t, material, pixel);
    else color = Environment(rd);
    // Cinematic color curve and subtle film grain.
    color = max(color,0.0);
    color = color / (1.0 + color * 0.48);
    color = pow(saturate(color), 1.0/2.2);
    float noise = frac(sin(dot(float2(pixel),float2(12.9898,78.233)) + gTime * 4.0)*43758.5453) - 0.5;
    color += noise / 255.0;
    color = ApplyOverlay(pixel, saturate(color));
    gOutput[pixel] = float4(saturate(color),1.0);
}
