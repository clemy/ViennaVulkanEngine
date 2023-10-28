/**
* Vulkan Video Decoder
*
* (c) Bernhard Clemens Schrenk, University of Vienna
*
*/

#include "VHVideoDecoder.h"
#include "H264ParameterSet.h"

namespace vh {
    VkResult VHVideoDecoder::init(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        uint32_t graphicsQueueFamily,
        VkQueue decodeQueue,
        uint32_t decodeQueueFamily,
        VkCommandPool decodeCommandPool,
        const VkSamplerYcbcrConversionInfo* pYCbCrConversionInfo)
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
        m_graphicsQueueFamily = graphicsQueueFamily;
        m_decodeQueue = decodeQueue;
        m_decodeQueueFamily = decodeQueueFamily;
        m_decodeCommandPool = decodeCommandPool;
        m_pYCbCrConversionInfo = pYCbCrConversionInfo;
        m_width = 800;
        m_height = 600;

        m_h264videoProfile = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR };
        m_h264videoProfile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        m_h264videoProfile.pictureLayout = VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR;

        m_videoProfile = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR };
        m_videoProfile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR;
        m_videoProfile.pNext = &m_h264videoProfile;
        m_videoProfile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        m_videoProfile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
        m_videoProfile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;

        VkVideoDecodeH264CapabilitiesKHR h264capabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR };
        
        VkVideoDecodeCapabilitiesKHR decodeCapabilities = { VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR };
        decodeCapabilities.pNext = &h264capabilities;

        VkVideoCapabilitiesKHR videoCapabilities = { VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR };
        videoCapabilities.pNext = &decodeCapabilities;

        VHCHECKRESULT(vkGetPhysicalDeviceVideoCapabilitiesKHR(physicalDevice, &m_videoProfile, &videoCapabilities));

        assert(decodeCapabilities.flags & VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR); // not supporting devices with separate DPB and output images yet


        m_videoProfileList = { VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR };
        m_videoProfileList.profileCount = 1;
        m_videoProfileList.pProfiles = &m_videoProfile;

        VkPhysicalDeviceVideoFormatInfoKHR videoFormatInfo = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR };
        videoFormatInfo.pNext = &m_videoProfileList;
        videoFormatInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_SAMPLED_BIT;

        uint32_t count = 0;
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(physicalDevice, &videoFormatInfo, &count, nullptr));
        std::vector<VkVideoFormatPropertiesKHR> formatProperties(count, { VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR });
        VHCHECKRESULT(vkGetPhysicalDeviceVideoFormatPropertiesKHR(physicalDevice, &videoFormatInfo, &count, formatProperties.data()));


        const VkExtensionProperties h264StdExtensionVersion = { VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION };

        VkVideoSessionCreateInfoKHR createInfo = { VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR };
        createInfo.pNext = nullptr;
        createInfo.queueFamilyIndex = m_decodeQueueFamily;
        createInfo.pVideoProfile = &m_videoProfile;
        createInfo.pictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxCodedExtent = {m_width, m_height};
        createInfo.referencePictureFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        createInfo.maxDpbSlots = 17;
        createInfo.maxActiveReferencePictures = 16; // or twice for fields ?
        createInfo.pStdHeaderVersion = &h264StdExtensionVersion;        

        VHCHECKRESULT(vkCreateVideoSessionKHR(m_device, &createInfo, nullptr, &m_videoSession));

        VHCHECKRESULT(allocateVideoSessionMemory());
        VHCHECKRESULT(createVideoSessionParameters(100));



        size_t startOffset = 0x20;
        size_t endOffset = 0xe0c7;
        size_t readSize = endOffset - startOffset;
        size_t bufferSize = h264::AlignSize(readSize, videoCapabilities.minBitstreamBufferSizeAlignment);

        VkBuffer m_bitStreamBuffer;
        VmaAllocation m_bitStreamBufferAllocation;
        void* m_bitStreamData;

        VHCHECKRESULT(vhBufCreateBuffer(m_allocator, bufferSize,
            VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU, // TODO: maybe use VMA_MEMORY_USAGE_CPU_COPY
            &m_bitStreamBuffer, &m_bitStreamBufferAllocation, &m_videoProfileList));
        VHCHECKRESULT(vmaMapMemory(m_allocator, m_bitStreamBufferAllocation, &m_bitStreamData));

        FILE* f = fopen("..\\..\\video.264", "rb");
        assert(f != nullptr);
        fseek(f, startOffset, SEEK_SET);
        size_t read = fread(m_bitStreamData, readSize, 1, f);
        assert(read == 1);
        fclose(f);

        vmaFlushAllocation(m_allocator, m_bitStreamBufferAllocation, 0, VK_WHOLE_SIZE);

        VHCHECKRESULT(allocateReferenceImages(2));


        VHCHECKRESULT(vhCmdCreateCommandBuffers(m_device, m_decodeCommandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdBeginCommandBuffer(m_device, m_decodeCommandBuffer, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT));



        VHCHECKRESULT(vhBufTransitionImageLayout(m_device, m_decodeQueue, m_decodeCommandBuffer,
            m_dpbImages[0], VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR));


        VkVideoPictureResourceInfoKHR pictureResource = { VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR };
        pictureResource.imageViewBinding = m_dpbImageViews[0];
        pictureResource.codedOffset = { 0, 0 };
        pictureResource.codedExtent = { m_width, m_height };
        pictureResource.baseArrayLayer = 0;

        StdVideoDecodeH264ReferenceInfo stdH264reference = {};
        stdH264reference.flags.top_field_flag = 0;
        stdH264reference.flags.bottom_field_flag = 0;
        stdH264reference.flags.used_for_long_term_reference = 0;
        stdH264reference.flags.is_non_existing = 0;
        stdH264reference.FrameNum = 0;
        stdH264reference.PicOrderCnt[0] = 0;
        stdH264reference.PicOrderCnt[1] = 1;

        VkVideoDecodeH264DpbSlotInfoKHR h264slot = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_DPB_SLOT_INFO_KHR };
        h264slot.pStdReferenceInfo = &stdH264reference;

        VkVideoReferenceSlotInfoKHR referenceSlot = { VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR };
        referenceSlot.pNext = &h264slot;
        referenceSlot.pPictureResource = &pictureResource;
        referenceSlot.slotIndex = -1;

        VkVideoBeginCodingInfoKHR beginInfo = { VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR };
        beginInfo.videoSession = m_videoSession;
        beginInfo.videoSessionParameters = m_videoSessionParameters;
        beginInfo.referenceSlotCount = 1;
        beginInfo.pReferenceSlots = &referenceSlot;

        vkCmdBeginVideoCodingKHR(m_decodeCommandBuffer, &beginInfo);

        //if (reset) {
        VkVideoCodingControlInfoKHR codingControlInfo = { VK_STRUCTURE_TYPE_VIDEO_CODING_CONTROL_INFO_KHR };
        codingControlInfo.flags = VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR;

        vkCmdControlVideoCodingKHR(m_decodeCommandBuffer, &codingControlInfo);
        //}



        uint32_t sliceOffset = 0;

        h264::DecodeFrameInfo frameInfo(0, m_width, m_height, m_sps, m_pps, true);

        VkVideoDecodeH264PictureInfoKHR h264decodeInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR };
        h264decodeInfo.pStdPictureInfo = frameInfo.getDecodeH264FrameInfo();
        h264decodeInfo.sliceCount = 1;
        h264decodeInfo.pSliceOffsets = &sliceOffset;

        referenceSlot.slotIndex = 0;

        VkVideoDecodeInfoKHR decodeInfo = { VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR };
        decodeInfo.pNext = &h264decodeInfo;
        decodeInfo.srcBuffer = m_bitStreamBuffer;
        decodeInfo.srcBufferOffset = 0;
        decodeInfo.srcBufferRange = bufferSize;
        decodeInfo.dstPictureResource = pictureResource;
        decodeInfo.pSetupReferenceSlot = &referenceSlot;
        decodeInfo.referenceSlotCount = 0;
        decodeInfo.pReferenceSlots = nullptr;


        vkCmdDecodeVideoKHR(m_decodeCommandBuffer, &decodeInfo);



        VkVideoEndCodingInfoKHR encodeEndInfo = { VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR };
        vkCmdEndVideoCodingKHR(m_decodeCommandBuffer, &encodeEndInfo);

        VHCHECKRESULT(vhBufTransitionImageLayout(m_device, m_decodeQueue, m_decodeCommandBuffer,
            m_dpbImages[0], VK_FORMAT_R8G8_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, 1, 1,
            VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));


        VHCHECKRESULT(vkEndCommandBuffer(m_decodeCommandBuffer));
        VHCHECKRESULT(vhCmdSubmitCommandBuffer(m_device, m_decodeQueue, m_decodeCommandBuffer, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE));


        VHCHECKRESULT(vkQueueWaitIdle(m_decodeQueue)); // TODO: replace with better synchronization!

        //uint8_t* dataImage = new uint8_t[m_width * m_height];
        //VkResult ret = vhBufCopyImageToHost(m_device, m_allocator, m_decodeQueue, m_decodeCommandPool,
        //    m_dpbImages[0], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_ASPECT_PLANE_0_BIT, VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
        //    dataImage, m_width, m_height, m_width * m_height);

        //VHCHECKRESULT(ret);

        //vmaUnmapMemory(m_allocator, m_bitStreamBufferAllocation);
        //vmaDestroyBuffer(m_allocator, m_bitStreamBuffer, m_bitStreamBufferAllocation);

        //std::string name("../../out.png");
        //stbi_write_png(name.c_str(), m_width, m_height, 1, dataImage, 1 * m_width);
        //delete[] dataImage;

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
        m_vui = h264::getStdVideoH264SequenceParameterSetVui(fps);
        m_sps = h264::getStdVideoH264SequenceParameterSet(m_width, m_height, &m_vui);
        m_pps = h264::getStdVideoH264PictureParameterSet();

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

        return vkCreateVideoSessionParametersKHR(m_device, &sessionParametersCreateInfo, nullptr, &m_videoSessionParameters);
    }

    VkResult VHVideoDecoder::allocateReferenceImages(uint32_t count)
    {
        uint32_t queueFamilies[] = { m_graphicsQueueFamily, m_decodeQueueFamily };

        m_dpbImages.resize(count);
        m_dpbImageAllocations.resize(count);
        m_dpbImageViews.resize(count);
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
            VHCHECKRESULT(vmaCreateImage(m_allocator, &tmpImgCreateInfo, &allocInfo, &m_dpbImages[i], &m_dpbImageAllocations[i], nullptr));
            VHCHECKRESULT(vhBufCreateImageView(m_device, m_dpbImages[i], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, VK_IMAGE_VIEW_TYPE_2D, 1, VK_IMAGE_ASPECT_COLOR_BIT, &m_dpbImageViews[i], m_pYCbCrConversionInfo));
        }
        return VK_SUCCESS;
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
        for (uint32_t i = 0; i < m_dpbImages.size(); i++) {
            vkDestroyImageView(m_device, m_dpbImageViews[i], nullptr);
            vmaDestroyImage(m_allocator, m_dpbImages[i], m_dpbImageAllocations[i]);
        }
        vkDestroyVideoSessionKHR(m_device, m_videoSession, nullptr);
        for (VmaAllocation& allocation : m_allocations) {
            vmaFreeMemory(m_allocator, allocation);
        }
        m_allocations.clear();

        m_initialized = false;
    }
};
