package me.hapke.opensmash;

import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The user's Switch files as the launcher finds them on the device (no Android API here, so it runs on a PC too):
 * which title a package is (from its name or the names inside it), the names inside a .rar / .zip / .nsp without
 * unpacking it, and config/dlc.txt, the list of owned add-on contents the runtime reads (runtime/nnmisc.c aoc_load).
 *
 * Smash's add-on contents (DLC fighters, stages, costumes, music) are data of the game update; an add-on content package
 * only unlocks its part. So importing DLC = listing the add-on content ids of the user's packages; their contents are
 * never read (as scripts/install.py step_dlc on the PC).
 */
final class SwitchFiles {
    private SwitchFiles() { }

    static final long APP = 0x01006A800016E000L;               /* Super Smash Bros. Ultimate */
    static final long UPDATE = APP + 0x800;
    /* a title id alone, or the first half of a rights id (a ticket's name: title id + 16 more digits) */
    private static final Pattern TID = Pattern.compile("(?i)(?<![0-9a-f])(0100[0-9a-f]{12})(?:[0-9a-f]{16})?(?![0-9a-f])");

    /** Every title id of this game in a name (base, update, add-on content), in order. */
    static List<Long> titleIds(String name) {
        List<Long> out = new ArrayList<>();
        Matcher m = TID.matcher(name);
        while (m.find()) {
            long t = Long.parseUnsignedLong(m.group(1), 16);
            if ((t & ~0x1FFFL) == APP) out.add(t);
        }
        return out;
    }
    /** The add-on content index of a title id (1..0xfff), or 0. */
    static int dlcIndex(long tid) {
        long i = tid - APP - 0x1000;
        return i > 0 && i < 0x1000 ? (int) i : 0;
    }
    static boolean isPackage(String n) { n = n.toLowerCase(Locale.ROOT); return n.endsWith(".nsp") || n.endsWith(".xci") || n.endsWith(".nsz") || n.endsWith(".xcz"); }
    static boolean isArchive(String n) { n = n.toLowerCase(Locale.ROOT); return n.endsWith(".rar") || n.endsWith(".zip"); }

