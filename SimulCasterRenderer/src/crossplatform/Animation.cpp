#include "Animation.h"

#include "Bone.h"

namespace scr
{
	BoneKeyframeList::BoneKeyframeList()
	{}

	void BoneKeyframeList::seekTime(float time)
	{
		std::shared_ptr<Bone> bone = bonePtr.lock();
		if(!bone)
			return;
		Transform transform = bone->GetLocalTransform();
		setPositionToTime(time, transform.m_Translation, positionKeyframes);
		setRotationToTime(time, transform.m_Rotation, rotationKeyframes);
//transform.m_Translation=positionKeyframes[0].value;
//transform.m_Rotation=rotationKeyframes[0].value;
		transform.UpdateModelMatrix();
		bone->SetLocalTransform(transform);
	}

	void BoneKeyframeList::setPositionToTime(float time, avs::vec3& bonePosition, const std::vector<avs::Vector3Keyframe>& keyframes)
	{
		if(keyframes.size() == 0)
			return;
		if(keyframes.size() == 1)
		{
			bonePosition = keyframes[0].value;
			return;
		}
		size_t nextKeyframeIndex = getNextKeyframeIndex(time, keyframes);
		const avs::Vector3Keyframe& previousKeyframe = (nextKeyframeIndex == 0 ? keyframes[nextKeyframeIndex] : keyframes[nextKeyframeIndex - 1]);
		const avs::Vector3Keyframe& nextKeyframe = keyframes[nextKeyframeIndex];

		//Linear interpolation between previous keyframe and next keyframe.
		float timeBlend = getTimeBlend(time, previousKeyframe.time, nextKeyframe.time);
		bonePosition = (1 - timeBlend) * previousKeyframe.value + timeBlend * nextKeyframe.value;
	}

	void BoneKeyframeList::setRotationToTime(float time, quat& boneRotation, const std::vector<avs::Vector4Keyframe>& keyframes)
	{
		if(keyframes.size() == 0)
			return;
		if(keyframes.size() == 1)
		{
			boneRotation = keyframes[0].value;
			return;
		}
		size_t nextKeyframeIndex = getNextKeyframeIndex(time, keyframes);
		const avs::Vector4Keyframe& previousKeyframe = (nextKeyframeIndex == 0 ? keyframes[nextKeyframeIndex] : keyframes[nextKeyframeIndex - 1]);
		const avs::Vector4Keyframe& nextKeyframe = keyframes[nextKeyframeIndex];

		//Linear interpolation between previous keyframe and next keyframe.
		float timeBlend = getTimeBlend(time, previousKeyframe.time, nextKeyframe.time);
		boneRotation = (1 - timeBlend) * previousKeyframe.value + timeBlend * nextKeyframe.value;
		//boneRotation = quat::Slerp(previousKeyframe.value, nextKeyframe.value, timeBlend);
	}

	size_t BoneKeyframeList::getNextKeyframeIndex(float time, const std::vector<avs::Vector3Keyframe>& keyframes)
	{
		for(size_t i = 1; i < keyframes.size(); i++)
		{
			if(keyframes[i].time >= time) return i;
		}

		return keyframes.size() - 1;
	}

	size_t BoneKeyframeList::getNextKeyframeIndex(float time, const std::vector<avs::Vector4Keyframe>& keyframes)
	{
		for(size_t i = 1; i < keyframes.size(); i++)
		{
			if(keyframes[i].time >= time) return i;
		}

		return keyframes.size() - 1;
	}

	float BoneKeyframeList::getTimeBlend(float currentTime, float previousTime, float nextTime)
	{
		return (currentTime - previousTime) / (nextTime - previousTime);
	}

	Animation::Animation(const std::string& name)
		:name(name)
	{}

	Animation::Animation(const std::string& name, std::vector<BoneKeyframeList> bk)
		:name(name), boneKeyframeLists(bk)
	{
		updateAnimationLength();
	}

	//Retrieve end time from latest time in any bone animations.
	//ASSUMPTION: This works for Unity, but does it work for Unreal?
	void Animation::updateAnimationLength()
	{
		if(boneKeyframeLists.empty())
			return;

		BoneKeyframeList& boneAnimation = boneKeyframeLists[0];
		if(!boneAnimation.positionKeyframes.empty())
		{
			endTime = std::max(endTime, boneAnimation.positionKeyframes[boneAnimation.positionKeyframes.size() - 1].time);
			return;
		}

		if(!boneAnimation.rotationKeyframes.empty())
		{
			endTime = std::max(endTime, boneAnimation.rotationKeyframes[boneAnimation.rotationKeyframes.size() - 1].time);
			return;
		}
	}

	bool Animation::finished()
	{
		return currentTime >= endTime;
	}

	void Animation::restart()
	{
		currentTime = 0.0f;

		for(BoneKeyframeList boneKeyframeList : boneKeyframeLists)
		{
			boneKeyframeList.seekTime(currentTime);
		}
	}

	void Animation::update(float deltaTime)
	{
		currentTime = std::min(currentTime + deltaTime, endTime);

		for(BoneKeyframeList boneKeyframeList : boneKeyframeLists)
		{
			boneKeyframeList.seekTime(currentTime);
		}
	}
}