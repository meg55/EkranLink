package org.ekranlink.android;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;
import android.view.DisplayCutout;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.util.Collections;

/**
 * Tam ekran yüzey + yerel imleç + bilgi katmanı. Bağlantıyı {@link EkranService} tutar: uygulama arka plana
 * alınınca bağlantı sürer, yalnızca görüntü duraklar; öne gelince kaldığı yerden devam eder.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback, EkranService.Ui {
    private final Handler handler = new Handler();
    private CursorView cursorView;
    private InfoLayer info;
    private EkranService service;
    private Surface surface;          // hizmet hazır olmadan gelen yüzey
    private int surfaceW, surfaceH;
    private volatile Session.Config config;
    private boolean menuOpen;         // kullanıcı üç parmakla menüyü açtı (gezinme tuşları da görünür)
    // Elle döndürme (menüdeki Döndür): -1 cihazın tutuluşuna uy; 0 yatay; 90 dikey. Uygulama açıkken geçerli.
    private int manualDeg = -1;
    private boolean suppressTouch;    // üç parmak hareketi sürerken PC'ye dokunma gönderme
    private OrientationWatcher orientationWatcher;
    // Görüntü hep yataydır. Pencere cihazla birlikte döner; dikey olunca görüntü, imleç ve dokunma saat
    // yönünde turn kadar çevrilir (bkz. onWindowShape).
    private int turn;
    private int lastLandscape = -1;   // pencerenin son yatay ekran dönüşü
    private int windowW, windowH;
    private Object displayListener;   // API 17+: 180° dönüşte boyut değişmez, yalnızca bu haber verir
    private int sentOrientation = -1; // oturuma son bildirilen tutuluş yönü
    private int pcRotation = -1;      // PC masaüstünün görüntüdeki yönü (PC bildirmediyse -1)
    // Uyku eşitleme: Windows ekranları kapattı. Tablet ekranı kendi zaman aşımıyla kapanır, bağlantı sürer.
    private boolean sleeping;
    private boolean started, resumed;
    private boolean connected;
    private boolean closing;          // kullanıcı "Bağlantıyı kes" dedi: hizmet yeniden başlatılmaz
    private android.os.PowerManager.WakeLock sleepLock; // ekran kapalıyken işlemci + ağ ayakta

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            // Katman gizliyken hiçbir şey yapma (ağ arayüzü taraması, metin düzeni, günlük)
            boolean welcome = info.welcomeVisible(), card = info.statsVisible() && !sleeping;
            EkranService s = service;
            if (s != null && (welcome || card)) {
                Stats.Snapshot snap = s.stats.snapshot();
                info.update(snap, welcome ? localAddresses() : null, welcome ? s.unpairedPcNearby() : null);
                if (card) {
                    Log.i("EkranStats", snap.logLine());
                }
            }
            handler.postDelayed(this, 1000);
        }
    };

    // Hizmetin onCreate'i ana iş parçacığında bizden sonra çalışır: hazır olana dek bekle
    private final Runnable attachService = new Runnable() {
        @Override
        public void run() {
            EkranService s = EkranService.get();
            if (s == null) {
                handler.postDelayed(this, 20);
                return;
            }
            service = s;
            pcRotation = s.contentRotation(); // bağlıyken yeniden açılan pencere
            s.setUiVisible(started);
            s.setVideoRotation(turn);
            if (surfaceW > 0) {
                s.setSurfaceSize(surfaceW, surfaceH);
            }
            s.setSurface(surface);
            s.attach(MainActivity.this);
            handler.removeCallbacks(tick);
            handler.post(tick);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        applyWindowOrientation(OrientationWatcher.autoRotate(this));
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

        // Görüntü ve imleç: çentik (kamera deliği) ile ekranın kesik kenarlarından uzak dursun. Android 15+
        // pencereyi bu alanlara da yayar; öncesinde tam ekran pencere onlardan zaten kaçınır (değerler 0).
        FrameLayout screen = new FrameLayout(this);
        if (Build.VERSION.SDK_INT >= 28) {
            screen.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
                @Override
                public WindowInsets onApplyWindowInsets(View v, WindowInsets wi) {
                    DisplayCutout c = wi.getDisplayCutout();
                    if (c != null) {
                        v.setPadding(c.getSafeInsetLeft(), c.getSafeInsetTop(), c.getSafeInsetRight(),
                                c.getSafeInsetBottom());
                    } else {
                        v.setPadding(0, 0, 0, 0);
                    }
                    return wi; // tüketme: bilgi katmanı da aynısını görsün
                }
            });
        }
        root.addView(screen, full);

        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        screen.addView(surfaceView, full);

        cursorView = new CursorView(this);
        cursorView.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent e) {
                onScreenTouch(e);
                return true;
            }
        });
        screen.addView(cursorView, full);

        String version = "?";
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
        } catch (PackageManager.NameNotFoundException ignored) {
        }
        final PairStore pairs = new PairStore(this);
        info = new InfoLayer(this, Build.MANUFACTURER + " " + Build.MODEL, version, new InfoLayer.Actions() {
            @Override
            public int pairCount() {
                return pairs.count();
            }

            @Override
            public void forgetPairs() {
                pairs.forgetAll();
            }

            @Override
            public boolean autoOpen() {
                return AutoStart.enabled(MainActivity.this);
            }

            @Override
            public void setAutoOpen(boolean on) {
                AutoStart.setEnabled(MainActivity.this, on);
            }

            @Override
            public void disconnect() {
                // Kullanıcı bitirdi: PC kablo takılı kaldıkça uygulamayı yeniden açmasın
                closing = true;
                EkranService s = service;
                if (s != null) {
                    s.disconnect();
                }
                finish();
            }

            @Override
            public void rotate() {
                // Şu an yataysa dikeye, dikeyse yataya; cihaz masada düzken ya da döndürme kilitliyken de çalışır
                manualDeg = currentDegrees() <= 0 ? 90 : 0;
                applyWindowOrientation(OrientationWatcher.autoRotate(MainActivity.this));
                applyOrientation();
                refreshRotationControls();
            }

            @Override
            public void followDevice() {
                manualDeg = -1;
                applyWindowOrientation(OrientationWatcher.autoRotate(MainActivity.this));
                applyOrientation();
                refreshRotationControls();
            }
        });
        root.addView(info, full);
        setContentView(root);

        orientationWatcher = new OrientationWatcher(this, handler, new OrientationWatcher.Listener() {
            @Override
            public void onOrientation(int degrees) {
                applyOrientation();
            }

            @Override
            public void onAutoRotate(boolean on) {
                applyWindowOrientation(on);
            }
        });

        // Android 13+: izin yoksa bağlıyken gösterilen bildirim (ve "Bağlantıyı kes") hiç görünmez.
        // Reddedilirse bağlantı yine çalışır; Android en çok iki kez sorar.
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission("android.permission.POST_NOTIFICATIONS") != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{"android.permission.POST_NOTIFICATIONS"}, 1);
        }

        // Kullanıcı uygulamayı kendisi açtı: "bağlantıyı kestim, yeniden açma" bekletmesi biter
        AutoStart.clearHold(this);
        EkranService.headless = getIntent().getBooleanExtra("yuzeysiz", false);

        if (getIntent().getBooleanExtra("sifre-olcum", false)) {
            new Thread(new Runnable() {
                @Override
                public void run() {
                    CryptoBench.run();
                }
            }, "ekran-bench").start();
        }
        if (Build.VERSION.SDK_INT >= 30) {
            getWindow().setDecorFitsSystemWindows(false); // pencere çubukların altına da yayılır, boyutu değişmez
        }
        hideSystemBars();
    }

    /**
     * Otomatik döndürme açıksa pencere cihazla birlikte döner (dikeyde sistem çubukları ve bildirimler de dik).
     * Kapalıysa monitör gibi yatay kalır: telefon yatay bir standdayken dönme kilidi dikeyde olsa da görüntü yan
     * durmasın. Android 6.0 öncesinde dikey pencere çizilemez (KEY_ROTATION yok): hep yatay.
     */
    private void applyWindowOrientation(boolean autoRotate) {
        int want;
        if (Build.VERSION.SDK_INT >= 23 && manualDeg >= 0) {
            // Elle döndürüldü: pencere o yönde kalır (iki yatay / iki dikey arasında yine algılayıcıya uyar)
            want = manualDeg == 0 ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
                    : ActivityInfo.SCREEN_ORIENTATION_SENSOR_PORTRAIT;
        } else {
            want = Build.VERSION.SDK_INT >= 23 && autoRotate ? ActivityInfo.SCREEN_ORIENTATION_USER
                    : ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE;
        }
        if (getRequestedOrientation() != want) {
            setRequestedOrientation(want);
        }
    }

    /**
     * Gezinme ve durum çubuğunu gizle: PC ekranının kenarı (görev çubuğu) çubukların altında kalmasın. Kenardan
     * kaydırınca çubuklar bir süre görüntünün üstünde belirir; yüzey boyutu değişmez. 4.4 öncesinde gezinme
     * çubuğu kalıcı gizlenemiyor, yalnızca karartılır (S6000'de yüzey 1280×752).
     */
    @SuppressWarnings("deprecation")
    private void hideSystemBars() {
        View decor = getWindow().getDecorView();
        if (Build.VERSION.SDK_INT >= 30) {
            hideSystemBars30();
        } else if (Build.VERSION.SDK_INT >= 19) {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        } else {
            decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LOW_PROFILE);
        }
    }

    @android.annotation.TargetApi(30)
    private void hideSystemBars30() {
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            c.hide(WindowInsets.Type.systemBars());
        }
    }

    /**
     * Menü açıkken Android'in gezinme tuşları (Geri, Ana ekran) da görünsün: kullanıcı uygulamadan nasıl çıkacağını
     * görür. Çubuklar görüntünün üstüne biner; pencere ve yüzey boyutu değişmez.
     */
    @SuppressWarnings("deprecation")
    private void showSystemBars() {
        if (Build.VERSION.SDK_INT >= 30) {
            showSystemBars30();
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        }
    }

    @android.annotation.TargetApi(30)
    private void showSystemBars30() {
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.show(WindowInsets.Type.systemBars());
        }
    }

    /** Üç parmak: menüyü aç / kapat (yalnızca bağlıyken). */
    private void setMenuOpen(boolean open) {
        menuOpen = open && session() != null;
        Log.i("EkranMenu", open ? (menuOpen ? "Menü açıldı" : "Menü açılmadı (bağlı değil)") : "Menü kapandı");
        info.setStatsVisible(menuOpen);
        if (Build.VERSION.SDK_INT >= 33) {
            watchBack33(menuOpen); // Android 13+ (hedef 33+): Geri artık onBackPressed'e gelmiyor
        }
        if (menuOpen) {
            info.hideMenuHint();
            // Menüyü bulan kullanıcıya ipucu bir daha gösterilmez
            getSharedPreferences("ekran_ipucu", MODE_PRIVATE).edit().putBoolean("menu_acildi", true).apply();
            refreshRotationControls();
            handler.removeCallbacks(tick);
            handler.post(tick); // hemen doldur
            showSystemBars();
        } else {
            hideSystemBars();
        }
    }

    private Object backCallback; // API 33+: menü açıkken Geri menüyü kapatır

    @android.annotation.TargetApi(33)
    private void watchBack33(boolean on) {
        android.window.OnBackInvokedDispatcher d = getOnBackInvokedDispatcher();
        if (on && backCallback == null) {
            android.window.OnBackInvokedCallback cb = new android.window.OnBackInvokedCallback() {
                @Override
                public void onBackInvoked() {
                    setMenuOpen(false);
                }
            };
            d.registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT, cb);
            backCallback = cb;
        } else if (!on && backCallback != null) {
            d.unregisterOnBackInvokedCallback((android.window.OnBackInvokedCallback) backCallback);
            backCallback = null;
        }
    }

    private void refreshRotationControls() {
        info.setRotationControls(Build.VERSION.SDK_INT >= 23, manualDeg >= 0);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus && !menuOpen) {
            hideSystemBars(); // bildirim paneli, izin penceresi vb. çubukları geri getirmiş olabilir
        }
    }

    /** Hizmeti başlat (zaten çalışıyorsa yalnızca bağlan). */
    private void connectService() {
        startService(new Intent(this, EkranService.class));
        handler.removeCallbacks(attachService);
        handler.post(attachService);
    }

    private Session session() {
        EkranService s = service;
        return s != null ? s.session() : null;
    }

    private void onScreenTouch(MotionEvent e) {
        int action = e.getActionMasked();
        Session ses = session();
        Session.Config c = config;
        // Üç parmak: menüyü aç/kapat. PC'deki yarım kalan dokunmalar iptal edilir ve
        // tüm parmaklar kalkana kadar hiçbir şey gönderilmez.
        if (suppressTouch) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                suppressTouch = false;
            }
            return;
        }
        if (action == MotionEvent.ACTION_POINTER_DOWN && e.getPointerCount() == 3) {
            setMenuOpen(!menuOpen);
            suppressTouch = true;
            if (ses != null) {
                ses.sendTouch(Session.TOUCH_CANCEL, 0, 0, 0, 0);
            }
            return;
        }
        int lw = cursorView.landscapeWidth(), lh = cursorView.landscapeHeight();
        if (ses == null || c == null || lw == 0 || lh == 0) {
            return;
        }
        float sx = c.streamW / (float) lw, sy = c.streamH / (float) lh;
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_UP: {
                int i = e.getActionIndex();
                int type = (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN)
                        ? Session.TOUCH_DOWN : Session.TOUCH_UP;
                send(ses, type, e, i, sx, sy, turn, lw, lh);
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) {
                    send(ses, Session.TOUCH_MOVE, e, i, sx, sy, turn, lw, lh);
                }
                break;
            case MotionEvent.ACTION_CANCEL:
                send(ses, Session.TOUCH_CANCEL, e, 0, sx, sy, turn, lw, lh);
                break;
            default:
                break;
        }
    }

    /** Dokunma (pencere pikseli) → yatay görüntüde nokta (dikey pencerede çevrilmenin tersi) → akış pikseli. */
    private static void send(Session ses, int type, MotionEvent e, int i, float sx, float sy, int turn, int lw, int lh) {
        float vx = e.getX(i), vy = e.getY(i), lx = vx, ly = vy;
        if (turn == 90) {
            lx = vy;
            ly = lh - vx;
        } else if (turn == 270) {
            lx = lw - vy;
            ly = vx;
        }
        int x = Math.max(0, Math.round(lx * sx));
        int y = Math.max(0, Math.round(ly * sy));
        int pressure = Math.min(1024, Math.max(0, Math.round(e.getPressure(i) * 1024)));
        ses.sendTouch(type, e.getPointerId(i), x, y, pressure);
    }

    /**
     * Pencere boyutu ya da ekran dönüşü değişti. Pencere yataysa görüntü olduğu gibi çizilir. Dikeyse (cihaz dik
     * tutuldu) görüntü, pencerenin son yatay dönüşündeymiş gibi çevrilir: kullanıcı yatay kilitli uygulamadakiyle
     * aynı şeyi görür, PC de monitörü aynı şekilde döndürür.
     */
    private void onWindowShape(boolean portrait) {
        if (Build.VERSION.SDK_INT < 23) {
            // Çözücü burada görüntüyü çeviremez; pencere yatay kilitli (dikey yalnızca açılışta bir an)
            portrait = false;
        }
        @SuppressWarnings("deprecation")
        int rot = getWindowManager().getDefaultDisplay().getRotation();
        int base;
        if (!portrait) {
            base = lastLandscape = rot;
        } else {
            // yatay dönüşler dikeyin iki yanındadır; sonuncusu yoksa birini seç
            base = lastLandscape >= 0 && ((lastLandscape - rot) & 1) != 0 ? lastLandscape : (rot + 1) & 3;
        }
        int t = ((base - rot) & 3) * 90;
        orientationWatcher.setTurn(t);
        cursorView.setTurn(t);
        if (t != turn) {
            turn = t;
            Log.i("EkranYon", "Pencere " + (t == 0 ? "yatay" : "dikey; görüntü " + t + "° çevriliyor"));
            EkranService s = service;
            if (s != null) {
                s.setVideoRotation(t);
            }
        }
        applyOrientation();
    }

    /**
     * Tutuluş yönünü bilgi katmanına ve PC'ye uygula. Algılayıcı kapalıysa (otomatik döndürme kapalı, ya da
     * cihaz masada düz) dikey pencerenin kendisi dik tutuluş sayılır.
     */
    private void applyOrientation() {
        int deg = currentDegrees();
        // Bilgi katmanı ekrandaki görüntüyle aynı yönde dursun: PC görüntüyü döndürmediyse (yansıtma, Windows'ta
        // elle sabitlenmiş yön) görüntü yan durur, kart da onunla birlikte. PC bildirmediyse tutuluş yönüne göre.
        int content = pcRotation >= 0 ? pcRotation : Math.max(deg, 0);
        info.setOrientation(((content - turn) % 360 + 360) % 360);
        Session ses = session();
        if (deg >= 0 && deg != sentOrientation && ses != null) {
            sentOrientation = deg;
            ses.sendOrientation(deg);
        }
    }

    /**
     * PC'ye bildirilecek yön (yatay görüntüye göre saat yönünde 0, 90, 270; bilinmiyorsa -1). Elle döndürülmüşse
     * pencerenin kendisi (dikeyde görüntünün çevrildiği açı); yoksa algılayıcı, o da yoksa yine pencere.
     */
    private int currentDegrees() {
        if (manualDeg >= 0) {
            return turn;
        }
        int deg = orientationWatcher.current();
        if (deg < 0 && turn != 0) {
            deg = turn;
        }
        return deg;
    }

    @Override
    public void onConfigurationChanged(android.content.res.Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        // Yeni boyutla yerleşimden önce: katmanlar doğrudan yeni yönde ölçülür (yüzey boyutu sonra gelir)
        if (newConfig.orientation != android.content.res.Configuration.ORIENTATION_UNDEFINED) {
            onWindowShape(newConfig.orientation == android.content.res.Configuration.ORIENTATION_PORTRAIT);
        }
    }

    @Override
    public void onBackPressed() {
        // Menü açıksa Geri menüyü kapatır. Bağlıyken Geri uygulamayı alta alır (bağlantı sürer); kesmek için
        // menüdeki ya da bildirimdeki "Bağlantıyı kes".
        if (menuOpen) {
            setMenuOpen(false);
        } else if (connected) {
            moveTaskToBack(true);
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        AutoStart.clearHold(this);
        // Tanı: `am start ... --ei yon 90` tableti çevirmeden dikey monitörü dener
        if (intent.hasExtra("yon")) {
            int deg = intent.getIntExtra("yon", 0);
            info.setOrientation(deg);
            Session ses = session();
            if (ses != null) {
                sentOrientation = deg;
                ses.sendOrientation(deg);
            }
        }
        // Tanı: `--ez uyku true|false` Windows'un ekranı kapatıp açmasını taklit eder
        if (intent.hasExtra("uyku")) {
            onDisplayPower(!intent.getBooleanExtra("uyku", false));
        }
        // Tanı: `--ez menu true|false` üç parmakla dokunmayı taklit eder (öykünücüde çok parmak yok)
        if (intent.hasExtra("menu")) {
            setMenuOpen(intent.getBooleanExtra("menu", false));
        }
    }

    /** Windows ekranları kapattı: kararıp ekranın kapanmasına izin ver; bağlantı ve ağ ayakta kalır. */
    private void enterSleep() {
        if (sleeping || !resumed) {
            return; // arka plandayken tablette başka bir uygulama kullanılıyor: ekrana dokunma
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
        Log.i("EkranUyku", wake ? "Bilgisayar ekranı açtı" : "Uyku bitti (bağlantı yok)");
    }

    @Override
    protected void onStart() {
        super.onStart();
        started = true;
        EkranService s = service;
        if (s != null) {
            s.setUiVisible(true);
        } else {
            connectService();
        }
    }

    @Override
    protected void onStop() {
        started = false;
        EkranService s = service;
        if (s != null) {
            s.setUiVisible(false); // bağlantı yoksa hizmet kapanır (onServiceGone)
        }
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        EkranService s = service;
        if (s != null && connected && !s.pcDisplayOn()) {
            enterSleep(); // arkadayken Windows ekranı kapatmıştı
        } else if (!sleeping) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        }
        orientationWatcher.setActive(true);
        handler.post(tick);
        if (Build.VERSION.SDK_INT >= 17) {
            watchDisplay(true);
        }
    }

    @android.annotation.TargetApi(17)
    private void watchDisplay(boolean on) {
        android.hardware.display.DisplayManager dm =
                (android.hardware.display.DisplayManager) getSystemService(DISPLAY_SERVICE);
        if (on && displayListener == null) {
            android.hardware.display.DisplayManager.DisplayListener l =
                    new android.hardware.display.DisplayManager.DisplayListener() {
                        @Override
                        public void onDisplayChanged(int displayId) {
                            if (windowW > 0) {
                                onWindowShape(windowH > windowW); // dikey pencere ters çevrildi
                            }
                        }

                        @Override
                        public void onDisplayAdded(int displayId) {
                        }

                        @Override
                        public void onDisplayRemoved(int displayId) {
                        }
                    };
            dm.registerDisplayListener(l, handler);
            displayListener = l;
        } else if (!on && displayListener != null) {
            dm.unregisterDisplayListener((android.hardware.display.DisplayManager.DisplayListener) displayListener);
            displayListener = null;
        }
    }

    @Override
    protected void onPause() {
        resumed = false;
        orientationWatcher.setActive(false);
        handler.removeCallbacks(tick);
        if (Build.VERSION.SDK_INT >= 17) {
            watchDisplay(false);
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        handler.removeCallbacks(attachService);
        handler.removeCallbacks(tick);
        EkranService s = service;
        if (s != null) {
            s.detach(this);
        }
        if (sleepLock != null) {
            sleepLock.release();
        }
        super.onDestroy();
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        surface = holder.getSurface();
        EkranService s = service;
        if (s != null) {
            s.setSurface(surface); // uygulama öne geldi / ekran açıldı: görüntü kaldığı yerden sürer
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        windowW = width;
        windowH = height;
        onWindowShape(height > width);
        // PC'ye hep yatay boyut: dikey pencerede görüntü çevrilerek çizilir
        surfaceW = Math.max(width, height);
        surfaceH = Math.min(width, height);
        EkranService s = service;
        if (s != null) {
            s.setSurfaceSize(surfaceW, surfaceH);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surface = null;
        EkranService s = service;
        if (s != null) {
            s.setSurface(null); // arka plana alındı ya da ekran kapandı: bağlantı sürer, çözücü bırakılır
        }
    }

    // --- EkranService.Ui ---

    @Override
    public void onServiceGone() {
        // Ana iş parçacığı (hizmetin onDestroy'u). Bu arada uygulama öne geldiyse hizmeti yeniden başlat.
        service = null;
        if (started && !closing) {
            connectService();
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
    public void onCursorShape(final int w, final int h, final int[] argb) {
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
    public void onContentRotation(final int degrees) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                pcRotation = degrees;
                applyOrientation();
            }
        });
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
    public void onSessionActive(final boolean active) {
        handler.post(new Runnable() {
            @Override
            public void run() {
                connected = active;
                if (!active) {
                    config = null;
                    pcRotation = -1;
                    cursorView.clear();
                }
                if (active) {
                    sentOrientation = -1;
                    applyOrientation(); // yeni sanal monitör yatay başlar
                    showMenuHintWhileNew();
                }
                // Bağlıyken yalnızca görüntü; menü üç parmakla açılır
                info.setConnected(active);
                if (!active) {
                    if (menuOpen) {
                        setMenuOpen(false);
                    }
                    info.hideMenuHint();
                    info.hidePair();
                    exitSleep(false);
                }
                handler.removeCallbacks(tick);
                handler.post(tick);
            }
        });
    }

    /**
     * "Menü için üç parmakla dokunun": ilk birkaç bağlantıda (menü hiç açılmadıysa) birkaç saniye. Ekranda menü
     * düğmesi yok (PC'nin görüntüsünü örtmesin); karşılama ekranı da aynısını söyler.
     */
    private void showMenuHintWhileNew() {
        android.content.SharedPreferences p = getSharedPreferences("ekran_ipucu", MODE_PRIVATE);
        int shown = p.getInt("menu_ipucu", 0);
        if (shown >= 5 || p.getBoolean("menu_acildi", false)) {
            return;
        }
        p.edit().putInt("menu_ipucu", shown + 1).apply();
        info.showMenuHint();
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
