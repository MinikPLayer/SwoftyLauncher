# Third-party notices

## Geode docs theme

`docs/.vitepress/theme/styles/flash.css` is adapted from the stylesheets in `flash-template/` of the Geode docs, <https://github.com/geode-sdk/docs>. Its selectors are changed to fit VitePress and the peach theme is the default.

```
MIT License

Copyright (c) 2022 Geode SDK developers

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## JSONTestSuite

`native/test/json/suite/` holds the `y_`, `n_` and `i_` parsing cases of JSONTestSuite at commit 1ef36fa, <https://github.com/nst/JSONTestSuite>, unchanged. The `swofty::json` unit test runs them.

```
MIT License

Copyright (c) 2016 Nicolas Seriot

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Geode website

The homepage in `docs/.vitepress/theme/components/Landing.vue` and `docs/.vitepress/theme/styles/landing.css` follows the look of <https://geode-sdk.org/>. It is written from scratch and contains no code or CSS from the Geode website.

## NefsEdit

`packages/formats/src/nefs.rs` reads the NeFS 1.6 archive layout that NefsEdit documents, <https://github.com/victorbush/ego.nefsedit>. It is written from scratch and contains no NefsEdit code.

## Simplified Suite

The built-in Team Simplified addon, `addons/simplified`, reads the `.serps` package format of the Simplified Suite by Team Simplified, <https://github.com/TeamSimplified/simplified-suite>. Its `mod.serps` reader (`src/serps.rs`) and its package root, content root and entry path checks (`src/package.rs`) are ported from `ModManifest.cs`, `ModImport.cs` and `ModStaging.cs` of the Simplified Launcher, with Team Simplified's permission. The texture array layout and slice merge behind the `erp.merge-slices` operation (`packages/formats/src/texarray.rs`, used by `packages/core/src/erpbuild.rs`) are ported from `src/texarray.rs` of the Team Simplified addon for Swofty Launcher, which ports them from `merge_resources.py` of the Simplified Launcher and `LogoBaker.cs` and `LogoSource.cs` of the Simplified Team Editor. The Simplified Suite is licensed under the GNU General Public License version 3, which section 13 lets this AGPL-3.0-or-later program combine with.

Based on Simplified Suite by Team Simplified - https://github.com/TeamSimplified/simplified-suite

```
Simplified Suite
Copyright (C) 2026 Team Simplified

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, version 3 of the License. It is distributed WITHOUT ANY WARRANTY;
see LICENSE for the full terms.

Under section 7(b) of the GNU General Public License version 3, the following
attribution requirement applies to this work and to every work based on it:

> Any program or library that contains, copies, ports or is based on code from
> Simplified Suite must credit it, by preserving this notice in its source and
> by stating, in its credits, About screen or documentation - wherever the
> program shows its own Appropriate Legal Notices:
>
> **Based on Simplified Suite by Team Simplified - https://github.com/TeamSimplified/simplified-suite**
```

## F1 25 RE-Optimization

The bundled addon `addons/re-optimization` (`xanpavle.re-optimization`) is a port of F1 25 RE-Optimization by xanpavle, <https://github.com/xanpavle/F1-25-RE-Optimization>, with his permission. Its settings delta, engine override names and values, in-engine enforcement table and modes, GPU detection and upscaler choice are ported from `src/profile.cpp`, `src/tune.cpp`, `src/enforce.cpp`, `src/gpu.cpp` and `f125_tune.ini` of version 0.2.5. The proxy `dinput8.dll`, the inline-detour engine and the signature scanner are not used; the addon loads as an engine plugin and hooks through the mixin system. The addon folder keeps the licence as `addons/re-optimization/LICENSE`.

```
MIT License

Copyright (c) 2026 xanpavle

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## bcdec and bcdec_rs

Swofty UI's BC7 texture decoder, `tools/swofty-ui/static/bc7.js`, is a JavaScript port of bcdec_rs 0.2.0 from <https://github.com/ScanMountGoat/image_dds>, which is itself a port of bcdec by Sergii Kudlai, <https://github.com/iOrange/bcdec>. bcdec_rs is MIT-licensed. bcdec is available under the MIT licence or the Unlicense; Swofty UI uses it under the MIT licence.

```
MIT License

Copyright (c) 2023 SMG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

```
MIT License

Copyright (c) 2022 Sergii Kudlai

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Swofty UI executable

`bin/swofty-ui` and `bin/swofty-ui.exe` in the release archives are built with PyInstaller, <https://pyinstaller.org/>, and bundle:

| Component | Licence |
|---|---|
| The CPython 3.14 runtime and standard library, <https://www.python.org/> | Python Software Foundation License Version 2, <https://docs.python.org/3/license.html> |
| The PyInstaller bootloader | GNU General Public License version 2 or later with the PyInstaller bootloader exception, <https://github.com/pyinstaller/pyinstaller/blob/develop/COPYING.txt> |
| The libraries CPython links, such as zstd, zlib, libffi and OpenSSL | Their own licences, listed in the CPython licence document above |

Python is Copyright (c) 2001 Python Software Foundation; All Rights Reserved.

## DirectX Shader Compiler

Releases include `dxc/`: dxc, dxcompiler and dxil built by `.github/workflows/dxc.yml` from <https://github.com/microsoft/DirectXShaderCompiler> at the tag in `tools/dxc/version`. They compile addons' post-process passes. `dxc/LICENSE.txt` (University of Illinois/NCSA Open Source License, with MIT-licensed parts) and `dxc/ThirdPartyNotices.txt` are copied from that tag.

## Discord logo

`apps/launcher/assets/discord.svg` is the Discord logo from Simple Icons, <https://simpleicons.org/>, released under CC0 1.0. Discord and the Discord logo are trademarks of Discord Inc.

## Fonts

Swofty UI uses the system's fonts and ships none. Its icon is a raster image and contains no font data.

The site loads Open Sans, Poppins, Quicksand, Source Code Pro and Varela Round from Google Fonts. They use the SIL Open Font License 1.1.
