package org.ekranlink.android;

import android.util.Log;

import java.io.ByteArrayOutputStream;

/**
 * SPS'e VUI bitstream_restriction ekler/düzeltir: max_num_reorder_frames = 0 ve
 * max_dec_frame_buffering = max_num_ref_frames.
 *
 * Bu bilgi olmadan birçok donanım çözücü (ör. MediaTek) çıkış vermeden önce birkaç kare biriktirir;
 * B-kare kullanmadığımız için buna gerek yok. Ölçüm (Lenovo S6000, MTK): ~58 ms → hedef < 20 ms.
 */
final class SpsRewriter {
    private static final String TAG = "EkranSps";

    private SpsRewriter() {
    }

    /** @return düzeltilmiş SPS NAL'i (başlık baytı dahil); ayrıştırılamazsa orijinali */
    static byte[] lowLatency(byte[] nal) {
        try {
            return rewrite(nal);
        } catch (RuntimeException e) {
            Log.w(TAG, "SPS yeniden yazılamadı, orijinali kullanılıyor", e);
            return nal;
        }
    }

    private static byte[] rewrite(byte[] nal) {
        Bits r = new Bits(unescape(nal, 1));
        BitWriter w = new BitWriter();

        int profile = r.u(8);
        w.u(8, profile);
        w.u(8, r.u(8)); // constraint bayrakları
        w.u(8, r.u(8)); // level_idc
        w.ue(r.ue()); // sps_id

        if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44
                || profile == 83 || profile == 86 || profile == 118 || profile == 128
                || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
            int chroma = r.ue();
            w.ue(chroma);
            if (chroma == 3) {
                w.u(1, r.u(1));
            }
            w.ue(r.ue());
            w.ue(r.ue());
            w.u(1, r.u(1));
            int scaling = r.u(1);
            w.u(1, scaling);
            if (scaling == 1) {
                int lists = chroma != 3 ? 8 : 12;
                for (int i = 0; i < lists; i++) {
                    int present = r.u(1);
                    w.u(1, present);
                    if (present == 1) {
                        int size = i < 6 ? 16 : 64, last = 8, next = 8;
                        for (int j = 0; j < size; j++) {
                            if (next != 0) {
                                int delta = r.se();
                                w.se(delta);
                                next = (last + delta + 256) % 256;
                            }
                            last = next == 0 ? last : next;
                        }
                    }
                }
            }
        }

        w.ue(r.ue()); // log2_max_frame_num_minus4
        int pocType = r.ue();
        w.ue(pocType);
        if (pocType == 0) {
            w.ue(r.ue());
        } else if (pocType == 1) {
            w.u(1, r.u(1));
            w.se(r.se());
            w.se(r.se());
            int cycle = r.ue();
            w.ue(cycle);
            for (int i = 0; i < cycle; i++) {
                w.se(r.se());
            }
        }
        int maxRefFrames = r.ue();
        w.ue(maxRefFrames);
        w.u(1, r.u(1)); // gaps
        w.ue(r.ue()); // width
        w.ue(r.ue()); // height
        int frameMbsOnly = r.u(1);
        w.u(1, frameMbsOnly);
        if (frameMbsOnly == 0) {
            w.u(1, r.u(1));
        }
        w.u(1, r.u(1)); // direct_8x8
        int crop = r.u(1);
        w.u(1, crop);
        if (crop == 1) {
            for (int i = 0; i < 4; i++) {
                w.ue(r.ue());
            }
        }

        int vuiPresent = r.u(1);
        w.u(1, 1);
        if (vuiPresent == 1) {
            copyVuiUntilRestriction(r, w);
            if (r.u(1) == 1) {
                // Mevcut kısıtlamaları oku, sıralama/tampon değerlerini ez.
                int mvOver = r.u(1);
                int bytesPerPic = r.ue();
                int bitsPerMb = r.ue();
                int mvH = r.ue();
                int mvV = r.ue();
                r.ue();
                r.ue();
                writeRestriction(w, mvOver, bytesPerPic, bitsPerMb, mvH, mvV, maxRefFrames);
            } else {
                writeRestriction(w, 1, 2, 1, 16, 16, maxRefFrames);
            }
        } else {
            w.u(1, 0); // aspect_ratio_info_present
            w.u(1, 0); // overscan_info_present
            w.u(1, 0); // video_signal_type_present
            w.u(1, 0); // chroma_loc_info_present
            w.u(1, 0); // timing_info_present
            w.u(1, 0); // nal_hrd
            w.u(1, 0); // vcl_hrd
            w.u(1, 0); // pic_struct_present
            writeRestriction(w, 1, 2, 1, 16, 16, maxRefFrames);
        }

