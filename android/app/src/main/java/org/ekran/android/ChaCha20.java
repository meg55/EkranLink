package org.ekran.android;

/**
 * ChaCha20 akış şifresi (RFC 8439), saf Java.
 *
 * Android 4.x'in AES'i (BouncyCastle, saf Java) S6000'de 1,4 MB/s. Dalvik JIT küçük metotları
 * satır içine almadığı için burada döndürmeler elle yazılmış ve çıktı doğrudan hedefe XOR'lanıyor;
 * yöntem çağrısı yok.
 */
final class ChaCha20 {
    private final int k0, k1, k2, k3, k4, k5, k6, k7;
    private final int[] ks = new int[16]; // tek iş parçacığı kullanır (her yönün kendi örneği var)

    ChaCha20(byte[] key32) {
        k0 = le32(key32, 0);
        k1 = le32(key32, 4);
        k2 = le32(key32, 8);
        k3 = le32(key32, 12);
        k4 = le32(key32, 16);
        k5 = le32(key32, 20);
        k6 = le32(key32, 24);
        k7 = le32(key32, 28);
    }

    /** Nonce = 4 bayt sıfır || 8 bayt sıra no (küçük-endian); blok sayacı 0'dan başlar. */
    void xor(long seq, byte[] b, int off, int len) {
        xor(seq, 0, b, off, len);
    }

    void xor(long seq, int counter, byte[] b, int off, int len) {
        final int n14 = (int) seq, n15 = (int) (seq >>> 32);
        final int[] ks = this.ks;
        while (len > 0) {
            int x0 = 0x61707865, x1 = 0x3320646e, x2 = 0x79622d32, x3 = 0x6b206574;
            int x4 = k0, x5 = k1, x6 = k2, x7 = k3, x8 = k4, x9 = k5, x10 = k6, x11 = k7;
            int x12 = counter, x13 = 0, x14 = n14, x15 = n15;
            int t;
            for (int i = 0; i < 10; i++) {
                x0 += x4; t = x12 ^ x0; x12 = (t << 16) | (t >>> 16); x8 += x12; t = x4 ^ x8; x4 = (t << 12) | (t >>> 20);
                x0 += x4; t = x12 ^ x0; x12 = (t << 8) | (t >>> 24); x8 += x12; t = x4 ^ x8; x4 = (t << 7) | (t >>> 25);
                x1 += x5; t = x13 ^ x1; x13 = (t << 16) | (t >>> 16); x9 += x13; t = x5 ^ x9; x5 = (t << 12) | (t >>> 20);
                x1 += x5; t = x13 ^ x1; x13 = (t << 8) | (t >>> 24); x9 += x13; t = x5 ^ x9; x5 = (t << 7) | (t >>> 25);
                x2 += x6; t = x14 ^ x2; x14 = (t << 16) | (t >>> 16); x10 += x14; t = x6 ^ x10; x6 = (t << 12) | (t >>> 20);
                x2 += x6; t = x14 ^ x2; x14 = (t << 8) | (t >>> 24); x10 += x14; t = x6 ^ x10; x6 = (t << 7) | (t >>> 25);
                x3 += x7; t = x15 ^ x3; x15 = (t << 16) | (t >>> 16); x11 += x15; t = x7 ^ x11; x7 = (t << 12) | (t >>> 20);
                x3 += x7; t = x15 ^ x3; x15 = (t << 8) | (t >>> 24); x11 += x15; t = x7 ^ x11; x7 = (t << 7) | (t >>> 25);

                x0 += x5; t = x15 ^ x0; x15 = (t << 16) | (t >>> 16); x10 += x15; t = x5 ^ x10; x5 = (t << 12) | (t >>> 20);
                x0 += x5; t = x15 ^ x0; x15 = (t << 8) | (t >>> 24); x10 += x15; t = x5 ^ x10; x5 = (t << 7) | (t >>> 25);
                x1 += x6; t = x12 ^ x1; x12 = (t << 16) | (t >>> 16); x11 += x12; t = x6 ^ x11; x6 = (t << 12) | (t >>> 20);
                x1 += x6; t = x12 ^ x1; x12 = (t << 8) | (t >>> 24); x11 += x12; t = x6 ^ x11; x6 = (t << 7) | (t >>> 25);
                x2 += x7; t = x13 ^ x2; x13 = (t << 16) | (t >>> 16); x8 += x13; t = x7 ^ x8; x7 = (t << 12) | (t >>> 20);
                x2 += x7; t = x13 ^ x2; x13 = (t << 8) | (t >>> 24); x8 += x13; t = x7 ^ x8; x7 = (t << 7) | (t >>> 25);
                x3 += x4; t = x14 ^ x3; x14 = (t << 16) | (t >>> 16); x9 += x14; t = x4 ^ x9; x4 = (t << 12) | (t >>> 20);
                x3 += x4; t = x14 ^ x3; x14 = (t << 8) | (t >>> 24); x9 += x14; t = x4 ^ x9; x4 = (t << 7) | (t >>> 25);
            }
            ks[0] = x0 + 0x61707865; ks[1] = x1 + 0x3320646e; ks[2] = x2 + 0x79622d32; ks[3] = x3 + 0x6b206574;
            ks[4] = x4 + k0; ks[5] = x5 + k1; ks[6] = x6 + k2; ks[7] = x7 + k3;
            ks[8] = x8 + k4; ks[9] = x9 + k5; ks[10] = x10 + k6; ks[11] = x11 + k7;
            ks[12] = x12 + counter; ks[13] = x13; ks[14] = x14 + n14; ks[15] = x15 + n15;
            counter++;

            if (len >= 64) {
                for (int w = 0, o = off; w < 16; w++, o += 4) {
                    int v = ks[w];
                    b[o] ^= (byte) v;
                    b[o + 1] ^= (byte) (v >>> 8);
                    b[o + 2] ^= (byte) (v >>> 16);
                    b[o + 3] ^= (byte) (v >>> 24);
                }
                off += 64;
                len -= 64;
            } else {
                for (int i = 0; i < len; i++) {
                    b[off + i] ^= (byte) (ks[i >> 2] >>> ((i & 3) << 3));
                }
                len = 0;
            }
        }
    }

    private static int le32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }
}
