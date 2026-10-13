import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import package_portable


class PortablePackageTests(unittest.TestCase):
    def test_complete_archive_and_provenance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, build, output = root / 'source', root / 'build', root / 'dist'
            (source / 'docs/user').mkdir(parents=True)
            for name in ('README.md', 'LICENSE', 'docs/user/GUI.md', 'docs/user/USAGE.md'):
                (source / name).write_text(name, encoding='utf-8')
            for name in ('core/gui/AnyPS5.exe', 'core/relinker/relinker.exe'):
                file = build / name
                file.parent.mkdir(parents=True)
                file.write_bytes(b'MZfake binary')

            def upstream_package(platform, build_path, output_path, version):
                output_path.mkdir()
                with zipfile.ZipFile(output_path / f'prx-windows-{version}.zip', 'w') as archive:
                    for name in ('libc.prx', 'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll'):
                        archive.writestr(f'libs/{name}', b'MZfake library')
                (output_path / f'relinker-{version}.exe').write_bytes(b'MZfake binary')

            sha = 'a' * 40
            with patch.object(package_portable.package_release, 'ROOT', source), \
                    patch.object(package_portable.package_release, 'package', upstream_package):
                package_portable.package(build, output, 'v0.2.0', sha)
            archive = output / 'AnyPS5-portable-v0.2.0.zip'
            with zipfile.ZipFile(archive) as portable:
                self.assertEqual(json.loads(portable.read('AnyPS5-portable-v0.2.0/BUILD-INFO.json'))['source_commit'], sha)
                self.assertEqual(portable.read('AnyPS5-portable-v0.2.0/docs/USAGE.md'), b'docs/user/USAGE.md')
                self.assertEqual(portable.read('AnyPS5-portable-v0.2.0/START-HERE.md'), b'docs/user/GUI.md')
            self.assertIn(archive.name, (output / 'SHA256SUMS.txt').read_text())
            with self.assertRaisesRegex(RuntimeError, 'commit does not match'):
                package_portable.verify(archive, 'AnyPS5-portable-v0.2.0', 'b' * 40)

    def test_invalid_commit_rejected_before_build(self):
        with self.assertRaises(ValueError):
            package_portable.package(Path('missing'), Path('missing'), 'v0.2.0', 'main')


if __name__ == '__main__':
    unittest.main()
