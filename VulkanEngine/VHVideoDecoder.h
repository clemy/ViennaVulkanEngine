/**
* Vulkan Video Decoder
*
* (c) Bernhard Clemens Schrenk, University of Vienna
*
*/

#ifndef VHVIDEODECODER_H
#define VHVIDEODECODER_H

#include "VHHelper.h"
#include "h264.h"

namespace vh {
    class VHVideoDecoder {
    public:
		class Session {
		public:
			uint32_t getWidth() const {
				return m_width;
			}
			uint32_t getHeight() const {
				return m_height;
			}

			VkResult assignTransferTarget(VkImage targetImage, VkImageView targetImageView);

			void close();

		private:
			Session(VHVideoDecoder* decoder, const std::string& filename) :
				m_decoder{ decoder }, m_filename{ filename }
			{}

			VkResult init();
			void process(double dt);
			VkResult transferImage();
			void deinit();

			void getNextNAL(uint8_t*& data, size_t& len);

			VkResult readFileHeaders();
			VkResult checkCapabilities();
			VkResult createVulkanVideoSession();
			VkResult allocateVideoSessionMemory();
			VkResult createVideoSessionParameters();
			VkResult allocateReferenceImages(uint32_t count);
			VkResult createYCbCrConversionDescriptorSets(VkImage targetImage, VkImageView targetImageView);

			VkResult decodeFrame();
			VkResult convertYCbCrToRGB(uint32_t currentImageIx);


			VHVideoDecoder* m_decoder;
			std::string m_filename;

			double m_nextFrameTime = 0.0;
			bool m_resetPending;

			uint32_t m_width;
			uint32_t m_height;

			std::vector<VkDescriptorSet> m_computeDescriptorSets;

			VkVideoDecodeH264CapabilitiesKHR m_h264capabilities;
			VkVideoDecodeCapabilitiesKHR m_decodeCapabilities;
			VkVideoCapabilitiesKHR m_videoCapabilities;
			VkVideoDecodeH264ProfileInfoKHR m_h264videoProfile;
			VkVideoProfileInfoKHR m_videoProfile;
			VkVideoProfileListInfoKHR m_videoProfileList;
			VkVideoSessionKHR m_videoSession;
			std::vector<VmaAllocation> m_allocations;
			//StdVideoH264SequenceParameterSetVui m_vui;
			h264::SPS m_h264sps;
			h264::PPS m_h264pps;
			StdVideoH264SequenceParameterSet m_sps;
			StdVideoH264PictureParameterSet m_pps;
			VkVideoSessionParametersKHR m_videoSessionParameters;

			VkCommandBuffer m_decodeCommandBuffer;

			std::vector<VkImage> m_dpbImages;
			std::vector<VmaAllocation> m_dpbImageAllocations;
			std::vector<VkImageView> m_dpbImageViews;
			std::vector<VkVideoPictureResourceInfoKHR> m_pictureResources;
			std::vector<StdVideoDecodeH264ReferenceInfo> m_stdH264references;
			std::vector<VkVideoDecodeH264DpbSlotInfoKHR> m_h264slots;
			std::vector<VkVideoReferenceSlotInfoKHR> m_referenceSlots;
			std::vector<uint64_t> m_gopPocs;
			std::vector<bool> m_viewed;
			uint32_t m_activeReferenceSlots;
			uint32_t m_activeDecodePicture;
			int m_activeViewPicture;
			int m_prev_pic_order_cnt_lsb{ 0 };
			int m_prev_pic_order_cnt_msb{ 0 };
			int m_poc_cycle{ 0 };

			VkImage m_targetImage;
			VkImageView m_targetImageView;

			VkBuffer m_bitStreamBuffer;
			VmaAllocation m_bitStreamBufferAllocation;
			void* m_bitStreamData;
			size_t m_bufferSize;

			VkCommandBuffer m_computeCommandBuffer;

			std::vector<uint8_t> m_data;
			uint8_t* m_nextData;

			VkFence m_decodeFinishedFence;

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

		void deinit();

		~VHVideoDecoder() {
			deinit();
		}
	
	private:
		VkResult allocateYCbCrConversionSampler();
		VkResult createYCbCrConversionPipeline();

		bool m_initialized{ false };

		VkPhysicalDevice m_physicalDevice;
		VkDevice m_device;
		VmaAllocator m_allocator;
		VkQueue m_graphicsQueue;
		uint32_t m_graphicsQueueFamily;
		VkCommandPool m_graphicsCommandPool;
		VkQueue m_decodeQueue;
		uint32_t m_decodeQueueFamily;
		VkCommandPool m_decodeCommandPool;

		VkSamplerYcbcrConversionInfo m_yCbCrConversionInfo;
		VkSampler m_yCbCrConversionSampler;
		VkDescriptorSetLayout m_computeDescriptorSetLayout;
		VkPipelineLayout m_computePipelineLayout;
		VkPipeline m_computePipeline;
		VkDescriptorPool m_descriptorPool;

		std::unordered_set<Session*> m_sessions;
	};
};

#endif
