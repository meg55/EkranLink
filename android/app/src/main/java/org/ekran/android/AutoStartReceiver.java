package org.ekran.android;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/**
 * PC (adb ile) "ekran olur musun?" diye sorar; cevap sonuç kodudur. Uygulamayı PC başlatır: Android 10+
 * arka plandan ekran açılmasına izin vermez, adb'nin izni vardır.
 */
public final class AutoStartReceiver extends BroadcastReceiver {
    static final int ALLOWED = 1;   // PC uygulamayı başlatabilir
    static final int DECLINED = 2;  // "otomatik aç" kapalı ya da kullanıcı bağlantıyı az önce kesti
    static final int OPEN = 3;      // uygulama zaten açık (önde ya da arka planda): PC bağlanabilir

    @Override
    public void onReceive(Context context, Intent intent) {
        if (EkranService.get() != null) {
            setResultCode(OPEN);
        } else if (AutoStart.held(context) || !AutoStart.enabled(context)) {
            setResultCode(DECLINED);
        } else {
            setResultCode(ALLOWED);
        }
    }
}
