package org.ekran.android;

import android.content.Context;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.util.Locale;

/**
 * Tabletteki bilgi katmanı: bağlantı beklerken karşılama ekranı, bağlıyken üç parmakla açılan ölçüm kartı ve
 * Wi-Fi eşleştirme onayı.
 *
 * Uygulamanın ekranı hep yatay kalır (görüntü akışı bozulmasın diye); tablet dik tutulunca bu katman kendini
 * çevirir. Android'in iletişim kutuları ve bildirimleri çevrilemediğinden hepsi burada çizilir.
 */
final class InfoLayer extends ViewGroup {
    interface Actions {
        int pairCount();

        void forgetPairs();
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
    private static final Locale TR = new Locale("tr", "TR");

    private static final String[] STAT_LABELS = {"Görüntü", "Kare hızı", "Veri", "Çözme", "Çözücü", "Düşen kare"};

    private final Actions actions;
    private final float density;
    private final FrameLayout content;
    private int rotation;

    private final View welcome;
    private final View welcomeDot;
    private final TextView welcomeStatus, wifiText, footer, forget;
    private boolean forgetArmed;

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
        density = ctx.getResources().getDisplayMetrics().density;
        content = new FrameLayout(ctx);
        addView(content);

        // --- Karşılama (bağlı değilken tam ekran) ---
        FrameLayout welcomeRoot = new FrameLayout(ctx);
        welcomeRoot.setBackgroundColor(BG);
        LinearLayout col = column(ctx, 560);
        col.setPadding(dp(32), dp(24), dp(32), dp(24));

        col.addView(text(ctx, "EKRAN", 40, TEXT, true));
        TextView tagline = text(ctx, "Bu tablet bilgisayarınızın ekranı olmaya hazır.", 15, MUTED, false);
        col.addView(tagline, margins(0, dp(2), 0, dp(28)));

        LinearLayout statusRow = row(ctx);
        welcomeDot = dot(ctx, AMBER);
        statusRow.addView(welcomeDot, dotParams(12, 12));
        welcomeStatus = text(ctx, "", 20, TEXT, false);
        statusRow.addView(welcomeStatus);
        col.addView(statusRow, margins(0, 0, 0, dp(24)));

