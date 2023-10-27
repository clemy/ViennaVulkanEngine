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
			VkQueue decodeQueue,
			VkCommandPool decodeCommandPool);

		//VkResult queueEncode(uint32_t currentImageIx);
		//VkResult finishEncode(const char*& data, size_t& size);
		void deinit();

		~VHVideoDecoder() {
			deinit();
		}
	
	private:
		VkResult allocateVideoSessionMemory();
		VkResult createVideoSessionParameters(uint32_t fps);


		bool m_initialized{ false };

		VkDevice m_device;
		VmaAllocator m_allocator;
		VkQueue m_decodeQueue;
		VkCommandPool m_decodeCommandPool;

		VkVideoSessionKHR m_videoSession;
		std::vector<VmaAllocation> m_allocations;
		VkVideoSessionParametersKHR m_videoSessionParameters;

		VkCommandBuffer m_decodeCommandBuffer;
    };
};

#endif
