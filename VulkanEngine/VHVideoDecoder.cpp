#include "VHVideoDecoder.h"
#include "H264ParameterSet.h"

namespace vh {
    VkResult VHVideoDecoder::init(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        VkQueue decodeQueue,
        VkCommandPool decodeCommandPool)
    {
        //assert(!m_running);

        if (decodeQueue == nullptr) {
            std::cout << "Vulkan VideoDecode extension not present.\n";
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        if (m_initialized) {
            return VK_SUCCESS;
            //if ((width & ~1) == m_width && (height & ~1) == m_height) {
            //    // nothing changed
            //    return VK_SUCCESS;
            //}

            // resolution changed
            //deinit();
        }

        m_device = device;
        m_allocator = allocator;
        m_decodeQueue = decodeQueue;
        m_decodeCommandPool = decodeCommandPool;
        
        int decodeQueueFamily = vh::vhDevFindQueueFamily(physicalDevice, VK_QUEUE_VIDEO_DECODE_BIT_KHR);
        if (decodeQueueFamily == -1) {
            std::cout << "Vulkan VideoDecode extension not present.\n";
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }

        VkVideoDecodeH264ProfileInfoKHR h264profile = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR };
        h264profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        h264profile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;

        VkVideoProfileInfoKHR profile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
        profile.pNext = &h264profile;
        profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;

        VkVideoDecodeH264CapabilitiesKHR h264capabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR };
        
