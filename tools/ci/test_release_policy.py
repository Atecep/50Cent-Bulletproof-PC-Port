import unittest

from release_policy import plan as release_plan, read_project_version


def plan(*args, project_version="0.8.0", windows_dxc="source"):
    return release_plan(*args, project_version, windows_dxc)


class ReleasePolicyTest(unittest.TestCase):
    def test_automatic_events_never_package(self):
        for event in ("pull_request", "push"):
            with self.subTest(event=event):
                result = plan(event, "build-and-draft-release", "v0.8.0")
                self.assertEqual((result["package"], result["draft"]), ("false", "false"))

    def test_manual_build_without_version(self):
        result = plan("workflow_dispatch", "build-only", "")
        self.assertEqual((result["package"], result["draft"]), ("true", "false"))
        self.assertEqual(result["linux_archive"], "Bulletproof-linux-x86_64.tar.xz")
        self.assertEqual(result["gnu_sources"], "Bulletproof-linux-gnu-sources.tar")
        self.assertEqual(result["windows_archive"], "Bulletproof-windows-x86_64.zip")

    def test_manual_build_with_matching_version_still_uses_unversioned_archives(self):
        result = plan("workflow_dispatch", "build-only", "v0.8.0")
        self.assertEqual((result["package"], result["draft"], result["version"]), ("true", "false", ""))
        self.assertEqual(result["linux_archive"], "Bulletproof-linux-x86_64.tar.xz")

    def test_drafts_require_explicit_stable_version(self):
        for version in ("", "0.8.0", "v0.7.9", "v0.8.0-beta", "v00.8.0", "v0.8.0+build", "v0.8.0\nextra"):
            with self.subTest(version=version):
                with self.assertRaisesRegex(ValueError, "stable"):
                    plan("workflow_dispatch", "build-and-draft-release", version)

    def test_draft_names_all_archives_with_validated_version(self):
        for version in ("v0.8.0", "v1.2.3"):
            with self.subTest(version=version):
                result = plan("workflow_dispatch", "build-and-draft-release", version,
                              project_version=version.removeprefix("v"))
                self.assertEqual((result["package"], result["draft"], result["version"]), ("true", "true", version))
                self.assertEqual(result["linux_archive"], f"Bulletproof-{version}-linux-x86_64.tar.xz")
                self.assertEqual(result["windows_archive"], f"Bulletproof-{version}-windows-x86_64.zip")
                self.assertEqual(result["gnu_sources"], f"Bulletproof-{version}-linux-gnu-sources.tar")

    def test_explicit_manual_version_must_match_cmake(self):
        cmake = "# project(Bulletproof VERSION 0.8.0)\nproject(\n Bulletproof VERSION 0.8.1\n LANGUAGES CXX)\n"
        current = read_project_version(cmake)
        self.assertEqual(current, "0.8.1")
        for operation in ("build-and-draft-release", "build-only"):
            with self.subTest(operation=operation):
                with self.assertRaisesRegex(ValueError, "must match"):
                    plan("workflow_dispatch", operation, "v0.8.0", project_version=current)
        with self.assertRaisesRegex(ValueError, "literal"):
            read_project_version("project(Bulletproof VERSION ${BULLETPROOF_VERSION})")

    def test_manual_operations_accept_both_dxc_inputs(self):
        for operation in ("build-only", "build-and-draft-release"):
            for dxc in ("source", "release-zip"):
                with self.subTest(operation=operation, dxc=dxc):
                    result = plan("workflow_dispatch", operation, "v0.8.0", windows_dxc=dxc)
                    self.assertEqual(result["package"], "true")
                    self.assertEqual(result["draft"], str(operation == "build-and-draft-release").lower())
                    self.assertEqual(result["windows_dxc"], dxc)

    def test_unknown_operation_and_dxc_input_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "valid Windows DXC"):
            plan("workflow_dispatch", "build-only", "", windows_dxc="arbitrary")
        with self.assertRaisesRegex(ValueError, "operation"):
            plan("workflow_dispatch", "publish", "v0.8.0")


if __name__ == "__main__":
    unittest.main()
