import argparse
import hashlib
import json
import shutil
import tempfile
import zipfile
from pathlib import Path

import package_release


def package(build, output, version, source_commit):
    if len(source_commit) != 40 or any(c not in '0123456789abcdef' for c in source_commit):
        raise ValueError('A full source commit SHA is required')
    package_release.package('windows', build, output, version)
    root = package_release.ROOT
    gui = build / 'core/gui/AnyPS5.exe'
    relinker = build / 'core/relinker/relinker.exe'
    for file in (gui, relinker):
        if not file.is_file() or not file.stat().st_size:
            raise RuntimeError(f'Missing or empty portable executable: {file}')
    name = f'AnyPS5-portable-{version}'
    with tempfile.TemporaryDirectory(prefix='.portable-', dir=output) as temporary:
        staging = Path(temporary)
        with zipfile.ZipFile(output / f'prx-windows-{version}.zip') as libraries:
            libraries.extractall(staging)
        shutil.copy2(gui, staging / 'AnyPS5.exe')
        shutil.copy2(relinker, staging / 'relinker.exe')
        for file in (root / 'README.md', root / 'LICENSE'):
            shutil.copy2(file, staging / file.name)
        shutil.copytree(root / 'docs/user', staging / 'docs')
        shutil.copy2(root / 'docs/user/GUI.md', staging / 'START-HERE.md')
        (staging / 'BUILD-INFO.json').write_text(json.dumps({
            'version': version, 'source_commit': source_commit,
            'platform': 'windows-x86_64', 'toolchain': 'WinLibs GCC 15.2.0 UCRT r7',
        }, indent=2) + '\n', encoding='utf-8')
        archive_path = output / f'{name}.zip'
        with zipfile.ZipFile(archive_path, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for file in sorted(staging.rglob('*')):
                if file.is_file():
                    archive.write(file, f'{name}/{file.relative_to(staging).as_posix()}')
        verify(archive_path, name, source_commit)
    package_release.collect_docs(root / 'docs/user', output)
    checksums = []
    for file in sorted(output.iterdir()):
        if file.is_file() and file.name != 'SHA256SUMS.txt':
            with file.open('rb') as stream:
                checksums.append(f'{hashlib.file_digest(stream, "sha256").hexdigest()}  {file.name}')
    (output / 'SHA256SUMS.txt').write_text('\n'.join(checksums) + '\n', encoding='utf-8')


def verify(archive_path, name, source_commit):
    with zipfile.ZipFile(archive_path) as archive:
        bad = archive.testzip()
        if bad:
            raise RuntimeError(f'Corrupt ZIP member: {bad}')
        for relative in ('AnyPS5.exe', 'relinker.exe', 'libs/libc.prx',
                         'libs/libgcc_s_seh-1.dll', 'libs/libstdc++-6.dll',
                         'libs/libwinpthread-1.dll', 'START-HERE.md', 'BUILD-INFO.json'):
            data = archive.read(f'{name}/{relative}')
            if not data:
                raise RuntimeError(f'Empty portable file: {relative}')
            if relative.endswith(('.exe', '.prx', '.dll')) and data[:2] != b'MZ':
                raise RuntimeError(f'Not a Windows binary: {relative}')
        if json.loads(archive.read(f'{name}/BUILD-INFO.json'))['source_commit'] != source_commit:
            raise RuntimeError('Portable source commit does not match the build')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    package(args.build, args.output, args.version, args.source_commit)


if __name__ == '__main__':
    main()
