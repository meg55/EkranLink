package org.ekranlink.android;

import java.io.IOException;
import java.io.InputStream;

/**
 * Ham H.264 Annex-B akışını (00 00 01 / 00 00 00 01 başlangıç kodlu) NAL birimlerine böler.
 *
 * Bir NAL'in bittiği ancak bir sonraki başlangıç kodu geldiğinde anlaşılır; bu yüzden ham akışta
 * en fazla bir karelik gecikme vardır. Faz 1 protokolünde kareler uzunluk önekli gelecek.
 */
final class AnnexBReader {
    /** Tek NAL için üst sınır: bozuk ya da kötü niyetli akış belleği sınırsız büyütmesin. */
    static final int MAX_NAL = 8 * 1024 * 1024;

    private final InputStream in;
    private byte[] buf = new byte[512 * 1024];
    private int end;        // buf[0..end) geçerli veri
    private int scan;       // başlangıç kodu aramasının kaldığı yer
    private int nalStart = -1; // mevcut NAL'in ilk baytı (başlangıç kodundan sonra), yoksa -1
    private boolean eof;

    AnnexBReader(InputStream in) {
        this.in = in;
    }

    /** Sonraki NAL'i (başlangıç kodu olmadan) döndürür; akış bittiyse null. */
    byte[] next() throws IOException {
        while (true) {
            int p = findStartCode(scan, end);
            if (p >= 0) {
                if (nalStart < 0) {
                    nalStart = p + 3;
                    scan = nalStart;
                    continue;
                }
                int nalEnd = p;
                while (nalEnd > nalStart && buf[nalEnd - 1] == 0) {
                    nalEnd--; // 4 baytlık başlangıç kodunun baştaki sıfırı / trailing_zero
                }
                byte[] nal = copy(nalStart, nalEnd);
                nalStart = p + 3;
                scan = nalStart;
                if (nal.length > 0) {
                    return nal;
                }
                continue;
            }

            scan = Math.max(scan, end - 2);
            if (eof) {
                if (nalStart >= 0 && end > nalStart) {
                    byte[] nal = copy(nalStart, end);
                    nalStart = end;
                    return nal;
                }
                return null;
            }
            fill();
        }
    }

    private int findStartCode(int from, int to) {
        byte[] b = buf;
        for (int i = from; i + 2 < to; i++) {
            if ((b[i + 2] & 0xFF) > 1) {
                i += 2; // hızlı atlama: 3. bayt 0/1 değilse bu konumlar başlangıç kodu olamaz
                continue;
            }
            if (b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1) {
                return i;
            }
        }
        return -1;
    }

    private void fill() throws IOException {
        // Tüketilmiş veriyi at, kalan veriyi başa kaydır.
        int keepFrom = nalStart >= 0 ? nalStart : Math.max(0, end - 2);
        if (keepFrom > 0) {
            System.arraycopy(buf, keepFrom, buf, 0, end - keepFrom);
            end -= keepFrom;
            scan -= keepFrom;
            if (nalStart >= 0) {
                nalStart -= keepFrom;
            }
            if (scan < 0) {
                scan = 0;
            }
        }
        if (end == buf.length) {
            // Tampon yalnızca tek bir NAL'le doluysa büyür (nalStart = 0); sınıra ulaştıysa bağlantı kapanır.
            if (buf.length >= MAX_NAL) {
                throw new IOException("NAL birimi " + MAX_NAL + " bayttan büyük");
            }
            byte[] bigger = new byte[Math.min(buf.length * 2, MAX_NAL)];
            System.arraycopy(buf, 0, bigger, 0, end);
            buf = bigger;
        }
        int n = in.read(buf, end, buf.length - end);
        if (n < 0) {
            eof = true;
        } else {
            end += n;
        }
    }

    private byte[] copy(int from, int to) {
        byte[] out = new byte[to - from];
        System.arraycopy(buf, from, out, 0, out.length);
        return out;
    }
}
