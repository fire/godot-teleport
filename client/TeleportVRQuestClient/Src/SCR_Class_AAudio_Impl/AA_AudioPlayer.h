// (C) Copyright 2018-2020 Simul Software Ltd
#pragma once

#include <crossplatform/AudioPlayer.h>
#include <aaudio/AAudio.h>
#include <android/ndk-version.h>

/*! A class to play audio from streams and files for PC
*/
class AA_AudioPlayer final : public sca::AudioPlayer
{
public:
	AA_AudioPlayer();
	~AA_AudioPlayer();

	sca::Result playStream(const uint8_t* data, size_t dataSize) override;

	sca::Result initializeAudioDevice() override;

	sca::Result configure(const sca::AudioSettings& audioSettings) override;

	sca::Result startRecording(std::function<void(const uint8_t * data, size_t dataSize)> recordingCallback) override;

	sca::Result processRecordedAudio() override;

	sca::Result stopRecording() override;

	sca::Result deconfigure() override;

	void onAudioProcessed() override;

private:
	AAudioStream* mAudioStream;
};


