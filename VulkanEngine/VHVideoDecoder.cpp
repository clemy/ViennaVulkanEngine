/**
* Vulkan Video Decoder
*
* (c) Bernhard Clemens Schrenk, University of Vienna
*
*/

#define H264_IMPLEMENTATION
#include "VHVideoDecoder.h"
#include "H264ParameterSet.h"
#include "h264.h"

namespace vh {
    VkResult VHVideoDecoder::init(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        VkQueue graphicsQueue,
        uint32_t graphicsQueueFamily,
        VkCommandPool graphicsCommandPool,
        VkQueue decodeQueue,
        uint32_t decodeQueueFamily,
        VkCommandPool decodeCommandPool)
    {
        if (decodeQueue == nullptr) {
            std::cout << "Vulkan VideoDecode extension not present.\n";
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        if (m_initialized) {
            return VK_SUCCESS;
        }

        m_physicalDevice = physicalDevice;
        m_device = device;
        m_allocator = allocator;
        m_graphicsQueue = graphicsQueue;
        m_graphicsQueueFamily = graphicsQueueFamily;
        m_graphicsCommandPool = graphicsCommandPool;
        m_decodeQueue = decodeQueue;
        m_decodeQueueFamily = decodeQueueFamily;
        m_decodeCommandPool = decodeCommandPool;

        VHCHECKRESULT(allocateYCbCrConversionSampler());
        VHCHECKRESULT(createYCbCrConversionPipeline());

        m_initialized = true;
        return VK_SUCCESS;
    }

    void VHVideoDecoder::process(double dt)
    {
        for (Session* session : m_sessions)
        {
            session->process(dt);
        }
    }

    VHVideoDecoder::Session* VHVideoDecoder::createVideoSession(const std::string& filename) {
        Session* session = new Session(this, filename);
        m_sessions.insert(session);
        if (session->init() != VK_SUCCESS)
        {
            session->close();
            return nullptr;
        }
        return session;
    }

    VkResult VHVideoDecoder::allocateYCbCrConversionSampler()
    {
        VkSamplerYcbcrConversionCreateInfo yCbCrConversionCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
        yCbCrConversionCreateInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        yCbCrConversionCreateInfo.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; // limitation: color model should depend on video file

        m_yCbCrConversionInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
        VHCHECKRESULT(vkCreateSamplerYcbcrConversionKHR(m_device, &yCbCrConversionCreateInfo, nullptr, &m_yCbCrConversionInfo.conversion));

        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 16;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.pNext = &m_yCbCrConversionInfo;

        VHCHECKRESULT(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_yCbCrConversionSampler));

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::createYCbCrConversionPipeline()
    {
        auto computeShaderCode = vhFileRead("../../media/shader/VideoDecode/comp.spv");
        VkShaderModule computeShaderModule = vhPipeCreateShaderModule(m_device, computeShaderCode);
        VkPipelineShaderStageCreateInfo computeShaderStageInfo{};
        computeShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        computeShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        computeShaderStageInfo.module = computeShaderModule;
        computeShaderStageInfo.pName = "main";

        std::array<VkDescriptorSetLayoutBinding, 2> layoutBindings{};
        for (uint32_t i = 0; i < layoutBindings.size(); i++) {
            layoutBindings[i].binding = i;
            layoutBindings[i].descriptorCount = 1;
            layoutBindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        layoutBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        layoutBindings[0].pImmutableSamplers = &m_yCbCrConversionSampler;
        layoutBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        layoutBindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = (uint32_t)layoutBindings.size();
        layoutInfo.pBindings = layoutBindings.data();
        VHCHECKRESULT(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_computeDescriptorSetLayout));

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &m_computeDescriptorSetLayout;
        VHCHECKRESULT(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_computePipelineLayout));

        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.layout = m_computePipelineLayout;
        pipelineInfo.stage = computeShaderStageInfo;
        VHCHECKRESULT(vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline));

        vkDestroyShaderModule(m_device, computeShaderModule, nullptr);

