# Apple Lossless (ALAC) decoder — vendored, with hardening patches

- Source: https://github.com/macosforge/alac, commit `c38887c5c5e64a4b31108733bd79ca9b2496d987`
  (2016-05-11; the repository is archived, the codec code dates from 2011).
- License: Apache License 2.0 (`LICENSE`, and the `APPLE_APACHE_LICENSE_HEADER` in every file below).
  The upstream `codec/` folder also holds an `APPLE_LICENSE.txt` (Apple Public Source License 2.0).
  None of the files used here refers to it, so it is not copied.
- The application uses only the decoder. The encoder files are also here, unchanged, for making
  test files (target `ap_alac_encoder` in `tests/`); they are not part of the application. The
  convert utility and the makefile are not copied.

## Files and SHA-256 of the upstream versions

| File | SHA-256 (upstream) | Changed here |
|---|---|---|
| `LICENSE` | `a337314dd82d26791112c1e5a85725b2eb92f5b7e6f9102fa4eaa2903f77518f` | no |
| `codec/ALACAudioTypes.h` | `a0fe17bf82191d28c500b9ee1e773d49700df5e33ad862ed61b13c8a094c03b9` | no |
| `codec/ALACBitUtilities.c` | `276ac7d7f2d240a4b0437d7e222afcba4c78bd8fd8104d0e9c6b0d45f67abcfe` | no |
| `codec/ALACBitUtilities.h` | `18d1c8f957a403cc5b9b7d99e9d3987001adeac6ceadb4a4b1b5dc1ea4bff8ca` | no |
| `codec/ALACDecoder.cpp` | `b071c3a82bcb0b5e8ad704cafb514aaeba5b0869b7dcc83fa79561bae994c019` | **yes** |
| `codec/ALACDecoder.h` | `779a1686417687f3ba107f9a12bb0f4c892885c0654d0bd5db7133bc4edfe24c` | no |
| `codec/EndianPortable.c` | `34b4d4c4dd599e05ac43f4a58eef61ef2eb5a7093e46209eebdf6ac7024b3dfc` | no |
| `codec/EndianPortable.h` | `5c3b1df02e8bef3adb4feee6f0ed0684b2d000d44e57a0c2ed754668794d40b0` | no |
| `codec/ag_dec.c` | `622d7551ef041d82fa6462292155c48fcc2d8b912ed0241e202b86afa3536747` | **yes** |
| `codec/aglib.h` | `ef715d64666e4a4faaa0fafcd5050bd5b7f8c45e9760382735f37cf183c55778` | no |
| `codec/dp_dec.c` | `1f8e8b25e61c5b667ad729a9c33e3916df7eb4883125dcac7266d719cbf23516` | no |
| `codec/dplib.h` | `fd2b9806f8cf2386dcbdc919ddf56c18608ce1b0812a74b6b9d0b0eb9df67d78` | no |
| `codec/matrix_dec.c` | `ae9dd8abc1a9d1cf7f6256c0b15ffd72f9d51c6ffdae9b833f6e96d9d8a03495` | no |
| `codec/matrixlib.h` | `00839b4672b4ae7887a4d0448d6258165753b1e5f85f84c53c67e7e7b8b9d76d` | no |
| `codec/ALACEncoder.cpp` (tests only) | `0328473c489d24801eab29de1d2785693a5017a87455a59248c26fd2a9d006bf` | no |
| `codec/ALACEncoder.h` (tests only) | `e2a581c46c9233ac2eac30cfedfc683245fbb28abe879bcceb1fc21c989be58b` | no |
| `codec/ag_enc.c` (tests only) | `3be76326402060d4dbcb16141340ff8e0d67877792ebc4e56155da840c75880d` | no |
| `codec/dp_enc.c` (tests only) | `140aaac67123e7db08bab2aedc3d38933899c91530f5cc410283e92a1e9631a9` | no |
| `codec/matrix_enc.c` (tests only) | `c7fcb3ff04b6830a046677155633a9e44b5e4f54f6e1c03713eeaed34efa80b5` | no |

## Why the decoder is patched

The published code has not been fixed since 2011. Decoders derived from it (MediaTek, Qualcomm)
were found to allow memory corruption from crafted audio files: CVE-2021-0674, CVE-2021-0675,
CVE-2021-30351 (Check Point Research, "ALHACK", 2022). The causes are values from the file used
without checks. This player opens files from anywhere, so every such value is checked before use.

## Changes (each marked `// AP:` in the source)

`codec/ALACDecoder.cpp`

1. `Init`: refuse a null cookie and a second `Init`; look at the optional `frma` / `alac` atom
   headers only when 12 bytes are there.
2. `Init`: validate the configuration before it sizes buffers: frame length 1..16384 (ReadMe:
   packets above 16384 frames are not supported), 1..8 channels, bit depth 16/20/24/32, `kb` <= 31
   (a shift count).
3. `Decode`: refuse a call before a successful `Init` and a requested sample count above the frame length.
4. `Decode`: a mono/LFE element is decoded only while the output buffer has room for another
   channel (the pair element already had this check).
5. `Decode`: the partial-frame sample count read from the frame must not exceed the frame length
   (the overflow behind the CVEs above).
6. `Decode`: the shifted-off bytes must leave at least one bit per sample, and a pair's channel
   width must be at most 32 bits (both are shift counts).
7. `Decode`: before skipping the shifted-bits area, before reading uncompressed samples, and after
   the element header and coefficients, the bits needed must be inside the packet.
8. `Decode`: `mixBits` (a shift count in `unmix*`) must be below 32.
9. `Decode`: when the bitstream holds fewer channels than requested, 20-bit output is zero-filled
   too (it was left as it was).

`codec/ag_dec.c`

10. `dyn_decomp`: the read limit counted `byteSize * 8` bits from the current position, i.e. up to a
    whole packet past its end. It now counts from the current position to the end of the packet.

## Contract with the caller (engine/src/alac_file_source.cpp)

- The input buffer holds the packet followed by 256 zero bytes. Single reads and single Golomb
  codes may look a few bytes past the packet; those reads stay inside the buffer, and the checks
  above stop decoding before anything larger.
- The output buffer holds frame length x channels x bytes per sample (2, 3, 3, 4 for 16/20/24/32 bits).
- Built with `TARGET_OS_WIN32=1` so that the byte-order helpers swap on x64 (they test for
  `__x86_64__`, which MSVC does not define).
