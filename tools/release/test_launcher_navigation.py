#!/usr/bin/env python3
"""Native launcher interaction checks using synthetic libraries and software SDL.

Pass --launcher after building launcher/. Without an artifact this is explicitly
SKIP, not a UI pass. No game files, game processes or network requests are needed.
Screenshots show synthetic UI states, not evidence that any game is playable.
"""
import argparse
import copy
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--launcher', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not args.launcher:
        print('SKIP: pass --launcher PATH to test a compiled native launcher')
        return 0
    launcher = args.launcher.resolve()
    if not launcher.is_file():
        parser.error('launcher artifact does not exist')
    with tempfile.TemporaryDirectory(prefix='qwark-ui-fixtures-') as tmp:
        scratch = Path(tmp)
        out = args.output.resolve() if args.output else scratch / 'shots'
        out.mkdir(parents=True, exist_ok=True)
        library = json.loads((ROOT / 'data/titles/library.example.json').read_text())
        library['locations'] = {}
        library['prefs'] = {'theme': 'dark', 'update_check': 'off'}
        for game in library['games']:
            game['art'] = {'icon': None, 'background': None}
            game['files'] = {}
            game['overrides'] = {}
            game['sources'] = {}
        selected = next(game['id'] for game in library['games'] if game['id'] == 'nexus')
        for game in library['games']:
            if game['id'] in ('tods', selected):
                game['playability'] = 'boots' if game['id'] == 'tods' else 'menus'
                game['playability_note'] = 'Synthetic UI fixture, not game evidence'
                game['playability_checked'] = 'fixture'
        inputs = {'normal': library, 'empty': {'schema': 1, 'games': []}, 'long': copy.deepcopy(library)}
        long_game = next(game for game in inputs['long']['games'] if game['id'] == selected)
        long_game['title'] += ' — A deliberately long synthetic title to check wrapping and keep all controls reachable'
        long_game['files'] = {'disc': 'C:\\Synthetic UI fixture\\' + 'a-long-folder-name\\' * 12}
        # ENHANCEMENT: the opt-in Unofficial group (prefs.unofficial on): the engine's entries, one set up, one not
        unofficial = copy.deepcopy(library)
        unofficial['prefs']['unofficial'] = 'on'
        unofficial['unofficial_exes'] = {}
        unofficial['unofficial'] = [
            {'id': 'unofficial-tods', 'title': 'Ratchet & Clank: Tools of Destruction', 'project': 'ratchet-tod-recomp',
             'licence': 'MIT', 'url': 'https://github.com/Re-TeRo17/ratchet-tod-recomp', 'exe_name': 'tod.exe',
             'exe': None, 'state': 'not_set_up', 'reason': "Not set up: choose the game's .exe", 'warning': None},
            {'id': 'unofficial-nexus', 'title': 'Ratchet & Clank: Into the Nexus', 'project': 'nexus-recomp',
             'licence': 'MIT', 'url': 'https://github.com/vagenam/nexus-recomp', 'exe_name': 'NexusRecomp.exe',
             'exe': str(scratch / 'Other.exe'), 'state': 'ready', 'reason': 'Ready to start',
             'warning': 'The README of nexus-recomp names NexusRecomp.exe; you chose Other.exe.'}]
        inputs['unofficial'] = unofficial
        paths = {}
        for name, value in inputs.items():
            paths[name] = scratch / (name + '.json')
            paths[name].write_text(json.dumps(value))
        paths['error'] = scratch / 'invalid.json'
        paths['error'].write_text('{ invalid synthetic library')
        initial_bytes = {name: path.read_bytes() for name, path in paths.items()}
        # Real read-only settings commands against temporary data directories.
        # Screenshot mode suppresses scans, kit fetches and update checks.
        engine = '"' + sys.executable + '" "' + str(ROOT / 'tools/release/catalog.py') + '"'
        cases = [
            ('open-details', 'normal', 'library', 'enter', 'Game Details', selected),
            ('back-retains-selection', 'normal', 'details', 'esc', 'Library', selected),
            ('reopen-retains-selection', 'normal', 'library', 'enter,esc,enter', 'Game Details', selected),
            ('menu-files', 'normal', 'library', 'f10,down,enter', 'Game Files', selected),
            ('menu-settings', 'normal', 'library', 'f10,down,down,enter', 'Settings', selected),
            ('menu-about', 'normal', 'library', 'f10,down,down,down,enter', 'About', selected),
            ('settings-popup-dismiss', 'normal', 'settings', 'f10,esc', 'Settings', selected),
            ('settings-back', 'normal', 'settings', 'esc', 'Library', selected),
            ('files-back', 'normal', 'files', 'esc', 'Library', selected),
            ('about-back', 'normal', 'about', 'esc', 'Library', selected),
            ('empty-menu', 'empty', 'library', 'f10,down,enter', 'Game Files', '(none)'),
            ('error-menu', 'error', 'library', 'f10,down,down,enter', 'Settings', '(none)'),
        ]
        results = []
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software')

        def run(name, fixture, screen, keys='', expected=None, game=selected, size='1280x720', theme='dark', scroll=None):
            command = [str(launcher), '--library', str(paths[fixture]), '--engine', engine,
                       '--data', str(scratch / 'data'), '--shared-dir', str(scratch / 'shared'),
                       '--screenshot', str(out / (name + '.png')), '--screen', screen,
                       '--select', selected, '--size', size, '--theme', theme]
            if keys:
                command += ['--keys', keys]
            if scroll is not None:
                command += ['--scroll', str(scroll)]
            png = out / (name + '.png')
            png.unlink(missing_ok=True)
            completed = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30, check=False)
            good = completed.returncode == 0 and png.is_file() and png.stat().st_size > 100
            if expected:
                good &= f'nav: screen={expected} ' in completed.stdout
                good &= f'selected={game}\n' in completed.stdout
            result = {'case': name, 'ok': good, 'exit': completed.returncode,
                      'stdout': completed.stdout, 'stderr': completed.stderr, 'size': size, 'theme': theme}
            results.append(result)
            print(('PASS ' if good else 'FAIL ') + name)
            if not good:
                print((completed.stdout + completed.stderr)[-2000:])

        for case in cases:
            run(*case)
        for theme in ('dark', 'light'):
            for size in ('1280x720', '560x720', '360x720'):
                run(f'library-{size}-{theme}', 'normal', 'library', size=size, theme=theme)
                run(f'details-{size}-{theme}', 'normal', 'details', size=size, theme=theme)
                run(f'settings-{size}-{theme}', 'normal', 'settings', size=size, theme=theme)
        run('details-wide', 'normal', 'details', size='1920x720')
        run('unofficial-library', 'unofficial', 'library', size='1280x1500', scroll=1100)
        run('unofficial-library-narrow', 'unofficial', 'library', size='360x720', scroll=2400)
        run('unofficial-settings', 'unofficial', 'settings', size='1280x1000', scroll=2700)
        run('unofficial-open-details', 'unofficial', 'library', 'enter', 'Game Details', selected)
        run('long-title', 'long', 'details', size='560x720')
        run('settings-stacked', 'normal', 'settings', size='360x720', scroll=500)
        run('files-narrow', 'normal', 'files', size='360x720')
        for name, path in paths.items():
            unchanged = path.read_bytes() == initial_bytes[name]
            results.append({'case': 'library-unchanged-' + name, 'ok': unchanged})
            print(('PASS ' if unchanged else 'FAIL ') + 'library-unchanged-' + name)
        (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        print(f'{sum(r["ok"] for r in results)}/{len(results)} checks passed; images require separate visual inspection')
        return 0 if all(r['ok'] for r in results) else 1


if __name__ == '__main__':
    sys.exit(main())
