#ifndef H264PARAMETERSET_H
#define H264PARAMETERSET_H

#include "VHHelper.h"
#include "vk_video/vulkan_video_codecs_common.h"
#include "vk_video/vulkan_video_codec_h264std.h"

namespace h264
{
    // from Nvidia sample code

    static const uint32_t H264MbSizeAlignment = 16;

    template<typename sizeType>
    static sizeType AlignSize(sizeType size, sizeType alignment) {
        assert((alignment & (alignment - 1)) == 0);
        return (size + alignment - 1) & ~(alignment - 1);
    }

    static StdVideoH264SequenceParameterSetVui getStdVideoH264SequenceParameterSetVui(uint32_t fps)
    {
        StdVideoH264SpsVuiFlags vuiFlags = {};
        vuiFlags.timing_info_present_flag = 1u;
        vuiFlags.fixed_frame_rate_flag = 1u;

        StdVideoH264SequenceParameterSetVui vui = {};
        vui.flags = vuiFlags;
        vui.num_units_in_tick = 1;
        vui.time_scale = fps * 2; // 2 fields

        return vui;
    }

    static StdVideoH264SequenceParameterSet getStdVideoH264SequenceParameterSet(uint32_t width, uint32_t height,
        StdVideoH264SequenceParameterSetVui* pVui)
    {
        StdVideoH264SpsFlags spsFlags = {};
        spsFlags.direct_8x8_inference_flag = 1u;
        spsFlags.frame_mbs_only_flag = 1u;
        spsFlags.vui_parameters_present_flag = (pVui == NULL) ? 0u : 1u;

        const uint32_t mbAlignedWidth = AlignSize(width, H264MbSizeAlignment);
        const uint32_t mbAlignedHeight = AlignSize(height, H264MbSizeAlignment);

        StdVideoH264SequenceParameterSet sps = {};
        sps.profile_idc = STD_VIDEO_H264_PROFILE_IDC_MAIN;
        sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_1;
        sps.seq_parameter_set_id = 0u;
        sps.chroma_format_idc = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
        sps.bit_depth_luma_minus8 = 0u;
        sps.bit_depth_chroma_minus8 = 0u;
        sps.log2_max_frame_num_minus4 = 0u;
        sps.pic_order_cnt_type = STD_VIDEO_H264_POC_TYPE_0;
        sps.max_num_ref_frames = 1u;
        sps.pic_width_in_mbs_minus1 = mbAlignedWidth / H264MbSizeAlignment - 1;
        sps.pic_height_in_map_units_minus1 = mbAlignedHeight / H264MbSizeAlignment - 1;
        sps.flags = spsFlags;
        sps.pSequenceParameterSetVui = pVui;
        sps.frame_crop_right_offset = mbAlignedWidth - width;
        sps.frame_crop_bottom_offset = mbAlignedHeight - height;

        // This allows for picture order count values in the range [0, 255].
        sps.log2_max_pic_order_cnt_lsb_minus4 = 4u;

        if (sps.frame_crop_right_offset || sps.frame_crop_bottom_offset) {

            sps.flags.frame_cropping_flag = true;

            if (sps.chroma_format_idc == STD_VIDEO_H264_CHROMA_FORMAT_IDC_420) {
                sps.frame_crop_right_offset >>= 1;
                sps.frame_crop_bottom_offset >>= 1;
            }
        }

        return sps;
    }

    static StdVideoH264PictureParameterSet getStdVideoH264PictureParameterSet(void)
    {
        StdVideoH264PpsFlags ppsFlags = {};
        //ppsFlags.transform_8x8_mode_flag = 1u;
        ppsFlags.transform_8x8_mode_flag = 0u;
        ppsFlags.constrained_intra_pred_flag = 0u;
        ppsFlags.deblocking_filter_control_present_flag = 1u;
        ppsFlags.entropy_coding_mode_flag = 1u;

        StdVideoH264PictureParameterSet pps = {};
        pps.seq_parameter_set_id = 0u;
        pps.pic_parameter_set_id = 0u;
        pps.num_ref_idx_l0_default_active_minus1 = 0u;
        pps.flags = ppsFlags;

        return pps;
    }