    /* ---------------------------------------------------------------- names inside archives / packages */
    /** The names of the files in a .zip, .rar (RAR 4 or 5, headers not encrypted) or .nsp (PFS0), or an empty list. */
    static List<String> namesInside(String name, InputStream raw) throws IOException {
        String n = name.toLowerCase(Locale.ROOT);
        InputStream in = new BufferedInputStream(raw, 1 << 16);
        if (n.endsWith(".zip")) return zipNames(in);
        if (n.endsWith(".rar")) return rarNames(in);
        if (n.endsWith(".nsp")) return pfs0Names(in);
        return new ArrayList<>();
    }
    static List<String> zipNames(InputStream in) throws IOException {
        List<String> out = new ArrayList<>();
        try (ZipInputStream z = new ZipInputStream(in)) {
            ZipEntry e;
            while ((e = z.getNextEntry()) != null) if (!e.isDirectory()) out.add(e.getName());
        }
        return out;
    }
    /** The file names of a PFS0 (an .nsp): the ticket's name is the rights id, whose first 16 digits are the title id. */
    static List<String> pfs0Names(InputStream in) throws IOException {
        byte[] h = readN(in, 16);
        List<String> out = new ArrayList<>();
        if (h[0] != 'P' || h[1] != 'F' || h[2] != 'S' || h[3] != '0') return out;
        int count = le32(h, 4), ssize = le32(h, 8);
        if (count < 0 || count > 4096 || ssize < 0 || ssize > (1 << 20)) return out;
        byte[] t = readN(in, count * 24 + ssize);
        for (int i = 0; i < count; i++) {
            int o = count * 24 + le32(t, i * 24 + 16), e = o;
            while (e < t.length && t[e] != 0) e++;
            out.add(new String(t, o, e - o, StandardCharsets.UTF_8));
        }
        return out;
    }
    /** The file names of a RAR archive, read from its block headers (nothing is unpacked). */
    static List<String> rarNames(InputStream in) throws IOException {
        byte[] sig = readN(in, 7);
        if (!(sig[0] == 'R' && sig[1] == 'a' && sig[2] == 'r' && sig[3] == '!' && sig[4] == 0x1a && sig[5] == 7)) throw new IOException("not a RAR archive");
        if (sig[6] == 1) { readN(in, 1); return rar5Names(in); }
        if (sig[6] == 0) return rar4Names(in);
        throw new IOException("unknown RAR version");
    }
    private static List<String> rar5Names(InputStream in) throws IOException {
        List<String> out = new ArrayList<>();
        for (;;) {
            byte[] crc = new byte[4];
            if (!readOrEnd(in, crc)) break;
            long size = vint(in);
            if (size <= 0 || size > (1 << 21)) throw new IOException("bad RAR header");
            byte[] hd = readN(in, (int) size);
            int[] p = {0};
            long type = vint(hd, p), flags = vint(hd, p), data = 0;
            if ((flags & 1) != 0) vint(hd, p);                                  /* extra area size */
            if ((flags & 2) != 0) data = vint(hd, p);
            if (type == 4) throw new IOException("the archive's names are encrypted (a password)");
            if (type == 5) break;                                               /* end of archive */
            if (type == 2) {                                                    /* file */
                long fflags = vint(hd, p);
                vint(hd, p); vint(hd, p);                                       /* unpacked size, attributes */
                if ((fflags & 2) != 0) p[0] += 4;                               /* time */
                if ((fflags & 4) != 0) p[0] += 4;                               /* CRC32 */
                vint(hd, p); vint(hd, p);                                       /* compression, host OS */
                int nl = (int) vint(hd, p);
                if ((fflags & 1) == 0 && p[0] + nl <= hd.length) out.add(new String(hd, p[0], nl, StandardCharsets.UTF_8));
            }
            skip(in, data);
        }
        return out;
    }
    private static List<String> rar4Names(InputStream in) throws IOException {
        List<String> out = new ArrayList<>();
        for (;;) {
            byte[] b = new byte[7];
            if (!readOrEnd(in, b)) break;
            int type = b[2] & 0xff, flags = (b[3] & 0xff) | (b[4] & 0xff) << 8, hsize = (b[5] & 0xff) | (b[6] & 0xff) << 8;
            if (hsize < 7) throw new IOException("bad RAR header");
            byte[] rest = readN(in, hsize - 7);
            long add = 0;
            if (type == 0x74 && rest.length >= 25) {                              /* file */
                add = le32(rest, 0) & 0xffffffffL;
                int nl = (rest[19] & 0xff) | (rest[20] & 0xff) << 8, at = 25;
                if ((flags & 0x100) != 0) { add |= (le32(rest, 25) & 0xffffffffL) << 32; at += 8; }
                if (at + nl <= rest.length) {
                    byte[] nb = java.util.Arrays.copyOfRange(rest, at, at + nl);
                    int z = 0;
                    while (z < nb.length && nb[z] != 0) z++;                      /* a Unicode name follows a 0 byte: the plain one is enough for ids */
                    out.add(new String(nb, 0, z, StandardCharsets.ISO_8859_1));
                }
            } else if ((flags & 0x8000) != 0 && rest.length >= 4) add = le32(rest, 0) & 0xffffffffL;
            if (type == 0x7b) break;                                            /* end of archive */
            if (type == 0x73 && (flags & 0x80) != 0) throw new IOException("the archive's names are encrypted (a password)");
            skip(in, add);
        }
        return out;
    }

