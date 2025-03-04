#pragma once

#include "libavstream/common.hpp"
#include "libavstream/geometry/mesh_interface.hpp"
#include "TeleportCore/ErrorHandling.h"

namespace avs
{
	struct FloatKeyframe
	{
		float time; //Milliseconds
		float value;
	};

	struct Vector3Keyframe
	{
		float time; //Milliseconds
		vec3 value;
	};

	struct Vector4Keyframe
	{
		float time; //Milliseconds
		vec4 value;
	};

	struct TransformKeyframeList
	{
		size_t boneIndex = -1; //Index of the bone used in the bones list.

		std::vector<avs::Vector3Keyframe> positionKeyframes;
		std::vector<avs::Vector4Keyframe> rotationKeyframes;

		static TransformKeyframeList convertToStandard(const TransformKeyframeList& keyframeList, avs::AxesStandard sourceStandard, avs::AxesStandard targetStandard)
		{
			TransformKeyframeList convertedKeyframeList = keyframeList;

			for(avs::Vector3Keyframe& vectorKeyframe : convertedKeyframeList.positionKeyframes)
			{
	#if TELEPORT_INTERNAL_CHECKS
				if (_isnanf(vectorKeyframe.value.x) || _isnanf(vectorKeyframe.value.y) || _isnanf(vectorKeyframe.value.z) || _isnanf(vectorKeyframe.time))
				{
					TELEPORT_CERR << "Invalid keyframe" << std::endl;
					return convertedKeyframeList;
				}
	#endif
				avs::ConvertPosition(sourceStandard, targetStandard, vectorKeyframe.value);
			}

			for(avs::Vector4Keyframe& vectorKeyframe : convertedKeyframeList.rotationKeyframes)
			{
	#if TELEPORT_INTERNAL_CHECKS
				if (_isnanf(vectorKeyframe.value.x) || _isnanf(vectorKeyframe.value.y) || _isnanf(vectorKeyframe.value.z) 
					|| _isnanf(vectorKeyframe.value.w) || _isnanf(vectorKeyframe.time))
				{
					TELEPORT_CERR << "Invalid keyframe" << std::endl;
					return convertedKeyframeList;
				}
	#endif
				avs::ConvertRotation(sourceStandard, targetStandard, vectorKeyframe.value);
			}

			return convertedKeyframeList;
		}
	};

	struct Animation
	{
		std::string name;
		std::vector<TransformKeyframeList> boneKeyframes;

		static Animation convertToStandard(const Animation& animation, avs::AxesStandard sourceStandard, avs::AxesStandard targetStandard)
		{
			Animation convertedAnimation;
			convertedAnimation.name = animation.name;

			for(const TransformKeyframeList& keyframe : animation.boneKeyframes)
			{
			
				convertedAnimation.boneKeyframes.push_back(TransformKeyframeList::convertToStandard(keyframe, sourceStandard, targetStandard));
			}

			return convertedAnimation;
		}
	};
}