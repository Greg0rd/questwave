# Native QSP 5.9.5 for Questwave

This directory contains the pinned upstream sources used by the Questwave debug build.
Nothing here is fetched at configure time, so CI works offline and builds are
repeatable.

## Sources

| Component | Version | Revision | License | Local path |
| --- | --- | --- | --- | --- |
| QSP | 5.9.5 | `0445921b0640c68bc7e7ab03393fc15e8373c8e9` | MPL-2.0 | `qsp/` |
| Oniguruma | 6.9.10 | `f95747b462de672b6f8dbdeb478245ddf061ca53` | BSD-2-Clause | `oniguruma/` |

Upstream: <https://github.com/QSPFoundation/qsp> and
<https://github.com/kkos/oniguruma>.

The QSP license files (`LICENSE`, `LICENSE_MPL2`, `LICENSE_BSD_2Clause`,
`LICENSE_BSD_3Clause`) are kept with the source, as is Oniguruma's `COPYING`.

Only the upstream engine core and the `bindings/default` binding are compiled.
The Java binding, the platform bindings and the import/build tooling of QSP are
not used. `qsp/bindings/default` is the host-neutral API.

## Layout

```
CMakeLists.txt          native build (engine + oniguruma + adapter)
androidqspwrapper.c     Questwave JNI adapter (not part of QSP)
qsp_config.h.cmakein    generated qsp_config.h template
qsp/                    QSP 5.9.5 sources (MPL-2.0)
oniguruma/              Oniguruma sources (BSD-2-Clause)
```

## Build notes

* `-fshort-wchar` makes `wchar_t`/`QSP_CHAR` 2 bytes, matching Java `jchar`.
  The QSP engine does not use `wcs*` functions, so this is safe on bionic.
* `QSP_CHAR` is UTF-16; Oniguruma is configured by QSP to use UTF-16LE/BE.
* `-Wl,-z,max-page-size=16384` prepares the library for 16 KB page devices.
* `qsp/` is deliberately **not** on the include path: it contains headers with
  system names (`time.h`), and `-I qsp` would break `<time.h>` includes.
  Engine sources resolve their local headers relative to the source files.

The adapter is Questwave's own file and is not distributed as part of QSP. See
`docs/qsp-5.9.5-api-mapping.md` for the mapping between the historical Questopia
JNI surface and the QSP 5.9.5 API.
