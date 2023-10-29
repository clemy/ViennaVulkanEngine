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
		class Session {
		public:
			uint32_t getWidth() const {
				return m_decoder->m_width;
			}
			uint32_t getHeight() const {
				return m_decoder->m_height;
			}

			VkResult assignTransferTarget(VkImage targetImage, VkImageView targetImageView);

			void close();

		private:
			Session(VHVideoDecoder* decoder, const std::string& filename) :
				m_decoder{ decoder }
			{}

			VkResult init();
			void process(double dt);
			VkResult transferImage();
			void deinit();

			VHVideoDecoder* m_decoder;
			double m_nextFrameTime = 0.0;


			friend class VHVideoDecoder;
		};

		VkResult init(
			VkPhysicalDevice physicalDevice,
			VkDevice device,
			VmaAllocator allocator,
			VkQueue graphicsQueue,
			uint32_t graphicsQueueFamily,
			VkCommandPool graphicsCommandPool,
			VkQueue decodeQueue,
			uint32_t decodeQueueFamily,
			VkCommandPool decodeCommandPool);

		void process(double dt);

		VHVideoDecoder::Session* createVideoSession(const std::string& filename);

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
		VkResult allocateYCbCrConversionSampler();
		VkResult allocateReferenceImages(uint32_t count);
		VkResult createYCbCrConversionPipeline();
		VkResult createYCbCrConversionDescriptorSets(VkImage targetImage, VkImageView targetImageView);

		VkResult convertYCbCrToRGB(uint32_t currentImageIx);

		bool m_initialized{ false };

		VkDevice m_device;
		VmaAllocator m_allocator;
		VkQueue m_graphicsQueue;
		uint32_t m_graphicsQueueFamily;
		VkCommandPool m_graphicsCommandPool;
		VkQueue m_decodeQueue;
		uint32_t m_decodeQueueFamily;
		VkCommandPool m_decodeCommandPool;
		uint32_t m_width;
		uint32_t m_height;

		VkSamplerYcbcrConversionInfo m_yCbCrConversionInfo;
		VkSampler m_yCbCrConversionSampler;
		VkDescriptorSetLayout m_computeDescriptorSetLayout;
		VkPipelineLayout m_computePipelineLayout;
		VkPipeline m_computePipeline;
		VkDescriptorPool m_descriptorPool;
		std::vector<VkDescriptorSet> m_computeDescriptorSets;

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
		std::vector<VmaAllocation> m_dpbImageAllocations;
		std::vector<VkImageView> m_dpbImageViews;

		VkImage m_targetImage;
		VkImageView m_targetImageView;

		VkCommandBuffer m_computeCommandBuffer;

		std::unordered_set<Session*> m_sessions;
	};
};

#endif
