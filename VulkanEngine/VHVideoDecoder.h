/**
* Vulkan Video Decoder
*
* (c) Bernhard Clemens Schrenk, University of Vienna
*
*/

#ifndef VHVIDEODECODER_H
#define VHVIDEODECODER_H

#include "VHHelper.h"

namespace vh {
    class VHVideoDecoder {
    public:
		VkResult init(
			VkPhysicalDevice physicalDevice,
			VkDevice device,
			VmaAllocator allocator,
			uint32_t graphicsQueueFamily,
			VkQueue decodeQueue,
			uint32_t decodeQueueFamily,
			VkCommandPool decodeCommandPool,
			const VkSamplerYcbcrConversionInfo* pYCbCrConversionInfo);

		VkImageView getImageView() {
			return m_dpbImageViews[0];
		}

		//VkResult queueEncode(uint32_t currentImageIx);
		//VkResult finishEncode(const char*& data, size_t& size);
		void deinit();

		~VHVideoDecoder() {
			deinit();
		}
	
	private:
		VkResult allocateVideoSessionMemory();
		VkResult createVideoSessionParameters(uint32_t fps);
		VkResult allocateReferenceImages(uint32_t count);

		bool m_initialized{ false };

		VkDevice m_device;
		VmaAllocator m_allocator;
		uint32_t m_graphicsQueueFamily;
		VkQueue m_decodeQueue;
		VkCommandPool m_decodeCommandPool;
		uint32_t m_width;
		uint32_t m_height;
		const VkSamplerYcbcrConversionInfo* m_pYCbCrConversionInfo;
		uint32_t m_decodeQueueFamily;

		VkVideoDecodeH264ProfileInfoKHR m_h264videoProfile;
		VkVideoProfileInfoKHR m_videoProfile;
		VkVideoProfileListInfoKHR m_videoProfileList;
		VkVideoSessionKHR m_videoSession;
		std::vector<VmaAllocation> m_allocations;
		StdVideoH264SequenceParameterSetVui m_vui;
		StdVideoH264SequenceParameterSet m_sps;
		StdVideoH264PictureParameterSet m_pps;
		VkVideoSessionParametersKHR m_videoSessionParameters;

		VkCommandBuffer m_decodeCommandBuffer;

		std::vector<VkImage> m_dpbImages;
		std::vector <VmaAllocation> m_dpbImageAllocations;
		std::vector <VkImageView> m_dpbImageViews;
	};
};

#endif
