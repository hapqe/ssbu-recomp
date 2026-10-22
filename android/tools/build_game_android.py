"""Builds the game folder of the Android port on a Mac or Linux PC, from your own dump (BUILDING.md: tools/extract/dump_folder.py):

    python3 android/tools/build_game_android.py --dump <folder> [--build <folder>] [--jobs N] [--modules all|a,b]
                                                [--redo <step> | --from <step> | --only <step>] [--push [--serial <adb serial>]]
                                                [--package [<file.zip>]]

The steps of scripts/setup.py that derive files from the dump run as they are there (stage, convert, modules, tables, lift,
sdk); compile and nro build the game libraries with the NDK's clang for arm64 (tools/lifter/build.py RECOMP_TARGET=android):
ELF shared objects under their Windows names (work/ssbu_game_eh_o1.dll, work/nro_bin_eh_o2/*.dll), which the launcher copies
into the app and android/compat's LoadLibraryA opens. Steps are resumable, as in setup.py (state.json of the build folder).

--push copies what the game reads at run time to the device's game folder (default /sdcard/OpenSmash, the launcher's):
state.json, binaries/<version>/, work/ (the libraries and fonts_open/) and the dump's RomFS as romfs/. data.arc goes in
parts of 512 MB, appended on the device one by one, so an interrupted push resumes where it stopped.
--package writes the game libraries alone into a .zip (default <build>/OpenSmash-libraries-<version>.zip): the one part of the
game folder that needs a PC. Copy it to the device and import it in the launcher ("Import libraries (.zip)"); the launcher
makes everything else there from your Switch files (keys, base game, update, DLC). The .zip holds your recompiled game: it
is for your own devices, do not share it.
Needs: python 3.10+ with requirements.txt, the Android NDK (NDK, else $ANDROID_HOME/ndk/29.0.14206865) and, for --push, adb.
"""
import argparse
import os
import shlex
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
sys.path.insert(0, os.path.join(REPO, 'scripts'))
import setup as S  # noqa: E402

STEPS = ['deps', 'stage', 'convert', 'modules', 'tables', 'lift', 'sdk', 'compile', 'nro']
SDK = os.environ.get('ANDROID_HOME', os.path.expanduser('~/Library/Android/sdk'))
NDK = os.environ.get('NDK', os.path.join(SDK, 'ndk', '29.0.14206865'))
HOST = 'darwin-x86_64' if sys.platform == 'darwin' else 'linux-x86_64'
PART = 512 << 20


def android_env(cx):
    # RECOMP_INLINE: the guest load / store / flag / NEON helpers inlined into the game code (as the Linux and macOS builds
    # of scripts/setup.py; build_opt.py falls back per file when an inlined compile takes too long)
    return cx.env(RECOMP_TARGET='android', RECOMP_CLANG=cx.tools['clang'], RECOMP_GEN='gen_eh_o2', RECOMP_OBJ='obj_eh_o1',
                  RECOMP_DLL='ssbu_game_eh_o1.dll', RECOMP_NRO_SUFFIX='_eh_o2', RECOMP_INLINE='1')


def step_deps(cx):
    clang = os.path.join(NDK, 'toolchains', 'llvm', 'prebuilt', HOST, 'bin', 'clang')
    if not os.path.isfile(clang):
        raise S.Fail('the NDK\'s clang was not found at %s (NDK=<ndk folder>)' % clang)
    cx.tools['clang'] = clang
    S.say('  clang: %s' % subprocess.run([clang, '--version'], capture_output=True, text=True).stdout.splitlines()[0])
    S.dump_checks(cx, cx.a)


def step_tables(cx):
    """setup.py's tables without the host-only headers (the self-tests' reference data: libmain.so is built from the
    stage of android/tools/stage.py, which has its own)."""
    for tool in ('gen_imports.py', 'gen_sdkdata.py', 'gen_eh_tests.py'):
        cx.run('tables', [sys.executable, tool])
    S.say('  imports_all.txt, sdkdata.c, eh_tests.h')


def step_compile(cx):
    cx.run('compile', [sys.executable, 'build_opt.py', 'main', str(cx.jobs)], env=android_env(cx))
    dll = os.path.join(cx.work, 'ssbu_game_eh_o1.dll')
    S.say('  ssbu_game_eh_o1.dll (arm64 ELF) %.0f MB' % (os.path.getsize(dll) / 1048576.0))


