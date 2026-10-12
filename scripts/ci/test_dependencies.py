#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline discovery, feature isolation and configured-tree preservation regressions."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CORE = ['-DBUILD_AUDIO=OFF', '-DBUILD_STORAGE=OFF', '-DBUILD_COMPOSITOR=OFF',
        '-DBUILD_COMPOSITOR_WAYLAND=OFF']
FEATURES = 'BUILD_AUDIO=OFF BUILD_STORAGE=OFF BUILD_COMPOSITOR=OFF BUILD_COMPOSITOR_WAYLAND=OFF'


class DependencyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='system dependencies with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        shutil.copytree(ROOT, self.source, ignore=shutil.ignore_patterns(
            '.git', 'build', '.cache', '__pycache__', 'compile_commands.json'))
        self.prefix = self.root / 'external prefix'
        self.prefix.mkdir()
        self.env = dict(os.environ, HOLONIGHT_BUILD_FEATURES=FEATURES)
        self.env.pop('HOLONIGHT_EXTERNAL_PREFIX', None)
        self.env.pop('HOLONIGHT_DEPENDENCY_PREFIX', None)

    def run_command(self, command, env=None):
        return subprocess.run(command, cwd=self.source, env=env or self.env,
                              text=True, capture_output=True)

    def package(self, name, version, targets=()):
        directory = self.prefix / 'lib/cmake' / name
        directory.mkdir(parents=True, exist_ok=True)
        (directory / f'{name}ConfigVersion.cmake').write_text(
            f'set(PACKAGE_VERSION {version})\n'
            'if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)\n'
            ' set(PACKAGE_VERSION_COMPATIBLE FALSE)\nelse()\n'
            ' set(PACKAGE_VERSION_COMPATIBLE TRUE)\nendif()\n')
        (directory / f'{name}Config.cmake').write_text(
            f'set({name}_FOUND TRUE)\n' + ''.join(
                f'if(NOT TARGET {target})\n add_library({target} INTERFACE IMPORTED)\nendif()\n'
                for target in targets))

    def configure(self, tests=False):
        return self.run_command(['cmake', '-S', '.', '-B', 'build/probe', '-G', 'Ninja',
                                 *CORE, f'-DBUILD_TESTS={"ON" if tests else "OFF"}',
                                 f'-DCMAKE_FIND_ROOT_PATH={self.prefix}',
                                 '-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY'])

    def test_missing_and_old_qt(self):
        for version in (None, '6.10.0'):
            with self.subTest(version=version):
                if version:
                    self.package('Qt6', version)
                result = self.configure()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Qt6', result.stdout + result.stderr)
                self.assertIn('6.11', result.stdout + result.stderr)

    def test_missing_old_gtest_and_missing_gmock(self):
        self.package('Qt6', '6.11.0', ('Qt6::Core', 'Qt6::Test'))
        for version in (None, '1.13.0', '1.14.0'):
            with self.subTest(version=version):
                if version:
                    self.package('GTest', version, ('GTest::gtest',))
                result = self.configure(tests=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('GTest::gmock' if version == '1.14.0' else 'GTest',
                              result.stdout + result.stderr)

    def test_disabled_components_do_not_probe_dependencies(self):
        result = self.run_command(['cmake', '-S', '.', '-B', 'build/core', '-G', 'Ninja',
                                  *CORE, '-DBUILD_TESTS=OFF', '-DBUILD_COMPOSITOR_WAYLAND=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_GTest=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Network=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6Gui=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6DBus=ON',
                                  '-DCMAKE_DISABLE_FIND_PACKAGE_Qt6WaylandClient=ON'])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.run_command(['cmake', '--build', 'build/core']).returncode, 0)

    def test_normal_commands_fail_without_preparation_and_preserve_stale_tree(self):
        env = dict(self.env, PKG_CONFIG_LIBDIR=str(self.prefix), PKG_CONFIG_PATH='',
                   HOLONIGHT_BUILD_FEATURES='BUILD_AUDIO=ON BUILD_STORAGE=OFF BUILD_COMPOSITOR=OFF BUILD_COMPOSITOR_WAYLAND=OFF')
        directory = self.source / 'build/test'
        directory.mkdir(parents=True)
        (directory / 'CMakeCache.txt').write_text('stale cache')
        marker = directory / 'marker'
        marker.write_text('preserved')
        for command in ('configure', 'build', 'test'):
            result = self.run_command(['python3', 'tooling/workflow.py', command, '--preset', 'test'], env)
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('libpulse', result.stdout + result.stderr)
            self.assertEqual(marker.read_text(), 'preserved')
            self.assertFalse((self.source / 'build/deps').exists())
            self.assertFalse((self.source / 'build/external').exists())

    def test_external_prefix_with_spaces_and_stale_cache_archive(self):
        qt = Path(subprocess.check_output(['pkg-config', '--variable=libdir', 'Qt6Core'], text=True).strip()) / 'cmake'
        (self.prefix / 'lib').mkdir()
        (self.prefix / 'lib/cmake').symlink_to(qt, target_is_directory=True)
        env = dict(self.env, HOLONIGHT_EXTERNAL_PREFIX=str(self.prefix),
                   HOLONIGHT_BUILD_FEATURES=FEATURES.replace('BUILD_AUDIO=OFF', 'BUILD_AUDIO=ON'))
        command = ['python3', 'tooling/workflow.py', 'configure', '--preset', 'debug']
        result = self.run_command(command, env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        directory = self.source / 'build/debug'
        state = json.loads((directory / 'dependency-state.json').read_text())
        self.assertIn(f'Qt6_DIR={self.prefix}/lib/cmake/Qt6', state['external']['resolved'])
        (directory / 'marker').write_text('preserved')
        state['toolchain'] = {'stale': True}
        (directory / 'dependency-state.json').write_text(json.dumps(state))
        result = self.run_command(command, env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.source / 'build/debug.previous-1/marker').read_text(), 'preserved')
        self.assertFalse((self.source / 'build/deps').exists())


if __name__ == '__main__':
    unittest.main()
