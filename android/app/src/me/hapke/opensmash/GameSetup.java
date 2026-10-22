package me.hapke.opensmash;

import android.content.Context;
import android.content.res.AssetManager;
import android.os.Environment;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.Locale;
import java.util.TreeMap;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/**
 * The game folder made on the device from the user's own Switch files, with no PC: what scripts/setup.py's dump steps and
 * android/tools/build_game_android.py --push do, except building the game libraries.
 *
 *   binaries/<version>/   the update's ExeFS and main.elf / sdk.elf     libswitchimport.so --exefs (android/tools/romfs_extract.c)
 *   romfs/                the base RomFS with the update applied        libswitchimport.so (resumable: whole files stay)
 *   state.json            "game_version"                                 read by the launcher and android_main.c
 *   config/dlc.txt        the add-on contents found                      read by runtime/nnmisc.c aoc_load
 *   work/fonts_open/      the open-source system fonts (APK assets)      runtime/nnpl.c
 *
 * The game libraries (work/ssbu_game_eh_o1.dll, work/nro_bin_eh_o2/) are the game's code recompiled ahead of time; they are
 * made on a PC from the same dump (android/tools/build_game_android.py --package) and imported as a .zip.
 */
final class GameSetup {
    private GameSetup() { }

    interface Progress { void status(String s); void progress(long done, long total); }

    /** What a folder of Switch files holds. */
    static final class Found {
        File prodKeys, titleKeys;
        final List<File> bases = new ArrayList<>(), updates = new ArrayList<>();
        final TreeMap<Integer, String> dlc = new TreeMap<>();
        final List<String> notes = new ArrayList<>();
        int dlcPackages;
        String summary() {
            StringBuilder sb = new StringBuilder();
            sb.append(prodKeys != null ? "● Keys: " + prodKeys.getName() + (titleKeys != null ? " + " + titleKeys.getName() : "") : "○ Keys: no prod.keys found").append('\n');
            sb.append(!bases.isEmpty() ? "● Base game: " + bases.get(0).getName() : "○ Base game (0100…6E000): not found").append('\n');
            sb.append(!updates.isEmpty() ? "● Update: " + updates.get(0).getName() + (updates.size() > 1 ? " (+" + (updates.size() - 1) + " more)" : "") : "○ Update 13.0.4 / 13.0.5 (0100…6E800): not found").append('\n');
            sb.append(dlc.isEmpty() ? "○ Add-on content: none found" : "● Add-on content: " + dlc.size() + " found").append('\n');
            for (String n : notes) sb.append("  ").append(n).append('\n');
            return sb.toString().trim();
        }
    }

    static File tool(Context c) { return new File(c.getApplicationInfo().nativeLibraryDir, "libswitchimport.so"); }

