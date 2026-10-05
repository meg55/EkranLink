package org.ekran.android;

import android.app.Activity;
import android.content.ContentResolver;
import android.database.ContentObserver;
import android.hardware.SensorManager;
import android.os.Handler;
import android.provider.Settings;
import android.view.OrientationEventListener;

/**
 * Tabletin nasıl tutulduğunu izler. Uygulamanın ekranı hep yatay kalır; tablet dikey tutulunca PC sanal
 * monitörü döndürür (gerçek bir monitörü dik çevirmek gibi). Görüntü, çözücü ve dokunma boyutları değişmez.
 *
 * Android'in "otomatik döndür" ayarı kapalıysa algılayıcı açılmaz ve PC'ye yön bildirilmez.
 * Algılayıcı yalnızca uygulama öndeyken (ekran zaten açıkken) ve en yavaş hızda (~5/sn) çalışır.
 */
final class OrientationWatcher extends OrientationEventListener {
    interface Listener {
        /** Yatay ekrana göre saat yönünde dönüş: 0, 90 ya da 270. Ana iş parçacığında çağrılır. */
        void onOrientation(int degrees);
    }

    private static final int SETTLE_MS = 400; // bu kadar süre aynı yönde kalınca bildir
    private static final int MARGIN = 30;     // 90°'nin katına bu kadar yakın değilse karar verme

    private final Activity activity;
    private final Handler handler;
    private final Listener listener;
    private final ContentObserver lockObserver;
    private boolean active;
    private int candidate = -1, reported = -1;
    private int turn;            // pencere dikeyken görüntünün çevrilme açısı, bkz. setTurn
    private int lastDeg = ORIENTATION_UNKNOWN; // son algılayıcı ölçümü (algılayıcı yalnızca değişince haber verebilir)

    private final Runnable settle = new Runnable() {
        @Override
        public void run() {
            if (candidate >= 0 && candidate != reported) {
                reported = candidate;
                android.util.Log.i("EkranYon", "Tutuluş yönü: " + candidate + "°");
                listener.onOrientation(candidate);
            }
        }
    };

    OrientationWatcher(Activity activity, Handler handler, Listener listener) {
        super(activity, SensorManager.SENSOR_DELAY_NORMAL);
        this.activity = activity;
        this.handler = handler;
        this.listener = listener;
        this.lockObserver = new ContentObserver(handler) {
            @Override
            public void onChange(boolean selfChange) {
                update();
            }
        };
    }

    /**
     * Android 16+ büyük ekranlarda yatay kilit yok sayılır; pencere dikey olunca görüntü saat yönünde bu kadar
     * (90/270) çevrilerek çizilir. Yön, pencereye değil görüntüye göre bildirilir.
     */
    void setTurn(int degrees) {
        if (degrees != turn) {
            turn = degrees;
            if (active && lastDeg != ORIENTATION_UNKNOWN) {
                onOrientationChanged(lastDeg); // cihaz kıpırdamasa da yeni açıyla yeniden hesapla
            }
        }
    }

    /** Son bildirilen yön (henüz yoksa -1). */
    int current() {
        return reported;
    }

    /** Uygulama öndeyken true. Her açılışta güncel yön yeniden bildirilir. */
    void setActive(boolean on) {
        if (on == active) {
            return;
        }
        active = on;
        ContentResolver cr = activity.getContentResolver();
        if (on) {
            candidate = reported = -1;
            lastDeg = ORIENTATION_UNKNOWN;
            cr.registerContentObserver(Settings.System.getUriFor(Settings.System.ACCELEROMETER_ROTATION), false,
                    lockObserver);
        } else {
            cr.unregisterContentObserver(lockObserver);
        }
        update();
    }

    private void update() {
        if (active && autoRotate() && canDetectOrientation()) {
            enable();
        } else {
            disable();
            handler.removeCallbacks(settle);
        }
    }

    private boolean autoRotate() {
        return Settings.System.getInt(activity.getContentResolver(), Settings.System.ACCELEROMETER_ROTATION, 1) != 0;
    }

    @Override
    public void onOrientationChanged(int deg) {
        lastDeg = deg;
        if (deg == ORIENTATION_UNKNOWN) {
            return; // masada düz duruyor
        }
        // deg: cihazın doğal yönüne göre saat yönünde dönüş; ekranın o anki dönüşünü (ve dikey pencerede
        // görüntünün çevrilmesini) ekleyince yatay görüntüye göre dönüş çıkar (telefonlarda doğal yön dikeydir).
        int rel = (deg + activity.getWindowManager().getDefaultDisplay().getRotation() * 90 + turn) % 360;
        int q = (rel + 45) / 90 % 4 * 90;
        int dist = Math.abs(rel - q);
        if (Math.min(dist, 360 - dist) > MARGIN) {
            return; // iki yön arasında: son kararı koru
        }
        if (q == 180) {
            q = 0; // ters yatay: Android ekranı kendisi çevirir
        }
        if (q != candidate) {
            candidate = q;
            handler.removeCallbacks(settle);
            handler.postDelayed(settle, SETTLE_MS);
        }
    }
}
