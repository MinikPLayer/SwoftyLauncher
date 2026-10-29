# render: D3D12/DXGI present path and the swapchain overlay

Owner: see `subsystems.tsv`. Consumer: `native/src/core/hud.cpp` (the overlay standard library, experimental `overlay` feature gate).

## Map
- The game renders with Direct3D 12 (`d3d12core.dll` + `dxgi.dll`); there is no exe-side present call site catalogued yet.
- The overlay reaches the present path through the stable COM vtable ABI, not through any `F1_25.exe` address, so it needs no KB VA and survives game updates. At arm time it creates a throwaway `D3D12CreateDevice` + `CreateDXGIFactory1` + `CreateSwapChainForHwnd` probe, reads three vtable slots, installs safetyhook inline hooks on the shared code they point at, then destroys the probe:
  - `IDXGISwapChain::Present` — vtable slot 8 (vt+0x40). Draws the frame, then calls the original.
  - `IDXGISwapChain::ResizeBuffers` — vtable slot 13 (vt+0x68). Releases the overlay's back-buffer references before the game resizes.
  - `ID3D12CommandQueue::ExecuteCommandLists` — vtable slot 10 (vt+0x50). Captures the game's `DIRECT` command queue (the one Present needs to submit the overlay's command list).

## Open
- The exe-side present call site (the render-loop function that calls `swapchain->Present`): decompile and catalogue as `Render_*` once observed in game, for a lower-overhead exe hook than the vtable probe.
- Text and image rendering: the first landing paints solid rects / outlines / lines only; a font atlas and textured-quad path remain.

## Inbox
- 2026-10-19 14:03 ui-re: new 'hud' lane holds the HUD view-model/logic side (what widgets publish + their setToOpen gates); render still owns the D3D present/draw path. Hud_Open_TyrePerformance (0x14152b5f0) publishes to view_model_data.hud_* registry nodes. HUD VM vtables cluster near 0x1456ed290 if you need draw-time addresses.
- 2026-10-28 22:57 re-import: (wx rounds 1-3, static, verified by wx-verify): render flags +0x32 live on the per-entity Render component (type 0x146cb7b70, vtable 0x145a90b10, parser 0x1444725e0); the Model component (type 0x146cb7da0, vtable 0x145a908a0) holds +0xb0 Render, +0x80 render instance (current world matrix inst+0x10..+0x3f, previous +0x40..+0x6f, generator mask +0x70), +0xd8 model path. Per-model setter Render_ModelComponent_ApplyRenderProps 0x14446ea70 -> 0x14446b9c0 (KB fix: not LitPlanarReflectionProxy). rainMask (0x20) = screen-space depth from the main camera that clips falling rain only; wetness map (0x40) = top-down ortho 75 x 75 m around the player, 256x256 (0x14478c050 / 0x14478e440); dry/wet meaning lives in material shaders. Quality handler 0x14015aa00 field map in game notes. | findings from PR #471 by vogtmarcel07-cloud
