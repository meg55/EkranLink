package org.ekran.android;

/** H.264 SPS'ten görüntü boyutunu ve profilini okur (MediaCodec yapılandırması için). */
final class SpsParser {
    final int profileIdc;
    final int levelIdc;
    final int width;
    final int height;

    private SpsParser(int profileIdc, int levelIdc, int width, int height) {
        this.profileIdc = profileIdc;
        this.levelIdc = levelIdc;
        this.width = width;
        this.height = height;
    }

    /** @param nal başlangıç kodu olmadan, NAL başlık baytı dahil SPS */
    static SpsParser parse(byte[] nal) {
        BitReader r = new BitReader(unescape(nal, 1));
        int profile = r.u(8);
        r.u(8); // constraint_set bayrakları
        int level = r.u(8);
        r.ue(); // seq_parameter_set_id

        int chromaFormat = 1;
        if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44
                || profile == 83 || profile == 86 || profile == 118 || profile == 128
                || profile == 138 || profile == 139 || profile == 134 || profile == 135) {
            chromaFormat = r.ue();
            if (chromaFormat == 3) {
                r.u(1); // separate_colour_plane_flag
            }
            r.ue(); // bit_depth_luma_minus8
            r.ue(); // bit_depth_chroma_minus8
            r.u(1); // qpprime_y_zero_transform_bypass_flag
            if (r.u(1) == 1) { // seq_scaling_matrix_present_flag
                int lists = chromaFormat != 3 ? 8 : 12;
                for (int i = 0; i < lists; i++) {
                    if (r.u(1) == 1) {
                        skipScalingList(r, i < 6 ? 16 : 64);
                    }
                }
            }
        }

        r.ue(); // log2_max_frame_num_minus4
        int pocType = r.ue();
        if (pocType == 0) {
            r.ue(); // log2_max_pic_order_cnt_lsb_minus4
        } else if (pocType == 1) {
            r.u(1);
            r.se();
            r.se();
            int cycle = r.ue();
            for (int i = 0; i < cycle; i++) {
                r.se();
            }
        }
        r.ue(); // max_num_ref_frames
        r.u(1); // gaps_in_frame_num_value_allowed_flag
        int widthMbs = r.ue() + 1;
        int heightMapUnits = r.ue() + 1;
        int frameMbsOnly = r.u(1);
        if (frameMbsOnly == 0) {
            r.u(1); // mb_adaptive_frame_field_flag
        }
        r.u(1); // direct_8x8_inference_flag

        int width = widthMbs * 16;
        int height = (2 - frameMbsOnly) * heightMapUnits * 16;
        if (r.u(1) == 1) { // frame_cropping_flag
            int left = r.ue(), right = r.ue(), top = r.ue(), bottom = r.ue();
            int cropX = chromaFormat == 0 ? 1 : (chromaFormat == 3 ? 1 : 2);
            int cropY = (chromaFormat == 0 ? 1 : (chromaFormat == 1 ? 2 : 1)) * (2 - frameMbsOnly);
            width -= cropX * (left + right);
            height -= cropY * (top + bottom);
        }
        return new SpsParser(profile, level, width, height);
    }

    private static void skipScalingList(BitReader r, int size) {
        int last = 8, next = 8;
        for (int j = 0; j < size; j++) {
            if (next != 0) {
                next = (last + r.se() + 256) % 256;
            }
            last = next == 0 ? last : next;
        }
    }

    /** Emülasyon önleme baytlarını (00 00 03) kaldırır. */
    private static byte[] unescape(byte[] nal, int offset) {
        byte[] out = new byte[nal.length - offset];
        int n = 0, zeros = 0;
        for (int i = offset; i < nal.length; i++) {
            byte b = nal[i];
            if (zeros >= 2 && b == 3) {
                zeros = 0;
                continue;
            }
            zeros = b == 0 ? zeros + 1 : 0;
            out[n++] = b;
        }
        byte[] trimmed = new byte[n];
        System.arraycopy(out, 0, trimmed, 0, n);
        return trimmed;
    }

    private static final class BitReader {
        private final byte[] data;
        private int pos; // bit konumu

        BitReader(byte[] data) {
            this.data = data;
        }

        int u(int bits) {
            int v = 0;
            for (int i = 0; i < bits; i++) {
                int byteIdx = pos >> 3;
                int bit = byteIdx < data.length ? (data[byteIdx] >> (7 - (pos & 7))) & 1 : 0;
                v = (v << 1) | bit;
                pos++;
            }
            return v;
        }

        int ue() {
            int zeros = 0;
            while (u(1) == 0 && zeros < 32) {
                zeros++;
            }
            return zeros == 0 ? 0 : ((1 << zeros) - 1 + u(zeros));
        }

        int se() {
            int k = ue();
            return (k & 1) == 1 ? (k + 1) / 2 : -(k / 2);
        }
    }
}
