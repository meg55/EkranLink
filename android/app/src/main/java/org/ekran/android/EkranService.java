package org.ekran.android;

import android.app.Notification;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.Point;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

/**
 * Bağlantıyı tutan hizmet: uygulama arka plana alınsa da PC'deki monitör kopmaz. Görüntü yalnızca uygulama
 * öndeyken çözülür (arkadayken PC de görüntü üretmez); öne gelince anahtar kareyle kaldığı yerden sürer.
 *
 * Yaşam süresi: uygulama önde olduğu sürece ya da bir bağlantı sürdükçe. Uygulama arkadayken bağlantı biterse
 * hizmet de kapanır (tablet boşta beklemez). Bağlıyken bildirim gösterilir; oradan bağlantı kesilebilir.
 */
public final class EkranService extends Service implements Session.Host {
    /** Ekrandaki arayüz (MainActivity). Ağ iş parçacığından çağrılır. */
    interface Ui {
        /** Hizmet kapandı (ana iş parçacığı). */
        void onServiceGone();
        void onConfig(Session.Config c);
        void onCursorShape(int w, int h, int[] argb);
        void onCursorPos(int x, int y, boolean visible);
        void onSessionActive(boolean active);
        void onDisplayPower(boolean on);
        Session.PairPrompt showPairing(String pcName, String code, Runnable onReject);
    }

    static final String ACTION_DISCONNECT = "org.ekran.android.BAGLANTIYI_KES";
    private static final int NOTIFICATION_ID = 1;
    private static final String CHANNEL = "baglanti";

    private static volatile EkranService instance;
    /** Tanı modu (`--ez yuzeysiz true`): çözücü yüzeye çizmeden çalışır. Hizmet başlamadan ayarlanır. */
    static volatile boolean headless;

    Stats stats; // onCreate'te kurulur (durum metinleri hizmetin kaynaklarıyla çevrilir)
    private StreamServer server;
    private DiscoveryResponder discovery;
    private PairStore pairStore;
    private String deviceId;
    private WifiManager.WifiLock wifiLock;
    private WifiManager.MulticastLock multicastLock;

    private volatile Ui ui;
    private boolean uiVisible;
    private volatile int surfaceW, surfaceH;
    private volatile boolean active;
    private volatile boolean pcDisplayOn = true;
    // Son durum: arayüz yeniden bağlanınca (ör. uygulama öne gelince) aynen verilir
    private volatile Session.Config config;
    private int cursorW, cursorH, cursorX, cursorY;
    private int[] cursorArgb;
    private boolean cursorVisible;

