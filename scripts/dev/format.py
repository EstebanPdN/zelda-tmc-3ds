#!/usr/bin/env python3
"""Check or apply the repository's C/C++ style without applying code rewrites."""
import argparse
import pathlib
import shutil
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
# Generated tables and imported libraries retain their producer/upstream style.
GENERATED = {
    'port/data_const_stubs.c', 'port/data_stubs_autogen.c', 'port/stubs_autogen.c',
    'port/port_rom_tables.c', 'port/flag_remap_generated.c', 'port/flag_remap_generated.h',
    'port/generated_sounds_embed.cpp',
}
# This retained regression input uses the engine's assembly script language,
# not C/C++; keep its original producer syntax and indentation.
NON_C_FIXTURES = {'tests/reference/scripts/kinstoneFusion/script_Goron2Kinstone6.inc'}
SUFFIXES = {'.c', '.h', '.cpp', '.hpp', '.inc'}


def source_files():
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    # Include new source during development as well as committed source.
    untracked = subprocess.check_output(
        ['git', 'ls-files', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    for name in sorted(set(tracked + untracked)):
        path = pathlib.PurePosixPath(name)
        if (path.parts and path.parts[0] in {'engine', 'port', 'platform', 'tests'}
                and name not in GENERATED and name not in NON_C_FIXTURES and not name.startswith('port/SDL3/')
                and path.suffix in SUFFIXES and (ROOT / name).is_file()):
            yield name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--write', action='store_true', help='apply formatting')
    parser.add_argument('--clang-format', default='clang-format', help='formatter executable (20.1.8)')
    args = parser.parse_args()
    tool = shutil.which(args.clang_format)
    if tool is None:
        parser.error('clang-format 20.1.8 is required')
    tool = str(pathlib.Path(tool).resolve())
    version = subprocess.check_output([tool, '--version'], text=True).strip()
    if 'version 20.1.8' not in version:
        parser.error(f'expected clang-format 20.1.8; found {version}')
    files = list(source_files())
    options = ['-i'] if args.write else ['--dry-run', '--Werror']
    for offset in range(0, len(files), 64):
        subprocess.run([tool, '-style=file', *options, *files[offset:offset + 64]], cwd=ROOT, check=True)
    print(f'{len(files)} maintained C/C++ files: {"formatted" if args.write else "style verified"}')


if __name__ == '__main__':
    main()
