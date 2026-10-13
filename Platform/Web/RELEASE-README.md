# SpaceCadetPinball — web build

WebAssembly build of [SpaceCadetPinball](https://github.com/k4zmu2a/SpaceCadetPinball)
(decompilation of 3D Pinball for Windows - Space Cadet), Emscripten 6.0.10.
Source and host contract: https://github.com/max31000/SpaceCadetPinball/blob/bliss-web/Doc/web.md

Contents:

- `SpaceCadetPinball.js` — ES module, `export default createPinball(moduleArgs) -> Promise<Module>`
- `SpaceCadetPinball.wasm`
- `LICENSE` — MIT (the decompilation code)

**No game data is included.** The original files (`PINBALL.DAT`, `SOUND*.WAV`, a TTF font for
Cyrillic UI text, optional `PINBALL.OGG` music) are owned by Microsoft and must be supplied by
the host page, which writes them into `/data` from a `preRun` callback:

```js
import createPinball from './SpaceCadetPinball.js';

const Module = await createPinball({
  canvas: document.querySelector('canvas'),
  locateFile: (name) => wasmUrl,            // URL of SpaceCadetPinball.wasm
  blissDataDir: '/data',
  preRun: [(M) => {
    M.addRunDependency('data');
    filesPromise.then((files) => {          // [[name, Uint8Array], ...]
      for (const [name, bytes] of files) M.FS.writeFile(`/data/${name}`, bytes);
      M.removeRunDependency('data');
    });
  }],
  onBlissEvent: (type, json) => console.log(type, JSON.parse(json)),
});
```
