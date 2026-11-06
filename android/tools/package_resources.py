#!/usr/bin/env python3
"""Prepare ZeroSpades paks for a future Android app (not KyroSpades assets)."""
import argparse
import hashlib
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / 'Resources'
# Mirrors Resources/mkpak.sh. Paths inside each ZIP are relative to Resources/.
PAKS = {
    'pak002-Base.pak': ['License/Credits-pak002-Base.md', 'Gfx', 'Scripts/Main.as',
                        'Scripts/Base', 'Shaders', 'Sounds/Feedback', 'Sounds/Misc',
                        'Sounds/Player', 'Textures'],
    'pak005-Models.pak': ['Maps', 'Models/MapObjects', 'Models/Player'],
    'pak010-BaseSkin.pak': ['License/Credits-pak010-BaseSkin.md', 'Scripts/Skin',
                            'Sounds/Weapons', 'Models/Weapons'],
    'pak050-Locales.pak': ['License/Credits-pak050-Locales.md', 'Locales'],
    'pak999-References.pak': ['License/Credits-pak999-References.md', 'Scripts/Reference'],
}
EXTERNAL = {
    'Nonfree/pak000-Nonfree.pak': 'pak000-Nonfree.pak',
    'OfficialMods/font-unifont.pak': 'font-unifont.pak',
    'Nonfree/LICENSE.md': 'LICENSE.pak000.md',
    'OfficialMods/LICENSE': 'LICENSE.unifont.txt',
}
# Pinned upstream tag r33, commit 3208085afea4faafe6ce1309b34283e3c174d65d.
# The two pak digests are the SHA-256 oids in that tag's Git LFS pointers;
# license digests are SHA-256 of the corresponding tracked license files.
R33_SHA256 = {
    'Nonfree/pak000-Nonfree.pak': '4c2daec4f853ffe09120ac5bf53a5fe76b30ecc967ec31b4e86710d405e7554b',
    'OfficialMods/font-unifont.pak': 'd2edbf8739ac946cc5f877b1eb85983da85de01a6d131599466feea5d3df4611',
    'Nonfree/LICENSE.md': '30ee9801bfd9909e0767390ba19ee7791a3670d602d866ee01e5b717664603c4',
    'OfficialMods/LICENSE': 'fc959c130a8519b7840c9aef52c8101bb9fb1dcb919ee92718e4db2c83caeb94',
}


def build_paks(output, resources=RESOURCES):
    output.mkdir(parents=True, exist_ok=True)
    for name, roots in PAKS.items():
        files = []
        for rel in roots:
            source = resources / rel
            if not source.exists():
                raise FileNotFoundError(source)
            files.extend([source] if source.is_file() else sorted(p for p in source.rglob('*') if p.is_file()))
        # Do not include editor asset caches and Finder metadata, matching mkpak.sh.
        files = sorted(p for p in files if '_Assets_' not in p.relative_to(resources).as_posix()
                       and '.DS_Store' not in p.parts)
        if not files:
            raise ValueError(f'{name}: no inputs')
        temp = output / (name + '.tmp')
        try:
            with zipfile.ZipFile(temp, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as pak:
                for path in files:
                    # Fixed metadata makes packages reproducible across builds.
                    entry = zipfile.ZipInfo(path.relative_to(resources).as_posix(), (1980, 1, 1, 0, 0, 0))
                    entry.compress_type = zipfile.ZIP_DEFLATED
                    entry.external_attr = 0o100644 << 16
                    pak.writestr(entry, path.read_bytes(), compresslevel=6)
            temp.replace(output / name)
        finally:
            temp.unlink(missing_ok=True)


def add_external(output, archive, verify_r33=False):
    """Copy only known entries, not arbitrary archive paths (avoids zip slip)."""
    with zipfile.ZipFile(archive) as devpak:
        for src in EXTERNAL:
            info = devpak.getinfo(src)
            if info.is_dir() or info.file_size <= 0 or info.file_size > 256 * 1024 * 1024:
                raise ValueError(f'invalid external asset: {src}')
        # Stage and validate every file before publishing any of them.
        with tempfile.TemporaryDirectory(prefix='.devpak-', dir=output) as work:
            work = Path(work)
            for src, dest in EXTERNAL.items():
                temp = work / dest
                with devpak.open(src) as source, temp.open('wb') as target:
                    shutil.copyfileobj(source, target)
                if verify_r33:
                    with temp.open('rb') as source:
                        digest = hashlib.sha256()
                        for chunk in iter(lambda: source.read(32768), b''):
                            digest.update(chunk)
                    if digest.hexdigest() != R33_SHA256[src]:
                        raise ValueError(f'asset does not match pinned upstream r33: {src}')
                if dest.endswith('.pak'):
                    with zipfile.ZipFile(temp) as pak:
                        if not pak.namelist() or pak.testzip() is not None:
                            raise ValueError(f'invalid pak: {src}')
            for dest in EXTERNAL.values():
                (work / dest).replace(output / dest)



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='directory for generated paks')
    parser.add_argument('--devpak', type=Path, help='local OpenSpades development package ZIP')
    parser.add_argument('--verify-r33', action='store_true',
                        help='require all external assets and notices to match pinned upstream r33')
    args = parser.parse_args()
    if args.verify_r33 and not args.devpak:
        parser.error('--verify-r33 requires --devpak')
    try:
        build_paks(args.output)
        if args.devpak:
            add_external(args.output, args.devpak, verify_r33=args.verify_r33)
        else:
            print('External essential assets not included; see Resources/PakLocation.txt.', file=sys.stderr)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as exc:
        parser.exit(1, f'Resource packaging failed: {exc}\n')


if __name__ == '__main__':
    main()
