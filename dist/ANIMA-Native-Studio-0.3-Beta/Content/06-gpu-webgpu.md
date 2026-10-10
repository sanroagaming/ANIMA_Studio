# GPU graphics and WebGPU
> Give ANIMA programs a bridge to shader-based graphics and compute.

## What the current browser bridge exposes

The browser runtime provides a `gpu` capability object to supported event handlers. WebGPU requires a compatible browser, driver, and secure context (normally HTTPS or localhost).

```anima
node RequestGPU: Button {
  text: "Request GPU";
  on click: gpu.request();
}
node ComputeGPU: Button {
  text: "Compute demo";
  on click: gpu.computeDemo();
}
node RenderGPU: Button {
  text: "Render demo";
  on click: gpu.renderDemo("GpuCanvas");
}
node StopGPU: Button {
  text: "Stop render";
  on click: gpu.stop();
}
node GpuCanvas: canvas {
  wide: "100%";
  tall: 260;
}
```

## Available helper methods

- `gpu.request()` — request an adapter/device after a user gesture.
- `gpu.compute(wgsl, numericArray[, workgroups])` — run the bridge's single-storage-array compute helper and read back float values.
- `gpu.render(canvasNodeName, fragmentFunction)` — create a render loop around a `fragmentColor(uv: vec2<f32>, time: f32) -> vec4<f32>` function.
- `gpu.roomRaytrace(canvasNodeName, enabled)` — start the procedural room study on a canvas node.
- `gpu.setRayTracing(true|false)` — choose the enhanced compute-tracing mode or direct-light reference mode, clearing progressive accumulation when the mode changes.
- `gpu.resetRayTracing()` — clear the accumulated samples.
- `gpu.roomStats()` — report width, height, mode, samples and active status.
- `gpu.stop()` — stop active GPU animation loops.
- `Anima.webgpu.requestDevice()` — host-side access to a raw `GPUAdapter` and `GPUDevice`.

## Architecture and caveats

WebGPU exposes general GPU compute and graphics pipelines. It is not automatically a dedicated hardware ray-tracing API. The current room shader explicitly tests rays against a small set of analytic shapes in a WGSL compute pipeline; that is distinct from a DXR acceleration-structure pipeline using an RTX card's dedicated RT cores.

The isolated preview editor may restrict GPU access. Use the standalone host example on HTTPS or `localhost` when testing actual WebGPU. If the browser doesn't expose `navigator.gpu`, the bridge should report the error rather than silently pretending the GPU path ran.

## Native direction

The native Windows track uses C++ and Direct3D 12. The included native renderer performs compute-shader tracing. A true DXR backend—with BLAS/TLAS acceleration structures and ray-generation/hit/miss shader stages—is the next native graphics milestone, not something the current project should claim it already does.