    /* ---------------------------------------------------------------- dlc.txt */
    /** The add-on contents a list of names holds: index -> a label from the name. */
    static void addDlc(String name, Map<Integer, String> rows) {
        for (long t : titleIds(name)) {
            int i = dlcIndex(t);
            if (i == 0) continue;
            String base = new File(name.replace('\\', '/')).getName();
            int dot = base.lastIndexOf('.');
            if (dot > 0) base = base.substring(0, dot);
            /* the name's words: bracketed parts that are not an id or a version ("[DLC Bomberman]") kept, the game's name left out */
            String label = base.replaceAll("(?i)\\[(?:[0-9a-f]{16}|v\\d+|UPD|DLC|US|EU|JP)\\]", " ").replaceAll("[\\[\\]]", " ")
                    .replaceAll("(?i)super smash bros\\.? ultimate", " ").replaceAll("\\s+", " ").trim();
            if (label.isEmpty() || label.matches("(?i)[0-9a-f]{16}")) label = String.format(Locale.ROOT, "add-on content %016X", t);
            if (!rows.containsKey(i) || rows.get(i).startsWith("add-on content")) rows.put(i, label);
        }
    }
    /** What dlc.txt lists now (index -> comment). */
    static TreeMap<Integer, String> readDlcList(File f) {
        TreeMap<Integer, String> rows = new TreeMap<>();
        if (!f.isFile()) return rows;
        try {
            for (String l : new String(java.nio.file.Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8).split("\n")) {
                Matcher m = Pattern.compile("^\\s*(0x[0-9a-fA-F]+|\\d+)\\s*(?:#\\s*(.*))?$").matcher(l.trim());
                if (!m.matches()) continue;
                String v = m.group(1);
                int i = v.startsWith("0x") || v.startsWith("0X") ? Integer.parseInt(v.substring(2), 16) : Integer.parseInt(v);
                if (i > 0 && i < 0x1000) rows.put(i, m.group(2) == null ? "" : m.group(2).trim());
            }
        } catch (IOException | NumberFormatException ignored) { }
        return rows;
    }
    static void writeDlcList(File f, Map<Integer, String> rows) throws IOException {
        f.getParentFile().mkdirs();
        File tmp = new File(f.getPath() + ".new");
        try (Writer w = new OutputStreamWriter(new FileOutputStream(tmp), StandardCharsets.UTF_8)) {
            w.write("# add-on contents owned (index = add-on content id - application id - 0x1000); written by the OpenSmash launcher\n");
            for (Map.Entry<Integer, String> e : new TreeMap<>(rows).entrySet())
                w.write(String.format(Locale.ROOT, "0x%03x   # %s\n", e.getKey(), e.getValue()));
        }
        if (!tmp.renameTo(f)) throw new IOException("cannot write " + f);
    }

    /* ---------------------------------------------------------------- helpers */
    static int le32(byte[] b, int o) { return (b[o] & 0xff) | (b[o + 1] & 0xff) << 8 | (b[o + 2] & 0xff) << 16 | (b[o + 3] & 0xff) << 24; }
    static byte[] readN(InputStream in, int n) throws IOException {
        byte[] b = new byte[n];
        if (!readOrEnd(in, b)) throw new EOFException();
        return b;
    }
    private static boolean readOrEnd(InputStream in, byte[] b) throws IOException {
        int got = 0;
        while (got < b.length) {
            int r = in.read(b, got, b.length - got);
            if (r < 0) { if (got == 0) return false; throw new EOFException(); }
            got += r;
        }
        return true;
    }
    private static void skip(InputStream in, long n) throws IOException {
        while (n > 0) {
            long k = in.skip(n);
            if (k <= 0) { if (in.read() < 0) throw new EOFException(); k = 1; }
            n -= k;
        }
    }
    private static long vint(InputStream in) throws IOException {
        long v = 0;
        for (int s = 0; s < 70; s += 7) { int b = in.read(); if (b < 0) throw new EOFException(); v |= (long) (b & 0x7f) << s; if ((b & 0x80) == 0) return v; }
        throw new IOException("bad RAR number");
    }
    private static long vint(byte[] b, int[] p) throws IOException {
        long v = 0;
        for (int s = 0; s < 70 && p[0] < b.length; s += 7) { int x = b[p[0]++] & 0xff; v |= (long) (x & 0x7f) << s; if ((x & 0x80) == 0) return v; }
        throw new IOException("bad RAR number");
    }
    static String readAll(InputStream in) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        byte[] b = new byte[8192];
        int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n);
        return o.toString("UTF-8");
    }

    /** A PC test: java SwitchFiles <file>... prints the names inside and the add-on contents found. */
    public static void main(String[] a) throws IOException {
        TreeMap<Integer, String> rows = new TreeMap<>();
        for (String p : a) {
            addDlc(p, rows);
            try (InputStream in = new FileInputStream(p)) {
                List<String> names = namesInside(p, in);
                System.out.println(p + ": " + names.size() + " names" + (names.isEmpty() ? "" : ", first " + names.get(0)));
                for (String n : names) addDlc(n, rows);
            }
        }
        for (Map.Entry<Integer, String> e : rows.entrySet()) System.out.printf(Locale.ROOT, "0x%03x %s%n", e.getKey(), e.getValue());
        System.out.println(rows.size() + " add-on contents");
    }
}
