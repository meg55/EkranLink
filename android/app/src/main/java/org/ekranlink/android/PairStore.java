package org.ekranlink.android;

import android.content.Context;
import android.content.SharedPreferences;

/** Eşleşmiş PC'lerin kalıcı anahtarları (uygulamanın özel alanında). */
final class PairStore {
    private final SharedPreferences prefs;

    PairStore(Context context) {
        prefs = context.getSharedPreferences("ekran_pairs", Context.MODE_PRIVATE);
    }

    byte[] load(byte[] pcId) {
        String v = prefs.getString(hex(pcId), null);
        return v != null ? unhex(v) : null;
    }

    void save(byte[] pcId, byte[] key, String pcName) {
        prefs.edit().putString(hex(pcId), hex(key)).putString(hex(pcId) + ".ad", pcName).commit();
    }

    void forget(byte[] pcId) {
        prefs.edit().remove(hex(pcId)).remove(hex(pcId) + ".ad").commit();
    }

    /** Eşleşmiş bilgisayar sayısı (her biri için anahtar + ad kaydı). */
    int count() {
        return prefs.getAll().size() / 2;
    }

    void forgetAll() {
        prefs.edit().clear().commit();
    }

    static String hex(byte[] b) {
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (byte x : b) {
            sb.append(Character.forDigit((x >> 4) & 15, 16)).append(Character.forDigit(x & 15, 16));
        }
        return sb.toString();
    }

    static byte[] unhex(String s) {
        byte[] b = new byte[s.length() / 2];
        for (int i = 0; i < b.length; i++) {
            b[i] = (byte) Integer.parseInt(s.substring(i * 2, i * 2 + 2), 16);
        }
        return b;
    }
}