        w.trailingBits();
        byte[] rbsp = w.toByteArray();
        byte[] escaped = escape(rbsp);
        byte[] out = new byte[escaped.length + 1];
        out[0] = nal[0];
        System.arraycopy(escaped, 0, out, 1, escaped.length);
        return out;
    }

    private static void writeRestriction(BitWriter w, int mvOver, int bytesPerPic, int bitsPerMb,
                                         int mvH, int mvV, int maxRefFrames) {
        w.u(1, 1); // bitstream_restriction_flag
        w.u(1, mvOver);
        w.ue(bytesPerPic);
        w.ue(bitsPerMb);
        w.ue(mvH);
        w.ue(mvV);
        w.ue(0);                           // max_num_reorder_frames
        w.ue(Math.max(1, maxRefFrames));   // max_dec_frame_buffering
    }

    /** VUI alanlarını bitstream_restriction_flag'e kadar aynen kopyalar. */
    private static void copyVuiUntilRestriction(Bits r, BitWriter w) {
        if (copyFlag(r, w)) { // aspect_ratio_info_present
            int idc = r.u(8);
            w.u(8, idc);
            if (idc == 255) {
                w.u(16, r.u(16));
                w.u(16, r.u(16));
            }
        }
        if (copyFlag(r, w)) { // overscan_info_present
            w.u(1, r.u(1));
        }
        if (copyFlag(r, w)) { // video_signal_type_present
            w.u(3, r.u(3));
            w.u(1, r.u(1));
            if (copyFlag(r, w)) { // colour_description_present
                w.u(8, r.u(8));
                w.u(8, r.u(8));
                w.u(8, r.u(8));
            }
        }
        if (copyFlag(r, w)) { // chroma_loc_info_present
            w.ue(r.ue());
            w.ue(r.ue());
        }
        if (copyFlag(r, w)) { // timing_info_present
            w.u(16, r.u(16));
            w.u(16, r.u(16)); // num_units_in_tick (32 bit)
            w.u(16, r.u(16));
            w.u(16, r.u(16)); // time_scale (32 bit)
            w.u(1, r.u(1));
        }
        boolean nalHrd = copyFlag(r, w);
        if (nalHrd) {
            copyHrd(r, w);
        }
        boolean vclHrd = copyFlag(r, w);
        if (vclHrd) {
            copyHrd(r, w);
        }
        if (nalHrd || vclHrd) {
            w.u(1, r.u(1)); // low_delay_hrd_flag
        }
        w.u(1, r.u(1)); // pic_struct_present_flag
    }

    private static void copyHrd(Bits r, BitWriter w) {
        int cpbCnt = r.ue();
        w.ue(cpbCnt);
        w.u(4, r.u(4));
        w.u(4, r.u(4));
        for (int i = 0; i <= cpbCnt; i++) {
            w.ue(r.ue());
            w.ue(r.ue());
            w.u(1, r.u(1));
        }
        w.u(5, r.u(5));
        w.u(5, r.u(5));
        w.u(5, r.u(5));
        w.u(5, r.u(5));
    }

    private static boolean copyFlag(Bits r, BitWriter w) {
        int f = r.u(1);
        w.u(1, f);
        return f == 1;
    }

    private static byte[] unescape(byte[] nal, int offset) {
        ByteArrayOutputStream out = new ByteArrayOutputStream(nal.length);
        int zeros = 0;
        for (int i = offset; i < nal.length; i++) {
            int b = nal[i] & 0xFF;
            if (zeros >= 2 && b == 3) {
                zeros = 0;
                continue;
            }
            zeros = b == 0 ? zeros + 1 : 0;
            out.write(b);
        }
        return out.toByteArray();
    }

    private static byte[] escape(byte[] rbsp) {
        ByteArrayOutputStream out = new ByteArrayOutputStream(rbsp.length + 8);
        int zeros = 0;
        for (byte value : rbsp) {
            int b = value & 0xFF;
            if (zeros >= 2 && b <= 3) {
                out.write(3);
                zeros = 0;
            }
            out.write(b);
            zeros = b == 0 ? zeros + 1 : 0;
        }
        return out.toByteArray();
    }

    private static final class Bits {
        private final byte[] data;
        private int pos;

        Bits(byte[] data) {
            this.data = data;
        }

        int u(int n) {
            int v = 0;
            for (int i = 0; i < n; i++) {
                int idx = pos >> 3;
                if (idx >= data.length) {
                    throw new IllegalStateException("SPS beklenenden kısa");
                }
                v = (v << 1) | ((data[idx] >> (7 - (pos & 7))) & 1);
                pos++;
            }
            return v;
        }

        int ue() {
            int zeros = 0;
            while (u(1) == 0) {
                if (++zeros > 31) {
                    throw new IllegalStateException("Geçersiz Exp-Golomb");
                }
            }
            return zeros == 0 ? 0 : (1 << zeros) - 1 + u(zeros);
        }

        int se() {
            int k = ue();
            return (k & 1) == 1 ? (k + 1) / 2 : -(k / 2);
        }
    }

    private static final class BitWriter {
        private final ByteArrayOutputStream out = new ByteArrayOutputStream(64);
        private int cur, n;

        void u(int bits, int value) {
            for (int i = bits - 1; i >= 0; i--) {
                cur = (cur << 1) | ((value >> i) & 1);
                if (++n == 8) {
                    out.write(cur);
                    cur = 0;
                    n = 0;
                }
            }
        }

        void ue(int value) {
            int v = value + 1;
            int len = 32 - Integer.numberOfLeadingZeros(v);
            u(len - 1, 0);
            u(len, v);
        }

        void se(int value) {
            ue(value > 0 ? 2 * value - 1 : -2 * value);
        }

        void trailingBits() {
            u(1, 1);
            while (n != 0) {
                u(1, 0);
            }
        }

        byte[] toByteArray() {
            return out.toByteArray();
        }
    }
}
