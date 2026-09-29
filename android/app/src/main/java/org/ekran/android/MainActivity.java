package org.ekran.android;

import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Point;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/** Tam ekran yüzey + yerel imleç + bilgi katmanı. */
public class MainActivity extends Activity implements SurfaceHolder.Callback, Session.Host {
    private final Stats stats = new Stats();
    private final Handler handler = new Handler();
    private SurfaceView surfaceView;
    private CursorView cursorView;
    private InfoLayer info;
    private StreamServer server;
    private WifiManager.WifiLock wifiLock;
    private volatile int surfaceW, surfaceH;
    private volatile Session.Config config;
    private boolean statsPinned; // kullanıcı üç parmakla ölçüm kartını açtı
    private boolean suppressTouch; // üç parmak hareketi sürerken PC'ye dokunma gönderme
    private DiscoveryResponder discovery;
    private String deviceId;
    private PairStore pairStore;
    private WifiManager.MulticastLock broadcastLock;
    private OrientationWatcher orientationWatcher;
    // Uyku eşitleme: Windows ekranları kapattı. Tablet ekranı kendi zaman aşımıyla kapanır, bağlantı sürer.
    private boolean sleeping;
    private boolean surfaceReady;
    private boolean resumed;
    private android.os.PowerManager.WakeLock sleepLock; // ekran kapalıyken işlemci + ağ ayakta

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            // Katman gizliyken hiçbir şey yapma (ağ arayüzü taraması, metin düzeni, günlük)
            boolean welcome = info.welcomeVisible(), card = info.statsVisible() && !sleeping;
            if (welcome || card) {
                Stats.Snapshot snap = stats.snapshot();
                info.update(snap, welcome ? localAddresses() : null);
                if (card) {
                    Log.i("EkranStats", snap.logLine());
                }
            }
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        // Monitör gibi davran: açılınca ekranı uyandır, kilit ekranının üstünde görün.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                | WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        FrameLayout.LayoutParams full = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT);

        surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent e) {
                onTouchEvent(v, e);
                return true;
            }
        });
        root.addView(surfaceView, full);

        cursorView = new CursorView(this);
        root.addView(cursorView, full);

        String androidId = android.provider.Settings.Secure.getString(getContentResolver(),
                android.provider.Settings.Secure.ANDROID_ID);
        deviceId = androidId != null ? androidId : Build.SERIAL;
        pairStore = new PairStore(this);

        String version = "?";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        info = new InfoLayer(this, Build.MANUFACTURER + " " + Build.MODEL, version, new InfoLayer.Actions() {
            @Override
            public int pairCount() {
                return pairStore.count();
            }

            @Override
            public void forgetPairs() {
                pairStore.forgetAll();
            }
        });
        root.addView(info, full);

        setContentView(root);

        discovery = new DiscoveryResponder(Build.MANUFACTURER + " " + Build.MODEL, deviceId);
        discovery.start();
        orientationWatcher = new OrientationWatcher(this, handler, new OrientationWatcher.Listener() {
            @Override
            public void onOrientation(int degrees) {
                info.setOrientation(degrees);
                sendOrientation(degrees);
            }
        });

        if (getIntent().getBooleanExtra("sifre-olcum", false)) {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    CryptoBench.run();
                }
            }, "ekran-bench").start();
        }
        // 4.4 öncesinde gezinme çubuğu kalıcı gizlenemiyor; sadece karartıyoruz.
        // Böylece yüzey boyutu sabit kalır (S6000'de 1280×752).
        root.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);
    }

    private void onTouchEvent(View v, MotionEvent e) {
        int action = e.getActionMasked();
        StreamServer srv = server;
        Session ses = srv != null ? srv.session() : null;
        Session.Config c = config;
        // Üç parmak: ölçüm kartını aç/kapat. PC'deki yarım kalan dokunmalar iptal edilir ve
        // tüm parmaklar kalkana kadar hiçbir şey gönderilmez.
        if (suppressTouch) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                suppressTouch = false;
            }
            return;
        }
        if (action == MotionEvent.ACTION_POINTER_DOWN && e.getPointerCount() == 3) {
            statsPinned = !statsPinned;
            info.setStatsVisible(statsPinned && ses != null);
            if (statsPinned) {
                handler.removeCallbacks(tick);
                handler.post(tick); // hemen doldur
            }
            suppressTouch = true;
            if (ses != null) {
                ses.sendTouch(Session.TOUCH_CANCEL, 0, 0, 0, 0);
            }
            return;
        }
        if (ses == null || c == null || v.getWidth() == 0 || v.getHeight() == 0) {
            return;
        }
        float sx = c.streamW / (float) v.getWidth(), sy = c.streamH / (float) v.getHeight();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP: {
                int i = e.getActionIndex();
                int type = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
                        ? Session.TOUCH_DOWN : Session.TOUCH_UP;
                send(ses, type, e, i, sx, sy);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    send(ses, Session.TOUCH_MOVE, e, i, sx, sy);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                send(ses, Session.TOUCH_CANCEL, e, 0, sx, sy);
                break;
            default:
                break;
        }
    }

    private static void send(Session ses, int type, MotionEvent e, int i, float sx, float sy) {
        int x = Math.max(0, Math.round(e.getX(i) * sx));
        int y = Math.max(0, Math.round(e.getY(i) * sy));
        int pressure = Math.min(1024, Math.max(0, Math.round(e.getPressure(i) * 1024)));
        ses.sendTouch(type, e.getPointerId(i), x, y, pressure);
    }

    private void sendOrientation(int degrees) {
        StreamServer srv = server;
        Session ses = srv != null ? srv.session() : null;
        if (ses != null) {
            ses.sendOrientation(degrees);
        }
    }

    @Override
    protected void onNewIntent(android.content.Intent intent) {
        super.onNewIntent(intent);
        // Tanı: `am start ... --ei yon 90` tableti çevirmeden dikey monitörü dener
        if (intent.hasExtra("yon")) {
            int deg = intent.getIntExtra("yon", 0);
            info.setOrientation(deg);
            sendOrientation(deg);
        }
        // Tanı: `--ez uyku true|false` Windows'un ekranı kapatıp açmasını taklit eder
        if (intent.hasExtra("uyku")) {
            onDisplayPower(!intent.getBooleanExtra("uyku", false));
        }
    }

    /** Windows ekranları kapattı: kararıp ekranın kapanmasına izin ver; bağlantı ve ağ ayakta kalır. */
    private void enterSleep() {
        if (sleeping) {
            return;
        }
        sleeping = true;
        info.setSleeping(true);
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = 0f; // ekran kapanana kadar en kısık
        getWindow().setAttributes(lp);
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (sleepLock == null) {
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            sleepLock = pm.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "ekran:uyku");
            sleepLock.setReferenceCounted(false);
        }
        sleepLock.acquire();
        Log.i("EkranUyku", "Bilgisayar ekranı kapattı");
    }

    /** Uykudan çık. wake: ekranı da aç (Windows ekranı açtı); false: bağlantı koptu, tablet kendi hâlinde kalsın. */
    private void exitSleep(boolean wake) {
        if (!sleeping) {
            return;
        }
        sleeping = false;
        info.setSleeping(false);
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE;
        getWindow().setAttributes(lp);
        if (wake) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            android.os.PowerManager pm = (android.os.PowerManager) getSystemService(POWER_SERVICE);
            @SuppressWarnings("deprecation")
            android.os.PowerManager.WakeLock w = pm.newWakeLock(android.os.PowerManager.SCREEN_BRIGHT_WAKE_LOCK
                    | android.os.PowerManager.ACQUIRE_CAUSES_WAKEUP | android.os.PowerManager.ON_AFTER_RELEASE,
                    "ekran:uyan");
            w.acquire(3000); // ekranı yakar; sonrasını pencerenin "ekranı açık tut" bayrağı sürdürür
        }
        if (sleepLock != null) {
            sleepLock.release();
        }
        if (!wake && !resumed && wifiLock != null) {
            wifiLock.release(); // uykuda tutulmuştu
        }
        if (!surfaceReady && !wake && server != null) {
            server.shutdown(); // ekran kapalı ve bağlantı yok: eskisi gibi, açılınca yeniden başlar
            server = null;
        }
        Log.i("EkranUyku", wake ? "Bilgisayar ekranı açtı" : "Uyku bitti (bağlantı yok)");
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        if (!sleeping) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        orientationWatcher.setActive(true);
        handler.post(tick);
        // Wi-Fi güç tasarrufu radyoyu aralıklı uyutur (ping ~100 ms); ekran olarak çalışırken kapat.
        if (wifiLock == null) {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wm != null) {
                wifiLock = wm.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "ekran");
                wifiLock.setReferenceCounted(false);
            }
        }
        if (wifiLock != null) {
            wifiLock.acquire();
        }
        // Bazı cihazlar Wi-Fi sürücüsünde yayın paketlerini süzer; bulunabilmek için izin iste.
        if (broadcastLock == null) {
            WifiManager wm = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wm != null) {
                broadcastLock = wm.createMulticastLock("ekran");
                broadcastLock.setReferenceCounted(false);
            }
        }
        if (broadcastLock != null) {
            broadcastLock.acquire();
        }
    }

    @Override
    protected void onDestroy() {
        if (discovery != null) {
            discovery.shutdown();
        }
        super.onDestroy();
    }

    @Override
    protected void onPause() {
        resumed = false;
        orientationWatcher.setActive(false);
        handler.removeCallbacks(tick);
        // Uykuda Wi-Fi bağlantısı sürmeli (Windows ekranı açınca tablet de uyansın)
        if (wifiLock != null && !sleeping) {
            wifiLock.release();
        }
        if (broadcastLock != null) {
            broadcastLock.release();
        }
        super.onPause();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surfaceReady = true;
        if (server != null) {
            server.setSurface(holder.getSurface()); // uykudan dönüş: bağlantı kopmadan görüntü sürer
            return;
        }
        // Tanı modu: `am start ... --ez yuzeysiz true` ile çözücü yüzeye çizmeden çalışır.
        boolean noSurface = getIntent().getBooleanExtra("yuzeysiz", false);
        server = new StreamServer(noSurface ? null : holder.getSurface(), stats, this);
        server.start();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        surfaceW = width;
        surfaceH = height;
        stats.setSurface(width, height);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        if (server == null) {
            return;
        }
        if (sleeping) {
            server.setSurface(null); // Windows ekranı kapalı: bağlantı sürer, çözücü bırakılır
        } else {
            server.shutdown(); // uygulamadan çıkıldı
            server = null;
        }
    }

    // --- Session.Host (ağ iş parçacığından çağrılır) ---

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
        Display d = getWindowManager().getDefaultDisplay();
        d.getMetrics(metrics);
        if (Build.VERSION.SDK_INT >= 17) {
            d.getRealSize(realSize);
        } else {
            realSize.set(metrics.widthPixels, metrics.heightPixels);
        }
    }

    @Override
    public void onConfig(final Session.Config c) {
        config = c;
        handler.post(new Runnable() {
            @Override
            public void run() {
                cursorView.setConfig(c);
            }
        });
    }

    @Override
    public void onCursorShape(final int w, final int h, int hotX, int hotY, final int[] argb) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                cursorView.setShape(w, h, argb);
            }
        });
    }

    @Override
    public void onCursorPos(final int x, final int y, final boolean visible) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                cursorView.setPosition(x, y, visible);
            }
        });
    }

    @Override
    public String deviceId() {
        return deviceId;
    }

    @Override
    public PairStore pairStore() {
        return pairStore;
    }

    @Override
    public Session.PairPrompt showPairing(final String pcName, final String code, final Runnable onReject) {
        final java.util.concurrent.CountDownLatch done = new java.util.concurrent.CountDownLatch(1);
        final boolean[] accepted = {false};
        handler.post(new Runnable() {
            @Override
            public void run() {
                info.showPair(pcName, code, new Runnable() {
                    @Override
                    public void run() {
                        accepted[0] = true;
                        done.countDown();
                    }
                }, new Runnable() {
                    @Override
                    public void run() {
                        done.countDown();
                        onReject.run();
                    }
                });
            }
        });
        return new Session.PairPrompt() {
            @Override
            public boolean await() {
                try {
                    // 2 dakikada cevap yoksa reddet
                    if (!done.await(120, java.util.concurrent.TimeUnit.SECONDS)) {
                        dismiss();
                    }
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                }
                return accepted[0];
            }

            @Override
            public void dismiss() {
                done.countDown();
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        info.hidePair();
                    }
                });
            }
        };
    }

    @Override
    public void onDisplayPower(final boolean on) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                if (on) {
                    exitSleep(true);
                } else {
                    enterSleep();
                }
            }
        });
    }

    @Override
    public void onSessionActive(final boolean active, final boolean encrypted) {
        if (active) {
            stats.setStatus(Stats.CONNECTED, encrypted ? "Bağlı · Wi-Fi, şifreli" : "Bağlı · USB");
        } else {
            stats.setStatus(Stats.WAITING, "Bilgisayar bekleniyor");
        }
        handler.post(new Runnable() {
            @Override
            public void run() {
                if (!active) {
                    config = null;
                    cursorView.clear();
                }
                if (active && orientationWatcher.current() >= 0) {
                    sendOrientation(orientationWatcher.current()); // yeni sanal monitör yatay başlar
                }
                // Bağlıyken yalnızca görüntü; ölçüm kartı üç parmakla açılır
                info.setConnected(active);
                info.setStatsVisible(active && statsPinned);
                if (!active) {
                    info.hidePair();
                    exitSleep(false);
                }
                handler.removeCallbacks(tick);
                handler.post(tick);
            }
        });
    }

    /** Tabletin IPv4 adresleri ("192.168.1.23, 192.168.42.129"); ağ yoksa null. */
    private static String localAddresses() {
        StringBuilder sb = new StringBuilder();
        try {
            for (NetworkInterface ni : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!ni.isUp() || ni.isLoopback()) {
                    continue;
                }
                for (InetAddress a : Collections.list(ni.getInetAddresses())) {
                    if (a instanceof Inet4Address) {
                        if (sb.length() > 0) {
                            sb.append(", ");
                        }
                        sb.append(a.getHostAddress());
                    }
                }
            }
        } catch (Exception ignored) {
        }
        return sb.length() > 0 ? sb.toString() : null;
    }
}
