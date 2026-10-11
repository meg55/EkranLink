package org.ekranlink.android;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.view.View;

/**
 * Fare imlecini tablette yerel olarak çizer. İmleç hareketi video kodlaması gerektirmez;
 * hem PC'yi daha az yorar hem de imleç gecikmesiz hissedilir.
 *
 * Koordinatlar yatay görüntüye göredir; pencere dikeyse (Android 16+ büyük ekran) tuval {@link #setTurn} kadar
 * çevrilerek çizilir, görüntünün çözücüde çevrildiği gibi.
 */
final class CursorView extends View {
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Rect dirty = new Rect();
    private final RectF dst = new RectF();

    private Bitmap bitmap;
    private int cursorX, cursorY; // kaynak ekran pikseli (imleç resminin sol üstü)
    private boolean visible;
    private Session.Config config;
    private int turn;

    CursorView(Context context) {
        super(context);
        setWillNotDraw(false);
    }

    /** Pencere dikeyken görüntünün saat yönünde çevrilme açısı (0, 90, 270). */
    void setTurn(int degrees) {
        turn = degrees;
        invalidate();
    }

    /** Yatay görüntünün bu görünümdeki genişliği (dikey pencerede en/boy yer değiştirir). */
    int landscapeWidth() {
        return turn == 0 ? getWidth() : getHeight();
    }

    int landscapeHeight() {
        return turn == 0 ? getHeight() : getWidth();
    }

    void setConfig(Session.Config c) {
        config = c;
        invalidate();
    }

    void setShape(int w, int h, int[] argb) {
        if (w <= 0 || h <= 0 || argb.length < w * h) {
            return; // boş/bozuk resim: eskisini koru
        }
        invalidateCursor();
        if (bitmap != null) {
            bitmap.recycle();
        }
        bitmap = Bitmap.createBitmap(argb, w, h, Bitmap.Config.ARGB_8888);
        invalidateCursor();
    }

    void setPosition(int x, int y, boolean vis) {
        invalidateCursor();
        cursorX = x;
        cursorY = y;
        visible = vis;
        invalidateCursor();
    }

    void clear() {
        visible = false;
        config = null;
        invalidate();
    }

    /** Kaynak ekran pikselinden görünüm pikseline ölçek. */
    private boolean computeDst() {
        Session.Config c = config;
        if (c == null || bitmap == null || c.srcW == 0 || c.streamW == 0) {
            return false;
        }
        float toStreamX = c.contentW / (float) c.srcW, toStreamY = c.contentH / (float) c.srcH;
        float toViewX = landscapeWidth() / (float) c.streamW, toViewY = landscapeHeight() / (float) c.streamH;
        float left = (c.contentX + cursorX * toStreamX) * toViewX;
        float top = (c.contentY + cursorY * toStreamY) * toViewY;
        dst.set(left, top, left + bitmap.getWidth() * toStreamX * toViewX,
                top + bitmap.getHeight() * toStreamY * toViewY);
        return true;
    }

    private void invalidateCursor() {
        if (turn != 0) {
            invalidate(); // donanım çiziminde dikdörtgen zaten yok sayılıyor
        } else if (visible && computeDst()) {
            dst.roundOut(dirty);
            dirty.inset(-2, -2);
            invalidate(dirty);
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (visible && computeDst()) {
            if (turn == 90) {
                canvas.translate(getWidth(), 0);
            } else if (turn == 270) {
                canvas.translate(0, getHeight());
            }
            canvas.rotate(turn);
            canvas.drawBitmap(bitmap, null, dst, paint);
        }
    }
}
