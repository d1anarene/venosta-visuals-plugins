# Third-party components in the Conductor panel

| Component | Version | Licence | Use |
| --- | --- | --- | --- |
| [mpg123-decoder](https://github.com/eshaz/wasm-audio-decoders) (JavaScript wrapper, `@wasm-audio-decoders/common`, `simple-yenc`) | 1.0.3 | MIT | MP3 decoding in the panel |
| [mpg123](https://www.mpg123.de/) (compiled to WebAssembly inside `mpg123-decoder`) | as shipped in mpg123-decoder 1.0.3 | LGPL-2.1 | MP3 decoding |

mpg123 is used unmodified as the WebAssembly module shipped by `mpg123-decoder`. Its source code is
available from https://www.mpg123.de/ and the wrapper's repository; the complete source of this
panel is in this repository, so the combined work can be rebuilt with a different mpg123 build.
