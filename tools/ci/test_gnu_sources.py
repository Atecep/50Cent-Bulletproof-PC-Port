import hashlib
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

import bundle_gnu_sources as sources


class GnuSourcesTest(unittest.TestCase):
    def archive(self, directory, *, version=sources.VERSION, wrong_hash=False, extra=False):
        path = directory / "Bulletproof-linux-x86_64.tar.xz"
        rows = []
        with tarfile.open(path, "w:xz") as archive:
            def add(name, content):
                member = tarfile.TarInfo("Bulletproof/" + name)
                member.size = len(content)
                archive.addfile(member, io.BytesIO(content))

            for name, package in sources.LIBRARIES.items():
                payload = name.encode()
                digest = hashlib.sha256(payload).hexdigest() if not wrong_hash else "0" * 64
                rows.append(f"{name}\t{digest}\t{package}\tfile.so")
                rows.append(f"{package}\t{version}\t{sources.SOURCE}\t{version}")
                add(name, payload)
            if extra:
                add("compiler/lib/libgomp.so.1", b"libgomp")
                rows.append("compiler/lib/libgomp.so.1\tdigest\tlibgomp1:amd64\tfile.so")
                rows.append(f"libgomp1:amd64\t{version}\t{sources.SOURCE}\t{version}")
            add("LICENSES/PROVENANCE.txt", "\n".join(rows).encode())
        return path

    def test_binary_members_must_match_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            sources.verify_binary_archive(self.archive(directory))
            for options, message in (({"version": "other"}, "version"),
                                     ({"wrong_hash": True}, "provenance"),
                                     ({"extra": True}, "payload changed")):
                with self.subTest(options=options), self.assertRaisesRegex(ValueError, message):
                    sources.verify_binary_archive(self.archive(directory, **options))

    def test_source_set_is_verified_and_published_as_one_archive(self):
        payloads = {name: name.encode() for name in sources.CHECKSUMS if name != sources.DESCRIPTOR}
        hashes = {name: hashlib.sha256(payload).hexdigest() for name, payload in payloads.items()}
        descriptor = (f"Source: {sources.SOURCE}\nVersion: {sources.VERSION}\nChecksums-Sha256:\n" +
                      "".join(f" {hashes[name]} {len(payload)} {name}\n" for name, payload in payloads.items()))
        payloads[sources.DESCRIPTOR] = descriptor.encode()
        hashes[sources.DESCRIPTOR] = hashlib.sha256(descriptor.encode()).hexdigest()

        def fetch(url, timeout):
            return io.BytesIO(payloads[url.removeprefix(sources.SNAPSHOT)])

        with tempfile.TemporaryDirectory() as temporary, patch.object(sources, "CHECKSUMS", hashes), \
                patch.object(sources, "urlopen", fetch):
            directory = Path(temporary)
            binary = self.archive(directory)
            output = directory / "sources.tar"
            sources.bundle(binary, output)
            with tarfile.open(output) as archive:
                self.assertEqual(set(archive.getnames()), {*hashes, "README.txt", "SHA256SUMS.txt"})
                for name, payload in payloads.items():
                    self.assertEqual(archive.extractfile(name).read(), payload)
            output.unlink()
            payloads[next(iter(hashes))] = b"corrupted archive"
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                sources.bundle(binary, output)
            self.assertFalse(output.exists())
            with self.assertRaisesRegex(ValueError, "names or checksums"):
                sources.verify_descriptor(descriptor.replace(next(iter(hashes)), "other.tar.gz"))


if __name__ == "__main__":
    unittest.main()