def step_nro(cx):
    mods = [m for m in (cx.a.modules or 'all').split(',') if m]
    cx.run('nro', [sys.executable, 'build_opt.py', 'nro', str(cx.jobs)] + ([] if mods == ['all'] else mods), env=android_env(cx))
    d = os.path.join(cx.work, 'nro_bin_eh_o2')
    S.say('  %d module libraries in nro_bin_eh_o2 (%.0f MB)' % (len([f for f in os.listdir(d) if f.endswith('.dll')]), S.disk_mb(d)))


RUNNERS = {'deps': step_deps, 'stage': S.step_stage, 'convert': S.step_convert, 'modules': S.step_modules, 'tables': step_tables,
           'lift': S.step_lift, 'sdk': S.step_sdk, 'compile': step_compile, 'nro': step_nro}


# ---------------------------------------------------------------------------------------------- package
def package(cx, out):
    """work/ssbu_game_eh_o1.dll and work/nro_bin_eh_o2/*.dll into a .zip (stored: the libraries hardly compress, and the
    launcher's progress counts the bytes of the file)."""
    import zipfile
    ver = cx.state['game_version']
    out = out or os.path.join(cx.build, 'OpenSmash-libraries-%s.zip' % ver)
    libs = [(f, 'work/' + rel) for f, rel in [(l, os.path.relpath(l, cx.work).replace(os.sep, '/')) for l, _ in push_list(cx) if l.lower().endswith('.dll')]]
    tmp = out + '.part'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED, allowZip64=True) as z:
        z.writestr('README.txt', 'OpenSmash game libraries for Android, game version %s. Your own recompiled game: for your own devices only.\n'
                                 'Import this file in the OpenSmash launcher: Game files > Import libraries (.zip).\n' % ver)
        for f, rel in libs:
            z.write(f, rel)
    os.replace(tmp, out)
    S.say('package: %d libraries, %.0f MB -> %s' % (len(libs), os.path.getsize(out) / 1048576.0, out))


# ---------------------------------------------------------------------------------------------- push
def push_list(cx):
    """(local file, path below the device's game folder) of everything the game reads at run time."""
    out = [(cx.state_file, 'state.json')]
    ver = cx.state['game_version']
    for n in sorted(os.listdir(cx.bin)):
        out.append((os.path.join(cx.bin, n), 'binaries/%s/%s' % (ver, n)))
    for root, dirs, files in os.walk(cx.work):
        rel = os.path.relpath(root, cx.work)
        top = rel.split(os.sep)[0]
        dirs[:] = [d for d in dirs if rel != '.' or d in ('nro_bin_eh_o2', 'fonts_open', 'fighters_bin')]
        for f in files:
            if f.lower().endswith('.dll') or top == 'fonts_open':
                out.append((os.path.join(root, f), 'work/' + os.path.normpath(os.path.join(rel, f)).replace(os.sep, '/')))
    romfs = cx.state['dump']['romfs']
    for root, _, files in os.walk(romfs):
        for f in files:
            out.append((os.path.join(root, f), 'romfs/' + os.path.relpath(os.path.join(root, f), romfs).replace(os.sep, '/')))
    return out