        const int maxFramesCount = 17;
        const int maxStreamsCount = 6;
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = maxFramesCount * maxStreamsCount;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[1].descriptorCount = maxFramesCount * maxStreamsCount;
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = maxFramesCount * maxStreamsCount;
        //poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // allow freeing
        VHCHECKRESULT(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));

        return VK_SUCCESS;
    }

    void VHVideoDecoder::deinit()
    {
        if (!m_initialized) {
            return;
        }

        for (Session* session: m_sessions)
        {
            session->deinit();
            delete session;
        }
        m_sessions.clear();

        vkDestroyPipeline(m_device, m_computePipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_computePipelineLayout, nullptr);
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_computeDescriptorSetLayout, nullptr);

        vkDestroySampler(m_device, m_yCbCrConversionSampler, nullptr);
        vkDestroySamplerYcbcrConversionKHR(m_device, m_yCbCrConversionInfo.conversion, nullptr);

        m_initialized = false;
    }

    /*----------------------------------------------------------------------------*/

    VkResult VHVideoDecoder::Session::init()
    {
        std::ifstream file(m_filename, std::ios::binary | std::ios::ate);
        assert(file.good());
        m_data.resize(file.tellg());
        file.seekg(0);
        file.read(reinterpret_cast<char*>(m_data.data()), m_data.size());
        m_nextData = m_data.data();
        m_resetPending = true;

        VHCHECKRESULT(readFileHeaders());
        VHCHECKRESULT(checkCapabilities());
        VHCHECKRESULT(createVulkanVideoSession());
        VHCHECKRESULT(allocateVideoSessionMemory());
        VHCHECKRESULT(createVideoSessionParameters());
        VHCHECKRESULT(allocateReferenceImages(m_sps.max_num_ref_frames + 1));

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::assignTransferTarget(VkImage targetImage, VkImageView targetImageView)
    {
        return createYCbCrConversionDescriptorSets(targetImage, targetImageView);
    }

    void VHVideoDecoder::Session::process(double dt)
    {
        //const double TIME_BETWEEN_FRAMES = 1.0 / 25;
        const double TIME_BETWEEN_FRAMES = 1.0 / 30;
        m_nextFrameTime -= dt;
        if (m_nextFrameTime <= 0) {
            m_nextFrameTime += TIME_BETWEEN_FRAMES;

            while (m_activeDecodePicture >= m_viewed.size() || m_viewed[m_activeDecodePicture])
            {
                decodeFrame();
            }
            transferImage();
        }
    }

    VkResult VHVideoDecoder::Session::transferImage()
    {
        int nextToView = -1;
        for (int i = 0; i < m_gopPocs.size(); ++i)
        {
            if (!m_viewed[i]) {
                if (nextToView < 0 || m_gopPocs[i] < m_gopPocs[nextToView])
                {
                    nextToView = i;
                }
            }
        }
        if (nextToView < 0)
        {
            return VK_SUCCESS;
        }
        //std::cout << nextToView << ": " << (m_gopPocs[nextToView] >> 32) << ", " << (m_gopPocs[nextToView] & 0xffffffff) << std::endl;
        m_viewed[nextToView] = true;
        return convertYCbCrToRGB(nextToView);
    }

    void VHVideoDecoder::Session::getNextNAL(uint8_t*& data, size_t& len)
    {
        bool alreadyLooped = false;
        do {
            uint8_t* endData = m_nextData + 1;
            while (!(endData[0] == 0 && endData[1] == 0 && endData[2] == 1))
            {
                endData++;
                if (endData >= m_data.data() + m_data.size() + 3) {
                    assert(!alreadyLooped);
                    alreadyLooped = true;
                    m_nextData = m_data.data();
                    endData = m_nextData + 1;
                }
            }
            data = m_nextData;
            len = endData - m_nextData;
            m_nextData = endData;
        } while (len < 4);
    }

    void VHVideoDecoder::Session::close()
    {
        deinit();
        m_decoder->m_sessions.erase(this);
        delete this;
    }

    void VHVideoDecoder::Session::deinit()
    {
        //if (m_running) {
    //    const char* data;
    //    size_t size;
    //    getOutputVideoPacket(data, size);
            //vkFreeCommandBuffers(m_decoder->m_device, m_decoder->m_graphicsCommandPool, 1, &m_computeCommandBuffer);
            //vkFreeCommandBuffers(m_decoder->m_device, m_decoder->m_decodeCommandPool, 1, &m_decodeCommandBuffer);
        //}

        vkDestroyVideoSessionParametersKHR(m_decoder->m_device, m_videoSessionParameters, nullptr);
        for (uint32_t i = 0; i < m_dpbImages.size(); i++) {
            vkDestroyImageView(m_decoder->m_device, m_dpbImageViews[i], nullptr);
            vmaDestroyImage(m_decoder->m_allocator, m_dpbImages[i], m_dpbImageAllocations[i]);
        }
        vkDestroyVideoSessionKHR(m_decoder->m_device, m_videoSession, nullptr);
        for (VmaAllocation& allocation : m_allocations) {
            vmaFreeMemory(m_decoder->m_allocator, allocation);
        }
        m_allocations.clear();
    }


    static StdVideoH264SequenceParameterSet convertStdVideoH264SequenceParameterSet(h264::SPS& s)
    {
        StdVideoH264SpsFlags spsFlags = {};
        spsFlags.direct_8x8_inference_flag = s.direct_8x8_inference_flag;
        spsFlags.frame_mbs_only_flag = s.frame_mbs_only_flag;
        spsFlags.vui_parameters_present_flag = 0u;
        spsFlags.frame_cropping_flag = s.frame_cropping_flag;

        StdVideoH264SequenceParameterSet sps = {};
        sps.profile_idc = (StdVideoH264ProfileIdc)s.profile_idc;
        switch (s.level_idc)
        {
        case 0:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_0;
            break;
        case 11:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_1;
            break;
        case 12:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_2;
            break;
        case 13:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_1_3;
            break;
        case 20:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_0;
            break;
        case 21:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_1;
            break;
        case 22:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_2_2;
            break;
        case 30:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_0;
            break;
        case 31:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_1;
            break;
        case 32:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_3_2;
            break;
        case 40:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_0;
            break;
        case 41:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_1;
            break;
        case 42:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_4_2;
            break;
        case 50:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_0;
            break;
        case 51:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_1;
            break;
        case 52:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_5_2;
            break;
        case 60:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_0;
            break;
        case 61:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_1;
            break;
        case 62:
            sps.level_idc = STD_VIDEO_H264_LEVEL_IDC_6_2;
            break;
        default:
            assert(0);
            break;
        }

        //sps.level_idc = (StdVideoH264LevelIdc)s.level_idc;
        sps.seq_parameter_set_id = s.seq_parameter_set_id;
        sps.chroma_format_idc = (StdVideoH264ChromaFormatIdc)s.chroma_format_idc;
        sps.bit_depth_luma_minus8 = s.bit_depth_luma_minus8;
        sps.bit_depth_chroma_minus8 = s.bit_depth_chroma_minus8;
        sps.log2_max_frame_num_minus4 = s.log2_max_frame_num_minus4;
        sps.pic_order_cnt_type = (StdVideoH264PocType)s.pic_order_cnt_type;
        sps.max_num_ref_frames = s.num_ref_frames;
        sps.pic_width_in_mbs_minus1 = s.pic_width_in_mbs_minus1;
        sps.pic_height_in_map_units_minus1 = s.pic_height_in_map_units_minus1;
        sps.flags = spsFlags;
        sps.pSequenceParameterSetVui = nullptr;
        sps.frame_crop_right_offset = s.frame_crop_right_offset;
        sps.frame_crop_left_offset = s.frame_crop_left_offset;
        sps.frame_crop_top_offset = s.frame_crop_top_offset;
        sps.frame_crop_bottom_offset = s.frame_crop_bottom_offset;
        sps.log2_max_pic_order_cnt_lsb_minus4 = s.log2_max_pic_order_cnt_lsb_minus4;

        return sps;
    }

    static StdVideoH264PictureParameterSet convertStdVideoH264PictureParameterSet(h264::PPS& p)
    {
        StdVideoH264PpsFlags ppsFlags = {};
        ppsFlags.transform_8x8_mode_flag = p.transform_8x8_mode_flag;
        ppsFlags.constrained_intra_pred_flag = p.constrained_intra_pred_flag;
        ppsFlags.deblocking_filter_control_present_flag = p.deblocking_filter_control_present_flag;
        ppsFlags.entropy_coding_mode_flag = p.entropy_coding_mode_flag;
        ppsFlags.weighted_pred_flag = p.weighted_pred_flag;

        StdVideoH264PictureParameterSet pps = {};
        pps.seq_parameter_set_id = p.seq_parameter_set_id;
        pps.pic_parameter_set_id = p.pic_parameter_set_id;
        pps.num_ref_idx_l0_default_active_minus1 = p.num_ref_idx_l0_active_minus1;
        pps.weighted_bipred_idc = (StdVideoH264WeightedBipredIdc)p.weighted_bipred_idc;
        pps.pic_init_qp_minus26 = p.pic_init_qp_minus26;
        pps.pic_init_qs_minus26 = p.pic_init_qs_minus26;
        pps.chroma_qp_index_offset = p.chroma_qp_index_offset;
        pps.second_chroma_qp_index_offset = p.second_chroma_qp_index_offset;
        pps.flags = ppsFlags;

        return pps;
    }

    VkResult VHVideoDecoder::Session::readFileHeaders()
    {
        uint8_t* data;
        size_t length;
        bool spsOk = false;
        bool ppsOk = false;
        h264::NALHeader nal = {};
        m_h264sps = {};
        m_h264sps.chroma_format_idc = 1; // default to 1 in main and baseline
        m_h264pps = {};

        while (!spsOk || !ppsOk) {
            getNextNAL(data, length);
            h264::Bitstream bitstream;
            bitstream.init(data + 3, length - 3);
            h264::read_nal_header(&nal, &bitstream);
            switch (nal.type)
            {
            case h264::NAL_UNIT_TYPE_SPS:
                h264::read_sps(&m_h264sps, &bitstream);
                spsOk = true;
                break;

            case h264::NAL_UNIT_TYPE_PPS:
                h264::read_pps(&m_h264pps, &bitstream);
                ppsOk = true;
                break;
            }
        }

        m_width = (m_h264sps.pic_width_in_mbs_minus1 + 1) * 16;
        m_width -= m_h264sps.frame_crop_left_offset + m_h264sps.frame_crop_right_offset;
        m_height = (m_h264sps.pic_height_in_map_units_minus1 + 1) * 16;
        if (!m_h264sps.frame_mbs_only_flag)
            m_height *= 2;
        m_height -= m_h264sps.frame_crop_top_offset + m_h264sps.frame_crop_bottom_offset;

        //double fps = 25;
        //if (m_h264sps.vui_parameters_present_flag && m_h264sps.vui.timing_info_present_flag)
        //{
        //    fps = m_h264sps.vui.time_scale / m_h264sps.vui.num_units_in_tick / 2;
        //}

        m_sps = convertStdVideoH264SequenceParameterSet(m_h264sps);
        m_pps = convertStdVideoH264PictureParameterSet(m_h264pps);

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::checkCapabilities()
    {
        m_h264videoProfile = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR };
        m_h264videoProfile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        m_h264videoProfile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;

        m_videoProfile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        m_videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
        m_videoProfile.pNext = &m_h264videoProfile;
        m_videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        m_videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        m_videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;

        m_h264capabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR };

        m_decodeCapabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR };
        m_decodeCapabilities.pNext = &m_h264capabilities;

        m_videoCapabilities = { VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR };
        m_videoCapabilities.pNext = &m_decodeCapabilities;

        VHCHECKRESULT(vkGetPhysicalDeviceVideoCapabilitiesKHR(m_decoder->m_physicalDevice, &m_videoProfile, &m_videoCapabilities));

        assert(m_decodeCapabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR); // not supporting devices with separate DPB and output images yet


        m_videoProfileList = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        m_videoProfileList.profileCount = 1;
        m_videoProfileList.pProfiles = &m_videoProfile;

        VkPhysicalDeviceVideoFormatInfoKHR videoFormatInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR };
        videoFormatInfo.pNext = &m_videoProfileList;
        videoFormatInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_SAMPLED_BIT;

        uint32_t count = 0;
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(m_decoder->m_physicalDevice, &videoFormatInfo, &count, nullptr));
        std::vector<VkVideoFormatPropertiesKHR> formatProperties(count, { VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR });
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(m_decoder->m_physicalDevice, &videoFormatInfo, &count, formatProperties.data()));

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::createVulkanVideoSession()
    {
        const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };

        VkVideoSessionCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
        createInfo.pNext = nullptr;
        createInfo.queueFamilyIndex = m_decoder->m_decodeQueueFamily;
        createInfo.pVideoProfile = &m_videoProfile;
        createInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxCodedExtent = { m_width, m_height };
        createInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxDpbSlots = 17;
        createInfo.maxActiveReferencePictures = 16; // or twice for fields ?
        createInfo.pStdHeaderVersion = &h264StdExtensionVersion;

        VHCHECKRESULT(vkCreateVideoSessionKHR(m_decoder->m_device, &createInfo, nullptr, &m_videoSession));

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::allocateVideoSessionMemory()
    {
        uint32_t videoSessionMemoryRequirementsCount = 0;
        VHCHECKRESULT(vkGetVideoSessionMemoryRequirementsKHR(m_decoder->m_device, m_videoSession,
            &videoSessionMemoryRequirementsCount, nullptr));
        std::vector<VkVideoSessionMemoryRequirementsKHR> encodeSessionMemoryRequirements(videoSessionMemoryRequirementsCount);
        for (uint32_t i = 0; i < videoSessionMemoryRequirementsCount; i++) {
            memset(&encodeSessionMemoryRequirements[i], 0, sizeof(VkVideoSessionMemoryRequirementsKHR));
            encodeSessionMemoryRequirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        }
        VHCHECKRESULT(vkGetVideoSessionMemoryRequirementsKHR(m_decoder->m_device, m_videoSession,
            &videoSessionMemoryRequirementsCount,
            encodeSessionMemoryRequirements.data()));

        std::vector<VkBindVideoSessionMemoryInfoKHR> encodeSessionBindMemory(videoSessionMemoryRequirementsCount);
        m_allocations.resize(videoSessionMemoryRequirementsCount);
        for (uint32_t memIdx = 0; memIdx < videoSessionMemoryRequirementsCount; memIdx++) {
            VmaAllocationCreateInfo allocCreateInfo = {};
            allocCreateInfo.memoryTypeBits = encodeSessionMemoryRequirements[memIdx].memoryRequirements.memoryTypeBits;

            VmaAllocationInfo allocInfo;
            VHCHECKRESULT(vmaAllocateMemory(m_decoder->m_allocator, &encodeSessionMemoryRequirements[memIdx].memoryRequirements, &allocCreateInfo, &m_allocations[memIdx], &allocInfo));

            encodeSessionBindMemory[memIdx].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
            encodeSessionBindMemory[memIdx].pNext = nullptr;
            encodeSessionBindMemory[memIdx].memory = allocInfo.deviceMemory;

            encodeSessionBindMemory[memIdx].memoryBindIndex = encodeSessionMemoryRequirements[memIdx].memoryBindIndex;
            encodeSessionBindMemory[memIdx].memoryOffset = allocInfo.offset;
            encodeSessionBindMemory[memIdx].memorySize = allocInfo.size;
        }
        return vkBindVideoSessionMemoryKHR(m_decoder->m_device, m_videoSession, videoSessionMemoryRequirementsCount,
            encodeSessionBindMemory.data());
    }

    VkResult VHVideoDecoder::Session::createVideoSessionParameters()
    {
        VkVideoDecodeH264SessionParametersAddInfoKHR decodeH264SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR };
        decodeH264SessionParametersAddInfo.pNext = nullptr;
        decodeH264SessionParametersAddInfo.stdSPSCount = 1;
        decodeH264SessionParametersAddInfo.pStdSPSs = &m_sps;
        decodeH264SessionParametersAddInfo.stdPPSCount = 1;
        decodeH264SessionParametersAddInfo.pStdPPSs = &m_pps;

        VkVideoDecodeH264SessionParametersCreateInfoKHR decodeH264SessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR };
        decodeH264SessionParametersCreateInfo.pNext = nullptr;
        decodeH264SessionParametersCreateInfo.maxStdSPSCount = 1;
        decodeH264SessionParametersCreateInfo.maxStdPPSCount = 1;
        decodeH264SessionParametersCreateInfo.pParametersAddInfo = &decodeH264SessionParametersAddInfo;

        VkVideoSessionParametersCreateInfoKHR sessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR };
        sessionParametersCreateInfo.pNext = &decodeH264SessionParametersCreateInfo;
        sessionParametersCreateInfo.videoSessionParametersTemplate = nullptr;
        sessionParametersCreateInfo.videoSession = m_videoSession;

        return vkCreateVideoSessionParametersKHR(m_decoder->m_device, &sessionParametersCreateInfo, nullptr, &m_videoSessionParameters);
    }

    VkResult VHVideoDecoder::Session::allocateReferenceImages(uint32_t count)
    {
        uint32_t queueFamilies[] = { m_decoder->m_graphicsQueueFamily, m_decoder->m_decodeQueueFamily };

        m_dpbImages.resize(count);
        m_dpbImageAllocations.resize(count);
        m_dpbImageViews.resize(count);
        m_pictureResources.resize(count);
        m_stdH264references.resize(count);
        m_h264slots.resize(count);
        m_referenceSlots.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            VkImageCreateInfo tmpImgCreateInfo;
            tmpImgCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            tmpImgCreateInfo.pNext = &m_videoProfileList;
            tmpImgCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            tmpImgCreateInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
            tmpImgCreateInfo.extent = { m_width, m_height, 1 };
            tmpImgCreateInfo.mipLevels = 1;
            tmpImgCreateInfo.arrayLayers = 1;
            tmpImgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            tmpImgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            tmpImgCreateInfo.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_SAMPLED_BIT;
            tmpImgCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT; // VK_SHARING_MODE_EXCLUSIVE here makes it not check for queueFamily
            tmpImgCreateInfo.queueFamilyIndexCount = 2;
            tmpImgCreateInfo.pQueueFamilyIndices = queueFamilies;
            tmpImgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            tmpImgCreateInfo.flags = 0;
            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
            VHCHECKRESULT(vmaCreateImage(m_decoder->m_allocator, &tmpImgCreateInfo, &allocInfo, &m_dpbImages[i], &m_dpbImageAllocations[i], nullptr));
            VHCHECKRESULT(vhBufCreateImageView(m_decoder->m_device, m_dpbImages[i], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_VIEW_TYPE_2D, 1, VK_IMAGE_ASPECT_COLOR_BIT, &m_dpbImageViews[i], &m_decoder->m_yCbCrConversionInfo));

            m_pictureResources[i] = {VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
            m_pictureResources[i].imageViewBinding = m_dpbImageViews[i];
            m_pictureResources[i].codedOffset = {0, 0};
            m_pictureResources[i].codedExtent = {m_width, m_height};
            m_pictureResources[i].baseArrayLayer = 0;

            m_stdH264references[i] = {};
            m_stdH264references[i].flags.top_field_flag = 0;
            m_stdH264references[i].flags.bottom_field_flag = 0;
            m_stdH264references[i].flags.used_for_long_term_reference = 0;
            m_stdH264references[i].flags.is_non_existing = 0;
            m_stdH264references[i].FrameNum = 0;
            m_stdH264references[i].PicOrderCnt[0] = 0;
            m_stdH264references[i].PicOrderCnt[1] = 0;

            m_h264slots[i] = {VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR};
            m_h264slots[i].pStdReferenceInfo = &m_stdH264references[i];

            m_referenceSlots[i] = { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
            m_referenceSlots[i].pNext = &m_h264slots[i];
            m_referenceSlots[i].pPictureResource = &m_pictureResources[i];
            m_referenceSlots[i].slotIndex = i;
        }
        m_activeReferenceSlots = 0;
        m_activeDecodePicture = 0;
        m_activeViewPicture = -1;
        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::createYCbCrConversionDescriptorSets(VkImage targetImage, VkImageView targetImageView)
    {
        m_targetImage = targetImage;
        m_targetImageView = targetImageView;

        std::vector<VkDescriptorSetLayout> layouts(m_dpbImageViews.size(), m_decoder->m_computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo descAllocInfo{};
        descAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        descAllocInfo.descriptorPool = m_decoder->m_descriptorPool;
        descAllocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
        descAllocInfo.pSetLayouts = layouts.data();
        m_computeDescriptorSets.resize(descAllocInfo.descriptorSetCount);
        VHCHECKRESULT(vkAllocateDescriptorSets(m_decoder->m_device, &descAllocInfo, m_computeDescriptorSets.data()));

        for (size_t i = 0; i < m_computeDescriptorSets.size(); i++) {
            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

            VkDescriptorImageInfo imageInfo0{};
            imageInfo0.imageView = m_dpbImageViews[i];
            imageInfo0.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = m_computeDescriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pImageInfo = &imageInfo0;

            VkDescriptorImageInfo imageInfo1{};
            imageInfo1.imageView = m_targetImageView;
            imageInfo1.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = m_computeDescriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pImageInfo = &imageInfo1;

            vkUpdateDescriptorSets(m_decoder->m_device, (uint32_t)descriptorWrites.size(), descriptorWrites.data(), 0, nullptr);
        }
        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::decodeFrame()
    {
        uint8_t* data;
        size_t length;
        h264::NALHeader nal = {};
        h264::Bitstream bitstream;
        do {
            getNextNAL(data, length);
            bitstream.init(data + 3, length - 3);
            h264::read_nal_header(&nal, &bitstream);
        } while (nal.type != h264::NAL_UNIT_TYPE_CODED_SLICE_IDR && nal.type != h264::NAL_UNIT_TYPE_CODED_SLICE_NON_IDR);

        if (nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR)
        {
            m_activeDecodePicture = 0;
            m_activeReferenceSlots = 0;
            //m_gopPocs.clear();
            //m_viewed.clear();
        }

        h264::SliceHeader sliceHeader = {};
        h264::read_slice_header(&sliceHeader, &nal, &m_h264pps, &m_h264sps, &bitstream);

        size_t bufferSize = h264ps::AlignSize(length, m_videoCapabilities.minBitstreamBufferSizeAlignment);

        VkBuffer m_bitStreamBuffer;
        VmaAllocation m_bitStreamBufferAllocation;
        void* m_bitStreamData;

        VHCHECKRESULT(vhBufCreateBuffer(m_decoder->m_allocator, bufferSize,
            VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, // TODO: maybe use VMA_MEMORY_USAGE_CPU_COPY
            &m_bitStreamBuffer, &m_bitStreamBufferAllocation, &m_videoProfileList));
        VHCHECKRESULT(vmaMapMemory(m_decoder->m_allocator, m_bitStreamBufferAllocation, &m_bitStreamData));
        memcpy(m_bitStreamData, data, length);

        vmaFlushAllocation(m_decoder->m_allocator, m_bitStreamBufferAllocation, 0, VK_WHOLE_SIZE);


        VHCHECKRESULT(vhCmdCreateCommandBuffers(m_decoder->m_device, m_decoder->m_decodeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdBeginCommandBuffer(m_decoder->m_device, m_decodeCommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));


        if (m_resetPending)
        {
            m_gopPocs.clear();
            m_viewed.clear();

            for (auto image : m_dpbImages) {
                VHCHECKRESULT(vhBufTransitionImageLayout(m_decoder->m_device, m_decoder->m_decodeQueue, m_decodeCommandBuffer,
                    image, VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR));
            }
        }

        int max_pic_order_cnt_lsb = 1 << (m_sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
        int pic_order_cnt_lsb = sliceHeader.pic_order_cnt_lsb;

        if (pic_order_cnt_lsb == 0)
        {
            m_poc_cycle++;
        }

        // Rec. ITU-T H.264 (08/2021) page 115
        // Also: https://www.ramugedia.com/negative-pocs
        int pic_order_cnt_msb = 0;
        if (pic_order_cnt_lsb < m_prev_pic_order_cnt_lsb && (m_prev_pic_order_cnt_lsb - pic_order_cnt_lsb) >= max_pic_order_cnt_lsb / 2)
        {
            pic_order_cnt_msb = m_prev_pic_order_cnt_msb + max_pic_order_cnt_lsb; // pic_order_cnt_lsb wrapped around
        }
        else if (pic_order_cnt_lsb > m_prev_pic_order_cnt_lsb && (pic_order_cnt_lsb - m_prev_pic_order_cnt_lsb) > max_pic_order_cnt_lsb / 2)
        {
            pic_order_cnt_msb = m_prev_pic_order_cnt_msb - max_pic_order_cnt_lsb; // here negative POC might occur
        }
        else
        {
            pic_order_cnt_msb = m_prev_pic_order_cnt_msb;
        }
        //pic_order_cnt_msb = pic_order_cnt_msb % 256;
        m_prev_pic_order_cnt_lsb = pic_order_cnt_lsb;
        m_prev_pic_order_cnt_msb = pic_order_cnt_msb;

        // https://www.vcodex.com/h264avc-picture-management/
        int poc = pic_order_cnt_msb + pic_order_cnt_lsb; // poc = TopFieldOrderCount
        int gop = m_poc_cycle - 1;

        m_referenceSlots[m_activeDecodePicture].slotIndex = -1;
        m_stdH264references[m_activeDecodePicture].FrameNum = sliceHeader.frame_num;
        m_stdH264references[m_activeDecodePicture].PicOrderCnt[0] = poc;
        m_stdH264references[m_activeDecodePicture].PicOrderCnt[1] = poc;

        VkVideoBeginCodingInfoKHR beginInfo = { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
        beginInfo.videoSession = m_videoSession;
        beginInfo.videoSessionParameters = m_videoSessionParameters;
        beginInfo.referenceSlotCount = m_activeReferenceSlots + 1;
        beginInfo.pReferenceSlots = m_referenceSlots.data();


        vkCmdBeginVideoCodingKHR(m_decodeCommandBuffer, &beginInfo);

        if (m_resetPending) {
            VkVideoCodingControlInfoKHR codingControlInfo = { VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
            codingControlInfo.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;

            vkCmdControlVideoCodingKHR(m_decodeCommandBuffer, &codingControlInfo);

            m_resetPending = false;
        }


        //std::cout << m_activeDecodePicture << ": " << poc << " (" << m_pictureResources[m_activeDecodePicture].imageViewBinding << ")" << std::endl;
        //for (int i = 0; i < beginInfo.referenceSlotCount; ++i)
        //{
        //    std::cout << "  " << i << ": " << m_referenceSlots[i].slotIndex << ": " << m_referenceSlots[i].pPictureResource->imageViewBinding << std::endl;
        //}

        uint32_t sliceOffset = 0;

        StdVideoDecodeH264PictureInfo decodeH264pictureInfo = {};
        decodeH264pictureInfo.flags.field_pic_flag = sliceHeader.field_pic_flag;
        decodeH264pictureInfo.flags.is_intra = nal.type == h264::NAL_UNIT_TYPE_CODED_SLICE_IDR ? 1 : 0;
        decodeH264pictureInfo.flags.is_reference = nal.idc > 0 ? 1 : 0;
        decodeH264pictureInfo.flags.IdrPicFlag = (decodeH264pictureInfo.flags.is_reference && decodeH264pictureInfo.flags.is_intra) ? 1 : 0;
        decodeH264pictureInfo.flags.bottom_field_flag = sliceHeader.bottom_field_flag;
        decodeH264pictureInfo.flags.complementary_field_pair = 0;
        decodeH264pictureInfo.seq_parameter_set_id = m_pps.seq_parameter_set_id;
        decodeH264pictureInfo.pic_parameter_set_id = sliceHeader.pic_parameter_set_id;
        decodeH264pictureInfo.frame_num = sliceHeader.frame_num;
        decodeH264pictureInfo.idr_pic_id = sliceHeader.idr_pic_id;
        decodeH264pictureInfo.PicOrderCnt[0] = poc;
        decodeH264pictureInfo.PicOrderCnt[1] = poc;

        VkVideoDecodeH264PictureInfoKHR h264decodeInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR };
        h264decodeInfo.pStdPictureInfo = &decodeH264pictureInfo;
        h264decodeInfo.sliceCount = 1;
        h264decodeInfo.pSliceOffsets = &sliceOffset;

        m_referenceSlots[m_activeDecodePicture].slotIndex = m_activeDecodePicture;
        size_t activeSlots = m_activeReferenceSlots + (m_activeDecodePicture < m_activeReferenceSlots ? 1 : 0);
        std::vector<VkVideoReferenceSlotInfoKHR> refSlots;
        std::copy_if(m_referenceSlots.begin(), m_referenceSlots.begin() + activeSlots, std::back_inserter(refSlots), [this](const VkVideoReferenceSlotInfoKHR& s) { return s.slotIndex != m_activeDecodePicture; });

        VkVideoDecodeInfoKHR decodeInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR };
        decodeInfo.pNext = &h264decodeInfo;
        decodeInfo.srcBuffer = m_bitStreamBuffer;
        decodeInfo.srcBufferOffset = 0;
        decodeInfo.srcBufferRange = bufferSize;
        decodeInfo.dstPictureResource = m_pictureResources[m_activeDecodePicture];
        decodeInfo.pSetupReferenceSlot = &m_referenceSlots[m_activeDecodePicture];
        decodeInfo.referenceSlotCount = refSlots.size();
        decodeInfo.pReferenceSlots = refSlots.size() ? refSlots.data() : nullptr;


        vkCmdDecodeVideoKHR(m_decodeCommandBuffer, &decodeInfo);



        VkVideoEndCodingInfoKHR encodeEndInfo = { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
        vkCmdEndVideoCodingKHR(m_decodeCommandBuffer, &encodeEndInfo);

        //VHCHECKRESULT(vhBufTransitionImageLayout(m_device, m_decodeQueue, m_decodeCommandBuffer,
        //    m_dpbImages[0], VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
        //    VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));


        VHCHECKRESULT(vkEndCommandBuffer(m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdSubmitCommandBuffer(m_decoder->m_device, m_decoder->m_decodeQueue, m_decodeCommandBuffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE));


        VHCHECKRESULT(vkQueueWaitIdle(m_decoder->m_decodeQueue)); // TODO: replace with better synchronization!

        //uint8_t* dataImage = new uint8_t[m_width * m_height];
        //VkResult ret = vhBufCopyImageToHost(m_device, m_allocator, m_decodeQueue, m_decodeCommandPool,
        //    m_dpbImages[0], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
        //    dataImage, m_width, m_height, m_width * m_height);

        //VHCHECKRESULT(ret);

        vmaUnmapMemory(m_decoder->m_allocator, m_bitStreamBufferAllocation);
        vmaDestroyBuffer(m_decoder->m_allocator, m_bitStreamBuffer, m_bitStreamBufferAllocation);

        //std::string name("../../out.png");
        //stbi_write_png(name.c_str(), m_width, m_height, 1, dataImage, 1 * m_width);
        //delete[] dataImage;

        vkFreeCommandBuffers(m_decoder->m_device, m_decoder->m_decodeCommandPool, 1, &m_decodeCommandBuffer);

        //std::cout << m_activeDecodePicture << ": " << poc << " (" << m_pictureResources[m_activeDecodePicture].imageViewBinding << ")" << std::endl;
        //for (const auto& s : refSlots)
        //{
        //    std::cout << "  " << s.slotIndex << ": " << s.pPictureResource->imageViewBinding << std::endl;
        //}

        if (m_gopPocs.size() <= m_activeDecodePicture)
        {
            m_gopPocs.resize(m_activeDecodePicture + 1, 0);
            m_viewed.resize(m_activeDecodePicture + 1, true);
        }
        m_gopPocs[m_activeDecodePicture] = (uint64_t)gop << 32 | poc;
        m_viewed[m_activeDecodePicture] = false;
        //m_activeViewPicture = m_activeDecodePicture;
        if (decodeH264pictureInfo.flags.is_reference) {
            m_activeDecodePicture = (m_activeDecodePicture + 1) % m_pictureResources.size();
            m_activeReferenceSlots = std::min(m_activeReferenceSlots + 1, (uint32_t)m_referenceSlots.size() - 1);
        }

        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::Session::convertYCbCrToRGB(uint32_t currentImageIx)
    {
        // begin command buffer for compute shader
        VHCHECKRESULT(vhCmdCreateCommandBuffers(m_decoder->m_device, m_decoder->m_graphicsCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &m_computeCommandBuffer));
        VHCHECKRESULT(vhCmdBeginCommandBuffer(m_decoder->m_device, m_computeCommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));

        VHCHECKRESULT(vhBufTransitionImageLayout(m_decoder->m_device, m_decoder->m_graphicsQueue, m_computeCommandBuffer,
            m_dpbImages[currentImageIx], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
            VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

        VHCHECKRESULT(vhBufTransitionImageLayout(m_decoder->m_device, m_decoder->m_graphicsQueue, m_computeCommandBuffer,
            m_targetImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL));

        // run the YCbCr->RGB conversion shader
        vkCmdBindPipeline(m_computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_decoder->m_computePipeline);
        vkCmdBindDescriptorSets(m_computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_decoder->m_computePipelineLayout, 0, 1, &m_computeDescriptorSets[currentImageIx], 0, 0);
        vkCmdDispatch(m_computeCommandBuffer, (m_width + 15) / 16, (m_height + 15) / 16, 1); // work item local size = 16x16

        VHCHECKRESULT(vhBufTransitionImageLayout(m_decoder->m_device, m_decoder->m_graphicsQueue, m_computeCommandBuffer,
            m_targetImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

        VHCHECKRESULT(vhBufTransitionImageLayout(m_decoder->m_device, m_decoder->m_graphicsQueue, m_computeCommandBuffer,
            m_dpbImages[currentImageIx], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR));

        VHCHECKRESULT(vkEndCommandBuffer(m_computeCommandBuffer));
        VHCHECKRESULT(vhCmdSubmitCommandBuffer(m_decoder->m_device, m_decoder->m_graphicsQueue, m_computeCommandBuffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE));

        VHCHECKRESULT(vkQueueWaitIdle(m_decoder->m_graphicsQueue)); // TODO: replace with better synchronization!

        vkFreeCommandBuffers(m_decoder->m_device, m_decoder->m_graphicsCommandPool, 1, &m_computeCommandBuffer);

        return VK_SUCCESS;
    }
};