    class DecodeFrameInfo {
    public:
        DecodeFrameInfo(uint32_t frameCount, uint32_t width, uint32_t height, StdVideoH264SequenceParameterSet sps, StdVideoH264PictureParameterSet pps, bool isI)
        {
            m_decodeH264pictureInfo.flags.field_pic_flag = 0;
            m_decodeH264pictureInfo.flags.is_intra = isI ? 1 : 0;
            m_decodeH264pictureInfo.flags.IdrPicFlag = 1;
            m_decodeH264pictureInfo.flags.bottom_field_flag = 0;
            m_decodeH264pictureInfo.flags.is_reference = 0;
            m_decodeH264pictureInfo.flags.complementary_field_pair = 0;
            m_decodeH264pictureInfo.seq_parameter_set_id = sps.seq_parameter_set_id;
            m_decodeH264pictureInfo.pic_parameter_set_id = pps.pic_parameter_set_id;
            m_decodeH264pictureInfo.frame_num = 0;
            m_decodeH264pictureInfo.idr_pic_id = 0;
            m_decodeH264pictureInfo.PicOrderCnt[0] = 0;
            m_decodeH264pictureInfo.PicOrderCnt[1] = 1;
        }

        inline StdVideoDecodeH264PictureInfo* getDecodeH264FrameInfo()
        {
            return &m_decodeH264pictureInfo;
        };

    private:
        StdVideoDecodeH264PictureInfo m_decodeH264pictureInfo = {};
    };

    class BitStream
    {
    public:
        void appendBit(bool bit)
        {
            if (m_bitPos < 0) {
                m_data.push_back(0);
                m_bitPos = 7;
            }
            if (bit)
                m_data.back() |= 1 << m_bitPos;
            --m_bitPos;
        }

        void appendBits(uint64_t data, uint8_t len) {
            for (int8_t i = len - 1; i >= 0; i--) {
                appendBit((data >> i) & 1);
            }
        }

        // Exponential-Golomb coding
        void appendExpG(uint32_t val) {
            uint32_t valinc = val + 1;
            uint32_t numbits = static_cast<uint32_t>(std::log2(valinc) + 1);
            appendBits(0x0, numbits - 1);
            appendBits(valinc, numbits);
        }

        void appendExpGSigned(int32_t val) {
            appendExpG(val <= 0 ? 2 * std::abs(val) : 2 * std::abs(val) - 1);
        }

        void padByte() {
            m_bitPos = -1;
        }

        void clear() {
            m_data.clear();
            m_bitPos = -1;
        }

        bool empty() const {
            return m_data.empty();
        }

        const uint8_t* data() const {
            return m_data.data();
        }

        const size_t size() const {
            return m_data.size();
        }

    private:
        std::vector<uint8_t> m_data;
        int8_t m_bitPos{ -1 };
    };

