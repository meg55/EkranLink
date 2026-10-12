package org.ekranlink.android;

import java.io.DataInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.SecureRandom;
import java.security.SecureRandomSpi;

/**
 * Doğrudan /dev/urandom okuyan SecureRandom (yalnızca Android 4.1–4.3 için, bkz. Crypto.RANDOM).
 *
 * API 16–18'de varsayılan SecureRandom'un tohumlanması hatalıdır (Google'ın Ağustos 2013 uyarısı):
 * zygote'tan çatallanan süreçler aynı durumla başlayabilir, üretilen sayılar tahmin edilebilir olur.
 * Çekirdeğin havuzu bu hatadan etkilenmez; her istekte oradan okuyoruz.
 */
final class UrandomRandom extends SecureRandom {
    private static final long serialVersionUID = 1L;

    UrandomRandom() {
        super(new Spi(), null);
    }

    private static final class Spi extends SecureRandomSpi {
        private static final long serialVersionUID = 1L;
        private static final File URANDOM = new File("/dev/urandom");
        private static final Object LOCK = new Object();
        // Tüm örnekler aynı akışı paylaşır: her istekte dosya açıp kapatmayalım
        private static DataInputStream in;
        private static OutputStream out;

        @Override
        protected void engineSetSeed(byte[] seed) {
            synchronized (LOCK) {
                try {
                    if (out == null) {
                        out = new FileOutputStream(URANDOM);
                    }
                    out.write(seed);
                    out.flush();
                } catch (IOException ignored) {
                    // Yazılamıyorsa önemli değil: çekirdek havuzu zaten yeterince karışık
                }
            }
        }

        @Override
        protected void engineNextBytes(byte[] bytes) {
            synchronized (LOCK) {
                try {
                    if (in == null) {
                        in = new DataInputStream(new FileInputStream(URANDOM));
                    }
                    in.readFully(bytes);
                } catch (IOException e) {
                    // Zayıf sayıyla devam etmektense dur: el sıkışma başarısız olur
                    throw new SecurityException("/dev/urandom okunamadı", e);
                }
            }
        }

        @Override
        protected byte[] engineGenerateSeed(int numBytes) {
            byte[] b = new byte[numBytes];
            engineNextBytes(b);
            return b;
        }
    }
}
