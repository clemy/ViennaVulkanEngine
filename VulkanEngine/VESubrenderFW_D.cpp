/**
* The Vienna Vulkan Engine
*
* (c) bei Helmut Hlavacs, University of Vienna
*
*/

#include "VEInclude.h"
#include "VERendererForward.h"

VkSamplerYcbcrConversionInfo g_yCbCrConversionInfo;

namespace ve
{
	/**
		* \brief Initialize the subrenderer
		*
		* Create descriptor set layout, pipeline layout and the PSO
		*
		*/
	void VESubrenderFW_D::initSubrenderer()
	{
		VESubrenderFW::initSubrenderer();

		VkSamplerYcbcrConversionCreateInfo yCbCrConversionCreateInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO };
		yCbCrConversionCreateInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
		yCbCrConversionCreateInfo.ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709;

		g_yCbCrConversionInfo = { VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
		vkCreateSamplerYcbcrConversionKHR(m_renderer.getDevice(), &yCbCrConversionCreateInfo, nullptr, &g_yCbCrConversionInfo.conversion);
		
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
		samplerInfo.pNext = &g_yCbCrConversionInfo;

		std::vector<VkSampler> sampler(m_resourceArrayLength);
		for(VkSampler& s: sampler)
			vkCreateSampler(m_renderer.getDevice(), &samplerInfo, nullptr, &s);

		vh::vhRenderCreateDescriptorSetLayout(m_renderer.getDevice(),
			{ m_resourceArrayLength },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER },
			{ VK_SHADER_STAGE_FRAGMENT_BIT },
			&m_descriptorSetLayoutResources,
			sampler.data());

		VkDescriptorSetLayout perObjectLayout = m_renderer.getDescriptorSetLayoutPerObject();

		vh::vhPipeCreateGraphicsPipelineLayout(m_renderer.getDevice(),
			{ perObjectLayout, perObjectLayout,
			 m_renderer.getDescriptorSetLayoutShadow(),
			 perObjectLayout, m_descriptorSetLayoutResources },
			{}, &m_pipelineLayout);

		m_pipelines.resize(1);
		vh::vhPipeCreateGraphicsPipeline(m_renderer.getDevice(),
			{ "../../media/shader/Forward/D/vert.spv", "../../media/shader/Forward/D/frag.spv" },
			m_renderer.getSwapChainExtent(),
			m_pipelineLayout, m_renderer.getRenderPass(),
			{ VK_DYNAMIC_STATE_BLEND_CONSTANTS },
			&m_pipelines[0]);

		if (m_maps.empty())
			m_maps.resize(1);
	}

	/**
		* \brief Set the danymic pipeline stat, i.e. the blend constants to be used
		*
		* \param[in] commandBuffer The currently used command buffer
		* \param[in] numPass The current pass number - in the forst pass, write over pixel colors, after this add pixel colors
		*
		*/
	void VESubrenderFW_D::setDynamicPipelineState(VkCommandBuffer
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
	void VESubrenderFW_D::addEntity(VEEntity *pEntity)
	{
		std::vector<VkDescriptorImageInfo> maps = { pEntity->m_pMaterial->mapDiffuse->m_imageInfo };

		addMaps(pEntity, maps);

		VESubrender::addEntity(pEntity);
	}

} // namespace ve