        col.addView(divider(ctx), new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 1));
        col.addView(step(ctx, "USB", text(ctx,
                "Kabloyu takın. Bilgisayarda EKRAN açık, tablette USB hata ayıklama açık olmalı.", 15, TEXT, false)),
                wide(0, dp(20), 0, 0));
        wifiText = text(ctx, "", 15, TEXT, false);
        col.addView(step(ctx, "Wi-Fi", wifiText), wide(0, dp(16), 0, dp(20)));
        col.addView(divider(ctx), new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 1));

        LinearLayout footerRow = row(ctx);
        footer = text(ctx, deviceName + " · sürüm " + version, 13, MUTED, false);
        footerRow.addView(footer, new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
        forget = text(ctx, "", 13, ACCENT, false);
        forget.setPadding(dp(12), dp(10), 0, dp(10));
        forget.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                onForgetClicked();
            }
        });
        footerRow.addView(forget);
        col.addView(footerRow, wide(0, dp(6), 0, 0));
        disarmForget();

        welcomeRoot.addView(col, centered());
        welcome = welcomeRoot;
        content.addView(welcome, full());

        // --- Ölçüm kartı (bağlıyken, üç parmakla) ---
        LinearLayout card = new LinearLayout(ctx);
        card.setOrientation(LinearLayout.VERTICAL);
        card.setBackground(round(CARD, 14));
        card.setPadding(dp(16), dp(14), dp(18), dp(14));
        LinearLayout titleRow = row(ctx);
        statsDot = dot(ctx, GREEN);
        titleRow.addView(statsDot, dotParams(10, 10));
        statsTitle = text(ctx, "", 16, TEXT, true);
        titleRow.addView(statsTitle);
        card.addView(titleRow, margins(0, 0, 0, dp(10)));
        for (int i = 0; i < STAT_LABELS.length; i++) {
            LinearLayout r = row(ctx);
            TextView label = text(ctx, STAT_LABELS[i], 13, MUTED, false);
            r.addView(label, new LinearLayout.LayoutParams(dp(92), LayoutParams.WRAP_CONTENT));
            statValues[i] = text(ctx, "–", 13, TEXT, false);
            r.addView(statValues[i]);
            card.addView(r, margins(0, dp(3), 0, dp(3)));
        }
        card.addView(text(ctx, "Gizlemek için üç parmakla dokunun", 12, MUTED, false), margins(0, dp(10), 0, 0));
        statsCard = card;
        FrameLayout.LayoutParams cardLp = new FrameLayout.LayoutParams(LayoutParams.WRAP_CONTENT,
                LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.START);
        cardLp.setMargins(dp(16), dp(16), dp(16), dp(16));
        content.addView(statsCard, cardLp);
        statsCard.setVisibility(GONE);

        // --- Uyku örtüsü: Windows ekranı kapattı; tablet ekranı kendi zaman aşımıyla kapanana kadar kararık ---
        // Dokunmalar geçer: dokunmak (gerçek dokunmatik monitörde olduğu gibi) Windows'u uyandırır.
        FrameLayout cover = new FrameLayout(ctx);
        cover.setBackgroundColor(0xFF000000);
        cover.addView(text(ctx, "Bilgisayarın ekranı kapalı", 15, 0xFF30363D, false),
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
        pc.setPadding(dp(28), dp(24), dp(28), dp(20));
        pc.addView(text(ctx, "Eşleştirme isteği", 22, TEXT, true));
        pairMessage = text(ctx, "", 15, MUTED, false);
        pc.addView(pairMessage, margins(0, dp(6), 0, dp(18)));
        pairCode = text(ctx, "", 52, TEXT, true);
        pairCode.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
        pairCode.setGravity(Gravity.CENTER);
        pairCode.setBackground(round(0x14FFFFFF, 12));
        pairCode.setPadding(0, dp(10), 0, dp(10));
        pc.addView(pairCode, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        pc.addView(text(ctx, "Bilgisayarda görünen kodla aynıysa onaylayın.", 14, MUTED, false),
                margins(0, dp(14), 0, dp(20)));
        LinearLayout buttons = row(ctx);
        buttons.setGravity(Gravity.END | Gravity.CENTER_VERTICAL);
        TextView reject = button(ctx, "Reddet", 0x00000000, 0x22FFFFFF, TEXT);
        reject.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finishPair(false);
            }
        });
        TextView accept = button(ctx, "Onayla", ACCENT, 0xFF2F6FD0, 0xFFFFFFFF);
        accept.setOnClickListener(new OnClickListener() {
            @Override
            public void onClick(View v) {
                finishPair(true);
            }
        });
        buttons.addView(reject, margins(0, 0, dp(12), 0));
        buttons.addView(accept);
        pc.addView(buttons, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        pairRoot.addView(pc, centered());
        pairView = pairRoot;
        content.addView(pairView, full());
        pairView.setVisibility(GONE);
    }

    // --- Dışarıdan ---

    /** Tabletin tutuluş yönü: yatay ekrana göre saat yönünde 0, 90 ya da 270 derece. */
    void setOrientation(int degrees) {
        if (degrees == rotation) {
            return;
        }
        rotation = degrees;
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
                    ? "Aynı ağdaki bilgisayar bu tableti kendiliğinden bulur.\nBu tabletin adresi: " + addresses
                    : "Tablet bir Wi-Fi ağına bağlı değil. USB ile bağlanabilirsiniz.");
            if (!forgetArmed) {
                forget.setVisibility(actions.pairCount() > 0 ? VISIBLE : GONE);
            }
        }
        if (statsVisible()) {
            setDotColor(statsDot, color);
            statsTitle.setText(s.status);
            statValues[0].setText(s.stream);
            statValues[1].setText(String.format(TR, "%.0f fps  (gelen %.0f)", s.fps, s.inFps));
            statValues[2].setText(String.format(TR, "%.2f Mbit/sn", s.mbps));
            statValues[3].setText(s.fps > 0
                    ? String.format(TR, "%.1f ms  (en az %.1f · en çok %.1f)", s.latAvgMs, s.latMinMs, s.latMaxMs)
                    : "–");
            statValues[4].setText(s.decoder);
            statValues[5].setText(String.format(TR, "%d / %d", s.droppedFrames, s.totalFrames));
        }
    }

    /** Eşleştirme sorusunu gösterir; kullanıcı seçince ilgili iş ana iş parçacığında çalışır. */
    void showPair(String pcName, String code, Runnable onAccept, Runnable onReject) {
        pairMessage.setText(pcName + " bu tablete görüntü göndermek istiyor.");
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
            forget.setText("Tüm eşleşmeler silinsin mi? Tekrar dokunun");
            forget.setTextColor(RED);
            postDelayed(disarm, 4000);
        } else {
            actions.forgetPairs();
            forget.setText("Eşleşmeler silindi");
            forget.setTextColor(MUTED);
            forget.setClickable(false);
            postDelayed(disarm, 2500);
        }
    }

    private void disarmForget() {
        removeCallbacks(disarm);
        forgetArmed = false;
        forget.setText("Eşleşmeleri unut");
        forget.setTextColor(ACCENT);
        forget.setClickable(true);
        forget.setVisibility(actions.pairCount() > 0 ? VISIBLE : GONE);
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

    private static TextView text(Context ctx, String s, float sp, int color, boolean bold) {
        TextView t = new TextView(ctx);
        t.setText(s);
        t.setTextSize(TypedValue.COMPLEX_UNIT_SP, sp);
        t.setTextColor(color);
        t.setLineSpacing(0, 1.15f);
        if (bold) {
            t.setTypeface(Typeface.DEFAULT_BOLD);
        } else if (sp >= 18) {
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

    private static LinearLayout row(Context ctx) {
        LinearLayout l = new LinearLayout(ctx);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    private View step(Context ctx, String badge, TextView body) {
        LinearLayout r = new LinearLayout(ctx);
        r.setOrientation(LinearLayout.HORIZONTAL);
        TextView b = text(ctx, badge, 13, ACCENT, true);
        b.setGravity(Gravity.CENTER);
        b.setBackground(round(0x263D8BFD, 8));
        b.setPadding(0, dp(4), 0, dp(4));
        r.addView(b, new LinearLayout.LayoutParams(dp(64), LayoutParams.WRAP_CONTENT));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LayoutParams.WRAP_CONTENT, 1);
        lp.leftMargin = dp(16);
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
        TextView b = text(ctx, label, 16, textColor, true);
        b.setGravity(Gravity.CENTER);
        b.setMinWidth(dp(120));
        b.setPadding(dp(20), dp(12), dp(20), dp(12));
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
