package org.ekran.android;

import android.os.Build;
import android.util.Log;
import android.view.Surface;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.GeneralSecurityException;
import java.util.concurrent.LinkedBlockingQueue;

/**
 * Protokol v2 oturumu (docs/PROTOCOL.md).
 *
 * Çerçeve: u32 gövdeUzunluğu, gövde = u8 tür + yük; şifreli bağlantıda gövde ChaCha20-Poly1305 ile
 * şifrelenir ve 16 bayt etiket eklenir. USB (yerel bağlantı) şifresizdir.
 */
final class Session implements H264Decoder.FrameListener {
    static final int VERSION = 2;

    static final int MSG_HELLO = 1;
    static final int MSG_CONFIG = 2;
    static final int MSG_VIDEO = 3;
    static final int MSG_PING = 4;
    static final int MSG_PONG = 5;
    static final int MSG_FRAME_ACK = 6;
    static final int MSG_CURSOR_SHAPE = 7;
    static final int MSG_CURSOR_POS = 8;
    static final int MSG_TOUCH = 10;
    static final int MSG_ORIENTATION = 11;
    static final int MSG_DISPLAY_POWER = 12;
    static final int MSG_KEY_REQUEST = 13;
    static final int MSG_PC_HELLO = 20;
    static final int MSG_TABLET_KEY = 21;
    static final int MSG_MODE = 22;
    static final int MSG_PAIR_RESULT = 23;

    static final int TOUCH_DOWN = 0, TOUCH_MOVE = 1, TOUCH_UP = 2, TOUCH_CANCEL = 3;

    /** Eşleştirme penceresi: kullanıcının kararı beklenir ya da PC reddederse kapatılır. */
    interface PairPrompt {
        boolean await();
        void dismiss();
    }

    /** Akışın tablet ekranındaki yerleşimi (Config mesajı). */
    static final class Config {
        int streamW, streamH, srcW, srcH, contentX, contentY, contentW, contentH;
    }

    interface Host {
        int surfaceWidth();
        int surfaceHeight();
        /** Fiziksel ekran boyutu ve yoğunluğu. */
        void displayInfo(android.graphics.Point realSize, android.util.DisplayMetrics metrics);
        String deviceId();
        PairStore pairStore();
        /** Wi-Fi'de ilk eşleştirme: kodu gösteren pencereyi aç (engellemez). */
        PairPrompt showPairing(String pcName, String code, Runnable onReject);
        void onConfig(Config c);
        void onCursorShape(int w, int h, int hotX, int hotY, int[] argb);
        void onCursorPos(int x, int y, boolean visible);
        void onSessionActive(boolean active, boolean encrypted);
        /** Windows ekranları kapattı / açtı (ağ iş parçacığından). */
        void onDisplayPower(boolean on);
    }

    private static final String TAG = "EkranSession";
    private static final byte[] STOP = new byte[0];

    private final DataInputStream in;
    private final OutputStream out;
    private final Host host;
    private final boolean localLink; // adb forward (USB) veya aynı cihazdan
    private final H264Decoder decoder;
    private final LinkedBlockingQueue<byte[]> sendQueue = new LinkedBlockingQueue<byte[]>();
    private volatile boolean closed;

    private Crypto.DirectionKey tx, rx;
    private long txSeq, rxSeq;
    private int pendingAcks; // onayı henüz gönderilmemiş çizilen kare sayısı
    private byte[] buf = new byte[256 * 1024];

    Session(DataInputStream in, OutputStream out, boolean localLink, Surface surface, Stats stats, Host host) {
        this.in = in;
        // Tamponlu: kuyruktaki mesajlar (kare onayları vb.) tek sistem çağrısında yazılır
        this.out = new java.io.BufferedOutputStream(out, 16 * 1024);
        this.localLink = localLink;
        this.host = host;
        this.decoder = new H264Decoder(surface, stats, this);
    }

    /** Karşı taraf "EKRN" gönderdi; oturum bağlantı kopana kadar sürer. */
    void run() throws IOException {
        int peerVersion = in.readUnsignedShort();
        if (peerVersion != VERSION) {
            throw new IOException("Desteklenmeyen PC protokol sürümü " + peerVersion + " (beklenen " + VERSION + ")");
        }
        try {
            handshake();
        } catch (GeneralSecurityException e) {
            throw new IOException("Güvenlik: " + e.getMessage());
        }

        Thread sender = new Thread(new Runnable() {
            @Override
            public void run() {
                sendLoop();
            }
        }, "ekran-send");
        sender.start();
        sendHello();
        host.onSessionActive(true, tx != null);

        try {
            while (!closed) {
                int len = readFrame();
                handle(buf[0] & 0xFF, buf, 1, len - 1);
            }
        } finally {
            closed = true;
            sendQueue.offer(STOP);
            decoder.release();
            host.onSessionActive(false, false);
        }
    }

