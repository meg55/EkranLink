package org.ekran.android;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaCodecList;
import android.media.MediaFormat;
import android.util.Log;
import android.view.Surface;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.Arrays;

/**
 * H.264 kareleri donanım çözücüye (MediaCodec) verir; çıkan kareler beklemeden yüzeye çizilir.
 *
 * İki giriş yolu var:
 * - {@link #onAccessUnit}: protokol v1, sınırları belli tam kare (gecikmesiz)
 * - {@link #onNal}: ham Annex-B (Faz 0 tanı modu), kare sınırı bir sonraki NAL'de anlaşılır
 *
 * API 16 uyumluluğu için eski getInputBuffers() arayüzü kullanılıyor.
 *
 * Ekran kapanınca {@link #suspend} çözücüyü bırakır (yüzey yok olur); {@link #resume} yenisiyle devam eder,
 * ilk anahtar karede çözücü yeniden kurulur. Metotlar ağ ve arayüz iş parçacıklarından çağrılır: senkron.
 */
final class H264Decoder {
    interface FrameListener {
        /** Kare çözülüp yüzeye verildi (çözücü çıkış iş parçacığından çağrılır). decoderIdle: içeride kare kalmadı. */
        void onFrameRendered(long pts, long decodeUs, boolean decoderIdle);
    }

    private static final String TAG = "EkranDecoder";
    private static final String MIME = "video/avc";
    private static final byte[] START_CODE = {0, 0, 0, 1};

    private Surface surface;
    private boolean suspended; // ekran kapalı: kare gelirse düşür
    private final Stats stats;
    private final FrameListener listener;

    private byte[] sps, pps;
    private byte[] configuredSps, configuredPps;

    private MediaCodec codec;
    private ByteBuffer[] inputBuffers;
    private Thread outputThread;
    private volatile boolean running;
    private boolean waitingForIdr;

    // Birikmekte olan kare (dilimler, başlangıç kodlarıyla)
    private byte[] au = new byte[256 * 1024];
    private int auLen;
    private boolean auHasSlice;
    private boolean auIsIdr;
    private long auPts = -1;

    // Çözücüye verilmiş kareler: {pts, yerel giriş zamanı µs}. Çözücü sırayı korur.
    private final ArrayDeque<long[]> pending = new ArrayDeque<long[]>();

    H264Decoder(Surface surface, Stats stats, FrameListener listener) {
        this.surface = surface;
        this.stats = stats;
        this.listener = listener;
    }

