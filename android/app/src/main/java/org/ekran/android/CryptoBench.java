package org.ekran.android;

import android.util.Log;

import java.security.MessageDigest;

import javax.crypto.Cipher;
import javax.crypto.Mac;
import javax.crypto.spec.IvParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** Tanı: `am start ... --ez sifre-olcum true` — cihazdaki şifreleme hızlarını logcat'e yazar. */
final class CryptoBench {
    private static final String TAG = "EkranBench";

    private CryptoBench() {
    }

    static void run() {
        try {
            byte[] key = new byte[32];
            byte[] frame = new byte[16 * 1024]; // tipik video karesi
            final int frames = 64;               // 1 MB

            Cipher aes = Cipher.getInstance("AES/CTR/NoPadding");
            SecretKeySpec k = new SecretKeySpec(key, 0, 16, "AES");
            long t = System.nanoTime();
            for (int i = 0; i < frames; i++) {
                aes.init(Cipher.ENCRYPT_MODE, k, new IvParameterSpec(new byte[16]));
                aes.doFinal(frame, 0, frame.length, frame, 0);
            }
            log("AES-128-CTR (" + aes.getProvider().getName() + ")", t, frames * frame.length);

            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key, "HmacSHA256"));
            t = System.nanoTime();
            for (int i = 0; i < frames; i++) {
                mac.update(frame, 0, frame.length);
                mac.doFinal();
            }
            log("HMAC-SHA256 (" + mac.getProvider().getName() + ")", t, frames * frame.length);

            MessageDigest md = MessageDigest.getInstance("SHA-256");
            t = System.nanoTime();
            for (int i = 0; i < frames; i++) {
                md.update(frame, 0, frame.length);
                md.digest();
            }
            log("SHA-256 (" + md.getProvider().getName() + ")", t, frames * frame.length);

            ChaCha20 cc = new ChaCha20(key);
            for (int i = 0; i < 8; i++) {
                cc.xor(i, frame, 0, frame.length); // JIT ısınması
            }
            t = System.nanoTime();
            for (int i = 0; i < frames; i++) {
                cc.xor(i, frame, 0, frame.length);
            }
            log("ChaCha20 (saf Java)", t, frames * frame.length);

            Poly1305 p = new Poly1305();
            byte[] tag = new byte[16];
            for (int i = 0; i < 8; i++) {
                p.mac(key, frame, 0, frame.length, tag, 0);
            }
            t = System.nanoTime();
            for (int i = 0; i < frames; i++) {
                p.mac(key, frame, 0, frame.length, tag, 0);
            }
            log("Poly1305 (saf Java)", t, frames * frame.length);

            // Küçük mesaj (kare onayı, 13 bayt): başlatma maliyeti
            byte[] small = new byte[13];
            t = System.nanoTime();
            for (int i = 0; i < 1000; i++) {
                aes.init(Cipher.ENCRYPT_MODE, k, new IvParameterSpec(new byte[16]));
                aes.doFinal(small, 0, small.length, small, 0);
                mac.update(small);
                mac.doFinal();
            }
            Log.i(TAG, String.format("küçük mesaj AES+HMAC: %.1f µs/mesaj", (System.nanoTime() - t) / 1000.0 / 1000));
            t = System.nanoTime();
            for (int i = 0; i < 1000; i++) {
                cc.xor(i, small, 0, small.length);
                p.mac(key, small, 0, small.length, tag, 0);
            }
            Log.i(TAG, String.format("küçük mesaj ChaCha20+Poly1305: %.1f µs/mesaj", (System.nanoTime() - t) / 1000.0 / 1000));
        } catch (Exception e) {
            Log.e(TAG, "ölçüm hatası", e);
        }
    }

    private static void log(String what, long startNs, int bytes) {
        double sec = (System.nanoTime() - startNs) / 1e9;
        Log.i(TAG, String.format("%s: %.1f MB/s", what, bytes / sec / 1e6));
    }
}