    /* ---------------------------------------------------------------- scanning */
    static Found scan(File dir, File tool, File game, Progress p) {
        Found f = new Found();
        List<File> packages = new ArrayList<>(), archives = new ArrayList<>();
        String skip;
        try { skip = game.getCanonicalPath(); } catch (IOException e) { skip = game.getAbsolutePath(); }
        walk(dir, 5, skip, f, packages, archives);
        if (f.prodKeys == null) {                                   /* the usual places of a keys file */
            File ext = Environment.getExternalStorageDirectory();
            for (String rel : new String[] {"switch/prod.keys", "switch/keys/prod.keys", "Download/prod.keys", "Documents/prod.keys", "yuzu/keys/prod.keys", "eden/keys/prod.keys"}) {
                File k = new File(ext, rel);
                if (k.isFile()) { f.prodKeys = k; File t = new File(k.getParentFile(), "title.keys"); if (f.titleKeys == null && t.isFile()) f.titleKeys = t; break; }
            }
        }
        for (File pk : packages) {
            String n = pk.getName();
            if (n.toLowerCase(Locale.ROOT).endsWith(".nsz") || n.toLowerCase(Locale.ROOT).endsWith(".xcz")) {
                if (!SwitchFiles.titleIds(n).isEmpty()) f.notes.add("◐ " + n + ": compressed (.nsz / .xcz) packages cannot be read; decompress it first");
                continue;
            }
            p.status("Reading " + n);
            List<Long> ids = SwitchFiles.titleIds(n);
            if (ids.isEmpty()) ids = idsInside(pk, tool, f);
            for (long t : ids) {
                if (t == SwitchFiles.APP) { if (!f.bases.contains(pk)) f.bases.add(pk); }
                else if (t == SwitchFiles.UPDATE) { if (!f.updates.contains(pk)) f.updates.add(pk); }
                else if (SwitchFiles.dlcIndex(t) > 0) {
                    int before = f.dlc.size();
                    SwitchFiles.addDlc(SwitchFiles.titleIds(n).isEmpty() ? String.format(Locale.ROOT, "[%016X]", t) : n, f.dlc);
                    if (f.dlc.size() > before) f.dlcPackages++;
                }
            }
        }
        for (File a : archives) {
            p.status("Reading " + a.getName());
            try {
                for (String n : namesOf(a)) {
                    if (SwitchFiles.isPackage(n)) {
                        int before = f.dlc.size();
                        SwitchFiles.addDlc(n, f.dlc);
                        f.dlcPackages += f.dlc.size() - before;
                        for (long t : SwitchFiles.titleIds(n)) if (t == SwitchFiles.APP || t == SwitchFiles.UPDATE)
                            f.notes.add("◐ " + new File(n).getName() + " is inside " + a.getName() + ": unpack it to use it");
                    }
                }
            } catch (IOException e) { f.notes.add("◐ " + a.getName() + ": " + e.getMessage()); }
        }
        f.bases.sort((x, y) -> Long.compare(y.length(), x.length()));     /* the largest first */
        return f;
    }
    private static void walk(File d, int depth, String skip, Found f, List<File> packages, List<File> archives) {
        File[] fs = d.listFiles();
        if (fs == null) return;
        for (File x : fs) {
            String n = x.getName(), l = n.toLowerCase(Locale.ROOT);
            if (n.startsWith(".")) continue;
            if (x.isDirectory()) {
                if (depth > 0 && !n.equals("Android") && !x.getAbsolutePath().equals(skip)) {
                    try { if (x.getCanonicalPath().equals(skip)) continue; } catch (IOException ignored) { }
                    walk(x, depth - 1, skip, f, packages, archives);
                }
                continue;
            }
            if (l.equals("prod.keys") && f.prodKeys == null) f.prodKeys = x;
            else if (l.equals("title.keys") && f.titleKeys == null) f.titleKeys = x;
            else if (SwitchFiles.isPackage(n)) packages.add(x);
            else if (SwitchFiles.isArchive(n) && x.length() < (8L << 30)) archives.add(x);
        }
    }
    static List<String> namesOf(File a) throws IOException {
        if (a.getName().toLowerCase(Locale.ROOT).endsWith(".zip")) {
            List<String> out = new ArrayList<>();
            try (ZipFile z = new ZipFile(a)) {
                Enumeration<? extends ZipEntry> e = z.entries();
                while (e.hasMoreElements()) out.add(e.nextElement().getName());
            }
            return out;
        }
        try (InputStream in = new FileInputStream(a)) { return SwitchFiles.namesInside(a.getName(), in); }
    }
    /** The title ids of a package whose name has none: its ticket's name, else its meta NCA's header (needs the keys). */
    static List<Long> idsInside(File pk, File tool, Found f) {
        List<Long> ids = new ArrayList<>();
        if (pk.getName().toLowerCase(Locale.ROOT).endsWith(".nsp")) {
            try (InputStream in = new FileInputStream(pk)) {
                for (String n : SwitchFiles.pfs0Names(in)) if (n.endsWith(".tik")) ids.addAll(SwitchFiles.titleIds(n));
            } catch (IOException ignored) { }
        }
        if (!ids.isEmpty() || f.prodKeys == null || !tool.canExecute()) return ids;
        try {
            List<String> args = new ArrayList<>();
            args.add("--info"); args.add(pk.getAbsolutePath()); args.add(f.prodKeys.getAbsolutePath());
            if (f.titleKeys != null) args.add(f.titleKeys.getAbsolutePath());
            for (String l : run(tool, args, null)) {
                String[] w = l.split(" ");
                if (w.length >= 5 && w[0].equals("nca") && w[2].equals("1")) ids.addAll(SwitchFiles.titleIds(w[1]));     /* the meta (cnmt) NCA: the package's own title */
            }
        } catch (IOException ignored) { }
        return ids;
    }

