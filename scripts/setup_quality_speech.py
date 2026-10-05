#!/usr/bin/env python3
"""Prepare pinned VOICEVOX native assets at build time; runtime is fully offline."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import tarfile
import tempfile
import urllib.request
import zipfile


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def download(path, asset):
    if path.is_file() and sha256(path) == asset['sha256']:
        return
    print('Downloading quality Japanese speech asset:', path.name, flush=True)
    temporary = path.with_suffix(path.suffix + '.part')
    try:
        with urllib.request.urlopen(asset['url'], timeout=120) as source, temporary.open('wb') as target:
            shutil.copyfileobj(source, target)
        if sha256(temporary) != asset['sha256']:
            raise ValueError('Checksum mismatch for ' + path.name)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    if len(sys.argv) != 2:
        raise ValueError('Usage: setup_quality_speech.py SPEECH_BUNDLE_DIRECTORY')
    bundle = Path(sys.argv[1]).resolve()
    if not (bundle / 'dictionary/sys.dic').is_file():
        raise ValueError('The shared Fast speech dictionary must be prepared first')
    manifest = Path(__file__).with_name('quality-assets.json')
    assets = json.loads(manifest.read_text())
    downloads = bundle.parent / 'speech-downloads'
    downloads.mkdir(parents=True, exist_ok=True)
    for name, asset in assets.items():
        download(downloads / name, asset)
    with tempfile.TemporaryDirectory(prefix='.quality-staging-', dir=bundle) as temporary:
        staging = Path(temporary)
        extracted = staging / 'extracted'
        extracted.mkdir()
        with zipfile.ZipFile(downloads / 'voicevox-core.zip') as archive:
            for entry in archive.infolist():
                if not (extracted / entry.filename).resolve().is_relative_to(extracted.resolve()):
                    raise ValueError('Unsafe path in core archive')
            archive.extractall(extracted)
        with tarfile.open(downloads / 'voicevox-onnxruntime.tgz') as archive:
            archive.extractall(extracted, filter='data')
        core = extracted / 'voicevox_core-linux-x64-0.17.0'
        runtime = extracted / 'voicevox_onnxruntime-linux-x64-1.17.3'
        quality = staging / 'quality'
        for name in ['lib', 'models', 'licenses/voicevox-core', 'licenses/voicevox-onnxruntime',
                     'licenses/voicevox-vvm']:
            (quality / name).mkdir(parents=True, exist_ok=True)
        shutil.copy2(core / 'lib/libvoicevox_core.so', quality / 'lib/libvoicevox_core.so')
        shutil.copy2(runtime / 'lib/libvoicevox_onnxruntime.so.1.17.3',
                     quality / 'lib/libvoicevox_onnxruntime.so.1.17.3')
        shutil.copytree(core / 'include', quality / 'include')
        shutil.copy2(downloads / 'voicevox-0.vvm', quality / 'models/0.vvm')
        # Preserve every upstream top-level notice and metadata file verbatim.
        for directory, destination in [(core, 'voicevox-core'), (runtime, 'voicevox-onnxruntime')]:
            for path in directory.iterdir():
                if path.is_file():
                    shutil.copy2(path, quality / 'licenses' / destination / path.name)
        for name in ['TERMS', 'README']:
            shutil.copy2(downloads / ('voicevox-vvm-' + name + '.txt'),
                         quality / 'licenses/voicevox-vvm' / (name + '.txt'))
        (quality / 'licenses/ATTRIBUTION.txt').write_text(
            'Moji Nook Quality Japanese pronunciation uses VOICEVOX.\n'
            'VOICEVOX:四国めたん (Shikoku Metan), neutral style 2.\n'
            'VOICEVOX:ずんだもん (Zundamon), neutral style 3.\n'
            'VOICEVOX:春日部つむぎ (Kasukabe Tsumugi), neutral style 8.\n'
            'VOICEVOX Core 0.17.0: MIT; see voicevox-core/LICENSE.\n'
            'VOICEVOX ONNX Runtime 1.17.3: see voicevox-onnxruntime/TERMS.txt and third-party-notices.html.\n'
            'VOICEVOX voice models 0.16.4: see voicevox-vvm/TERMS.txt and README.txt.\n'
            'The voice models are redistributed without modification.\n'
            'The Japanese dictionary and its notices are shared with the Fast speech bundle.\n'
            'Source URLs and SHA256 checksums: quality-assets.json.\n', encoding='utf-8')
        shutil.copy2(manifest, quality / 'licenses/quality-assets.json')
        (quality / '.complete').write_text(sha256(manifest) + '\n' + sha256(Path(__file__)) + '\n')
        destination = bundle / 'quality'
        if destination.exists():
            shutil.rmtree(destination)
        quality.replace(destination)
    print('Quality Japanese speech bundle ready:', bundle / 'quality', flush=True)


if __name__ == '__main__':
    main()
