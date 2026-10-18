#!/usr/bin/env python3
"""Build host regression targets and run each executable in a disposable directory."""
import argparse
import json
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('targets', nargs='*', help='default: all named *_test xmake targets')
    parser.add_argument('--output', type=pathlib.Path, help='write JSON results and per-target logs')
    args = parser.parse_args()
    manifest = (ROOT / 'xmake.lua').read_text()
    targets = args.targets or re.findall(r'target\("([^"]+_test)"\)', manifest) + [
        'palette_release_test', 'enemy_death_fx_test'
    ]
    output = args.output.resolve() if args.output else None
    if output:
        output.mkdir(parents=True, exist_ok=True)
    results = []
    for target in dict.fromkeys(targets):
        build = subprocess.run(['xmake', 'build', '-y', target], cwd=ROOT,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        log = build.stdout
        result = {'target': target, 'build': build.returncode, 'run': None}
        if build.returncode == 0:
            with tempfile.TemporaryDirectory(prefix='tmc-regression-') as work:
                run = subprocess.run([str(ROOT / 'build/pc' / target)], cwd=work,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                     timeout=180)
                result['run'] = run.returncode
                log += '\n' + run.stdout
        if output:
            (output / f'{target}.log').write_text(log)
        print(f'{target}: {"PASS" if result["build"] == result["run"] == 0 else "FAIL"}', flush=True)
        results.append(result)
        if output:
            (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    raise SystemExit(any(item['build'] != 0 or item['run'] != 0 for item in results))


if __name__ == '__main__':
    main()
