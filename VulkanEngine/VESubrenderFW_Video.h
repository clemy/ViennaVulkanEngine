/**
* The Vienna Vulkan Engine
*
* (c) bei Helmut Hlavacs, University of Vienna
* 
* Subrender for Video Decoding
* by Bernhard C. Schrenk
*
*/

#ifndef VESUBRENDERFWVIDEO_H
#define VESUBRENDERFWVIDEO_H

namespace ve
{
	/**
		* \brief Subrenderer that manages entities that have a video texture
		*/
	class VESubrenderFW_Video : public VESubrenderFW
	{
	public:
		///Constructor for class VESubrenderFW_DN
		VESubrenderFW_Video(VERendererForward& renderer)
			: VESubrenderFW(renderer) {};

		///Destructor for class VESubrenderFW_DN
		virtual ~VESubrenderFW_Video() override {};

		///\returns the class of the subrenderer
		virtual veSubrenderClass getClass() override
		{
			return VE_SUBRENDERER_CLASS_OBJECT;
		};

		///\returns the type of the subrenderer
		virtual veSubrenderType getType() override
		{
			return VE_SUBRENDERER_TYPE_VIDEO;
		};

		virtual void initSubrenderer() override;

		virtual void closeSubrenderer() override;

		virtual void setDynamicPipelineState(VkCommandBuffer commandBuffer, uint32_t numPass) override;

		virtual void addEntity(VEEntity* pEntity) override;

	private:
		VkSamplerYcbcrConversionInfo m_yCbCrConversionInfo{ VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO };
		VkSampler m_sampler{ VK_NULL_HANDLE };
		vh::VHVideoDecoder m_videoDecoder;
	};
} // namespace ve

#endif