    /** Protokol: tek karenin Annex-B verisi. */
    synchronized void onAccessUnit(byte[] b, int off, int len, long pts) throws IOException {
        // Kısa yol: karelerin çoğu SPS/PPS içermeyen tek dilimli P-karesi. Çözücü hazırsa bayt bayt
        // taramadan ve ara belleğe kopyalamadan olduğu gibi ver (tabletin işlemcisi için).
        int sc = len > 4 && b[off] == 0 && b[off + 1] == 0
                ? (b[off + 2] == 1 ? 3 : (b[off + 2] == 0 && b[off + 3] == 1 ? 4 : 0)) : 0;
        if (sc > 0 && codec != null && !waitingForIdr && (b[off + sc] & 0x1F) == 1) {
            queue(b, off, len, pts);
            return;
        }
        auPts = pts;
        int end = off + len;
        int nalStart = -1;
        for (int i = off; i + 2 < end; i++) {
            if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
                if (nalStart >= 0) {
                    int nalEnd = i;
                    while (nalEnd > nalStart && b[nalEnd - 1] == 0) {
                        nalEnd--;
                    }
                    onNal(b, nalStart, nalEnd - nalStart);
                }
                nalStart = i + 3;
                i += 2;
            }
        }
        if (nalStart >= 0 && nalStart < end) {
            onNal(b, nalStart, end - nalStart);
        }
        flushAu();
        auPts = -1;
    }

    /** Ham akış: tek NAL (başlangıç kodu olmadan). */
    synchronized void onNal(byte[] nal) throws IOException {
        onNal(nal, 0, nal.length);
    }

    private void onNal(byte[] b, int off, int len) throws IOException {
        if (len <= 0) {
            return;
        }
        int type = b[off] & 0x1F;
        switch (type) {
            case 7: // SPS
                flushAu();
                sps = Arrays.copyOfRange(b, off, off + len);
                return;
            case 8: // PPS
                flushAu();
                pps = Arrays.copyOfRange(b, off, off + len);
                return;
            case 1: // dilim
            case 5: // IDR dilimi
                boolean firstSliceOfPicture = len > 1 && (b[off + 1] & 0x80) != 0; // first_mb_in_slice == 0
                if (firstSliceOfPicture) {
                    flushAu();
                }
                if (!auHasSlice) {
                    auIsIdr = type == 5;
                }
                append(START_CODE, 0, START_CODE.length);
                append(b, off, len);
                auHasSlice = true;
                return;
            default: // AUD, SEI vb.: çözücüye gerekmiyor
                flushAu();
        }
    }

    /** Ham akış bitti: bekleyen son kareyi de gönder. */
    synchronized void finish() throws IOException {
        flushAu();
    }

    private void append(byte[] src, int off, int len) {
        if (auLen + len > au.length) {
            au = Arrays.copyOf(au, Math.max(au.length * 2, auLen + len));
        }
        System.arraycopy(src, off, au, auLen, len);
        auLen += len;
    }

    private void flushAu() throws IOException {
        if (!auHasSlice) {
            return;
        }
        int len = auLen;
        boolean idr = auIsIdr;
        auLen = 0;
        auHasSlice = false;

        ensureCodec();
        if (codec == null || (waitingForIdr && !idr)) {
            stats.onDropped(); // henüz SPS/PPS ya da anahtar kare gelmedi
            return;
        }
        waitingForIdr = false;
        queue(au, 0, len, auPts >= 0 ? auPts : System.nanoTime() / 1000);
    }

    private void queue(byte[] data, int off, int len, long pts) {
        int index = codec.dequeueInputBuffer(500_000);
        if (index < 0) {
            stats.onDropped();
            Log.w(TAG, "Giriş tamponu yok, kare düştü");
            return;
        }
        ByteBuffer buffer = inputBuffers[index];
        buffer.clear();
        if (len > buffer.capacity()) {
            Log.w(TAG, "Kare tampondan büyük: " + len + " > " + buffer.capacity());
            codec.queueInputBuffer(index, 0, 0, 0, 0);
            stats.onDropped();
            waitingForIdr = true;
            return;
        }
        buffer.put(data, off, len);
        long now = System.nanoTime() / 1000;
        synchronized (pending) {
            pending.add(new long[]{pts, now});
            if (pending.size() > 64) {
                pending.poll();
            }
        }
        codec.queueInputBuffer(index, 0, len, pts, 0);
        stats.onInput(len);
    }

    /** Yüzey yok oluyor: çözücüyü bırak (dönüşte yüzey kullanılmamalı). */
    synchronized void suspend() {
        release();
        suspended = true;
    }

    /** Yeni yüzey: bir sonraki anahtar karede çözücü yeniden kurulur. */
    synchronized void resume(Surface s) {
        surface = s;
        suspended = false;
    }

    private void ensureCodec() throws IOException {
        if (suspended || sps == null || pps == null) {
            return;
        }
        if (codec != null && Arrays.equals(sps, configuredSps) && Arrays.equals(pps, configuredPps)) {
            return;
        }
        release();

        SpsParser info = SpsParser.parse(sps);
        String name = findDecoderName();
        MediaFormat format = MediaFormat.createVideoFormat(MIME, info.width, info.height);
        format.setByteBuffer("csd-0", withStartCode(SpsRewriter.lowLatency(sps)));
        format.setByteBuffer("csd-1", withStartCode(pps));
        format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, Math.max(info.width * info.height, 512 * 1024));
        // Düşük gecikme istekleri: tanımayan çözücü bu anahtarları yok sayar.
        format.setInteger("low-latency", 1);                                // Android 11+
        format.setInteger("vdec-lowlatency", 1);                            // MediaTek
        format.setInteger("vendor.low-latency.enable", 1);                  // Exynos
        format.setInteger("vendor.qti-ext-dec-low-latency.enable", 1);      // Qualcomm
        format.setInteger("vendor.rtc-ext-dec-low-latency.enable", 1);      // Amlogic

        codec = name != null ? MediaCodec.createByCodecName(name) : MediaCodec.createDecoderByType(MIME);
        codec.configure(format, surface, null, 0);
        codec.start();
        inputBuffers = codec.getInputBuffers();
        configuredSps = sps;
        configuredPps = pps;
        waitingForIdr = true;
        synchronized (pending) {
            pending.clear();
        }

        String profile = info.profileIdc == 66 ? "Baseline" : info.profileIdc == 77 ? "Main"
                : info.profileIdc == 100 ? "High" : ("profile_idc " + info.profileIdc);
        stats.setDecoder(name, // null: varsayılan çözücü (metni Stats cihaz diline çevirir)
                info.width + "×" + info.height + " H.264 " + profile + " L" + (info.levelIdc / 10f));
        Log.i(TAG, "Çözücü başladı: " + name + " " + info.width + "x" + info.height);

        running = true;
        outputThread = new Thread(new Runnable() {
            @Override
            public void run() {
                drainOutput();
            }
        }, "ekran-decoder-out");
        outputThread.start();
    }

    private void drainOutput() {
        MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
        MediaCodec c = codec;
        boolean render = surface != null;
        try {
            while (running) {
                int index = c.dequeueOutputBuffer(info, 250_000);
                if (index >= 0) {
                    c.releaseOutputBuffer(index, render);
                    long now = System.nanoTime() / 1000;
                    long pts = info.presentationTimeUs;
                    long queuedAt = -1;
                    boolean idle;
                    synchronized (pending) {
                        long[] e;
                        while ((e = pending.poll()) != null) {
                            if (e[0] == pts) {
                                queuedAt = e[1];
                                break;
                            }
                        }
                        idle = pending.isEmpty();
                    }
                    long decodeUs = queuedAt >= 0 ? now - queuedAt : 0;
                    // İtici kareler (tek pts) bir sonraki değişime kadar çözücüde bekleyebilir; gecikmeye katma
                    stats.onOutput((pts & 1) != 0 ? -1 : decodeUs);
                    if (listener != null) {
                        listener.onFrameRendered(pts, decodeUs, idle);
                    }
                } else if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    Log.i(TAG, "Çıkış biçimi: " + c.getOutputFormat());
                }
            }
        } catch (IllegalStateException e) {
            if (running) {
                Log.w(TAG, "Çözücü çıkış hatası", e);
            }
        }
    }

    synchronized void release() {
        running = false;
        if (outputThread != null) {
            try {
                outputThread.join(500);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            outputThread = null;
        }
        if (codec != null) {
            try {
                codec.stop();
            } catch (RuntimeException ignored) {
            }
            codec.release();
            codec = null;
        }
        configuredSps = configuredPps = null;
    }

    private static ByteBuffer withStartCode(byte[] nal) {
        ByteBuffer b = ByteBuffer.allocate(START_CODE.length + nal.length);
        b.put(START_CODE).put(nal);
        b.flip();
        return b;
    }

    /** Donanım çözücüyü tercih et (OMX.google.* yazılım çözücüdür). */
    static String findDecoderName() {
        String software = null;
        for (int i = 0; i < MediaCodecList.getCodecCount(); i++) {
            MediaCodecInfo info = MediaCodecList.getCodecInfoAt(i);
            if (info.isEncoder()) {
                continue;
            }
            for (String type : info.getSupportedTypes()) {
                if (type.equalsIgnoreCase(MIME)) {
                    String n = info.getName();
                    if (n.startsWith("OMX.google.") || n.startsWith("c2.android.")) {
                        if (software == null) {
                            software = n;
                        }
                    } else {
                        return n;
                    }
                }
            }
        }
        return software;
    }
}
