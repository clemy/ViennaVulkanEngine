/**
* The Vienna Vulkan Engine
*
* (c) bei Helmut Hlavacs, University of Vienna
*
*/


#include <format>
#include "VEInclude.h"
#include "irrKlang.h"

namespace ve {


	///user defined manager class, derived from VEEngine
	class MyVulkanEngine : public VEEngine {
	public:
		MyVulkanEngine(veRendererType type = veRendererType::VE_RENDERER_TYPE_FORWARD, bool debug = false) : VEEngine(type, debug) {};
		~MyVulkanEngine() {};

		irrklang::ISoundEngine* m_irrklangEngine = irrklang::createIrrKlangDevice();

		///Register an event listener to interact with the user
		virtual void registerEventListeners();

		///Load the first level into the game engine
		///The engine uses Y-UP, Left-handed
		virtual void loadLevel(uint32_t numLevel = 1) {

			VEEngine::loadLevel(numLevel);			//create standard cameras and lights

			VESceneNode* pScene;
			VECHECKPOINTER(pScene = getSceneManagerPointer()->createSceneNode("Level 1", getRoot()));

			//scene models

			VESceneNode* sp1;
			VECHECKPOINTER(sp1 = getSceneManagerPointer()->createSkybox("The Sky", "../../media/models/test/sky/cloudy",
				{ "bluecloud_ft.jpg", "bluecloud_bk.jpg", "bluecloud_up.jpg",
					"bluecloud_dn.jpg", "bluecloud_rt.jpg", "bluecloud_lf.jpg" }, pScene));

			VESceneNode* e4;
			VECHECKPOINTER(e4 = getSceneManagerPointer()->loadModel("The Plane", "../../media/models/test/plane", "plane_t_n_s.obj", 0, pScene));
			e4->setTransform(glm::scale(glm::mat4(1.0f), glm::vec3(1000.0f, 1.0f, 1000.0f)));

			VEEntity* pE4;
			VECHECKPOINTER(pE4 = (VEEntity*)getSceneManagerPointer()->getSceneNode("The Plane/plane_t_n_s.obj/plane/Entity_0"));
			pE4->setParam(glm::vec4(1000.0f, 1000.0f, 0.0f, 0.0f));

			{
				VESceneNode* eV, * eParentVideo;
				eParentVideo = getSceneManagerPointer()->createSceneNode("The VideoCube Parent", pScene, glm::mat4(1.0));
				VECHECKPOINTER(eV = getSceneManagerPointer()->loadModel("The VideoCubeVideo", "../../media/models/video/crateVideo", "cube.obj"));
				eParentVideo->multiplyTransform(glm::scale(glm::mat4(1.0f), glm::vec3(1.6f, 1.2f, 1.6f)));
				eParentVideo->multiplyTransform(glm::rotate(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
				eParentVideo->multiplyTransform(glm::rotate(-0.5f, glm::vec3(0.0f, 1.0f, 0.0f)));
				eParentVideo->multiplyTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 5.0f)));
				eParentVideo->addChild(eV);
			}

			{
				VESceneNode* eV, * eParentVideo;
				eParentVideo = getSceneManagerPointer()->createSceneNode("The VideoSphere Parent", pScene, glm::mat4(1.0));
				VECHECKPOINTER(eV = getSceneManagerPointer()->loadModel("The VideoSphereVideo", "../../media/models/video/sphere", "sphere.obj"));
				eParentVideo->multiplyTransform(glm::scale(glm::mat4(1.0f), glm::vec3(1.6f, 1.2f, 1.6f)));
				eParentVideo->multiplyTransform(glm::rotate(glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
				eParentVideo->multiplyTransform(glm::rotate(-0.5f, glm::vec3(0.0f, 1.0f, 0.0f)));
				eParentVideo->multiplyTransform(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 20.0f, 2.0f)));
				eParentVideo->addChild(eV);
			}
			m_irrklangEngine->play2D("../../media/sounds/ophelia.wav", true);
		};
	};


	double g_time = 0.0;

	//
	//Zeichne das GUI
	//
	class EventListenerGUI : public VEEventListener {
	protected:
		
		virtual void onDrawOverlay(veEvent event) {
			g_time += event.dt;

			VESubrender_Nuklear * pSubrender = (VESubrender_Nuklear*)getEnginePointer()->getRenderer()->getOverlay();
			if (pSubrender == nullptr) return;

			struct nk_context * ctx = pSubrender->getContext();

			if (nk_begin(ctx, "", nk_rect(0, 0, 200, 120), NK_WINDOW_BORDER)) {
				nk_layout_row_dynamic(ctx, 45, 1);
				nk_label(ctx, std::format("Time: {:04.1f}", g_time).c_str(), NK_TEXT_LEFT);

				static double fps = 0.0;
				if (event.dt > 0)
					fps = 0.05 / event.dt + 0.95 * fps;
				nk_layout_row_dynamic(ctx, 30, 1);
				nk_label(ctx, std::format("FPS: {:.0f}", fps).c_str(), NK_TEXT_LEFT);

				nk_end(ctx);
			}
		}

	public:
		///Constructor of class EventListenerGUI
		EventListenerGUI(std::string name) : VEEventListener(name) { };

		///Destructor of class EventListenerGUI
		virtual ~EventListenerGUI() {};
	};

	void MyVulkanEngine::registerEventListeners() {
		VEEngine::registerEventListeners();
		registerEventListener(new EventListenerGUI("GUI"), { veEvent::VE_EVENT_DRAW_OVERLAY });
		//registerEventListener(new VEEventListenerNuklearDebug("Debug"), { veEvent::VE_EVENT_DRAW_OVERLAY });
	};

}

using namespace ve;

int main() {
	bool debug = true;

	MyVulkanEngine mve(veRendererType::VE_RENDERER_TYPE_FORWARD, debug);	//enable or disable debugging (=callback, validation layers)

	mve.initEngine();
	mve.loadLevel(1);
	mve.run();

	return 0;
}
