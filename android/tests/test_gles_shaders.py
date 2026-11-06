"""Offline checks for the engine's actual GLES shader assembly code.

The validator is optional locally; CI installs it and sets GLES_SHADER_VALIDATOR_REQUIRED.
Driver-specific compilation, linking and runtime rendering still require Android tests.
"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / 'Resources'


class GlesShaderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('g++'):
            raise unittest.SkipTest('g++ required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='zs-gles-test-')
        cls.work = Path(cls.tmp.name)
        cls.tool = cls.work / 'shader-tool'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'android/native/GLESShaderSource.cpp'),
                        str(ROOT / 'android/native/tests/GLESShaderTool.cpp'),
                        '-o', str(cls.tool)], check=True)
        cls.validator = shutil.which('glslangValidator')
        if os.environ.get('GLES_SHADER_VALIDATOR_REQUIRED') and not cls.validator:
            raise RuntimeError('glslangValidator is required in CI')

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def assemble(self, manifest, stage, config, resource_root=RESOURCES, check=True):
        return subprocess.run([str(self.tool), str(resource_root), str(manifest), stage,
                               *map(str, config)], capture_output=True, text=True, check=check)

    def test_unknown_special_component_fails(self):
        manifest = self.work / 'bad.program'
        manifest.write_text('*not-a-real-component*\n')
        result = self.assemble(manifest, 'vertex', (0, 0, 0, 0, 0, 0), check=False)
        self.assertNotEqual(0, result.returncode)
        self.assertIn('Unknown special shader', result.stderr)

    def test_duplicate_declaration_keeps_conditional_scope(self):
        if not self.validator:
            self.skipTest('glslangValidator not installed')
        root = self.work / 'conditional'
        root.mkdir(exist_ok=True)
        (root / 'first.vs').write_text('#if 0\nuniform float amount;\n#endif\n')
        (root / 'second.vs').write_text('uniform float amount;\nvoid main() { gl_Position = vec4(amount); }\n')
        manifest = root / 'test.program'
        manifest.write_text('first.vs\nsecond.vs\n')
        result = self.assemble(manifest, 'vertex', (0, 0, 0, 0, 0, 0), root)
        shader = root / 'test.vert'
        shader.write_text(result.stdout)
        subprocess.run([self.validator, str(shader)], check=True, capture_output=True, text=True)

    def test_all_game_programs_compile_and_link_for_representative_presets(self):
        if not self.validator:
            self.skipTest('glslangValidator not installed')
        programs = sorted((RESOURCES / 'Shaders/OpenGL').rglob('*.program'))
        self.assertGreater(len(programs), 0)
        # Low preset, high preset, and sparse-shadow / maximum-radiosity preset.
        configs = [(0, 0, 0, 0, 0, 0), (1, 1, 1, 1, 1, 0), (0, 1, 1, 2, 1, 1)]
        for program in programs:
            for config in configs:
                with self.subTest(program=str(program.relative_to(RESOURCES)), config=config):
                    shader_paths = []
                    for stage, extension in [('vertex', '.vert'), ('fragment', '.frag')]:
                        result = self.assemble(program, stage, config)
                        path = self.work / ('shader' + extension)
                        path.write_text(result.stdout)
                        shader_paths.append(str(path))
                    result = subprocess.run([self.validator, '-l', *shader_paths],
                                            capture_output=True, text=True)
                    self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
