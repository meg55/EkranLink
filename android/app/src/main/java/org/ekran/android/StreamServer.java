package org.ekran.android;

import android.util.Log;
import android.view.Surface;

import java.io.BufferedInputStream;
import java.io.ByteArrayInputStream;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.SequenceInputStream;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;

/**
 * TCP sunucusu: PC bağlanır.
 *
 * Android 4.x'te `adb reverse` olmadığı için bağlantıyı her zaman PC başlatır:
 * USB'de `adb forward tcp:27183 tcp:27183`, Wi-Fi'de doğrudan tabletin IP'si.
 *
 * İlk 4 bayt "EKRN" ise protokol v1 oturumu; değilse ham H.264 (Faz 0 tanı modu).
 */
final class StreamServer extends Thread {
    static final int PORT = 27183;
    private static final String TAG = "EkranServer";
    private static final byte[] MAGIC = {'E', 'K', 'R', 'N'};

    private volatile Surface surface; // null: ekran kapalı (bağlantı sürer, görüntü duraklar)
    private final boolean headless;   // tanı: yüzeysiz çözme
    private final Stats stats;
    private final Session.Host host;
    private volatile boolean stopped;
    private ServerSocket serverSocket;
    private Socket client;
    private volatile Session session;

    StreamServer(Surface surface, Stats stats, Session.Host host) {
        super("ekran-server");
        this.surface = surface;
        this.headless = surface == null;
        this.stats = stats;
        this.host = host;
    }

    Session session() {
        return session;
    }

    /** Yüzey değişti (ekran kapandı: null, açıldı: yeni yüzey). Oturum varsa kopmadan sürer. */
    void setSurface(Surface s) {
        surface = s;
        Session ses = session;
        if (ses != null) {
            if (s == null) {
                ses.suspendVideo();
            } else {
                ses.resumeVideo(s);
            }
        }
    }

    @Override
    public void run() {
        try {
            serverSocket = new ServerSocket();
            serverSocket.setReuseAddress(true);
            serverSocket.bind(new InetSocketAddress(PORT));
        } catch (IOException e) {
            stats.setStatus(Stats.ERROR, "Port açılamadı: " + e.getMessage());
            Log.e(TAG, "bind", e);
            return;
        }

        while (!stopped) {
            stats.setStatus(Stats.WAITING, "Bilgisayar bekleniyor");
            Socket s;
            try {
                s = serverSocket.accept();
            } catch (IOException e) {
                if (!stopped) {
                    Log.w(TAG, "accept", e);
                }
                break;
            }
            synchronized (this) {
                client = s;
            }
            handle(s);
            synchronized (this) {
                client = null;
            }
        }
        closeQuietly(serverSocket);
    }

    private void handle(Socket s) {
        String peer = String.valueOf(s.getRemoteSocketAddress());
        stats.resetSession();
        stats.setStatus(Stats.CONNECTING, "Bağlanıyor…");
        Log.i(TAG, "Bağlandı: " + peer);
        try {
            s.setTcpNoDelay(true);
            s.setReceiveBufferSize(256 * 1024);
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream(), 64 * 1024));
            byte[] head = new byte[4];
            in.readFully(head);
            if (head[0] == MAGIC[0] && head[1] == MAGIC[1] && head[2] == MAGIC[2] && head[3] == MAGIC[3]) {
                // adb forward bağlantısı tablete 127.0.0.1'den gelir: USB kablosu = güvenilir, şifresiz olabilir.
                boolean local = s.getInetAddress() != null && s.getInetAddress().isLoopbackAddress();
                Surface sf = surface;
                Session ses = new Session(in, s.getOutputStream(), local, sf, stats, host);
                if (sf == null && !headless) {
                    ses.suspendVideo(); // ekran kapalıyken bağlanıldı
                }
                session = ses;
                Surface now = surface;
                if (now != sf && now != null) {
                    ses.resumeVideo(now); // bu arada ekran açıldı
                }
                ses.run();
            } else {
                runRaw(new SequenceInputStream(new ByteArrayInputStream(head), in));
            }
        } catch (Exception e) {
            if (!stopped) {
                Log.w(TAG, "Akış hatası", e);
                stats.setStatus(Stats.ERROR, "Bağlantı koptu");
            }
        } finally {
            session = null;
            closeQuietly(s);
            Log.i(TAG, "Bağlantı kapandı: " + peer);
        }
    }

    /** Faz 0 tanı modu: ham Annex-B H.264 (ör. ffmpeg). */
    private void runRaw(java.io.InputStream in) throws IOException {
        H264Decoder decoder = new H264Decoder(surface, stats, null);
        try {
            AnnexBReader reader = new AnnexBReader(in);
            byte[] nal;
            while (!stopped && (nal = reader.next()) != null) {
                decoder.onNal(nal);
            }
            decoder.finish();
        } finally {
            decoder.release();
        }
    }

    void shutdown() {
        stopped = true;
        Session ses = session;
        if (ses != null) {
            ses.close();
        }
        closeQuietly(serverSocket);
        synchronized (this) {
            closeQuietly(client);
        }
        try {
            join(1000);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    // API 16'da Socket/ServerSocket Closeable değil; ayrı ayrı kapatıyoruz.
    private static void closeQuietly(Socket s) {
        if (s != null) {
            try {
                s.close();
            } catch (IOException ignored) {
            }
        }
    }

    private static void closeQuietly(ServerSocket s) {
        if (s != null) {
            try {
                s.close();
            } catch (IOException ignored) {
            }
        }
    }
}