        VkVideoDecodeCapabilitiesKHR decodeCapabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR };
        decodeCapabilities.pNext = &h264capabilities;

        VkVideoCapabilitiesKHR videoCapabilities = { VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR };
        videoCapabilities.pNext = &decodeCapabilities;

        VHCHECKRESULT(vkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &profile, &videoCapabilities));

        assert(decodeCapabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR); // not supporting devices with separate DPB and output images yet


        VkVideoProfileListInfoKHR profileListInfo = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        profileListInfo.profileCount = 1;
        profileListInfo.pProfiles = &profile;

        VkPhysicalDeviceVideoFormatInfoKHR videoFormatInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR };
        videoFormatInfo.pNext = &profileListInfo;
        videoFormatInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        uint32_t count = 0;
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(physicalDevice, &videoFormatInfo, &count, nullptr));
        std::vector<VkVideoFormatPropertiesKHR> formatProperties(count, { VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR });
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(physicalDevice, &videoFormatInfo, &count, formatProperties.data()));


        const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };

        VkVideoSessionCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
        createInfo.pNext = nullptr;
        createInfo.queueFamilyIndex = decodeQueueFamily;
        createInfo.pVideoProfile = &profile;
        createInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxCodedExtent = {800, 608};
        createInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxDpbSlots = 17;
        createInfo.maxActiveReferencePictures = 16; // or twice for fields ?
        createInfo.pStdHeaderVersion = &h264StdExtensionVersion;        

        VHCHECKRESULT(vkCreateVideoSessionKHR(m_device, &createInfo, nullptr, &m_videoSession));

        VHCHECKRESULT(allocateVideoSessionMemory());
        VHCHECKRESULT(createVideoSessionParameters(100));





        VHCHECKRESULT(vhCmdCreateCommandBuffers(m_device, m_decodeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdBeginCommandBuffer(m_device, m_decodeCommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));





        VkVideoBeginCodingInfoKHR beginInfo = { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
        beginInfo.videoSession = m_videoSession;
        beginInfo.videoSessionParameters = m_videoSessionParameters;
        beginInfo.referenceSlotCount = 0;
        beginInfo.pReferenceSlots = nullptr;

        vkCmdBeginVideoCodingKHR(m_decodeCommandBuffer, &beginInfo);

        //if (reset) {
        VkVideoCodingControlInfoKHR codingControlInfo = { VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
        codingControlInfo.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;

        vkCmdControlVideoCodingKHR(m_decodeCommandBuffer, &codingControlInfo);
        //}


        VkVideoDecodeInfoKHR decodeInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR };


        //vkCmdDecodeVideoKHR(m_decodeCommandBuffer, &decodeInfo);



        VkVideoEndCodingInfoKHR encodeEndInfo = { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
        vkCmdEndVideoCodingKHR(m_decodeCommandBuffer, &encodeEndInfo);


        VHCHECKRESULT(vkEndCommandBuffer(m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdSubmitCommandBuffer(m_device, m_decodeQueue, m_decodeCommandBuffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE));

        VHCHECKRESULT(vkQueueWaitIdle(m_decodeQueue)); // TODO: replace with better synchronization!

        m_initialized = true;
        return VK_SUCCESS;
    }

    VkResult VHVideoDecoder::allocateVideoSessionMemory()
    {
        uint32_t videoSessionMemoryRequirementsCount = 0;
        VHCHECKRESULT(vkGetVideoSessionMemoryRequirementsKHR(m_device, m_videoSession,
            &videoSessionMemoryRequirementsCount, nullptr));
        std::vector<VkVideoSessionMemoryRequirementsKHR> encodeSessionMemoryRequirements(videoSessionMemoryRequirementsCount);
        for (uint32_t i = 0; i < videoSessionMemoryRequirementsCount; i++) {
            memset(&encodeSessionMemoryRequirements[i], 0, sizeof(VkVideoSessionMemoryRequirementsKHR));
            encodeSessionMemoryRequirements[i].sType = VK_STRUCTURE_TYPE_VIDEO_SESSION_MEMORY_REQUIREMENTS_KHR;
        }
        VHCHECKRESULT(vkGetVideoSessionMemoryRequirementsKHR(m_device, m_videoSession,
            &videoSessionMemoryRequirementsCount,
            encodeSessionMemoryRequirements.data()));

        std::vector<VkBindVideoSessionMemoryInfoKHR> encodeSessionBindMemory(videoSessionMemoryRequirementsCount);
        m_allocations.resize(videoSessionMemoryRequirementsCount);
        for (uint32_t memIdx = 0; memIdx < videoSessionMemoryRequirementsCount; memIdx++) {
            VmaAllocationCreateInfo allocCreateInfo = {};
            allocCreateInfo.memoryTypeBits = encodeSessionMemoryRequirements[memIdx].memoryRequirements.memoryTypeBits;

            VmaAllocationInfo allocInfo;
            VHCHECKRESULT(vmaAllocateMemory(m_allocator, &encodeSessionMemoryRequirements[memIdx].memoryRequirements, &allocCreateInfo, &m_allocations[memIdx], &allocInfo));

            encodeSessionBindMemory[memIdx].sType = VK_STRUCTURE_TYPE_BIND_VIDEO_SESSION_MEMORY_INFO_KHR;
            encodeSessionBindMemory[memIdx].pNext = nullptr;
            encodeSessionBindMemory[memIdx].memory = allocInfo.deviceMemory;

            encodeSessionBindMemory[memIdx].memoryBindIndex = encodeSessionMemoryRequirements[memIdx].memoryBindIndex;
            encodeSessionBindMemory[memIdx].memoryOffset = allocInfo.offset;
            encodeSessionBindMemory[memIdx].memorySize = allocInfo.size;
        }
        return vkBindVideoSessionMemoryKHR(m_device, m_videoSession, videoSessionMemoryRequirementsCount,
            encodeSessionBindMemory.data());
    }

    VkResult VHVideoDecoder::createVideoSessionParameters(uint32_t fps)
    {
        StdVideoH264SequenceParameterSetVui vui = h264::getStdVideoH264SequenceParameterSetVui(fps);
        StdVideoH264SequenceParameterSet sps = h264::getStdVideoH264SequenceParameterSet(800, 600, &vui);
        StdVideoH264PictureParameterSet pps = h264::getStdVideoH264PictureParameterSet();

        VkVideoDecodeH264SessionParametersAddInfoKHR decodeH264SessionParametersAddInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_ADD_INFO_KHR };
        decodeH264SessionParametersAddInfo.pNext = nullptr;
        decodeH264SessionParametersAddInfo.stdSPSCount = 1;
        decodeH264SessionParametersAddInfo.pStdSPSs = &sps;
        decodeH264SessionParametersAddInfo.stdPPSCount = 1;
        decodeH264SessionParametersAddInfo.pStdPPSs = &pps;

        VkVideoDecodeH264SessionParametersCreateInfoKHR decodeH264SessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR };
        decodeH264SessionParametersCreateInfo.pNext = nullptr;
        decodeH264SessionParametersCreateInfo.maxStdSPSCount = 1;
        decodeH264SessionParametersCreateInfo.maxStdPPSCount = 1;
        decodeH264SessionParametersCreateInfo.pParametersAddInfo = &decodeH264SessionParametersAddInfo;

        VkVideoSessionParametersCreateInfoKHR sessionParametersCreateInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR };
        sessionParametersCreateInfo.pNext = &decodeH264SessionParametersCreateInfo;
        sessionParametersCreateInfo.videoSessionParametersTemplate = nullptr;
        sessionParametersCreateInfo.videoSession = m_videoSession;

        return vkCreateVideoSessionParametersKHR(m_device, &sessionParametersCreateInfo, nullptr, &m_videoSessionParameters);
    }



    void VHVideoDecoder::deinit()
    {
        if (!m_initialized) {
            return;
        }

        //if (m_running) {
        //    const char* data;
        //    size_t size;
        //    getOutputVideoPacket(data, size);
            vkFreeCommandBuffers(m_device, m_decodeCommandPool, 1, &m_decodeCommandBuffer);
        //}

        vkDestroyVideoSessionParametersKHR(m_device, m_videoSessionParameters, nullptr);
        vkDestroyVideoSessionKHR(m_device, m_videoSession, nullptr);
        for (VmaAllocation& allocation : m_allocations) {
            vmaFreeMemory(m_allocator, allocation);
        }
        m_allocations.clear();

        m_initialized = false;
    }
};
