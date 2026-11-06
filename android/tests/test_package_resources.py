import hashlib
import importlib.util
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'tools' / 'package_resources.py'
spec = importlib.util.spec_from_file_location('package_resources', SCRIPT)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


class PackagingTests(unittest.TestCase):
    def test_actual_paks_are_reproducible_and_contain_game_files(self):
        with tempfile.TemporaryDirectory() as a, tempfile.TemporaryDirectory() as b:
            mod.build_paks(Path(a)); mod.build_paks(Path(b))
            for name in mod.PAKS:
                one, two = Path(a) / name, Path(b) / name
                self.assertEqual(hashlib.sha256(one.read_bytes()).digest(), hashlib.sha256(two.read_bytes()).digest())
                with zipfile.ZipFile(one) as pak:
                    self.assertIsNone(pak.testzip())
            with zipfile.ZipFile(Path(a) / 'pak002-Base.pak') as pak:
                self.assertIn('Scripts/Main.as', pak.namelist())
                self.assertIn('Gfx/Title/Logo.png', pak.namelist())
            with zipfile.ZipFile(Path(a) / 'pak010-BaseSkin.pak') as pak:
                self.assertTrue(any(n.startswith('Scripts/Skin/') for n in pak.namelist()))

    def test_external_archive_requires_all_known_files(self):
        with tempfile.TemporaryDirectory() as d:
            archive = Path(d) / 'dev.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('Nonfree/pak000-Nonfree.pak', b'invalid')
            with self.assertRaises(KeyError):
                mod.add_external(Path(d), archive)
            self.assertFalse((Path(d) / 'pak000-Nonfree.pak').exists())

    def test_invalid_nested_pak_does_not_replace_existing_assets(self):
        import io
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            old = path / 'pak000-Nonfree.pak'
            old.write_bytes(b'old asset')
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, 'w') as pak:
                pak.writestr('valid.txt', 'asset')
            archive = path / 'dev.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                for src, dest in mod.EXTERNAL.items():
                    data = (b'invalid zip' if dest == 'font-unifont.pak' else
                            buf.getvalue() if dest.endswith('.pak') else b'license')
                    z.writestr(src, data)
            with self.assertRaises(zipfile.BadZipFile):
                mod.add_external(path, archive)
            self.assertEqual(b'old asset', old.read_bytes())

    def test_verified_external_assets_are_all_checked_before_replacement(self):
        import io
        from unittest.mock import patch
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)
            archive = path / 'dev.zip'
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, 'w') as pak:
                pak.writestr('valid.txt', 'asset')
            values = {src: buf.getvalue() if dest.endswith('.pak') else b'license'
                      for src, dest in mod.EXTERNAL.items()}
            with zipfile.ZipFile(archive, 'w') as z:
                for src, data in values.items():
                    z.writestr(src, data)
            expected = {src: hashlib.sha256(data).hexdigest() for src, data in values.items()}
            (path / 'pak000-Nonfree.pak').write_bytes(b'old')
            wrong = dict(expected)
            wrong['OfficialMods/font-unifont.pak'] = '0' * 64
            with patch.object(mod, 'R33_SHA256', wrong):
                with self.assertRaisesRegex(ValueError, 'does not match pinned'):
                    mod.add_external(path, archive, verify_r33=True)
            self.assertEqual(b'old', (path / 'pak000-Nonfree.pak').read_bytes())
            with patch.object(mod, 'R33_SHA256', expected):
                mod.add_external(path, archive, verify_r33=True)
            self.assertEqual(buf.getvalue(), (path / 'pak000-Nonfree.pak').read_bytes())

    def test_external_archive_uses_only_named_entries(self):
        import io
        with tempfile.TemporaryDirectory() as d:
            archive = Path(d) / 'dev.zip'
            buf = io.BytesIO()
            with zipfile.ZipFile(buf, 'w') as pak:
                pak.writestr('valid.txt', 'asset')
            with zipfile.ZipFile(archive, 'w') as z:
                for src, dest in mod.EXTERNAL.items():
                    z.writestr(src, buf.getvalue() if dest.endswith('.pak') else b'license')
                z.writestr('../escape', b'bad')
            mod.add_external(Path(d), archive)
            for name in mod.EXTERNAL.values(): self.assertTrue((Path(d) / name).is_file())
            self.assertFalse((Path(d).parent / 'escape').exists())


if __name__ == '__main__': unittest.main()
