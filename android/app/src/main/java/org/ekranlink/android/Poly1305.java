package org.ekranlink.android;

/**
 * Poly1305 tek kullanımlık doğrulayıcı (RFC 8439), saf Java, 26 bitlik parçalarla (poly1305-donna).
 * Akışlı kullanım: init(anahtar) → update(...)* → finish(etiket).
 */
final class Poly1305 {
    private static final int M = 0x3ffffff;

    private int r0, r1, r2, r3, r4, s1, s2, s3, s4;
    private int h0, h1, h2, h3, h4;
    private int pad0, pad1, pad2, pad3;
    private final byte[] partial = new byte[16];
    private int partialLen;

    void init(byte[] key, int ko) {
        int t0 = le32(key, ko), t1 = le32(key, ko + 4), t2 = le32(key, ko + 8), t3 = le32(key, ko + 12);
        r0 = t0 & 0x3ffffff;
        r1 = ((t0 >>> 26) | (t1 << 6)) & 0x3ffff03;
        r2 = ((t1 >>> 20) | (t2 << 12)) & 0x3ffc0ff;
        r3 = ((t2 >>> 14) | (t3 << 18)) & 0x3f03fff;
        r4 = (t3 >>> 8) & 0x00fffff;
        s1 = r1 * 5;
        s2 = r2 * 5;
        s3 = r3 * 5;
        s4 = r4 * 5;
        h0 = h1 = h2 = h3 = h4 = 0;
        pad0 = le32(key, ko + 16);
        pad1 = le32(key, ko + 20);
        pad2 = le32(key, ko + 24);
        pad3 = le32(key, ko + 28);
        partialLen = 0;
    }

    void update(byte[] m, int off, int len) {
        if (partialLen > 0) {
            int n = Math.min(16 - partialLen, len);
            System.arraycopy(m, off, partial, partialLen, n);
            partialLen += n;
            off += n;
            len -= n;
            if (partialLen < 16) {
                return;
            }
            block(partial, 0, 1 << 24);
            partialLen = 0;
        }
        while (len >= 16) {
            block(m, off, 1 << 24);
            off += 16;
            len -= 16;
        }
        if (len > 0) {
            System.arraycopy(m, off, partial, 0, len);
            partialLen = len;
        }
    }

    /** RFC 8439 AEAD: ciphertext'i 16'nın katına sıfırla tamamla. */
    void padTo16() {
        if (partialLen > 0) {
            for (int i = partialLen; i < 16; i++) {
                partial[i] = 0;
            }
            block(partial, 0, 1 << 24);
            partialLen = 0;
        }
    }

    void finish(byte[] out, int oo) {
        if (partialLen > 0) {
            partial[partialLen] = 1;
            for (int i = partialLen + 1; i < 16; i++) {
                partial[i] = 0;
            }
            block(partial, 0, 0);
            partialLen = 0;
        }
        int c;
        c = h1 >>> 26; h1 &= M; h2 += c;
        c = h2 >>> 26; h2 &= M; h3 += c;
        c = h3 >>> 26; h3 &= M; h4 += c;
        c = h4 >>> 26; h4 &= M; h0 += c * 5;
        c = h0 >>> 26; h0 &= M; h1 += c;

        int g0 = h0 + 5; c = g0 >>> 26; g0 &= M;
        int g1 = h1 + c; c = g1 >>> 26; g1 &= M;
        int g2 = h2 + c; c = g2 >>> 26; g2 &= M;
        int g3 = h3 + c; c = g3 >>> 26; g3 &= M;
        int g4 = h4 + c - (1 << 26);

        int mask = (g4 >>> 31) - 1; // h >= p ise tüm bitler 1 (g'yi seç)
        g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
        mask = ~mask;
        h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
        h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;

        h0 = h0 | (h1 << 26);
        h1 = (h1 >>> 6) | (h2 << 20);
        h2 = (h2 >>> 12) | (h3 << 14);
        h3 = (h3 >>> 18) | (h4 << 8);

        long f = (h0 & 0xffffffffL) + (pad0 & 0xffffffffL);
        put(out, oo, (int) f);
        f = (h1 & 0xffffffffL) + (pad1 & 0xffffffffL) + (f >>> 32);
        put(out, oo + 4, (int) f);
        f = (h2 & 0xffffffffL) + (pad2 & 0xffffffffL) + (f >>> 32);
        put(out, oo + 8, (int) f);
        f = (h3 & 0xffffffffL) + (pad3 & 0xffffffffL) + (f >>> 32);
        put(out, oo + 12, (int) f);
    }

    /** Tek seferde MAC (ölçüm için). */
    void mac(byte[] key, byte[] m, int off, int len, byte[] out, int oo) {
        init(key, 0);
        update(m, off, len);
        finish(out, oo);
    }

    private void block(byte[] m, int o, int hibit) {
        // Dalvik JIT küçük metotları satır içine almadığı için elle açılmış okuma
        int t0 = (m[o] & 0xFF) | ((m[o + 1] & 0xFF) << 8) | ((m[o + 2] & 0xFF) << 16) | ((m[o + 3] & 0xFF) << 24);
        int t1 = (m[o + 4] & 0xFF) | ((m[o + 5] & 0xFF) << 8) | ((m[o + 6] & 0xFF) << 16) | ((m[o + 7] & 0xFF) << 24);
        int t2 = (m[o + 8] & 0xFF) | ((m[o + 9] & 0xFF) << 8) | ((m[o + 10] & 0xFF) << 16) | ((m[o + 11] & 0xFF) << 24);
        int t3 = (m[o + 12] & 0xFF) | ((m[o + 13] & 0xFF) << 8) | ((m[o + 14] & 0xFF) << 16) | ((m[o + 15] & 0xFF) << 24);
        h0 += t0 & M;
        h1 += ((t0 >>> 26) | (t1 << 6)) & M;
        h2 += ((t1 >>> 20) | (t2 << 12)) & M;
        h3 += ((t2 >>> 14) | (t3 << 18)) & M;
        h4 += (t3 >>> 8) | hibit;

        long d0 = (long) h0 * r0 + (long) h1 * s4 + (long) h2 * s3 + (long) h3 * s2 + (long) h4 * s1;
        long d1 = (long) h0 * r1 + (long) h1 * r0 + (long) h2 * s4 + (long) h3 * s3 + (long) h4 * s2;
        long d2 = (long) h0 * r2 + (long) h1 * r1 + (long) h2 * r0 + (long) h3 * s4 + (long) h4 * s3;
        long d3 = (long) h0 * r3 + (long) h1 * r2 + (long) h2 * r1 + (long) h3 * r0 + (long) h4 * s4;
        long d4 = (long) h0 * r4 + (long) h1 * r3 + (long) h2 * r2 + (long) h3 * r1 + (long) h4 * r0;

        long c = d0 >>> 26; h0 = (int) d0 & M;
        d1 += c; c = d1 >>> 26; h1 = (int) d1 & M;
        d2 += c; c = d2 >>> 26; h2 = (int) d2 & M;
        d3 += c; c = d3 >>> 26; h3 = (int) d3 & M;
        d4 += c; c = d4 >>> 26; h4 = (int) d4 & M;
        h0 += (int) (c * 5);
        int cc = h0 >>> 26;
        h0 &= M;
        h1 += cc;
    }

    private static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    private static void put(byte[] b, int o, int v) {
        b[o] = (byte) v;
        b[o + 1] = (byte) (v >>> 8);
        b[o + 2] = (byte) (v >>> 16);
        b[o + 3] = (byte) (v >>> 24);
    }
}
