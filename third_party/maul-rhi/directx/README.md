# Microsoft DirectX headers

The C headers of the Direct3D 12 API, kept exactly as published, for
the D3D12 driver (mrhi-0003; the library profile, mrhi-0001, names
`d3d12.dll` and `dxgi.dll` as platform dependencies). They are the
platform's API, not code of this project. DXGI's interface headers
(`dxgi1_6.h`) come from the Windows SDK every Windows toolchain has.

- **Source:** https://github.com/microsoft/DirectX-Headers
- **Version:** tag `v1.619.5`, commit
  `ee479f0bd5f7b884f202bcf0c3f076cc050dd256`
- **Licence:** `MIT`; the text is in `LICENSE`, from the repository's
  root.
- **Files:** `include/directx/d3d12.h`, `d3dcommon.h`,
  `d3d12sdklayers.h`, `dxgicommon.h` and `dxgiformat.h`, under
  `directx/`.

The files are the tag's but for line endings, which the repository
keeps as LF where the tag has CRLF. An update replaces the files from a
newer tag and this list.
