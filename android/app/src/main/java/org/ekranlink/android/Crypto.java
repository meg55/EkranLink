package org.ekranlink.android;

import android.os.Build;
import android.util.Log;

import java.math.BigInteger;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.interfaces.ECPublicKey;
import java.security.spec.ECFieldFp;
import java.security.spec.ECGenParameterSpec;
import java.security.spec.ECParameterSpec;
import java.security.spec.ECPoint;
import java.security.spec.ECPublicKeySpec;

import javax.crypto.KeyAgreement;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/**
 * Android 4.x ile Windows CNG'nin ortak paydası: ECDH P-256, AES-128-CTR, HMAC-SHA256.
 * Hepsi Android 3.0+ standart sağlayıcılarında var; dış kütüphane gerekmez.
 */
final class Crypto {
    private static final String TAG = "EkranCrypto";

    /**
     * Tek örnek: nonce'lar ve ECDH anahtarı buradan. Android 4.1–4.3'te SecureRandom'un tohumlaması hatalı
     * (Ağustos 2013 uyarısı): orada doğrudan /dev/urandom okunur.
     */
    static final SecureRandom RANDOM = Build.VERSION.SDK_INT <= 18 ? new UrandomRandom() : new SecureRandom();

    private Crypto() {
    }

    static byte[] random(int n) {
        byte[] b = new byte[n];
        RANDOM.nextBytes(b);
        return b;
    }

    static byte[] sha256(byte[]... parts) throws GeneralSecurityException {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        for (byte[] p : parts) {
            md.update(p);
        }
        return md.digest();
    }

