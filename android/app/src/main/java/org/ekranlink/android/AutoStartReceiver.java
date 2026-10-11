package org.ekranlink.android;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

/**
 * PC (adb ile) "ekran olur musun?" diye sorar; cevap sonuç kodudur. Uygulamayı PC başlatır: Android 10+
 * arka plandan ekran açılmasına izin vermez, adb'nin izni vardır.
 *
 * Sonuç verisi "kimlik|ad": PC birden çok cihazı ayırt eder, aynı cihazı USB ve Wi-Fi'de iki kez saymaz.
 * "kimlik" ekiyle gelen soru yalnızca bunu öğrenmek içindir (cihaz bulunurken): bekletme süresine dokunmaz.
 */
public final class AutoStartReceiver extends BroadcastReceiver {
    static final int ALLOWED = 1;   // PC uygulamayı başlatabilir
    static final int DECLINED = 2;  // "otomatik aç" kapalı ya da kullanıcı bağlantıyı az önce kesti
    static final int OPEN = 3;      // uygulama zaten açık (önde ya da arka planda): PC bağlanabilir

    @Override
    public void onReceive(Context context, Intent intent) {
        boolean probe = intent.getBooleanExtra("kimlik", false);
        if (EkranService.get() != null) {
            setResultCode(OPEN);
        } else if (!AutoStart.enabled(context) || (probe ? AutoStart.isHeld(context) : AutoStart.held(context))) {
            setResultCode(DECLINED);
        } else {
            setResultCode(ALLOWED);
        }
        setResultData(deviceId(context) + "|" + Build.MANUFACTURER + " " + Build.MODEL);
    }

    /** EkranService ile aynı kimlik (Wi-Fi'de bulunurken ve el sıkışmada gönderilen). */
    static String deviceId(Context context) {
        String id = android.provider.Settings.Secure.getString(context.getContentResolver(),
                android.provider.Settings.Secure.ANDROID_ID);
        return id != null ? id : Build.SERIAL;
    }
}
