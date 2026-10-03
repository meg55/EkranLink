package org.ekran.android;

import android.content.res.Resources;

/**
 * Ekrandaki bilgi katmanı için durum ve sayaçlar. Tüm metotlar iş parçacığı güvenli.
 *
 * Durum metni dize kaynağı olarak saklanır (ağ iş parçacığının Activity'si yok); özet alınırken hizmetin
 * kaynaklarıyla cihaz diline çevrilir.
 */
final class Stats {
    static final int WAITING = 0, CONNECTING = 1, CONNECTED = 2, ERROR = 3;

    /** Saniyelik özet (bilgi katmanında gösterilir). */
    static final class Snapshot {
        int state;
        String status, surface, stream, decoder;
        float inFps, fps, mbps, latAvgMs, latMinMs, latMaxMs, inFlight;
        long totalFrames, droppedFrames;

        String logLine() {
            return String.format(java.util.Locale.US,
                    "%s | yüzey %s akış %s | %s | gelen %.1f çizilen %.1f fps %.2f Mbit/s | çözme %.1f ms (%.1f-%.1f)"
                            + " bekleyen %.1f | toplam %d düşen %d",
                    status, surface, stream, decoder, inFps, fps, mbps, latAvgMs, latMinMs, latMaxMs, inFlight,
                    totalFrames, droppedFrames);
        }
    }

    private long windowStartMs = System.currentTimeMillis();
    private int inFrames, outFrames, latCount;
    private long inBytes;
    private long latSumUs, latMinUs = Long.MAX_VALUE, latMaxUs;

    private float fps, inFps, mbps, latAvgMs, latMinMs, latMaxMs;
    private long totalFrames, droppedFrames;

    private final Resources res;
    private int state = WAITING;
    private int statusRes = R.string.status_waiting;
    private Object[] statusArgs = {};
    private String decoderName = "-"; // null: sistemin varsayılan çözücüsü
    private String streamInfo = "-";
    private String surfaceInfo = "-";

    // Çözücünün içinde bekleyen (girmiş ama çıkmamış) kare sayısı: gecikmenin kare cinsinden karşılığı.
    private long totalIn;
    private long inFlightSum;
    private float inFlightAvg;

    synchronized void onInput(int bytes) {
        inFrames++;
        totalIn++;
        inBytes += bytes;
    }

    /** latencyUs < 0: gecikme ölçümüne katılmayan kare (itici kare). */
    synchronized void onOutput(long latencyUs) {
        outFrames++;
        totalFrames++;
        inFlightSum += totalIn - totalFrames;
        if (latencyUs < 0) {
            return;
        }
        latCount++;
        latSumUs += latencyUs;
        latMinUs = Math.min(latMinUs, latencyUs);
        latMaxUs = Math.max(latMaxUs, latencyUs);
    }

    synchronized void onDropped() {
        droppedFrames++;
    }

    Stats(Resources res) {
        this.res = res;
    }

    /** resId: durum metninin dize kaynağı; args: içindeki biçim değerleri. */
    synchronized void setStatus(int state, int resId, Object... args) {
        this.state = state;
        statusRes = resId;
        statusArgs = args;
    }

    synchronized void setDecoder(String name, String stream) {
        decoderName = name;
        streamInfo = stream;
    }

    synchronized void setSurface(int w, int h) {
        surfaceInfo = w + "×" + h;
    }

    synchronized void resetSession() {
        totalFrames = 0;
        totalIn = 0;
        droppedFrames = 0;
        fps = inFps = mbps = latAvgMs = latMinMs = latMaxMs = 0;
    }

    /** Saniyede bir çağrılır; ölçüm penceresini kapatıp özeti döndürür. */
    synchronized Snapshot snapshot() {
        long now = System.currentTimeMillis();
        float sec = Math.max(0.001f, (now - windowStartMs) / 1000f);
        fps = outFrames / sec;
        inFps = inFrames / sec;
        mbps = inBytes * 8f / sec / 1_000_000f;
        if (outFrames > 0) {
            inFlightAvg = inFlightSum / (float) outFrames;
        }
        if (latCount > 0) {
            latAvgMs = latSumUs / (float) latCount / 1000f;
            latMinMs = latMinUs / 1000f;
            latMaxMs = latMaxUs / 1000f;
        }
        windowStartMs = now;
        inFrames = outFrames = latCount = 0;
        inBytes = 0;
        latSumUs = 0;
        inFlightSum = 0;
        latMinUs = Long.MAX_VALUE;
        latMaxUs = 0;

        Snapshot s = new Snapshot();
        s.state = state;
        s.status = res.getString(statusRes, statusArgs);
        s.surface = surfaceInfo;
        s.stream = streamInfo;
        s.decoder = decoderName != null ? decoderName : res.getString(R.string.stat_decoder_default);
        s.inFps = inFps;
        s.fps = fps;
        s.mbps = mbps;
        s.latAvgMs = latAvgMs;
        s.latMinMs = latMinMs;
        s.latMaxMs = latMaxMs;
        s.inFlight = inFlightAvg;
        s.totalFrames = totalFrames;
        s.droppedFrames = droppedFrames;
        return s;
    }
}
