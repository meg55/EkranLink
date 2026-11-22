package org.ekranlink.android;

import android.os.SystemClock;

/**
 * Bu cihazı ağdaki hangi bilgisayarlar görebilir. Hiç eşleşme yoksa herkes (ilk kurulum değişmesin). En az bir
 * bilgisayarla eşleştikten sonra yalnızca eşleştiği bilgisayarlar: aynı Wi-Fi'deki başkalarının listesinde
 * görünmez, onlardan eşleştirme sorusu da gelmez. Yeni bir bilgisayar eklemek için kullanıcı karşılama ekranında
 * "Yeni bilgisayar ekle"ye dokunur: cihaz 5 dakika herkese görünür.
 *
 * USB kablosu (yerel bağlantı) bundan etkilenmez: kabloyu takan, cihazın yanındadır.
 */
final class Visibility {
    static final long OPEN_MS = 5 * 60 * 1000;

    private static volatile long openUntilMs;

    private Visibility() {
    }

    /** "Yeni bilgisayar ekle": 5 dakika herkese görünür. */
    static void openForNewPcs() {
        openUntilMs = SystemClock.elapsedRealtime() + OPEN_MS;
    }

    /** Herkese görünürlüğün kalan süresi (kapalıysa 0). */
    static long remainingMs() {
        return Math.max(0, openUntilMs - SystemClock.elapsedRealtime());
    }

    /** Tanımadığımız bir bilgisayar bu cihazı bulabilir ve eşleştirme isteyebilir mi. */
    static boolean openToNewPcs(int pairCount) {
        return pairCount == 0 || remainingMs() > 0;
    }
}
