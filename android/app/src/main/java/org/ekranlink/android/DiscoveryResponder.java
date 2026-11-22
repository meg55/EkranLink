package org.ekranlink.android;

import android.os.SystemClock;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;

/**
 * Ağda bulunma: PC UDP yayınıyla "EKRN?" sorar, tablet kendini tanıtan bir cevap döner.
 * Sadece soru gelince çalışır; boşta hiçbir paket göndermez (pil ve ağ dostu). PC hem ağ yayınıyla hem
 * (daha önce bağlandıysa) doğrudan bu tabletin adresine sorar. Cihaz bir bilgisayarla eşleştikten sonra
 * tanımadığı bilgisayarlara cevap vermez (Visibility).
 *
 * Soru:  "EKRN?" + u16 sürüm [+ 16 pcKimlik + str pcAdı (0.5.3+)]
 * Cevap: "EKRN!" + u16 sürüm + u16 tcpPort + str ad + str kimlik
 */
final class DiscoveryResponder extends Thread {
    private static final String TAG = "EkranDiscovery";

    /** Soran PC (0.5.3+ soruda adını ve kimliğini gönderir). */
    static final class Asker {
        final byte[] pcId;
        final String pcName;
        final long atMs; // SystemClock.elapsedRealtime

        Asker(byte[] pcId, String pcName, long atMs) {
            this.pcId = pcId;
            this.pcName = pcName;
            this.atMs = atMs;
        }
    }

    private final String name;
    private final String id;
    private final PairStore pairs;
    private volatile boolean stopped;
    private volatile DatagramSocket socket;
    private volatile Asker lastAsker;

    /** Son soran PC (yoksa null). */
    Asker lastAsker() {
        return lastAsker;
    }

    DiscoveryResponder(String name, String id, PairStore pairs) {
        super("ekran-discovery");
        this.name = name;
        this.id = id;
        this.pairs = pairs;
        setDaemon(true);
    }

    @Override
    public void run() {
        // Port başka bir uygulamadaysa boşalana kadar dene (bulunamamak sessiz bir arızadır)
        while (!stopped) {
            DatagramSocket s = null;
            try {
                s = new DatagramSocket(null);
                s.setReuseAddress(true);
                s.setBroadcast(true);
                s.bind(new InetSocketAddress(StreamServer.PORT));
                socket = s;
                break;
            } catch (SocketException e) {
                if (s != null) {
                    s.close();
                }
                Log.w(TAG, "UDP portu açılamadı", e);
                try {
                    Thread.sleep(3000);
                } catch (InterruptedException ie) {
                    return;
                }
            }
        }
        if (stopped) {
            if (socket != null) {
                socket.close();
            }
            return;
        }
        byte[] reply = buildReply();
        byte[] buf = new byte[256];
        DatagramPacket in = new DatagramPacket(buf, buf.length);
        while (!stopped) {
            try {
                in.setLength(buf.length);
                socket.receive(in);
                int n = in.getLength();
                if (n >= 5 && buf[0] == 'E' && buf[1] == 'K' && buf[2] == 'R' && buf[3] == 'N'
                        && buf[4] == '?') {
                    // 0.5.3+ PC'ler kimliğini ve adını da gönderir. Kimlik yalnızca eşleşme kaydıyla karşılaştırılır,
                    // ad yalnızca ekranda gösterilir (temizlenmiş)
                    byte[] pcId = null;
                    if (n >= 7 + 16 + 2) {
                        int nameLen = ((buf[23] & 0xFF) << 8) | (buf[24] & 0xFF);
                        if (nameLen <= n - 25) {
                            pcId = new byte[16];
                            System.arraycopy(buf, 7, pcId, 0, 16);
                            String pcName = Session.cleanName(new String(buf, 25, nameLen, "UTF-8"));
                            lastAsker = new Asker(pcId, pcName, SystemClock.elapsedRealtime());
                        }
                    }
                    // Eşleştikten sonra yalnızca tanıdığımız bilgisayarlara cevap ver (bkz. Visibility). Kimlik
                    // göndermeyen eski PC'ler ayırt edilemez: onlara cevap verilir.
                    if (pcId == null || Visibility.openToNewPcs(pairs.count()) || pairs.load(pcId) != null) {
                        socket.send(new DatagramPacket(reply, reply.length, in.getSocketAddress()));
                    }
                }
            } catch (IOException e) {
                if (!stopped) {
                    Log.w(TAG, "UDP", e);
                }
            }
        }
    }

    private byte[] buildReply() {
        try {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            DataOutputStream d = new DataOutputStream(bos);
            d.writeBytes("EKRN!");
            d.writeShort(Session.VERSION);
            d.writeShort(StreamServer.PORT);
            d.writeUTF(name);
            d.writeUTF(id);
            return bos.toByteArray();
        } catch (IOException e) {
            throw new IllegalStateException(e);
        }
    }

    void shutdown() {
        stopped = true;
        interrupt(); // port beklerken uyuyorsa
        DatagramSocket s = socket;
        if (s != null) {
            s.close();
        }
    }
}
