# Offline Japanese speech and attribution

Moji Nook's Linux x86_64 build prepares two local pronunciation engines; the
Windows x64 build prepares Quality only, using the Open JTalk UTF-8 dictionary
1.11 from `scripts/quality-assets-windows.json`. The app
speaks the source's accepted kana reading to avoid ambiguous kanji interpretation.
Voice output is a practice aid, not authoritative pitch-accent instruction.
See [installation](INSTALL.md#speech-options) for build options and
[usage](USAGE.md#japanese-pronunciation) for controls.

## Shipped components and asset sources

**Fast** uses Open JTalk 1.11-3, HTS Engine 1.10-6, the compiled NAIST Japanese
dictionary, and neutral Mei, Takumi, and Tohoku F01 voices. Its executable,
library, and dictionary come from pinned Debian packages extracted locally;
they are not installed system-wide. Voice URLs and SHA-256 values are recorded
in [speech-assets.json](../scripts/speech-assets.json).

Mei comes from the [MMDAgent-EX example](https://github.com/mmdagent-ex/example/tree/95e3a0b389be60d5c8fd0215684f82512cb81d36/voice/mei).
Takumi comes from a pinned mirror; its model hash
`9130cc911c881cc2c4c442a67374d8601b436f9dd415b555fe3c2d2c94444d42`
was checked against the first-party [MMDAgent Example 1.8 distribution](https://sourceforge.net/projects/mmdagent/files/MMDAgent_Example/MMDAgent_Example-1.8/).
Tohoku F01 comes from the [laboratory repository](https://github.com/icn-lab/htsvoice-tohoku-f01/tree/8e3306021db135c265f5eda5f062dc489707ddf8).

**Quality** uses VOICEVOX Core 0.17.0, VOICEVOX ONNX Runtime 1.17.3, and voice
models 0.16.4. The `0.vvm` model supplies neutral Shikoku Metan (style 2),
Zundamon (3), and Kasukabe Tsumugi (8). It reuses Fast's dictionary.
[quality-assets.json](../scripts/quality-assets.json) pins all downloads and hashes.
The native C ABI keeps Python and a separate HTTP speech server out of runtime.
Sources: [Core release](https://github.com/VOICEVOX/voicevox_core/tree/0.17.0),
[inference runtime](https://github.com/VOICEVOX/onnxruntime-builder/releases/tag/voicevox_onnxruntime-1.17.3),
and [model mapping](https://github.com/VOICEVOX/voicevox_vvm/blob/0.16.4/README.md).

The preparation scripts download at configure time, verify hashes, extract into
the build directory, and copy upstream notices. CMake installs those notices
alongside the assets. Other platforms need native bundles and validation.

## Attribution and notices

Preserve complete upstream notices rather than treating this summary as a license.
The generated bundle includes `licenses/` and `quality/licenses/`, attribution,
and copies of both asset manifests. Source links and versions were checked when
the bundles were introduced; recheck terms when updating assets.

- **Mei and Takumi:** MMDAgent Project Team, Nagoya Institute of Technology.
  Unmodified neutral HTS voices, [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).
  Include each voice's full copyright notice. Character artwork has separate terms.
- **Tohoku F01:** Intelligent Communication Network (Ito-Nose) Laboratory,
  Tohoku University. Unmodified voice, [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
  Include its copyright notice and identify any future modifications.
- **Open JTalk and HTS Engine:** preserve their BSD notices and the complete
  dictionary/MeCab/NAIST/UniDic notices shipped with the pinned packages.
  See [Open JTalk](https://open-jtalk.sourceforge.net/readme_open_jtalk.php) and
  [HTS Engine](https://hts-engine.sourceforge.net/readme_hts_engine_API.php).
- **VOICEVOX Core 0.17.0:** preserve its [MIT license](https://github.com/VOICEVOX/voicevox_core/blob/0.17.0/LICENSE).
  The inference runtime has its own terms and third-party notices, copied by the setup script.
- **VOICEVOX models:** preserve the [model terms](https://github.com/VOICEVOX/voicevox_vvm/blob/0.16.4/TERMS.txt)
  and README. Model/voice conditions are separate from Core's source license and
  include generated-audio conditions for downstream users.
- **Selected Quality voices:** keep discoverable credits `VOICEVOX:四国めたん`,
  `VOICEVOX:ずんだもん`, and `VOICEVOX:春日部つむぎ`, plus the selected voices' terms.
  Sources: [Metan/Zundamon terms](https://zunko.jp/con_ongen_kiyaku.html),
  [Tsumugi terms](https://tsumugi-official.studio.site/rule), and
  [application FAQ](https://tsumugi-official.studio.site/rule2).

Qt and LayerShellQt are used as system libraries under their own licenses
(LGPL v3). Moji Nook's own source is [MIT licensed](../LICENSE); that license
does not replace the voice-model, upstream content, or dependency terms above.
Installed builds place these notices under `share/moji-nook/speech/licenses/`
and `share/moji-nook/speech/quality/licenses/`.