    static EkranService get() {
        return instance;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        stats = new Stats(getResources());
        instance = this;
        String androidId = android.provider.Settings.Secure.getString(getContentResolver(),
                android.provider.Settings.Secure.ANDROID_ID);
        deviceId = androidId != null ? androidId : Build.SERIAL;
        pairStore = new PairStore(this);
        discovery = new DiscoveryResponder(Build.MANUFACTURER + " " + Build.MODEL, deviceId);
        discovery.start();

        WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
        if (wm != null) {
            // Wi-Fi güç tasarrufu radyoyu aralıklı uyutur (ping ~100 ms); ekran olarak çalışırken kapat.
            wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "ekran");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
            // Bazı cihazlar Wi-Fi sürücüsünde yayın paketlerini süzer; bulunabilmek için izin iste.
            multicastLock = wm.createMulticastLock("ekran");
            multicastLock.setReferenceCounted(false);
            multicastLock.acquire();
        }
        server = new StreamServer(null, headless, stats, this);
        server.start();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && ACTION_DISCONNECT.equals(intent.getAction())) {
            disconnect();
        }
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null; // aynı süreç: arayüz get() ile ulaşır
    }

    @Override
    public void onDestroy() {
        instance = null;
        stopForeground(true);
        server.shutdown();
        discovery.shutdown();
        if (wifiLock != null) {
            wifiLock.release();
        }
        if (multicastLock != null) {
            multicastLock.release();
        }
        Ui u = ui;
        ui = null;
        if (u != null) {
            u.onServiceGone();
        }
        super.onDestroy();
    }

    // --- Arayüzden (ana iş parçacığı) ---

    void attach(Ui u) {
        ui = u;
        Session.Config c = config;
        if (c != null) {
            u.onConfig(c);
        }
        synchronized (this) {
            if (cursorArgb != null) {
                u.onCursorShape(cursorW, cursorH, cursorArgb);
            }
            u.onCursorPos(cursorX, cursorY, cursorVisible);
        }
        u.onSessionActive(active);
    }

    void detach(Ui u) {
        if (ui == u) {
            ui = null;
        }
    }

    /** Uygulama önde mi. Arkadayken bağlantı yoksa hizmet kapanır. */
    void setUiVisible(boolean visible) {
        uiVisible = visible;
        if (!visible && !active) {
            stopSelf();
        }
    }

    void setSurface(Surface s) {
        server.setSurface(s);
    }

    void setSurfaceSize(int w, int h) {
        surfaceW = w;
        surfaceH = h;
        stats.setSurface(w, h);
    }

    Session session() {
        return server.session();
    }

    boolean isActive() {
        return active;
    }

    boolean pcDisplayOn() {
        return pcDisplayOn;
    }

    PairStore pairs() {
        return pairStore;
    }

    /** Kullanıcı bağlantıyı kesti: PC kablo takılı kaldıkça yeniden açmasın. */
    void disconnect() {
        AutoStart.holdOff(this);
        stopSelf();
    }

    // --- Session.Host (ağ iş parçacığı) ---

    @Override
    public int surfaceWidth() {
        return surfaceW;
    }

    @Override
    public int surfaceHeight() {
        return surfaceH;
    }

    @Override
    public void displayInfo(Point realSize, DisplayMetrics metrics) {
        Display d = ((WindowManager) getSystemService(Context.WINDOW_SERVICE)).getDefaultDisplay();
        d.getMetrics(metrics);
        if (Build.VERSION.SDK_INT >= 17) {
            d.getRealSize(realSize);
        } else {
            realSize.set(metrics.widthPixels, metrics.heightPixels);
        }
        if (surfaceW == 0) {
            // Arayüz hiç yüzey vermediyse (arka planda bağlanıldı) ekran boyutunu kullan
            surfaceW = Math.max(realSize.x, realSize.y);
            surfaceH = Math.min(realSize.x, realSize.y);
        }
    }

    @Override
    public String deviceId() {
        return deviceId;
    }

    @Override
    public String appVersion() {
        try {
            return getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (android.content.pm.PackageManager.NameNotFoundException e) {
            return "?";
        }
    }

    @Override
    public PairStore pairStore() {
        return pairStore;
    }

    @Override
    public Session.PairPrompt showPairing(String pcName, String code, Runnable onReject) {
        Ui u = ui;
        if (u == null) {
            return null; // arayüz yok: onaylanamaz (Session reddeder)
        }
        return u.showPairing(pcName, code, onReject);
    }

    @Override
    public void onConfig(Session.Config c) {
        config = c;
        Ui u = ui;
        if (u != null) {
            u.onConfig(c);
        }
    }

    @Override
    public void onCursorShape(int w, int h, int hotX, int hotY, int[] argb) {
        synchronized (this) {
            cursorW = w;
            cursorH = h;
            cursorArgb = argb;
        }
        Ui u = ui;
        if (u != null) {
            u.onCursorShape(w, h, argb);
        }
    }

    @Override
    public void onCursorPos(int x, int y, boolean visible) {
        synchronized (this) {
            cursorX = x;
            cursorY = y;
            cursorVisible = visible;
        }
        Ui u = ui;
        if (u != null) {
            u.onCursorPos(x, y, visible);
        }
    }

    @Override
    public void onDisplayPower(boolean on) {
        pcDisplayOn = on;
        Ui u = ui;
        if (u != null) {
            u.onDisplayPower(on);
        }
    }

    @Override
    public void onSessionActive(final boolean isActive, boolean encrypted) {
        active = isActive;
        if (isActive) {
            stats.setStatus(Stats.CONNECTED,
                    encrypted ? R.string.status_connected_wifi : R.string.status_connected_usb);
        } else {
            stats.setStatus(Stats.WAITING, R.string.status_waiting);
            config = null;
            pcDisplayOn = true;
            synchronized (this) {
                cursorArgb = null;
                cursorVisible = false;
            }
        }
        final String link = encrypted ? "Wi-Fi" : "USB";
        new android.os.Handler(getMainLooper()).post(new Runnable() {
            @Override
            public void run() {
                if (isActive) {
                    // Bağlıyken ön plan hizmeti: Android arka plandayken süreci kapatmasın.
                    // Bağlantı hep uygulama öndeyken başladığı için Android 12+ kısıtlamasına takılmaz.
                    startForeground(NOTIFICATION_ID, buildNotification(link));
                } else {
                    stopForeground(true);
                    if (!uiVisible) {
                        stopSelf(); // arka planda bağlantı bitti: boşta bekleme
                    }
                }
            }
        });
        Ui u = ui;
        if (u != null) {
            u.onSessionActive(isActive);
        }
    }

    @SuppressWarnings("deprecation")
    private Notification buildNotification(String link) {
        NotificationManager nm = (NotificationManager) getSystemService(NOTIFICATION_SERVICE);
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            // Kanal varsa yalnızca adı güncellenir (cihaz dili değiştiyse ayarlarda da yeni dilde görünsün)
            nm.createNotificationChannel(new android.app.NotificationChannel(CHANNEL,
                    getString(R.string.notif_channel), NotificationManager.IMPORTANCE_LOW));
            b = new Notification.Builder(this, CHANNEL);
        } else {
            b = new Notification.Builder(this).setPriority(Notification.PRIORITY_LOW);
        }
        int immutable = Build.VERSION.SDK_INT >= 23 ? PendingIntent.FLAG_IMMUTABLE : 0;
        PendingIntent open = PendingIntent.getActivity(this, 0,
                new Intent(this, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK
                        | Intent.FLAG_ACTIVITY_SINGLE_TOP), immutable);
        PendingIntent cut = PendingIntent.getService(this, 1,
                new Intent(this, EkranService.class).setAction(ACTION_DISCONNECT), immutable);
        return b.setSmallIcon(android.R.drawable.presence_online)
                .setContentTitle(getString(R.string.notif_title, link))
                .setContentText(getString(R.string.notif_text))
                .setContentIntent(open)
                .setOngoing(true)
                .addAction(android.R.drawable.ic_menu_close_clear_cancel, getString(R.string.disconnect), cut)
                .build();
    }
}