    // --- Anahtar değişimi ve eşleştirme ---

    private void handshake() throws IOException, GeneralSecurityException {
        int len = readFrame();
        if ((buf[0] & 0xFF) != MSG_PC_HELLO || len < 1 + 1 + 16 + 65 + 16 + 2) {
            throw new IOException("PcHello bekleniyordu");
        }
        int o = 1;
        int flags = buf[o++] & 0xFF;
        byte[] pcId = slice(o, 16);
        o += 16;
        byte[] pcPub = slice(o, 65);
        o += 65;
        byte[] pcNonce = slice(o, 16);
        o += 16;
        int nameLen = readU16(buf, o);
        String pcName = new String(buf, o + 2, Math.min(nameLen, len - o - 2), "UTF-8");

        PairStore store = host.pairStore();
        byte[] pairKey = store.load(pcId);
        Crypto.Ecdh ecdh = new Crypto.Ecdh();
        byte[] tPub = ecdh.publicKey();
        byte[] tNonce = Crypto.random(16);
        String deviceId = host.deviceId();
        String deviceName = Build.MANUFACTURER + " " + Build.MODEL;
        {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            DataOutputStream d = new DataOutputStream(bos);
            d.writeByte(MSG_TABLET_KEY);
            d.write(tPub);
            d.write(tNonce);
            d.writeUTF(deviceId);
            d.writeUTF(deviceName);
            d.writeByte(pairKey != null ? 1 : 0);
            writeFrameNow(bos.toByteArray());
            out.flush(); // el sıkışma: gönderim iş parçacığı henüz yok, tamponu hemen boşalt
        }

        len = readFrame();
        if ((buf[0] & 0xFF) != MSG_MODE || len != 3) {
            throw new IOException("Mode bekleniyordu");
        }
        boolean paired = buf[1] != 0;
        boolean encrypt = buf[2] != 0;
        if (!encrypt && !localLink) {
            throw new GeneralSecurityException("ağdan gelen şifresiz bağlantı reddedildi");
        }
        if (paired && pairKey == null) {
            throw new GeneralSecurityException("PC eşleşmiş olduğumuzu sanıyor ama anahtar yok");
        }
        if ((flags & 1) != 0 && !localLink) {
            Log.w(TAG, "PC şifresiz istedi ama bağlantı yerel değil");
        }

        byte[] z = ecdh.agree(pcPub);
        byte[] h = Crypto.sha256(pcPub, tPub, pcNonce, tNonce, pcId, deviceId.getBytes("UTF-8"));
        byte[] prk = Crypto.hmac(h, z, paired ? pairKey : new byte[32]);
        if (encrypt) {
            tx = new Crypto.DirectionKey(Crypto.derive(prk, "ekran t2p key"));
            rx = new Crypto.DirectionKey(Crypto.derive(prk, "ekran p2t key"));
        }

        if (!paired) {
            byte[] sas = Crypto.derive(prk, "ekran sas");
            long n = (((sas[0] & 0xFFL) << 24) | ((sas[1] & 0xFF) << 16) | ((sas[2] & 0xFF) << 8) | (sas[3] & 0xFF))
                    % 1000000L;
            String code = String.format(java.util.Locale.US, "%03d %03d", n / 1000, n % 1000);
            // Pencere açıkken PC'nin kararını oku; PC reddederse pencere hemen kapanır.
            // Tablette reddedilirse bağlantı kapatılır, PC de hemen öğrenir.
            PairPrompt prompt = localLink ? null : host.showPairing(pcName, code, new Runnable() {
                @Override
                public void run() {
                    close();
                    try {
                        in.close();
                    } catch (IOException ignored) {
                    }
                }
            });
            boolean pcAccepted;
            try {
                len = readFrame();
                pcAccepted = (buf[0] & 0xFF) == MSG_PAIR_RESULT && len == 2 && buf[1] == 1;
            } catch (IOException e) {
                if (prompt != null) {
                    prompt.dismiss(); // PC vazgeçti / bağlantı koptu
                }
                throw e;
            }
            if (!pcAccepted && prompt != null) {
                prompt.dismiss();
            }
            boolean accepted = pcAccepted && (prompt == null || prompt.await());
            writeFrameNow(new byte[]{(byte) MSG_PAIR_RESULT, (byte) (accepted ? 1 : 0)});
            out.flush();
            if (!accepted) {
                throw new GeneralSecurityException("eşleştirme reddedildi");
            }
            store.save(pcId, Crypto.derive(prk, "ekran pair"), pcName);
            Log.i(TAG, "Eşleştirildi: " + pcName);
        }
        Log.i(TAG, "Oturum: " + pcName + (encrypt ? " (şifreli)" : " (USB, şifresiz)"));
    }

