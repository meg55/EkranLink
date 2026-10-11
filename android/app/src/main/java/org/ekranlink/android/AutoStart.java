package org.ekranlink.android;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * "Bilgisayara takılınca otomatik aç" ayarı. PC de kendi "otomatik bağlan" ayarına bakar: ikisi de açıksa
 * tablet takılınca kendiliğinden ekran olur.
 *
 * Kullanıcı bağlantıyı kendisi keserse PC (kablo takılı olduğu sürece birkaç saniyede bir sorar) uygulamayı
 * hemen yeniden açmasın: PC'nin soruları 30 sn kesilene (kablo çıkarılana) kadar reddedilir.
 */
final class AutoStart {
    private static final String PREFS = "ekran_ayar";
    private static final String KEY_ENABLED = "otomatik";
    private static final String KEY_HOLD = "bekle";
    private static final String KEY_LAST_ASK = "son_soru";
    private static final long HOLD_GAP_MS = 30_000;

    private AutoStart() {
    }

    private static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    static boolean enabled(Context c) {
        return prefs(c).getBoolean(KEY_ENABLED, true);
    }

    static void setEnabled(Context c, boolean on) {
        prefs(c).edit().putBoolean(KEY_ENABLED, on).apply();
    }

    /** Kullanıcı bağlantıyı kesti. */
    static void holdOff(Context c) {
        prefs(c).edit().putBoolean(KEY_HOLD, true).putLong(KEY_LAST_ASK, System.currentTimeMillis()).apply();
    }

    /** Kullanıcı uygulamayı kendisi açtı: bekletme biter. */
    static void clearHold(Context c) {
        prefs(c).edit().remove(KEY_HOLD).apply();
    }

    /** Bekletme sürüyor mu (değiştirmeden; PC yalnızca cihazı tanımak için sordu). */
    static boolean isHeld(Context c) {
        SharedPreferences p = prefs(c);
        return p.getBoolean(KEY_HOLD, false) && System.currentTimeMillis() - p.getLong(KEY_LAST_ASK, 0) <= HOLD_GAP_MS;
    }

    /** PC sordu: bekletme sürüyor mu? Sorular 30 sn kesildiyse kablo çıkarılmıştır, bekletme biter. */
    static boolean held(Context c) {
        SharedPreferences p = prefs(c);
        if (!p.getBoolean(KEY_HOLD, false)) {
            return false;
        }
        long now = System.currentTimeMillis();
        if (now - p.getLong(KEY_LAST_ASK, 0) > HOLD_GAP_MS) {
            p.edit().remove(KEY_HOLD).apply();
            return false;
        }
        p.edit().putLong(KEY_LAST_ASK, now).apply();
        return true;
    }
}
