package org.ekranlink.android;

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
 * (daha önce bağlandıysa) doğrudan bu tabletin adresine sorar.
 *
 * Soru:  "EKRN?" + u16 sürüm
 * Cevap: "EKRN!" + u16 sürüm + u16 tcpPort + str ad + str kimlik
 */
final class DiscoveryResponder extends Thread {
    private static final String TAG = "EkranDiscovery";

    private final String name;
    private final String id;
    private volatile boolean stopped;
    private volatile DatagramSocket socket;

    DiscoveryResponder(String name, String id) {
        super("ekran-discovery");
        this.name = name;
        this.id = id;
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
        byte[] buf = new byte[64];
        DatagramPacket in = new DatagramPacket(buf, buf.length);
        while (!stopped) {
            try {
                in.setLength(buf.length);
                socket.receive(in);
                if (in.getLength() >= 5 && buf[0] == 'E' && buf[1] == 'K' && buf[2] == 'R' && buf[3] == 'N'
                        && buf[4] == '?') {
                    socket.send(new DatagramPacket(reply, reply.length, in.getSocketAddress()));
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
