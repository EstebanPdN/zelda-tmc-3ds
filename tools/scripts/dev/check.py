#!/usr/bin/env python3
"""Validate maintained repository references and public-source boundaries."""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[3]
CURRENT_DOCS = [
    'README.md', 'docs/README.md', 'docs/ARCHITECTURE.md', 'docs/DEVELOPMENT.md',
    'tools/tests/README.md', 'tools/scripts/dev/README.md',
]
PRIVATE = {'AGENTS.md', 'PROTOCOL.md', 'WORKFLOW.md', 'GITHUB_RELEASE_WORKFLOW.md'}
RETIRED = re.compile(r'Port3DSFullView|PORT_3DS_FULL_VIEW|virtuappu_mode1_ws_full_view|3DSFullViewComboEnabled')


def main():
    errors = []
    files = subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'], cwd=ROOT
    ).decode().split('\0')
    files = sorted({name for name in files if name and (ROOT / name).is_file()})
    for name in files:
        path = pathlib.PurePosixPath(name)
        if path.name in PRIVATE or path.suffix.lower() in {'.gba', '.sav', '.ppu1', '.dmp', '.dump'}:
            errors.append(f'private/runtime file in public tree: {name}')
        if path.parts[0] in {'src', 'platform', 'tools'} and path.suffix in {'.c', '.h', '.cpp', '.hpp'}:
            if RETIRED.search((ROOT / name).read_text()):
                errors.append(f'retired presentation policy reference: {name}')
    for name in CURRENT_DOCS:
        path = ROOT / name
        text = path.read_text()
        links = re.findall(r'\]\(([^)]+)\)', text) + re.findall(r'(?:src|href)="([^"]+)"', text)
        for link in links:
            link = link.split('#')[0]
            if not link or re.match(r'[a-z]+:', link):
                continue
            if not (path.parent / link).exists():
                errors.append(f'{name}: missing local link {link}')
    for name in re.findall(r'"(tools/tests/port/[^"*]+)"', (ROOT / 'xmake.lua').read_text()):
        if not (ROOT / name).is_file():
            errors.append(f'xmake test source missing: {name}')
    for error in errors:
        print(error, file=sys.stderr)
    print(f'{len(files)} source-tree files; current documentation links and test references checked')
    return bool(errors)


if __name__ == '__main__':
    sys.exit(main())
