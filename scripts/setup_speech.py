#!/usr/bin/env python3
"""Populate a relocatable local Japanese speech bundle; build-time only.

Packages are extracted, never installed system-wide. Every download is pinned.
No Python interpreter or network is used by Moji Nook at runtime.
"""
import hashlib
import io
import json
from pathlib import Path
import shutil
import sys
import tarfile
import urllib.request


def extract_deb(archive, destination):
    data = archive.read_bytes()
    if not data.startswith(b'!<arch>\n'):
        raise ValueError('Invalid Debian archive')
    offset = 8
    while offset + 60 <= len(data):
        header = data[offset:offset + 60]
        size = int(header[48:58].decode().strip())
        name = header[:16].decode().strip().rstrip('/')
        payload = data[offset + 60:offset + 60 + size]
        if name.startswith('data.tar'):
            with tarfile.open(fileobj=io.BytesIO(payload), mode='r:*') as package:
                package.extractall(destination, filter='data')
            return
        offset += 60 + size + size % 2
    raise ValueError('No data payload in Debian archive')


def main():
    bundle = Path(sys.argv[1]).resolve()
    assets = json.loads(Path(__file__).with_name('speech-assets.json').read_text())
    bundle.mkdir(parents=True, exist_ok=True)
    downloads = bundle.parent / 'speech-downloads'
    downloads.mkdir(exist_ok=True)
    for name, asset in assets.items():
        path = downloads / name
        if not path.exists() or hashlib.sha256(path.read_bytes()).hexdigest() != asset['sha256']:
            print('Downloading Japanese speech asset:', name, flush=True)
            temporary = path.with_suffix(path.suffix + '.part')
            urllib.request.urlretrieve(asset['url'], temporary)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != asset['sha256']:
                temporary.unlink()
                raise ValueError('Checksum mismatch for ' + name)
            temporary.replace(path)
    extracted = downloads / 'extracted'
    if extracted.exists():
        shutil.rmtree(extracted)
    extracted.mkdir()
    for name in ['open-jtalk.deb', 'htsengine.deb', 'dictionary.deb']:
        extract_deb(downloads / name, extracted)
    for subdir in ['bin', 'lib', 'voices', 'licenses']:
        (bundle / subdir).mkdir(exist_ok=True)
    shutil.copy2(extracted / 'usr/bin/open_jtalk', bundle / 'bin/open_jtalk')
    for library in (extracted / 'usr/lib/x86_64-linux-gnu').glob('libHTSEngine*'):
        shutil.copy2(library, bundle / 'lib' / library.name)
    dictionary = extracted / 'var/lib/mecab/dic/open-jtalk/naist-jdic'
    shutil.copytree(dictionary, bundle / 'dictionary', dirs_exist_ok=True)
    for name in ['mei', 'takumi', 'tohoku']:
        shutil.copy2(downloads / (name + '.htsvoice'), bundle / 'voices' / (name + '.htsvoice'))
        shutil.copy2(downloads / (name + '-COPYRIGHT.txt'), bundle / 'licenses' / (name + '.txt'))
    for package in ['open-jtalk', 'libhtsengine1', 'open-jtalk-mecab-naist-jdic']:
        shutil.copy2(extracted / 'usr/share/doc' / package / 'copyright', bundle / 'licenses' / (package + '.txt'))
    (bundle / 'licenses' / 'ATTRIBUTION.txt').write_text(
        'Moji Nook local Japanese pronunciation bundle. Voices are redistributed without changes.\n'
        'Mei and Takumi: MMDAgent Project Team, Nagoya Institute of Technology, CC BY 3.0.\n'
        'https://creativecommons.org/licenses/by/3.0/\n'
        'Tohoku-f01: Intelligent Communication Network Laboratory, Tohoku University, CC BY 4.0.\n'
        'https://creativecommons.org/licenses/by/4.0/\n'
        'Open JTalk, HTS Engine API, NAIST/UniDic dictionary: see included BSD license notices.\n'
        'Upstream packages and SHA256 checksums: speech-assets.json.\n')
    shutil.copy2(Path(__file__).with_name('speech-assets.json'), bundle / 'licenses/speech-assets.json')
    manifest_sha = hashlib.sha256(Path(__file__).with_name('speech-assets.json').read_bytes()).hexdigest()
    setup_sha = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    (bundle / '.complete').write_text(manifest_sha + '\n' + setup_sha + '\n')
    print('Japanese speech bundle ready:', bundle, flush=True)


if __name__ == '__main__':
    main()