    private byte[] slice(int off, int n) {
        byte[] b = new byte[n];
        System.arraycopy(buf, off, b, 0, n);
        return b;
    }

    // --- Çerçeveleme ---

    /** Bir çerçeve okur; gövde (tür + yük) buf[0..len) içinde. */
    private int readFrame() throws IOException {
        int len = in.readInt();
        if (len < 1 || len > (1 << 24)) {
            throw new IOException("Geçersiz çerçeve uzunluğu " + len);
        }
        int total = len + (rx != null ? 16 : 0);
        if (total > buf.length) {
            buf = new byte[Math.max(total, buf.length * 2)];
        }
        in.readFully(buf, 0, total);
        if (rx != null && !rx.decrypt(rxSeq++, buf, 0, len, buf, len)) {
            throw new IOException("Güvenlik: mesaj doğrulanamadı");
        }
        return len;
    }

    /** Gövdeyi (tür + yük) çerçeveleyip hemen yazar; yalnızca el sıkışmada ve gönderim iş parçacığında. */
    private void writeFrameNow(byte[] body) throws IOException {
        int len = body.length;
        byte[] frame = new byte[4 + len + (tx != null ? 16 : 0)];
        writeInt(frame, 0, len);
        System.arraycopy(body, 0, frame, 4, len);
        if (tx != null) {
            tx.encrypt(txSeq++, frame, 4, len, frame, 4 + len);
        }
        out.write(frame);
    }

    private void sendLoop() {
        try {
            while (true) {
                byte[] body = sendQueue.take();
                if (body == STOP) {
                    return;
                }
                writeFrameNow(body);
                // Kuyrukta bekleyenleri de aynı anda yaz (daha az sistem çağrısı)
                while ((body = sendQueue.poll()) != null) {
                    if (body == STOP) {
                        out.flush();
                        return;
                    }
                    writeFrameNow(body);
                }
                out.flush();
            }
        } catch (InterruptedException ignored) {
        } catch (IOException e) {
            if (!closed) {
                Log.w(TAG, "Gönderim hatası", e);
            }
        }
    }

    // --- Mesajlar ---

    private void handle(int type, byte[] b, int o, int len) throws IOException {
        switch (type) {
            case MSG_VIDEO: {
                long pts = readLong(b, o);
                decoder.onAccessUnit(b, o + 9, len - 9, pts);
                break;
            }
            case MSG_CONFIG: {
                Config c = new Config();
                c.streamW = readU16(b, o);
                c.streamH = readU16(b, o + 2);
                // b[o+4]: codec (1 = H.264)
                c.srcW = readU16(b, o + 5);
                c.srcH = readU16(b, o + 7);
                c.contentX = readU16(b, o + 9);
                c.contentY = readU16(b, o + 11);
                c.contentW = readU16(b, o + 13);
                c.contentH = readU16(b, o + 15);
                host.onConfig(c);
                break;
            }
            case MSG_PING: {
                byte[] msg = body(MSG_PONG, 8);
                System.arraycopy(b, o, msg, 1, 8);
                sendQueue.offer(msg);
                break;
            }
            case MSG_CURSOR_SHAPE: {
                int w = readU16(b, o), h = readU16(b, o + 2), hx = readU16(b, o + 4), hy = readU16(b, o + 6);
                int n = w * h;
                if (8 + n * 4 > len) {
                    break;
                }
                int[] argb = new int[n];
                for (int i = 0, p = o + 8; i < n; i++, p += 4) {
                    argb[i] = ((b[p] & 0xFF) << 24) | ((b[p + 1] & 0xFF) << 16) | ((b[p + 2] & 0xFF) << 8)
                            | (b[p + 3] & 0xFF);
                }
                host.onCursorShape(w, h, hx, hy, argb);
                break;
            }
            case MSG_CURSOR_POS:
                host.onCursorPos((short) readU16(b, o), (short) readU16(b, o + 2), b[o + 4] != 0);
                break;
            case MSG_DISPLAY_POWER:
                if (len >= 1) {
                    host.onDisplayPower(b[o] != 0);
                }
                break;
            default:
                break; // bilinmeyen mesaj: yeni sürümle uyum için yok say
        }
    }

