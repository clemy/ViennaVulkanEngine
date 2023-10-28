/**
* The Vienna Vulkan Engine
*
* (c) bei Helmut Hlavacs, University of Vienna
*
* Subrender for Video Decoding
* by Bernhard C. Schrenk
*
*/

#include "VEInclude.h"
#include "VERendererForward.h"

namespace ve
{
	/**
		* \brief Initialize the subrenderer
		*
		* Create descriptor set layout, pipeline layout and the PSO
		*
		*/
	void VESubrenderFW_Video::initSubrenderer()
	{
		VESubrenderFW::initSubrenderer();

		VkSamplerYcbcrConversionCreateInfo yCbCrConversionCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
		yCbCrConversionCreateInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
		yCbCrConversionCreateInfo.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709; // limitation: color model should depend on video file

		m_yCbCrConversionInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
		vkCreateSamplerYcbcrConversionKHR(m_renderer.getDevice(), &yCbCrConversionCreateInfo, nullptr, &m_yCbCrConversionInfo.conversion);

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

		vkCreateSampler(m_renderer.getDevice(), &samplerInfo, nullptr, &m_sampler);
		std::vector sampler(m_resourceArrayLength, m_sampler);

		vh::vhRenderCreateDescriptorSetLayout(m_renderer.getDevice(), //binding 0...array, binding 1...array
			{ m_resourceArrayLength, m_resourceArrayLength },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_FRAGMENT_BIT },
			&m_descriptorSetLayoutResources,
			{ sampler.data(), nullptr });

		VkDescriptorSetLayout perObjectLayout = m_renderer.getDescriptorSetLayoutPerObject();

		vh::vhPipeCreateGraphicsPipelineLayout(m_renderer.getDevice(),
			{ perObjectLayout, perObjectLayout,
			 m_renderer.getDescriptorSetLayoutShadow(),
			 perObjectLayout, m_descriptorSetLayoutResources },
			{}, &m_pipelineLayout);

		m_pipelines.resize(1);
		vh::vhPipeCreateGraphicsPipeline(m_renderer.getDevice(),
			{ "../../media/shader/Forward/Video/vert.spv", "../../media/shader/Forward/Video/frag.spv" },
			m_renderer.getSwapChainExtent(),
			m_pipelineLayout, m_renderer.getRenderPass(),
			{ VK_DYNAMIC_STATE_BLEND_CONSTANTS },
			&m_pipelines[0]);

		if (m_maps.empty())
			m_maps.resize(2);
	}

	void VESubrenderFW_Video::closeSubrenderer()
	{
		VESubrenderFW::closeSubrenderer();

		m_videoDecoder.deinit();

		vkDestroySampler(m_renderer.getDevice(), m_sampler, nullptr);
		vkDestroySamplerYcbcrConversionKHR(m_renderer.getDevice(), m_yCbCrConversionInfo.conversion, nullptr);
	}

	/**
		* \brief Set the danymic pipeline stat, i.e. the blend constants to be used
		*
		* \param[in] commandBuffer The currently used command buffer
		* \param[in] numPass The current pass number - in the forst pass, write over pixel colors, after this add pixel colors
		*
		*/
	void VESubrenderFW_Video::setDynamicPipelineState(VkCommandBuffer
		commandBuffer,
		uint32_t numPass)
	{
		if (numPass == 0)
		{
			float blendConstants[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			vkCmdSetBlendConstants(commandBuffer, blendConstants);
			return;
		}

		float blendConstants[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
		vkCmdSetBlendConstants(commandBuffer, blendConstants);
	}

	/**
	* \brief Add an entity to the subrenderer
	*
	* Create a UBO for this entity, a descriptor set per swapchain image, and update the descriptor sets
	*
	*/
	void VESubrenderFW_Video::addEntity(VEEntity* pEntity)
	{
		vh::QueueFamilyIndices queueFamilyIndices =
			vh::vhDevFindQueueFamilies(m_renderer.getPhysicalDevice(), m_renderer.getSurface(), true);

		m_videoDecoder.init(
			m_renderer.getPhysicalDevice(),
			m_renderer.getDevice(),
			m_renderer.getVmaAllocator(),
			queueFamilyIndices.graphicsFamily,
			m_renderer.getVideoDecodeQueue(),
			queueFamilyIndices.videoDecodeFamily,
			m_renderer.getVideoDecodeCommandPool(),
			&m_yCbCrConversionInfo);

		VkDescriptorImageInfo imageInfo;
		imageInfo.sampler = VK_NULL_HANDLE;
		imageInfo.imageView = m_videoDecoder.getImageView();
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::vector<VkDescriptorImageInfo> maps = {
			imageInfo,
			//pEntity->m_pMaterial->mapDiffuse->m_imageInfo,
			//pEntity->m_pMaterial->mapNormal->m_imageInfo, // prevent crash until video shader is ready
			pEntity->m_pMaterial->mapNormal->m_imageInfo };

		addMaps(pEntity, maps);

		VESubrender::addEntity(pEntity);
	}

} // namespace ve
