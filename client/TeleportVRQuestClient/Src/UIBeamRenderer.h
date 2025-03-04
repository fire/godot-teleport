/************************************************************************************

Filename	:   SimpleBeamRenderer.h
Content	 :   Helper for VRAPI beam and particle rendering
Created	 :   July 2020
Authors	 :   Matthew Langille
Copyright   :   Copyright (c) Facebook Technologies, LLC and its affiliates. All rights reserved.

************************************************************************************/
#pragma once

#include "OVR_Math.h"
#include <Render/BeamRenderer.h>
#include <Render/ParticleSystem.h>

namespace teleport
{
	struct HitTest {
		OVR::Vector3f pointerStart = {0.0f, 0.0f, 0.0f};
		OVR::Vector3f pointerEnd = {0.0f, 0.0f, 1.0f};
		OVRFW::VRMenuObject* hitObject = nullptr;
		bool clicked = false;
	};
	class UIBeamRenderer
	{
	   public:
			UIBeamRenderer() = default;
			~UIBeamRenderer()
			{
				delete spriteAtlas_;
			}

		void Init(
			OVRFW::ovrFileSys* fileSys,
			const char* particleTexture,
			OVR::Vector4f particleColor,
			float scale = 1.0f) {
			PointerParticleColor_ = particleColor;
			Scale = scale;
			beamRenderer_.Init(8, true);

			if (particleTexture != nullptr)
			{
				spriteAtlas_ = new OVRFW::ovrTextureAtlas();
				spriteAtlas_->Init(*fileSys, particleTexture);
				spriteAtlas_->BuildSpritesFromGrid(4, 2, 8);
				particleSystem_.Init(
					1024, spriteAtlas_, OVRFW::ovrParticleSystem::GetDefaultGpuState(), false);
			} else
			{
				particleSystem_.Init(1024, nullptr, OVRFW::ovrParticleSystem::GetDefaultGpuState(), false);
			}
		}

		void Shutdown()
		{
			beamRenderer_.Shutdown();
			particleSystem_.Shutdown();
		}

		void Update(
			const OVRFW::ovrApplFrameIn& in,
			const std::vector<HitTest>& hitTestDevices)
		{
			// Clear old beams and particles
			while (beams_.size())
			{
				beamRenderer_.RemoveBeam(beams_[0]);
				beams_.erase(beams_.begin());
			}
			for (auto h : particles_)
			{
				particleSystem_.RemoveParticle(h);
			}

			// Add UI pointers to render
			for (auto& device : hitTestDevices)
			{
				const auto& beam = beamRenderer_.AddBeam(
					in, 0.03f, device.pointerStart, device.pointerEnd, {0.5f, 0.8f, 1.0f, 1.0f});
				beams_.push_back(beam);

				// if (LaserHit)
				const auto& particle = particleSystem_.AddParticle(
					in,
					device.pointerEnd,
					0.0f,
					OVR::Vector3f(0.0f),
					OVR::Vector3f(0.0f),
					PointerParticleColor_,
					OVRFW::ovrEaseFunc::NONE,
					0.0f,
					0.1f * Scale,
					0.1f,
					0);
				particles_.push_back(particle);
			}
		}
		void Render(const OVRFW::ovrApplFrameIn& in, OVRFW::ovrRendererOutput& out)
		{
			/// Render beams
			const OVR::Matrix4f projectionMatrix;
			beamRenderer_.Frame(in, out.FrameMatrices.CenterView);
			beamRenderer_.Render(out.Surfaces);
			particleSystem_.Frame(in, spriteAtlas_, out.FrameMatrices.CenterView);
			particleSystem_.RenderEyeView(out.FrameMatrices.CenterView, projectionMatrix, out.Surfaces);
		}

	   private:
		OVRFW::ovrBeamRenderer beamRenderer_;
		OVR::Vector4f PointerParticleColor_;
		OVRFW::ovrParticleSystem particleSystem_;
		OVRFW::ovrTextureAtlas* spriteAtlas_ = nullptr;
		std::vector<OVRFW::ovrBeamRenderer::handle_t> beams_;
		std::vector<OVRFW::ovrParticleSystem::handle_t> particles_;
		float Scale;
	};

} // namespace OVRFW
