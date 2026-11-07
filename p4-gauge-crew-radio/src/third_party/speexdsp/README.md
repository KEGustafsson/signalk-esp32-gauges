# speexdsp 1.2.1 (vendored, unmodified)

The acoustic echo canceller (`mdf.c`) and the preprocessor (`preprocess.c`:
noise suppression, residual echo suppression) of Xiph.org's speexdsp, and
what they need (`fftwrap.c` with `smallft.c`, `filterbank.c`), from
<https://downloads.xiph.org/releases/speex/speexdsp-1.2.1.tar.gz>
(SHA-256 `8c777343e4a6399569c72abc38a95b24db56882c83dbdb6c6424a5f4aeb54d3d`).
BSD 3-clause (`COPYING`), compatible with the EUPL-1.2.

The files are as released, apart from `speex/speexdsp_config_types.h`,
which configure generates and is written here by hand. They are not
compiled from this folder (the library's `srcFilter` leaves it out):
`../../dsp/` compiles each one with the build's settings (floating point,
smallft), so no global `-D` flag is needed.
