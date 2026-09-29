package org.ekran.android;

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
 */
final class CursorView extends View {
    private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
    private final Rect dirty = new Rect();
    private final RectF dst = new RectF();

    private Bitmap bitmap;
    private int cursorX, cursorY; // kaynak ekran pikseli (imleç resminin sol üstü)
    private boolean visible;
    private Session.Config config;

    CursorView(Context context) {
        super(context);
        setWillNotDraw(false);
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
        float toViewX = getWidth() / (float) c.streamW, toViewY = getHeight() / (float) c.streamH;
        float left = (c.contentX + cursorX * toStreamX) * toViewX;
        float top = (c.contentY + cursorY * toStreamY) * toViewY;
        dst.set(left, top, left + bitmap.getWidth() * toStreamX * toViewX,
                top + bitmap.getHeight() * toStreamY * toViewY);
        return true;
    }

    private void invalidateCursor() {
        if (visible && computeDst()) {
            dst.roundOut(dirty);
            dirty.inset(-2, -2);
            invalidate(dirty);
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        if (visible && computeDst()) {
            canvas.drawBitmap(bitmap, null, dst, paint);
        }
    }
}
