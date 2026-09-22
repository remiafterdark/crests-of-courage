"""Build crests_of_courage.dusk.

A .dusk is a zip: mod.json, the DLL under lib/<platform>/, and whatever else the mod wants to carry.
The models go in it, which is the whole reason this script exists - a download with an empty Models
tab is not the mod we promised people. They ride under res/, because ResourceService (the only way a
mod can read its own bundle) resolves against res/ and a bundle is NOT a folder on disk -
HostService::mod_dir is a scratch directory, not the bundle's contents. ResourceService also cannot
list a directory, so an index.txt naming every file is written beside them; skins.cpp reads it once
and unpacks the lot into the player's own models folder.

WHAT IS NOT ALLOWED IN, and why this script checks rather than trusts:

  - Audiores/. That is the game's own sound bank, ~150 MB of unmodified disc data that the mod never
    reads (voices come from voices.bin, built by tools/voices.py out of the differences alone). The
    Dusklight team asked mods not to ship unmodified game data, and they are right.
  - Anything over the size ceiling, so a model somebody drops in the folder cannot quietly turn the
    download into a gigabyte.

Usage:
    python tools/package.py                     # from build_ninja, models from your data dir
    python tools/package.py --dll <path> --out <path> [--models <dir>]
    python tools/package.py --bundle a.dusk --bundle b.dusk ... --models models --out <path>

EVERY PLATFORM IN ONE FILE. Dusklight loads lib/<platform>/mod.dll or mod.so for the platform it
is running on (windows-amd64, linux-x86_64, macos-arm64, android-aarch64, ios-arm64...), so a
.dusk that carries all of them works everywhere. CI builds each platform with the SDK's own
packaging (one lib/ folder per bundle); --bundle takes the lib/ folder out of each of those, and
this script adds the rest once.
"""

import argparse
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Directories inside a model that must never be shipped, matched on the first path segment under
# files/.
BANNED_UNDER_FILES = ('Audiores',)
BANNED_NAMES = ('opening.bnr',)
# A single file this big inside a model is a mistake, not a model.
MAX_FILE_MB = 32
# And the whole bundle.
MAX_TOTAL_MB = 128


def default_models_dir():
    appdata = os.environ.get('APPDATA', '')
    if not appdata:
        return ''
    return os.path.join(appdata, 'TwilitRealm', 'Dusklight', 'mod_data',
                        'dev.remiafterdark.coop_mod', 'models')


def model_files(models_dir):
    """Every file to ship, as (absolute path, path inside the bundle). Refuses the banned ones."""
    out = []
    problems = []
    for root, dirs, names in os.walk(models_dir):
        # Bookkeeping the mod writes into the models folder at runtime - the .shipped marker that
        # records which version was unpacked, and anything else hidden. Packaging those would ship a
        # marker saying the work is already done, and list it as if it were a model.
        dirs[:] = sorted(d for d in dirs if not d.startswith('.'))
        for name in sorted(n for n in names if not n.startswith('.')):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, models_dir).replace('\\', '/')
            parts = rel.split('/')
            # <model>/files/<first segment>/...
            if len(parts) >= 3 and parts[1] == 'files' and parts[2] in BANNED_UNDER_FILES:
                problems.append('%s (the game\'s own sound bank - see tools/install_skin.py)' % rel)
                continue
            if name in BANNED_NAMES:
                continue
            size_mb = os.path.getsize(full) / (1024.0 * 1024.0)
            if size_mb > MAX_FILE_MB:
                problems.append('%s (%.1f MB, over the %d MB ceiling)' % (rel, size_mb, MAX_FILE_MB))
                continue
            out.append((full, 'res/models/' + rel))
    return out, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dll', default=os.path.join(HERE, 'build_ninja', 'coop_mod.dll'))
    ap.add_argument('--out', default=os.path.join(HERE, 'build_ninja', 'crests_of_courage.dusk'))
    ap.add_argument('--models', default=default_models_dir())
    ap.add_argument('--no-models', action='store_true',
                    help='build without them (a quick test build)')
    ap.add_argument('--bundle', action='append', default=[],
                    help='a per-platform .dusk whose lib/ folder to include (repeatable)')
    args = ap.parse_args()

    entries = [(os.path.join(HERE, 'mod.json'), 'mod.json')]
    # (bundle, name inside it, name inside ours) for the platform libraries taken from bundles.
    from_bundles = []
    platforms = set()
    for bundle in args.bundle:
        with zipfile.ZipFile(bundle) as zb:
            for name in zb.namelist():
                if name.startswith('lib/') and not name.endswith('/'):
                    platform = name.split('/')[1]
                    if name in {dst for _, _, dst in from_bundles}:
                        continue
                    from_bundles.append((bundle, name, name))
                    platforms.add(platform)
    if not from_bundles:
        if not os.path.isfile(args.dll):
            sys.exit('no DLL at %s - build first' % args.dll)
        entries.append((args.dll, 'lib/windows-amd64/mod.dll'))
        platforms.add('windows-amd64')
    print('platforms: ' + ', '.join(sorted(platforms)))

    if not args.no_models:
        if not os.path.isdir(args.models):
            sys.exit('no models folder at %s (use --models, or --no-models)' % args.models)
        found, problems = model_files(args.models)
        if problems:
            print('REFUSED, and nothing was written:')
            for p in problems:
                print('  ' + p)
            sys.exit(1)
        entries += found
        # ResourceService cannot list a directory, so the mod is handed an index naming every file
        # it should unpack. Written next to them, inside the bundle.
        index = ''.join(p[len('res/models/'):] + chr(10) for _, p in found)
        index_path = os.path.join(os.path.dirname(args.out), 'models-index.txt')
        os.makedirs(os.path.dirname(index_path), exist_ok=True)
        with open(index_path, 'w', encoding='utf-8', newline=chr(10)) as f:
            f.write(index)
        entries.append((index_path, 'res/models/index.txt'))
        shipped = sorted({p.split('/')[2] for _, p in found})
        print('models: ' + ', '.join(shipped))

    total = sum(os.path.getsize(src) for src, _ in entries) / (1024.0 * 1024.0)
    for bundle, name, _ in from_bundles:
        with zipfile.ZipFile(bundle) as zb:
            total += zb.getinfo(name).file_size / (1024.0 * 1024.0)
    if total > MAX_TOTAL_MB:
        sys.exit('bundle would be %.1f MB, over the %d MB ceiling' % (total, MAX_TOTAL_MB))

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with zipfile.ZipFile(args.out, 'w', zipfile.ZIP_DEFLATED) as z:
        for src, dst in entries:
            z.write(src, dst)
        for bundle, name, dst in from_bundles:
            with zipfile.ZipFile(bundle) as zb:
                z.writestr(dst, zb.read(name))
    print('wrote %s (%d files, %.1f MB on disk -> %.1f MB packed)' % (
        args.out, len(entries) + len(from_bundles), total,
        os.path.getsize(args.out) / (1024.0 * 1024.0)))


if __name__ == '__main__':
    main()
