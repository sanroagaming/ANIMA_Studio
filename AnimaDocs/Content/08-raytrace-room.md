# Ray-traced room study
> A moody interior with a tall window, dust, overgrowth, and a ray-lighting comparison.

[image:room-comparison]

## The comparison

The room scene draws on the mood of an abandoned interior lit by a large window: weathered surfaces, plant growth, strong light direction, and visible suspended dust. It is an original procedural study, not a reproduction of a specific game scene.

- **Enhanced / ON:** compute-shader ray tests for visibility, one secondary diffuse sample, selected reflective contributions, progressive image accumulation, tone mapping, and a short ray-marched approximation of a light volume.
- **Reference / OFF:** same room/camera with direct-light shading; ray-tested visibility, secondary bounce and volume effects are disabled.

## Browser ANIMA source

```anima
node RayOn: Button {
  text: "Ray tracing · ON";
  on click: gpu.setRayTracing(true);
}
node RayOff: Button {
  text: "Ray tracing · OFF";
  on click: gpu.setRayTracing(false);
}
node ResetRoom: Button {
  text: "Reset samples";
  on click: gpu.resetRayTracing();
}
node RoomCanvas: canvas {
  wide: "100%";
  tall: 520;
}
// Host: instance.gpu.roomRaytrace("RoomCanvas", true);
```

## Run the native room

Use **Run room demo** in this page's native app toolbar action, or run `AnimaNative.exe` from Visual Studio after building the solution. Toggle lighting with the on-screen buttons, press `R`, or use `1` and `0` for ON and OFF. The title bar reports the selected adapter and whether DXR support was detected.

## Honest renderer status

> The current native renderer uses ray-marching / ray tests in a D3D12 compute shader. It checks for DXR support but does not yet build DXR acceleration structures or a DXR state object. The ON label means enhanced compute tracing, not proof that dedicated RTX RT cores are active.

This remains a prototype: analytic primitive geometry, no BVH acceleration, no denoiser, no complete BSDF/material system, and no production-quality atmospheric model. The browser fallback comparison image is a static illustrative asset, not a GPU render.