def push(cx, serial, dest):
    adb = ['adb'] + (['-s', serial] if serial else [])

    def sh(cmd):
        return subprocess.run(adb + ['shell', cmd], capture_output=True, text=True).stdout.strip()

    def remote_size(path):
        s = sh('stat -c %%s %s 2>/dev/null' % shlex.quote(path))
        return int(s) if s.isdigit() else -1

    files = push_list(cx)
    total = sum(os.path.getsize(f) for f, _ in files)
    S.say('push: %d files, %.1f GB -> %s' % (len(files), total / 2 ** 30, dest))
    t0, sent = time.time(), 0
    for local, rel in files:
        size, remote = os.path.getsize(local), dest + '/' + rel
        have = remote_size(remote)
        if have == size and not rel.endswith(('.dll', '.json')):      # (libraries and state.json change between builds)
            continue
        sh('mkdir -p %s' % shlex.quote(os.path.dirname(remote)))
        if size <= PART:
            r = subprocess.run(adb + ['push', local, remote], capture_output=True, text=True)
            if r.returncode:
                raise S.Fail('adb push %s failed: %s' % (rel, (r.stdout + r.stderr)[-500:]))
            sent += size
            continue
        # a large file (data.arc): parts appended on the device; what is there already (a whole number of parts) stays
        done = have if 0 <= have < size and have % PART == 0 else 0
        if not done:
            sh('rm -f %s' % shlex.quote(remote))
        part = os.path.join(cx.build, 'push_part.bin')
        with open(local, 'rb') as f:
            f.seek(done)
            while done < size:
                with open(part, 'wb') as p:
                    p.write(f.read(PART))
                r = subprocess.run(adb + ['push', part, remote + '.part'], capture_output=True, text=True)
                if r.returncode:
                    raise S.Fail('adb push of %s at %d failed: %s' % (rel, done, (r.stdout + r.stderr)[-500:]))
                sh('cat %s >> %s && rm %s' % (shlex.quote(remote + '.part'), shlex.quote(remote), shlex.quote(remote + '.part')))
                done += os.path.getsize(part)
                sent += os.path.getsize(part)
                if remote_size(remote) != done:
                    raise S.Fail('%s on the device is not %d bytes after appending a part: run --push again' % (remote, done))
                S.say('  %s %5.1f / %.1f GB   %.1f MB/s' % (rel, done / 2 ** 30, size / 2 ** 30, sent / 1048576.0 / max(1, time.time() - t0)))
        os.remove(part)
    S.say('push: done, %.1f GB sent in %s' % (sent / 2 ** 30, S.fmt(time.time() - t0)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dump', help='folder with exefs/ and romfs/ (and optionally sysdata/) dumped from your console')
    ap.add_argument('--exefs'), ap.add_argument('--romfs'), ap.add_argument('--sysdata')
    ap.add_argument('--build', default=os.path.join(REPO, 'android', 'build', 'game'), help='build folder (default android/build/game)')
    ap.add_argument('--jobs', type=int, help='parallel compiler jobs (default: the number of CPUs)')
    ap.add_argument('--modules', help='script modules to build: all (default) or a comma list')
    ap.add_argument('--sdk-roots', help='another list of SDK functions to lift (default: tools/lifter/sdklift_roots.txt)')
    ap.add_argument('--redo'), ap.add_argument('--from', dest='from_step'), ap.add_argument('--only')
    ap.add_argument('--push', action='store_true', help='copy the game folder to the device (adb) after the build')
    ap.add_argument('--serial', help='adb serial of the device')
    ap.add_argument('--dest', default='/sdcard/OpenSmash', help='game folder on the device (default /sdcard/OpenSmash)')
    ap.add_argument('--package', nargs='?', const='', default=None, metavar='ZIP',
                    help='write the game libraries into a .zip for the launcher\'s "Import libraries" (default <build>/OpenSmash-libraries-<version>.zip)')
    a = ap.parse_args()
    a.nocrt = a.runtime = None
    cx = S.Ctx(a)
    cx.jobs = a.jobs or os.cpu_count() or 8
    done = cx.state.setdefault('done', {})
    for opt in (a.redo, a.from_step, a.only):
        if opt and opt not in STEPS:
            S.say('unknown step %s (steps: %s)' % (opt, ' '.join(STEPS)))
            return 2
    force = {a.redo} if a.redo else set()
    if a.from_step:
        force |= set(STEPS[STEPS.index(a.from_step):])
    todo = sorted({'deps', 'stage', a.only}, key=STEPS.index) if a.only else STEPS
    if a.only:
        force.add(a.only)
    if 'dump' not in cx.state and not (a.dump or a.exefs):
        S.say('--dump <folder> is needed for the first run')
        return 2
    t_all = time.time()
    for i, s in enumerate(STEPS):
        if s not in todo:
            continue
        if s in done and s not in force and s not in ('deps', 'stage'):
            S.say('[%d/%d] %s: done earlier (%s), skipped' % (i + 1, len(STEPS), s, S.fmt(done[s]['secs'])))
            continue
        if s == 'deps' and not (a.dump or a.exefs):             # a later run: the dump of state.json
            d = cx.state['dump']
            a.exefs, a.romfs, a.sysdata = d['exefs'], d['romfs'], d.get('sysdata')
        S.say('[%d/%d] %s ...' % (i + 1, len(STEPS), s))
        t0 = time.time()
        try:
            RUNNERS[s](cx)
        except S.Fail as e:
            S.say('FAILED at step %s: %s' % (s, e))
            cx.save()
            return 1
        done[s] = {'secs': time.time() - t0, 'at': time.strftime('%Y-%m-%d %H:%M')}
        cx.save()
        S.say('[%d/%d] %s: ok  %s' % (i + 1, len(STEPS), s, S.fmt(done[s]['secs'])))
    S.say('build finished in %s: %s' % (S.fmt(time.time() - t_all), cx.build))
    if a.package is not None:
        package(cx, a.package)
    if a.push:
        try:
            push(cx, a.serial, a.dest.rstrip('/'))
        except S.Fail as e:
            S.say('FAILED at push: %s' % e)
            return 1
        S.say('On the device: open OpenSmash, choose %s as the game folder, copy the game libraries, start.' % a.dest)
    return 0


if __name__ == '__main__':
    sys.exit(main())
