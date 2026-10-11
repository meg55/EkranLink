package org.ekranlink.android;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.os.Build;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Tabletteki bilgi katmanı: bağlantı beklerken karşılama ekranı, bağlıyken üç parmakla açılan ölçüm kartı ve
 * Wi-Fi eşleştirme onayı.
 *
 * Uygulamanın ekranı hep yatay kalır (görüntü akışı bozulmasın diye); tablet dik tutulunca bu katman kendini
 * çevirir. Android'in iletişim kutuları ve bildirimleri çevrilemediğinden hepsi burada çizilir.
 *
 * Telefonda (kısa kenar 520 dp'den az) boşluklar ve yazılar küçülür; yine de sığmayan ekranlar kaydırılır.
 * Sistem çubukları ve çentik (Android 15+ pencereyi kenardan kenara açar) yazıların ve kartların üstüne binmez.
 */
final class InfoLayer extends ViewGroup {
    interface Actions {
        int pairCount();

        void forgetPairs();

        boolean autoOpen();

        void setAutoOpen(boolean on);

        /** Bağlantıyı kes ve uygulamayı kapat. */
        void disconnect();
    }

    private static final int BG = 0xFF0B0E13;
    private static final int CARD = 0xE6161B22;
    private static final int CARD_SOLID = 0xFF161B22;
    private static final int TEXT = 0xFFF2F4F7;
    private static final int MUTED = 0xFF8B95A3;
    private static final int LINE = 0x1FFFFFFF;
    private static final int ACCENT = 0xFF3D8BFD;
    private static final int GREEN = 0xFF34C759;
    private static final int AMBER = 0xFFFFB020;
    private static final int RED = 0xFFFF5A52;

    private static final int[] STAT_LABELS = {R.string.stat_video, R.string.stat_fps, R.string.stat_data,
            R.string.stat_decode, R.string.stat_decoder, R.string.stat_dropped};

    private final Actions actions;
    private final Resources res;
    private final float density;
    private final boolean compact; // telefon: daha sık boşluklar, biraz küçük yazılar
    private final FrameLayout content;
    private int rotation;
    private final Rect insets = new Rect(); // sistem çubukları + çentik (ekranın kendi yönünde)

    private final View welcome;
    private final View welcomeDot;
    private final TextView welcomeStatus, wifiText, footer, forget;
    private boolean forgetArmed;
    private TextView autoToggle;

    private final View statsCard;
    private final View statsDot;
    private final TextView statsTitle;
    private final TextView[] statValues = new TextView[STAT_LABELS.length];

    private final View sleepCover;
    private final View pairView;
    private final TextView pairMessage, pairCode;
    private Runnable pairAccept, pairReject;

    InfoLayer(Context ctx, String deviceName, String version, Actions actions) {
        super(ctx);
        this.actions = actions;
        res = ctx.getResources();
        DisplayMetrics dm = res.getDisplayMetrics();
        density = dm.density;
        // Kısa kenar: yatay telefonda yükseklik, dik tutulunca (katman döner) genişlik. 7" tablet ~552-600 dp.
        compact = Math.min(dm.widthPixels, dm.heightPixels) / density < 520;
        content = new FrameLayout(ctx);
        addView(content);

        // --- Karşılama (bağlı değilken tam ekran) ---
        FrameLayout welcomeRoot = new FrameLayout(ctx);
        welcomeRoot.setBackgroundColor(BG);
        LinearLayout col = column(ctx, 560);
        col.setPadding(size(32, 20), size(24, 12), size(32, 20), size(24, 12));

        col.addView(text(ctx, "EkranLink", font(40, 30), TEXT, true));
        TextView tagline = text(ctx, res.getString(R.string.welcome_tagline), font(15, 14), MUTED, false);
        col.addView(tagline, margins(0, dp(2), 0, size(28, 12)));

        LinearLayout statusRow = row(ctx);
        welcomeDot = dot(ctx, AMBER);
        statusRow.addView(welcomeDot, dotParams(12, 12));
        welcomeStatus = text(ctx, "", font(20, 17), TEXT, false);
        statusRow.addView(welcomeStatus);
        col.addView(statusRow, margins(0, 0, 0, size(24, 12)));

        col.addView(divider(ctx), new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 1));
        col.addView(step(ctx, "USB", text(ctx, res.getString(R.string.welcome_usb_step), font(15, 14), TEXT, false)),
                wide(0, size(20, 12), 0, 0));
        wifiText = text(ctx, "", font(15, 14), TEXT, false);
        col.addView(step(ctx, "Wi-Fi", wifiText), wide(0, size(16, 10), 0, 0));

        // Bilgisayara takılınca otomatik aç (PC'nin "Otomatik bağlan" ayarıyla birlikte)
        LinearLayout autoRow = row(ctx);
        LinearLayout autoText = new LinearLayout(ctx);
        autoText.setOrientation(LinearLayout.VERTICAL);
        autoText.addView(text(ctx, res.getString(R.string.auto_open_title), font(15, 14), TEXT, false));
        autoText.addView(text(ctx, res.getString(R.string.auto_open_hint), font(13, 12), MUTED, false));
        autoRow.addView(autoText, new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        autoToggle = text(ctx, "", font(14, 13), TEXT, true);
        autoToggle.setGravity(Gravity.CENTER);
        autoToggle.setMinWidth(size(84, 72));
        autoToggle.setPadding(size(14, 12), size(8, 6), size(14, 12), size(8, 6));
        autoToggle.setClickable(true);
        autoToggle.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                InfoLayer.this.actions.setAutoOpen(!InfoLayer.this.actions.autoOpen());
                refreshAutoToggle();
            }
        });
        autoRow.addView(autoToggle, margins(size(16, 12), 0, 0, 0));
        col.addView(autoRow, wide(0, size(18, 12), 0, size(20, 12)));
        refreshAutoToggle();

        col.addView(divider(ctx), new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 1));

        LinearLayout footerRow = row(ctx);
        footer = text(ctx, res.getString(R.string.footer_version, deviceName, version), font(13, 12), MUTED, false);
        footerRow.addView(footer, new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        forget = text(ctx, "", font(13, 12), ACCENT, false);
        forget.setPadding(dp(12), size(10, 6), 0, size(10, 6));
        forget.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                onForgetClicked();
            }
        });
        footerRow.addView(forget);
        col.addView(footerRow, wide(0, size(6, 2), 0, 0));
        disarmForget();

        // Sığarsa ortada durur; sığmazsa (ör. yatay telefon) kaydırılır
        welcomeRoot.addView(scroller(ctx, col, true), full());
        welcome = welcomeRoot;
        content.addView(welcome, full());

        // --- Ölçüm kartı (bağlıyken, üç parmakla) ---
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(round(CARD, 14));
        card.setPadding(size(16, 12), size(14, 10), size(18, 14), size(14, 10));
        LinearLayout titleRow = row(ctx);
        statsDot = dot(ctx, GREEN);
        titleRow.addView(statsDot, dotParams(10, 10));
        statsTitle = text(ctx, "", font(16, 15), TEXT, true);
        titleRow.addView(statsTitle);
        card.addView(titleRow, margins(0, 0, 0, size(10, 6)));
        // Etiket sütunu en uzun etikete göre (uzunluk dile göre değişir); tablette en az eskisi gibi 92 dp
        TextView[] labels = new TextView[STAT_LABELS.length];
        float labelW = 0;
        for (int i = 0; i < STAT_LABELS.length; i++) {
            labels[i] = text(ctx, res.getString(STAT_LABELS[i]), 13, MUTED, false);
            labelW = Math.max(labelW, labels[i].getPaint().measureText(labels[i].getText().toString()));
        }
        int labelCol = Math.max(size(92, 76), (int) Math.ceil(labelW) + dp(12));
        for (int i = 0; i < STAT_LABELS.length; i++) {
            LinearLayout r = row(ctx);
            r.addView(labels[i], new LinearLayout.LayoutParams(labelCol, LayoutParams.WRAP_CONTENT));
            statValues[i] = text(ctx, "–", 13, TEXT, false);
            r.addView(statValues[i]);
            card.addView(r, margins(0, size(3, 2), 0, size(3, 2)));
        }
        card.addView(text(ctx, res.getString(R.string.stats_hide_hint), 12, MUTED, false),
                margins(0, size(10, 6), 0, 0));
        // Kart dokunmaları geçirir (arkadaki masaüstüne); yalnızca bu düğme tutar
        TextView cut = button(ctx, res.getString(R.string.disconnect), 0x00000000, 0x33FF5A52, RED);
        cut.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        cut.setPadding(size(14, 12), size(8, 6), size(14, 12), size(8, 6));
        cut.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                InfoLayer.this.actions.disconnect();
            }
        });
        card.addView(cut, margins(0, size(12, 8), 0, 0));
        statsCard = card;
        // Kenar boşlukları applyInsets'te verilir (çentik ve sistem çubukları eklenerek)
        content.addView(statsCard, new FrameLayout.LayoutParams(LayoutParams.WRAP_CONTENT,
                LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START));
        statsCard.setVisibility(GONE);

        // --- Uyku örtüsü: Windows ekranı kapattı; tablet ekranı kendi zaman aşımıyla kapanana kadar kararık ---
        // Dokunmalar geçer: dokunmak (gerçek dokunmatik monitörde olduğu gibi) Windows'u uyandırır.
        FrameLayout cover = new FrameLayout(ctx);
        cover.setBackgroundColor(0xFF000000);
        TextView coverText = text(ctx, res.getString(R.string.sleep_cover), 15, 0xFF30363D, false);
        coverText.setGravity(Gravity.CENTER);
        coverText.setPadding(dp(16), 0, dp(16), 0);
        cover.addView(coverText,
                new FrameLayout.LayoutParams(LayoutParams.WRAP_CONTENT, LayoutParams.WRAP_CONTENT, Gravity.CENTER));
        sleepCover = cover;
        content.addView(sleepCover, full());
        sleepCover.setVisibility(GONE);

        // --- Eşleştirme onayı (Wi-Fi'de ilk bağlantı) ---
        FrameLayout pairRoot = new FrameLayout(ctx);
        pairRoot.setBackgroundColor(0xCC000000);
        pairRoot.setClickable(true); // arkadaki görüntüye dokunma geçmesin
        LinearLayout pc = column(ctx, 520);
        pc.setBackground(round(CARD_SOLID, 18));
        pc.setPadding(size(28, 20), size(24, 16), size(28, 20), size(20, 14));
        pc.addView(text(ctx, res.getString(R.string.pair_title), font(22, 18), TEXT, true));
        pairMessage = text(ctx, "", font(15, 14), MUTED, false);
        pc.addView(pairMessage, margins(0, size(6, 4), 0, size(18, 10)));
        pairCode = text(ctx, "", font(52, 40), TEXT, true);
        pairCode.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        pairCode.setGravity(Gravity.CENTER);
        pairCode.setBackground(round(0x14FFFFFF, 12));
        pairCode.setPadding(0, size(10, 6), 0, size(10, 6));
        pc.addView(pairCode, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        pc.addView(text(ctx, res.getString(R.string.pair_hint), font(14, 13), MUTED, false),
                margins(0, size(14, 10), 0, size(20, 12)));
        LinearLayout buttons = row(ctx);
        buttons.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        TextView reject = button(ctx, res.getString(R.string.pair_reject), 0x00000000, 0x22FFFFFF, TEXT);
        reject.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finishPair(false);
            }
        });
        TextView accept = button(ctx, res.getString(R.string.pair_accept), ACCENT, 0xFF2F6FD0, 0xFFFFFFFF);
        accept.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finishPair(true);
            }
        });
        buttons.addView(reject, margins(0, 0, dp(12), 0));
        buttons.addView(accept);
        pc.addView(buttons, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        // Kart ekran kenarına yapışmasın (dik tutulan telefonda genişlik ~360 dp); çok kısa ekranda kaydırılır
        pairRoot.addView(scroller(ctx, pc, false), full());
        pairView = pairRoot;
        content.addView(pairView, full());
        pairView.setVisibility(GONE);
        applyInsets();

        // Android 15+ (targetSdk 35) pencere kenardan kenaradır: gezinme çubuğu ve çentik katmanın altına
        // girebilir. Öncesinde tam ekran pencere bu alanlardan zaten kaçınır; değerler 0 gelir.
        if (Build.VERSION.SDK_INT >= 20) {
            setOnApplyWindowInsetsListener(new OnApplyWindowInsetsListener() {
                @Override
                @SuppressWarnings("deprecation")
                public WindowInsets onApplyWindowInsets(View v, WindowInsets wi) {
                    int l = wi.getSystemWindowInsetLeft(), t = wi.getSystemWindowInsetTop();
                    int r = wi.getSystemWindowInsetRight(), b = wi.getSystemWindowInsetBottom();
                    if (Build.VERSION.SDK_INT >= 28 && wi.getDisplayCutout() != null) {
                        android.view.DisplayCutout c = wi.getDisplayCutout();
                        l = Math.max(l, c.getSafeInsetLeft());
                        t = Math.max(t, c.getSafeInsetTop());
                        r = Math.max(r, c.getSafeInsetRight());
                        b = Math.max(b, c.getSafeInsetBottom());
                    }
                    if (insets.left != l || insets.top != t || insets.right != r || insets.bottom != b) {
                        insets.set(l, t, r, b);
                        applyInsets();
                    }
                    return wi; // tüketme: alttaki görünümler de aynısını görsün
                }
            });
        }
    }

    // --- Dışarıdan ---

    /** İçeriğin dik okunması için pencereye göre saat yönünün tersine çevrilme açısı: 0, 90, 180 ya da 270. */
    void setOrientation(int degrees) {
        if (degrees == rotation) {
            return;
        }
        rotation = degrees;
        applyInsets(); // kenar boşlukları içeriğin yeni yönüne göre yer değiştirir
        content.setAlpha(0f);
        requestLayout();
        content.animate().alpha(1f).setDuration(180).start();
    }

    void setConnected(boolean connected) {
        welcome.setVisibility(connected ? GONE : VISIBLE);
        if (!connected) {
            disarmForget();
        }
    }

    void setSleeping(boolean sleeping) {
        sleepCover.setVisibility(sleeping ? VISIBLE : GONE);
    }

    void setStatsVisible(boolean visible) {
        statsCard.setVisibility(visible ? VISIBLE : GONE);
    }

    boolean statsVisible() {
        return statsCard.getVisibility() == VISIBLE;
    }

    boolean welcomeVisible() {
        return welcome.getVisibility() == VISIBLE;
    }

    /** addresses: tabletin IPv4 adresleri (yoksa null). */
    void update(Stats.Snapshot s, String addresses) {
        int color = s.state == Stats.CONNECTED ? GREEN : s.state == Stats.ERROR ? RED
                : s.state == Stats.CONNECTING ? ACCENT : AMBER;
        if (welcomeVisible()) {
            setDotColor(welcomeDot, color);
            welcomeStatus.setText(s.status);
            wifiText.setText(addresses != null
                    ? res.getString(R.string.welcome_wifi_found, addresses)
                    : res.getString(R.string.welcome_wifi_none));
            if (!forgetArmed) {
                forget.setVisibility(actions.pairCount() > 0 ? VISIBLE : GONE);
            }
        }
        if (statsVisible()) {
            setDotColor(statsDot, color);
            statsTitle.setText(s.status);
            statValues[0].setText(s.stream);
            // Sayılar cihaz dilinin biçimiyle (Türkçede ondalık virgül)
            statValues[1].setText(res.getString(R.string.stat_fps_value, s.fps, s.inFps));
            statValues[2].setText(res.getString(R.string.stat_data_value, s.mbps));
            statValues[3].setText(s.fps > 0
                    ? res.getString(R.string.stat_decode_value, s.latAvgMs, s.latMinMs, s.latMaxMs)
                    : "–");
            statValues[4].setText(s.decoder);
            statValues[5].setText(s.droppedFrames + " / " + s.totalFrames);
        }
    }

    /** Eşleştirme sorusunu gösterir; kullanıcı seçince ilgili iş ana iş parçacığında çalışır. */
    void showPair(String pcName, String code, Runnable onAccept, Runnable onReject) {
        pairMessage.setText(res.getString(R.string.pair_message, pcName));
        pairCode.setText(code);
        pairAccept = onAccept;
        pairReject = onReject;
        pairView.setVisibility(VISIBLE);
    }

    void hidePair() {
        pairAccept = pairReject = null;
        pairView.setVisibility(GONE);
    }

    private void finishPair(boolean accepted) {
        Runnable r = accepted ? pairAccept : pairReject;
        hidePair();
        if (r != null) {
            r.run();
        }
    }

    private void refreshAutoToggle() {
        boolean on = actions.autoOpen();
        autoToggle.setText(on ? R.string.toggle_on : R.string.toggle_off);
        autoToggle.setTextColor(on ? 0xFFFFFFFF : MUTED);
        autoToggle.setBackground(round(on ? ACCENT : 0x22FFFFFF, 16));
    }

    // --- Eşleşmeleri unut: yanlışlıkla dokunmaya karşı iki adımlı ---

    private final Runnable disarm = new Runnable() {
        @Override
        public void run() {
            disarmForget();
        }
    };

    private void onForgetClicked() {
        removeCallbacks(disarm);
        if (!forgetArmed) {
            forgetArmed = true;
            forget.setText(R.string.forget_confirm);
            forget.setTextColor(RED);
            postDelayed(disarm, 4000);
        } else {
            actions.forgetPairs();
            forget.setText(R.string.forget_done);
            forget.setTextColor(MUTED);
            forget.setClickable(false);
            postDelayed(disarm, 2500);
        }
    }

    private void disarmForget() {
        removeCallbacks(disarm);
        forgetArmed = false;
        forget.setText(R.string.forget_pairs);
        forget.setTextColor(ACCENT);
        forget.setClickable(true);
        forget.setVisibility(actions.pairCount() > 0 ? VISIBLE : GONE);
    }

    // --- Sistem çubukları ve çentik ---

    /**
     * Sistem çubuklarının ve çentiğin kapladığı ekran kenarlarını içeriğin (döndürülmüş) çerçevesine çevirip
     * boşluk olarak verir. Arka planlar tam ekran kalır; yalnızca yazılar ve kartlar içeri çekilir.
     */
    private void applyInsets() {
        int l, t, r, b;
        if (rotation == 90) {
            // İçerik saat yönünün tersine 90° döner: üst kenarı ekranın soluna, sol kenarı ekranın altına düşer
            t = insets.left;
            l = insets.bottom;
            b = insets.right;
            r = insets.top;
        } else if (rotation == 270) {
            // İçerik saat yönünde 90° döner: üst kenarı ekranın sağına, sol kenarı ekranın üstüne düşer
            t = insets.right;
            l = insets.top;
            b = insets.left;
            r = insets.bottom;
        } else if (rotation == 180) {
            t = insets.bottom;
            l = insets.right;
            b = insets.top;
            r = insets.left;
        } else {
            l = insets.left;
            t = insets.top;
            r = insets.right;
            b = insets.bottom;
        }
        welcome.setPadding(l, t, r, b);
        sleepCover.setPadding(l, t, r, b);
        int p = size(24, 12);
        pairView.setPadding(p + l, p + t, p + r, p + b);
        int m = size(16, 8);
        FrameLayout.LayoutParams lp = (FrameLayout.LayoutParams) statsCard.getLayoutParams();
        lp.setMargins(m + l, m + t, m + r, m + b);
        statsCard.setLayoutParams(lp);
    }

    // --- Döndürme: içerik dik tutuluşta en/boy yer değiştirmiş ölçülür, ortalanır ve çevrilir ---

    @Override
    protected void onMeasure(int widthSpec, int heightSpec) {
        int w = MeasureSpec.getSize(widthSpec), h = MeasureSpec.getSize(heightSpec);
        boolean sideways = rotation == 90 || rotation == 270;
        content.measure(MeasureSpec.makeMeasureSpec(sideways ? h : w, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(sideways ? w : h, MeasureSpec.EXACTLY));
        setMeasuredDimension(w, h);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int cw = content.getMeasuredWidth(), ch = content.getMeasuredHeight();
        int x = (r - l - cw) / 2, y = (b - t - ch) / 2;
        content.layout(x, y, x + cw, y + ch);
        content.setPivotX(cw / 2f);
        content.setPivotY(ch / 2f);
        // Tablet saat yönünde döndüyse içerik ters yönde çevrilir ki kullanıcıya dik görünsün
        content.setRotation(-rotation);
    }

    // --- Yardımcılar ---

    private int dp(float v) {
        return Math.round(v * density);
    }

    /** Boşluk (dp): tablette normal, telefonda sık. */
    private int size(float normalDp, float compactDp) {
        return dp(compact ? compactDp : normalDp);
    }

    /** Yazı boyu (sp): tablette normal, telefonda biraz küçük. */
    private float font(float normalSp, float compactSp) {
        return compact ? compactSp : normalSp;
    }

    private static TextView text(Context ctx, String s, float sp, int color, boolean bold) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setLineSpacing(0, 1.15f);
        if (bold) {
            t.setTypeface(Typeface.DEFAULT_BOLD);
        } else if (sp >= 17) {
            t.setTypeface(Typeface.create("sans-serif-light", Typeface.NORMAL));
        }
        return t;
    }

    private LinearLayout column(Context ctx, final int maxWidthDp) {
        LinearLayout l = new LinearLayout(ctx) {
            @Override
            protected void onMeasure(int widthSpec, int heightSpec) {
                int max = dp(maxWidthDp);
                if (MeasureSpec.getSize(widthSpec) > max) {
                    widthSpec = MeasureSpec.makeMeasureSpec(max, MeasureSpec.EXACTLY);
                }
                super.onMeasure(widthSpec, heightSpec);
            }
        };
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    /**
     * Görünümü dikey kaydırılabilir yapar: sığıyorsa ortada durur, sığmıyorsa kaydırılır.
     * passThrough: kaydırılacak bir şey yokken dokunmaları tutma (üç parmak hareketi arkaya geçsin).
     */
    private static View scroller(Context ctx, View child, final boolean passThrough) {
        ScrollView sv = new ScrollView(ctx) {
            private boolean idle() {
                return passThrough && !canScrollVertically(1) && !canScrollVertically(-1);
            }

            @Override
            public boolean onInterceptTouchEvent(MotionEvent e) {
                return !idle() && super.onInterceptTouchEvent(e);
            }

            @Override
            public boolean onTouchEvent(MotionEvent e) {
                return !idle() && super.onTouchEvent(e);
            }
        };
        sv.setFillViewport(true); // içerik kısaysa kaydırıcı boyuna uzar ki ortalanabilsin
        sv.setVerticalScrollBarEnabled(false);
        FrameLayout holder = new FrameLayout(ctx);
        holder.addView(child, centered());
        sv.addView(holder, new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        return sv;
    }

    private static LinearLayout row(Context ctx) {
        LinearLayout l = new LinearLayout(ctx);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    private View step(Context ctx, String badge, TextView body) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        TextView b = text(ctx, badge, font(13, 12), ACCENT, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(round(0x263D8BFD, 8));
        b.setPadding(0, dp(4), 0, dp(4));
        r.addView(b, new LinearLayout.LayoutParams(size(64, 56), LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1);
        lp.leftMargin = size(16, 12);
        r.addView(body, lp);
        return r;
    }

    private static View dot(Context ctx, int color) {
        View v = new View(ctx);
        GradientDrawable d = new GradientDrawable();
        d.setShape(GradientDrawable.OVAL);
        d.setColor(color);
        v.setBackground(d);
        return v;
    }

    private LinearLayout.LayoutParams dotParams(int sizeDp, int gapDp) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(sizeDp), dp(sizeDp));
        lp.rightMargin = dp(gapDp);
        return lp;
    }

    private static void setDotColor(View dot, int color) {
        ((GradientDrawable) dot.getBackground()).setColor(color);
    }

    private static View divider(Context ctx) {
        View v = new View(ctx);
        v.setBackgroundColor(LINE);
        return v;
    }

    private TextView button(Context ctx, String label, int fill, int pressed, int textColor) {
        TextView b = text(ctx, label, font(16, 15), textColor, true);
        b.setGravity(Gravity.CENTER);
        b.setMinWidth(size(120, 96));
        b.setPadding(size(20, 16), size(12, 9), size(20, 16), size(12, 9));
        StateListDrawable bg = new StateListDrawable();
        bg.addState(new int[]{android.R.attr.state_pressed}, round(pressed, 10));
        Drawable normal = round(fill, 10);
        if (fill == 0) {
            ((GradientDrawable) normal).setStroke(dp(1), 0x44FFFFFF);
        }
        bg.addState(new int[]{}, normal);
        b.setBackground(bg);
        b.setClickable(true);
        return b;
    }

    private GradientDrawable round(int color, int radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }

    private static LinearLayout.LayoutParams margins(int l, int t, int r, int b) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LayoutParams.WRAP_CONTENT,
                LayoutParams.WRAP_CONTENT);
        lp.setMargins(l, t, r, b);
        return lp;
    }

    private static LinearLayout.LayoutParams wide(int l, int t, int r, int b) {
        LinearLayout.LayoutParams lp = margins(l, t, r, b);
        lp.width = LayoutParams.MATCH_PARENT;
        return lp;
    }

    private static FrameLayout.LayoutParams full() {
        return new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.MATCH_PARENT);
    }

    private static FrameLayout.LayoutParams centered() {
        return new FrameLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT, Gravity.CENTER);
    }
}