    @Override
    public void onFrameRendered(long pts, long decodeUs, boolean decoderIdle) {
        // Toplu onay: hareket sürerken 3 karede bir; çözücü boşalınca (son kare ekranda) hemen.
        // Yalnızca çözücü çıkış iş parçacığından çağrılır.
        if (++pendingAcks < 3 && !decoderIdle) {
            return;
        }
        byte[] msg = body(MSG_FRAME_ACK, 14);
        writeLong(msg, 1, pts);
        writeInt(msg, 9, (int) Math.min(decodeUs, Integer.MAX_VALUE));
        writeU16(msg, 13, pendingAcks);
        pendingAcks = 0;
        sendQueue.offer(msg);
    }

    void sendTouch(int action, int pointerId, int x, int y, int pressure) {
        if (closed) {
            return;
        }
        byte[] msg = body(MSG_TOUCH, 8);
        msg[1] = (byte) action;
        msg[2] = (byte) pointerId;
        writeU16(msg, 3, x);
        writeU16(msg, 5, y);
        writeU16(msg, 7, pressure);
        sendQueue.offer(msg);
    }

    /** Ekran kapandı: çözücüyü bırak, bağlantı sürer. */
    void suspendVideo() {
        decoder.suspend();
    }

    /** Ekran açıldı: yeni yüzeyle devam et, PC'den anahtar kare iste. */
    void resumeVideo(Surface surface) {
        decoder.resume(surface);
        if (!closed) {
            sendQueue.offer(body(MSG_KEY_REQUEST, 0));
        }
    }

    /** Tabletin tutuluş yönü (0, 90, 270): yatay ekrana göre saat yönünde dönüş. PC sanal monitörü döndürür. */
    void sendOrientation(int degrees) {
        if (closed) {
            return;
        }
        byte[] msg = body(MSG_ORIENTATION, 2);
        writeU16(msg, 1, degrees);
        sendQueue.offer(msg);
    }

    private void sendHello() throws IOException {
        ByteArrayOutputStream bos = new ByteArrayOutputStream();
        DataOutputStream d = new DataOutputStream(bos);
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics();
        android.graphics.Point real = new android.graphics.Point();
        host.displayInfo(real, dm);
        d.writeByte(MSG_HELLO);
        d.writeShort(VERSION);
        d.writeShort(host.surfaceWidth());
        d.writeShort(host.surfaceHeight());
        d.writeShort(real.x);
        d.writeShort(real.y);
        d.writeShort(dm.densityDpi);
        d.writeShort(Build.VERSION.SDK_INT);
        d.writeUTF(Build.MANUFACTURER + " " + Build.MODEL);
        String dec = H264Decoder.findDecoderName();
        d.writeUTF(dec != null ? dec : "-");
        sendQueue.offer(bos.toByteArray());
    }

    void close() {
        closed = true;
        sendQueue.offer(STOP);
    }

    private static byte[] body(int type, int payloadLen) {
        byte[] m = new byte[1 + payloadLen];
        m[0] = (byte) type;
        return m;
    }

    private static int readU16(byte[] b, int o) {
        return ((b[o] & 0xFF) << 8) | (b[o + 1] & 0xFF);
    }

    private static long readLong(byte[] b, int o) {
        long v = 0;
        for (int i = 0; i < 8; i++) {
            v = (v << 8) | (b[o + i] & 0xFF);
        }
        return v;
    }

    private static void writeU16(byte[] b, int o, int v) {
        b[o] = (byte) (v >> 8);
        b[o + 1] = (byte) v;
    }

    private static void writeInt(byte[] b, int o, int v) {
        b[o] = (byte) (v >> 24);
        b[o + 1] = (byte) (v >> 16);
        b[o + 2] = (byte) (v >> 8);
        b[o + 3] = (byte) v;
    }

    private static void writeLong(byte[] b, int o, long v) {
        for (int i = 7; i >= 0; i--) {
            b[o + i] = (byte) v;
            v >>= 8;
        }
    }
}