    static byte[] hmac(byte[] key, byte[]... parts) throws GeneralSecurityException {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key, "HmacSHA256"));
        for (byte[] p : parts) {
            mac.update(p);
        }
        return mac.doFinal();
    }

    static byte[] derive(byte[] prk, String label) throws GeneralSecurityException {
        return hmac(prk, ascii(label));
    }

    static byte[] ascii(String s) {
        byte[] b = new byte[s.length()];
        for (int i = 0; i < b.length; i++) {
            b[i] = (byte) s.charAt(i);
        }
        return b;
    }

    /** Geçici ECDH P-256 anahtarı. */
    static final class Ecdh {
        private final KeyPair pair;
        private final ECParameterSpec params;
        private final String provider; // null: varsayılan sağlayıcı

        Ecdh() throws GeneralSecurityException {
            // Android 4.1–4.3: AndroidOpenSSL'in EC üreticisi (sürümde varsa) verilen SecureRandom'u yok sayıp
            // OpenSSL'in kendi (aynı tohumlama sorunlu) üretecini kullanır. BouncyCastle RANDOM'u kullanır: onu seç.
            KeyPair kp = null;
            String prov = null;
            if (Build.VERSION.SDK_INT <= 18) {
                try {
                    kp = generate(KeyPairGenerator.getInstance("EC", "BC"));
                    prov = "BC";
                } catch (GeneralSecurityException | RuntimeException e) {
                    Log.w(TAG, "BC ile EC anahtarı üretilemedi, varsayılan sağlayıcı kullanılıyor", e);
                }
            }
            if (kp == null) {
                kp = generate(KeyPairGenerator.getInstance("EC"));
            }
            pair = kp;
            params = ((ECPublicKey) pair.getPublic()).getParams();
            provider = prov;
        }

        private static KeyPair generate(KeyPairGenerator g) throws GeneralSecurityException {
            g.initialize(new ECGenParameterSpec("secp256r1"), RANDOM);
            return g.generateKeyPair();
        }

        /** 0x04 || X || Y (65 bayt) */
        byte[] publicKey() {
            ECPoint w = ((ECPublicKey) pair.getPublic()).getW();
            byte[] out = new byte[65];
            out[0] = 4;
            fixed32(w.getAffineX(), out, 1);
            fixed32(w.getAffineY(), out, 33);
            return out;
        }

        byte[] agree(byte[] peer65) throws GeneralSecurityException {
            if (peer65.length != 65 || peer65[0] != 4) {
                throw new GeneralSecurityException("Geçersiz açık anahtar");
            }
            BigInteger x = new BigInteger(1, java.util.Arrays.copyOfRange(peer65, 1, 33));
            BigInteger y = new BigInteger(1, java.util.Arrays.copyOfRange(peer65, 33, 65));
            // Nokta eğri üzerinde mi? (geçersiz eğri saldırısına karşı)
            BigInteger p = ((ECFieldFp) params.getCurve().getField()).getP();
            BigInteger a = params.getCurve().getA(), b = params.getCurve().getB();
            if (x.signum() < 0 || x.compareTo(p) >= 0 || y.signum() < 0 || y.compareTo(p) >= 0
                    || !y.multiply(y).mod(p).equals(x.pow(3).add(a.multiply(x)).add(b).mod(p))) {
                throw new GeneralSecurityException("Anahtar noktası eğri üzerinde değil");
            }
            // Anahtar BC'de üretildiyse anlaşma da BC'de: sağlayıcılar arası anahtar dönüştürmeye güvenmeyelim
            KeyFactory kf = provider != null ? KeyFactory.getInstance("EC", provider) : KeyFactory.getInstance("EC");
            PublicKey peer = kf.generatePublic(new ECPublicKeySpec(new ECPoint(x, y), params));
            KeyAgreement ka = provider != null ? KeyAgreement.getInstance("ECDH", provider) : KeyAgreement.getInstance("ECDH");
            ka.init(pair.getPrivate());
            ka.doPhase(peer, true);
            byte[] z = ka.generateSecret();
            // Eski BouncyCastle sürümleri baştaki sıfır baytları atabilir: 32 bayta tamamla.
            byte[] out = new byte[32];
            if (z.length >= 32) {
                System.arraycopy(z, z.length - 32, out, 0, 32);
            } else {
                System.arraycopy(z, 0, out, 32 - z.length, z.length);
            }
            return out;
        }

        private static void fixed32(BigInteger v, byte[] out, int off) {
            byte[] b = v.toByteArray();
            int n = Math.min(b.length, 32);
            System.arraycopy(b, b.length - n, out, off + 32 - n, n);
        }
    }

    /**
     * Tek yön: ChaCha20-Poly1305 AEAD (RFC 8439, ek veri boş). Nonce = 4 bayt sıfır || sıra no (LE).
     * Sıra no her mesajda artar; iki yönün anahtarları farklı olduğu için nonce hiç tekrarlanmaz.
     *
     * AES-CTR+HMAC yerine seçildi: S6000'de BouncyCastle AES 1,4 MB/s, HMAC 1,7 MB/s iken
     * ChaCha20 15 MB/s, Poly1305 18 MB/s (şifreli Wi-Fi'de tablet CPU'su %104'ten düşsün diye).
     */
    static final class DirectionKey {
        private final ChaCha20 chacha;
        private final Poly1305 poly = new Poly1305();
        private final byte[] otk = new byte[64];
        private final byte[] lens = new byte[16];
        private final byte[] expect = new byte[16];

        DirectionKey(byte[] key32) {
            chacha = new ChaCha20(key32);
        }

        void encrypt(long seq, byte[] b, int off, int len, byte[] tag, int tagOff) {
            oneTimeKey(seq);
            chacha.xor(seq, 1, b, off, len);
            mac(b, off, len, tag, tagOff);
        }

        boolean decrypt(long seq, byte[] b, int off, int len, byte[] tag, int tagOff) {
            oneTimeKey(seq);
            mac(b, off, len, expect, 0);
            int diff = 0;
            for (int i = 0; i < 16; i++) {
                diff |= expect[i] ^ tag[tagOff + i]; // sabit süreli karşılaştırma
            }
            if (diff != 0) {
                return false;
            }
            chacha.xor(seq, 1, b, off, len);
            return true;
        }

        private void oneTimeKey(long seq) {
            java.util.Arrays.fill(otk, (byte) 0);
            chacha.xor(seq, 0, otk, 0, 64); // blok 0'ın ilk 32 baytı Poly1305 anahtarı
        }

        private void mac(byte[] b, int off, int len, byte[] out, int outOff) {
            poly.init(otk, 0);
            poly.update(b, off, len);
            poly.padTo16();
            java.util.Arrays.fill(lens, (byte) 0); // ek veri uzunluğu 0 (LE64) || şifreli metin uzunluğu (LE64)
            lens[8] = (byte) len;
            lens[9] = (byte) (len >>> 8);
            lens[10] = (byte) (len >>> 16);
            lens[11] = (byte) (len >>> 24);
            poly.update(lens, 0, 16);
            poly.finish(out, outOff);
        }
    }
}