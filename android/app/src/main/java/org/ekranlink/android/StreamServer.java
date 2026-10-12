package org.ekranlink.android;

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
 * İlk 4 bayt "EKRN" ise protokol v1 oturumu; değilse ham H.264 (Faz 0 tanı modu). Ham mod kimlik doğrulamasız
 * olduğu için yalnızca yerel (127.0.0.1, adb forward) bağlantıda kabul edilir.
 */
final class StreamServer extends Thread {
    static final int PORT = 27183;
    private static final String TAG = "EkranServer";
    private static final byte[] MAGIC = {'E', 'K', 'R', 'N'};
    private static final int HANDSHAKE_TIMEOUT_MS = 15000; // kabulden sonra; Session el sıkışmadan sonra değiştirir

    private volatile Surface surface; // null: ekran kapalı (bağlantı sürer, görüntü duraklar)
    private final boolean headless;   // tanı: yüzeysiz çözme
    private final Stats stats;
    private final Session.Host host;
    private volatile boolean stopped;
    private boolean incompatible; // son bağlantı sürüm uyuşmazlığıyla bitti (yalnızca sunucu iş parçacığı)
    private volatile ServerSocket serverSocket;
    private Socket client;
    private volatile Session session;
    private volatile int rotation;    // pencere dikeyken görüntünün çevrilme açısı

    StreamServer(Surface surface, boolean headless, Stats stats, Session.Host host) {
        super("ekran-server");
        this.surface = surface;
        this.headless = headless;
        this.stats = stats;
        this.host = host;
    }

    Session session() {
        return session;
    }

    /** Pencere dikeyken görüntünün çevrilme açısı; sonraki oturumlar da kullanır. */
    void setVideoRotation(int degrees) {
        rotation = degrees;
        Session ses = session;
        if (ses != null) {
            ses.setVideoRotation(degrees);
        }
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
        // Port başka bir uygulamadaysa (ya da eski süreç henüz kapanmadıysa) boşalana kadar dene
        while (!stopped) {
            try {
                ServerSocket ss = new ServerSocket();
                ss.setReuseAddress(true);
                try {
                    ss.bind(new InetSocketAddress(PORT));
                } catch (IOException e) {
                    closeQuietly(ss);
                    throw e;
                }
                serverSocket = ss;
                break;
            } catch (IOException e) {
                stats.setStatus(Stats.ERROR, R.string.status_port_busy, PORT);
                Log.w(TAG, "bind", e);
                try {
                    Thread.sleep(3000);
                } catch (InterruptedException ie) {
                    return;
                }
            }
        }
        if (serverSocket == null) {
            return;
        }

        while (!stopped) {
            if (!incompatible) {
                stats.setStatus(Stats.WAITING, R.string.status_waiting);
            }
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
        if (!incompatible) {
            stats.setStatus(Stats.CONNECTING, R.string.status_connecting);
        }
        Log.i(TAG, "Bağlandı: " + peer);
        try {
            // Tek oturum yeri var: hiçbir şey göndermeyen bağlantı onu sonsuza dek tutmasın. Sihirli sözcük ve
            // sürüm bu süre içinde gelmeli; el sıkışmadan sonrasını Session kendisi ayarlar.
            s.setSoTimeout(HANDSHAKE_TIMEOUT_MS);
            s.setTcpNoDelay(true);
            s.setReceiveBufferSize(256 * 1024);
            // adb forward bağlantısı tablete 127.0.0.1'den gelir: USB kablosu = güvenilir, şifresiz olabilir.
            boolean local = s.getInetAddress() != null && s.getInetAddress().isLoopbackAddress();
            DataInputStream in = new DataInputStream(new BufferedInputStream(s.getInputStream(), 64 * 1024));
            byte[] head = new byte[4];
            in.readFully(head);
            if (head[0] == MAGIC[0] && head[1] == MAGIC[1] && head[2] == MAGIC[2] && head[3] == MAGIC[3]) {
                Surface sf = surface;
                Session ses = new Session(in, s.getOutputStream(), local, sf, stats, host);
                ses.setSocket(s); // el sıkışmadan sonra okuma zaman aşımını Session ayarlar
                if (sf == null && !headless) {
                    ses.suspendVideo(); // ekran kapalıyken bağlanıldı
                }
                session = ses;
                ses.setVideoRotation(rotation); // session atandıktan sonra: arada gelen değişiklik kaçmaz
                Surface now = surface;
                if (now != sf && now != null) {
                    ses.resumeVideo(now); // bu arada ekran açıldı
                }
                incompatible = false;
                ses.run();
            } else if (local) {
                runRaw(new SequenceInputStream(new ByteArrayInputStream(head), in));
            } else {
                // Ham akış kimlik doğrulamasız: ağdan gelen herkes ekrana görüntü basabilirdi
                Log.w(TAG, "Ham H.264 yalnızca yerel bağlantıda (adb forward) kabul edilir, reddedildi: " + peer);
            }
        } catch (Session.IncompatibleException e) {
            // PC birkaç saniyede bir yeniden dener: uyarı ekranda kalsın
            incompatible = true;
            stats.setStatus(Stats.ERROR, e.messageRes);
        } catch (Exception e) {
            incompatible = false;
            if (!stopped) {
                Log.w(TAG, "Akış hatası", e);
                stats.setStatus(Stats.ERROR, R.string.status_connection_lost);
            }
        } catch (OutOfMemoryError e) {
            // Bozuk ya da kötü niyetli akış bellek tüketti: yalnızca bu bağlantı kapanır, sunucu çalışmayı sürdürür
            incompatible = false;
            Log.e(TAG, "Bellek yetmedi, bağlantı kapatılıyor: " + peer, e);
            if (!stopped) {
                stats.setStatus(Stats.ERROR, R.string.status_connection_lost);
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
        interrupt(); // port beklerken uyuyorsa
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
