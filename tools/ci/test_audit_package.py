import io
from pathlib import Path
import stat
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from audit_package import validate
from tools.linux.package_portable import write_archive


class AssetsDirectoryTest(unittest.TestCase):
    def test_linux_packager_explicitly_writes_empty_assets(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            stage = root / "Bulletproof"
            (stage / "assets").mkdir(parents=True)
            (stage / "compiler").mkdir()
            (stage / "compiler" / "fixture").write_bytes(b"synthetic payload")
            package = root / "fixture.tar.xz"
            with patch.dict("os.environ", {"TAR_OPTIONS": "--exclude=assets"}):
                write_archive(stage, package, 0)
            with tarfile.open(package, "r:xz") as archive:
                self.assertTrue(archive.getmember("Bulletproof/assets").isdir())
                self.assertEqual(archive.getnames().count("Bulletproof/assets"), 1)
                self.assertEqual(archive.extractfile("Bulletproof/compiler/fixture").read(), b"synthetic payload")
                self.assertFalse(any(name.startswith("Bulletproof/assets/") for name in archive.getnames()))
            (stage / "assets" / "fixture").touch()
            with self.assertRaisesRegex(RuntimeError, "must exist and be empty"):
                write_archive(stage, package, 0)

    def test_audit_requires_explicit_empty_directory_in_both_formats(self):
        cases = (
            ([], "Missing explicit assets/ directory entry"),
            ([("assets", b"")], "Missing explicit assets/ directory entry"),
            ([("assets/fixture", b"data")], "Missing explicit assets/ directory entry"),
            ([("assets/", None), ("assets/fixture", b"data")], "The assets directory must be empty"),
            ([("assets/", None), ("assets/subdir/", None)], "The assets directory must be empty"),
            ([("assets/", None)], "Missing required package entries"),
        )
        with tempfile.TemporaryDirectory() as work:
            for extension in (".zip", ".tar.xz"):
                for members, error in cases:
                    with self.subTest(extension=extension, members=members):
                        path = Path(work) / ("fixture" + extension)
                        if extension == ".zip":
                            with zipfile.ZipFile(path, "w") as archive:
                                for name, data in members:
                                    if data is None:
                                        archive.mkdir(name)
                                    else:
                                        archive.writestr(name, data)
                        else:
                            with tarfile.open(path, "w:xz") as archive:
                                for name, data in members:
                                    member = tarfile.TarInfo("Bulletproof/" + name)
                                    if data is None:
                                        member.type = tarfile.DIRTYPE
                                        archive.addfile(member)
                                    else:
                                        member.size = len(data)
                                        archive.addfile(member, io.BytesIO(data))
                        with self.assertRaisesRegex(ValueError, error):
                            validate(path, b"", "unused")

    def test_audit_rejects_malformed_zip_directory(self):
        with tempfile.TemporaryDirectory() as work:
            path = Path(work) / "fixture.zip"
            for mode, data in ((stat.S_IFDIR, b"data"), (stat.S_IFREG, b"")):
                with self.subTest(mode=mode):
                    with zipfile.ZipFile(path, "w") as archive:
                        entry = zipfile.ZipInfo("assets/")
                        entry.external_attr = (mode << 16) | 0x10
                        archive.writestr(entry, data)
                    with self.assertRaisesRegex(ValueError, "Expected an empty directory entry"):
                        validate(path, b"", "unused")