    static void encodeSps(const StdVideoH264SequenceParameterSet& sps, BitStream& out) {
        
        // this will only work with BASELINE or MAIN profile

        out.appendBits(0x00000001, 32); // NAL header
        out.appendBits(0x0, 1); // forbidden_bit
        out.appendBits(0x3, 2); // nal_ref_idc
        out.appendBits(0x7, 5); // nal_unit_type : 7 ( SPS )
        out.appendBits(sps.profile_idc, 8); // profile_idc
        out.appendBits(sps.flags.constraint_set0_flag, 1); // constraint_set0_flag
        out.appendBits(sps.flags.constraint_set1_flag, 1); // constraint_set1_flag
        out.appendBits(sps.flags.constraint_set2_flag, 1); // constraint_set2_flag
        out.appendBits(sps.flags.constraint_set3_flag, 1); // constraint_set3_flag
        out.appendBits(sps.flags.constraint_set4_flag, 1); // constraint_set4_flag
        out.appendBits(sps.flags.constraint_set5_flag, 1); // constraint_set5_flag
        out.appendBits(0x0, 2); // reserved_zero_2bits /* equal to 0 */
        out.appendBits(sps.level_idc, 8); // level_idc: 3.1 (0x0a)
        out.appendExpG(sps.seq_parameter_set_id);  // seq_parameter_set_id
        out.appendExpG(sps.log2_max_frame_num_minus4); // log2_max_frame_num_minus4
        out.appendExpG(sps.pic_order_cnt_type); // pic_order_cnt_type
        out.appendExpG(sps.log2_max_pic_order_cnt_lsb_minus4); // log2_max_pic_order_cnt_lsb_minus4
        out.appendExpG(sps.max_num_ref_frames); // max_num_refs_frames
        out.appendBits(sps.flags.gaps_in_frame_num_value_allowed_flag, 1); // gaps_in_frame_num_value_allowed_flag
        out.appendExpG(sps.pic_width_in_mbs_minus1); // pic_width_in_mbs_minus1
        out.appendExpG(sps.pic_height_in_map_units_minus1); // pic_height_in_map_units_minus_1
        out.appendBits(sps.flags.frame_mbs_only_flag, 1); // frame_mbs_only_flag
        out.appendBits(sps.flags.direct_8x8_inference_flag, 1); // direct_8x8_interfernce
        out.appendBits(sps.flags.frame_cropping_flag, 1); // frame_cropping_flag
        if (sps.flags.frame_cropping_flag) {
            out.appendExpG(sps.frame_crop_left_offset); // frame_crop_left_offset
            out.appendExpG(sps.frame_crop_right_offset); // frame_crop_right_offset
            out.appendExpG(sps.frame_crop_top_offset); // frame_crop_top_offset
            out.appendExpG(sps.frame_crop_bottom_offset); // frame_crop_bottom_offset
        }
        out.appendBits(sps.flags.vui_parameters_present_flag, 1); // vui_parameter_present
        if (sps.flags.vui_parameters_present_flag) {
            out.appendBits(0x0, 1); // aspect_ratio_info_present_flag
            out.appendBits(0x0, 1); // overscan_info_present_flag 
            out.appendBits(0x0, 1); // video_signal_type_present_flag
            out.appendBits(0x0, 1); // chroma_loc_info_present_flag
            out.appendBits(sps.pSequenceParameterSetVui->flags.timing_info_present_flag, 1); // timing_info_present_flag
            if (sps.pSequenceParameterSetVui->flags.timing_info_present_flag) {
                out.appendBits(sps.pSequenceParameterSetVui->num_units_in_tick, 32); // num_units_in_tick
                out.appendBits(sps.pSequenceParameterSetVui->time_scale, 32); // time_scale
                out.appendBits(sps.pSequenceParameterSetVui->flags.fixed_frame_rate_flag, 1); // fixed_frame_rate_flag
            }
            out.appendBits(0x0, 1); // nal_hrd_parameters_present_flag
            out.appendBits(0x0, 1); // vcl_hrd_parameters_present_flag
            out.appendBits(0x0, 1); // pic_struct_present_flag
            out.appendBits(0x0, 1); // bitstream_restriction_flag
        }
        out.appendBits(0x1, 1); // rbsp stop bit
        out.padByte();
    }

    static void encodePps(const StdVideoH264PictureParameterSet& pps, BitStream& out) {
        out.appendBits(0x00000001, 32); // NAL header
        out.appendBits(0x0, 1); // forbidden_bit
        out.appendBits(0x3, 2); // nal_ref_idc
        out.appendBits(0x8, 5); // nal_unit_type : 8 ( PPS )
        out.appendExpG(pps.pic_parameter_set_id); // pic_parameter_set_id
        out.appendExpG(pps.seq_parameter_set_id); // seq_parameter_set_id
        out.appendBits(pps.flags.entropy_coding_mode_flag, 1); // entropy_coding_mode_flag
        out.appendBits(pps.flags.bottom_field_pic_order_in_frame_present_flag, 1); // bottom_field_pic_order_in frame_present_flag
        out.appendExpG(0); // num_slices_groups_minus1
        out.appendExpG(pps.num_ref_idx_l0_default_active_minus1); // num_ref_idx10_default_active_minus
        out.appendExpG(pps.num_ref_idx_l1_default_active_minus1); // num_ref_idx11_default_active_minus
        out.appendBits(pps.flags.weighted_pred_flag, 1); // weighted_pred_flag
        out.appendBits(pps.weighted_bipred_idc, 2); // weighted_bipred_idc
        out.appendExpGSigned(pps.pic_init_qp_minus26); // pic_init_qp_minus26
        out.appendExpGSigned(pps.pic_init_qs_minus26); // pic_init_qs_minus26
        out.appendExpGSigned(pps.chroma_qp_index_offset); // chroma_qp_index_offset
        out.appendBits(pps.flags.deblocking_filter_control_present_flag, 1); //deblocking_filter_present_flag
        out.appendBits(pps.flags.constrained_intra_pred_flag, 1); // constrained_intra_pred_flag
        out.appendBits(pps.flags.redundant_pic_cnt_present_flag, 1); //redundant_pic_cnt_present_flag
        out.appendBits(0x1, 1); // rbsp stop bit
        out.padByte();
    }
};
#endif