    /* ---------------------------------------------------------------- the native tool */
    /** Runs the tool; stdout lines are returned, "progress <done> <total>" lines of stderr go to p. A failure: its last lines. */
    static List<String> run(File tool, List<String> args, Progress p) throws IOException {
        List<String> cmd = new ArrayList<>();
        cmd.add(tool.getAbsolutePath());
        cmd.addAll(args);
        Process pr = new ProcessBuilder(cmd).start();
        List<String> err = new ArrayList<>();
        Thread t = new Thread(() -> {
            try (BufferedReader r = new BufferedReader(new InputStreamReader(pr.getErrorStream(), StandardCharsets.UTF_8))) {
                String l;
                while ((l = r.readLine()) != null) {
                    if (l.startsWith("progress ")) {
                        String[] w = l.split(" ");
                        if (p != null && w.length == 3) try { p.progress(Long.parseLong(w[1]), Long.parseLong(w[2])); } catch (NumberFormatException ignored) { }
                    } else synchronized (err) { err.add(l); }
                }
            } catch (IOException ignored) { }
        });
        t.start();
        List<String> out = new ArrayList<>();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(pr.getInputStream(), StandardCharsets.UTF_8))) {
            String l;
            while ((l = r.readLine()) != null) out.add(l);
        }
        int rc;
        try { rc = pr.waitFor(); t.join(); } catch (InterruptedException e) { pr.destroy(); throw new IOException("interrupted"); }
        if (rc != 0) {
            StringBuilder sb = new StringBuilder();
            synchronized (err) { for (String l : err.subList(Math.max(0, err.size() - 3), err.size())) sb.append(l.replace("romfs_extract: ", "")).append('\n'); }
            for (String l : out.subList(Math.max(0, out.size() - 2), out.size())) sb.append(l).append('\n');
            IOException e = new IOException(sb.toString().trim().isEmpty() ? "the tool failed (" + rc + ")" : sb.toString().trim());
            if (rc == 3) throw new UnknownBuild(e.getMessage());
            throw e;
        }
        return out;
    }
    static final class UnknownBuild extends IOException { UnknownBuild(String m) { super(m); } }

    /* ---------------------------------------------------------------- setting up */
    /** Makes binaries/, state.json, romfs/, config/dlc.txt and work/fonts_open/ in game. Returns the game version. */
    static String setup(Context c, Found f, File game, Progress p) throws IOException {
        File tool = tool(c);
        if (!tool.canExecute()) throw new IOException("the import tool is missing from the app (" + tool + ")");
        if (f.prodKeys == null) throw new IOException("no prod.keys: put your keys file into the folder");
        if (f.bases.isEmpty() || f.updates.isEmpty()) throw new IOException("the base game and its update 13.0.4 / 13.0.5 are both needed");
        game.mkdirs();
        /* 1. the update's ExeFS: which build it is */
        String version = null;
        File update = null, tmp = new File(game, "binaries/.import");
        StringBuilder why = new StringBuilder();
        for (File u : f.updates) {
            p.status("Reading the game's code from " + u.getName());
            deleteTree(tmp);
            List<String> args = keyArgs(f, "--exefs", u.getAbsolutePath(), tmp.getAbsolutePath());
            try {
                for (String l : run(tool, args, p)) if (l.startsWith("game_version ")) version = l.substring(13).trim();
                if (version != null) { update = u; break; }
            } catch (UnknownBuild e) { why.append(u.getName()).append(": ").append(e.getMessage()).append('\n'); }
        }
        if (version == null) { deleteTree(tmp); throw new IOException("no update of a version this port runs (13.0.4 or 13.0.5)\n" + why.toString().trim()); }
        File bin = new File(game, "binaries/" + version);
        deleteTree(bin);
        if (!tmp.renameTo(bin)) throw new IOException("cannot rename " + tmp);
        /* 2. state.json */
        File state = new File(game, "state.json");
        String js = state.isFile() ? LauncherActivity.readText(state, 1 << 20) : "";
        if (js.contains("\"game_version\"")) js = js.replaceFirst("\"game_version\"\\s*:\\s*\"[^\"]*\"", "\"game_version\": \"" + version + "\"");
        else js = "{\n \"game_version\": \"" + version + "\",\n \"made_by\": \"the OpenSmash launcher (on the device)\"\n}\n";
        writeText(state, js);
        /* 3. the RomFS */
        p.status("Unpacking the game data (RomFS, about 15 GB)… this takes a while");
        run(tool, keyArgs(f, f.bases.get(0).getAbsolutePath(), update.getAbsolutePath(), new File(game, "romfs").getAbsolutePath()), p);
        /* 4. add-on content, 5. fonts */
        if (!f.dlc.isEmpty()) addDlc(game, f.dlc);
        installFonts(c, game);
        return version;
    }
    private static List<String> keyArgs(Found f, String... a) {
        List<String> args = new ArrayList<>(java.util.Arrays.asList(a));
        args.add(f.prodKeys.getAbsolutePath());
        if (f.titleKeys != null) args.add(f.titleKeys.getAbsolutePath());
        return args;
    }

    static File dlcFile(File game) { return new File(game, "config/dlc.txt"); }
    /** Adds the add-on contents to config/dlc.txt; returns how many were new. */
    static int addDlc(File game, TreeMap<Integer, String> rows) throws IOException {
        TreeMap<Integer, String> have = SwitchFiles.readDlcList(dlcFile(game));
        int added = 0;
        for (java.util.Map.Entry<Integer, String> e : rows.entrySet()) if (have.put(e.getKey(), e.getValue()) == null) added++;
        SwitchFiles.writeDlcList(dlcFile(game), have);
        return added;
    }

    /** The open-source fonts (runtime/fonts_open, in the APK's assets) into work/fonts_open when they are not there. */
    static void installFonts(Context c, File game) throws IOException {
        AssetManager am = c.getAssets();
        String[] names = am.list("fonts_open");
        if (names == null) return;
        File dir = new File(game, "work/fonts_open");
        dir.mkdirs();
        byte[] buf = new byte[1 << 16];
        for (String n : names) {
            File t = new File(dir, n);
            if (t.isFile() && t.length() > 0) continue;
            try (InputStream in = am.open("fonts_open/" + n); OutputStream out = new FileOutputStream(t)) {
                int k;
                while ((k = in.read(buf)) > 0) out.write(buf, 0, k);
            }
        }
    }

    /** A .zip of game libraries (build_game_android.py --package): every .dll (and fonts_open/) below work/. Returns the count. */
    static int importLibraries(InputStream zip, File game, Progress p, long size) throws IOException {
        File work = new File(game, "work");
        String root = work.getCanonicalPath() + File.separator;
        int n = 0;
        long done = 0;
        byte[] buf = new byte[1 << 20];
        try (java.util.zip.ZipInputStream in = new java.util.zip.ZipInputStream(new java.io.BufferedInputStream(zip, 1 << 20))) {
            ZipEntry e;
            while ((e = in.getNextEntry()) != null) {
                String name = e.getName().replace('\\', '/');
                if (e.isDirectory()) continue;
                int w = name.indexOf("work/");
                String rel = w >= 0 ? name.substring(w + 5) : name;
                boolean lib = rel.toLowerCase(Locale.ROOT).endsWith(".dll"), font = rel.startsWith("fonts_open/");
                if (!lib && !font) continue;
                File t = new File(work, rel);
                if (!t.getCanonicalPath().startsWith(root)) continue;              /* no paths out of the folder */
                t.getParentFile().mkdirs();
                File part = new File(t.getPath() + ".part");
                p.status("Importing " + rel);
                try (OutputStream out = new FileOutputStream(part)) {
                    int k;
                    while ((k = in.read(buf)) > 0) { out.write(buf, 0, k); done += k; p.progress(done, size); }
                }
                if (t.exists() && !t.delete()) throw new IOException("cannot replace " + t);
                if (!part.renameTo(t)) throw new IOException("cannot rename " + part);
                if (lib) n++;
            }
        }
        if (n == 0) throw new IOException("the .zip holds no game libraries (.dll files)");
        return n;
    }

    static void writeText(File f, String s) throws IOException {
        f.getParentFile().mkdirs();
        try (OutputStream o = new FileOutputStream(f)) { o.write(s.getBytes(StandardCharsets.UTF_8)); }
    }
    static void deleteTree(File f) {
        File[] fs = f.listFiles();
        if (fs != null) for (File c : fs) deleteTree(c);
        f.delete();
    }
}
